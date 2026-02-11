#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <wchar.h>
#include "MinHook.h"

#define LUA_REGISTRYINDEX (-10000)
#define MAX_SCRIPT_SIZE (10 * 1024 * 1024)

typedef struct lua_State lua_State;

// Định nghĩa các kiểu hàm Lua API
typedef int  (*luaL_ref_Func)(lua_State* L, int t);
typedef void (*luaL_unref_Func)(lua_State* L, int t, int ref);
typedef lua_State* (*lua_newthread_Func)(lua_State* L);
typedef void (*lua_settop_Func)(lua_State* L, int index);
typedef int  (*luaL_loadstring_Func)(lua_State*, const char*);
typedef int  (*lua_vpcall_Func)(lua_State*, int, int, int);

static luaL_loadstring_Func luaL_loadstring_ptr = NULL;
static lua_vpcall_Func      lua_vpcall_ptr      = NULL;
static lua_newthread_Func   lua_newthread_ptr   = NULL;
static luaL_ref_Func        luaL_ref_ptr        = NULL;
static luaL_unref_Func      luaL_unref_ptr      = NULL;

typedef int (__cdecl* tLuaL_loadstring)(lua_State* L, const char* s);
static tLuaL_loadstring oLuaL_loadstring_Hook = NULL;

static lua_State* g_LuaState = NULL;  
static void* g_pTargetFunction = NULL; // Lưu địa chỉ hàm gốc để gỡ hook

// Cấu trúc truyền tham số cho luồng thực thi
typedef struct {
    char* code;
} LuaExecParam;

// Luồng gỡ bỏ hook sau khi đã lấy được State
DWORD WINAPI UnhookThread(LPVOID param) {
    Sleep(500);
    if (g_pTargetFunction) {
        MH_DisableHook(g_pTargetFunction);
        MH_RemoveHook(g_pTargetFunction);
    }
    return 0;
}

// Luồng thực thi mã Lua
DWORD WINAPI LuaExecutionThread(LPVOID param) {
    LuaExecParam* p = (LuaExecParam*)param;
    if (!p) return 0;

    if (!g_LuaState) {
        MessageBoxW(NULL, L"Lua State bị NULL!", L"Lỗi Exec", MB_OK | MB_ICONERROR);
        if (p->code) free(p->code);
        free(p);
        return 0;
    }

    // Sử dụng cơ chế lua_newthread để tạo môi trường chạy cô lập, tránh crash game
    lua_State* co = lua_newthread_ptr(g_LuaState);
    if (co) {
        // Neo thread vào Registry để tránh bị Garbage Collector dọn mất
        int ref = luaL_ref_ptr(g_LuaState, LUA_REGISTRYINDEX);
        
        if (luaL_loadstring_ptr(co, p->code) == 0) {
            if (lua_vpcall_ptr(co, 0, 0, 0) != 0) {
                MessageBoxW(NULL, L"Lỗi khi chạy Script (Runtime Error)!", L"Lua Error", MB_OK | MB_ICONWARNING);
            }
        } else {
             MessageBoxW(NULL, L"Lỗi cú pháp Script (Syntax Error)!", L"Lua Error", MB_OK | MB_ICONWARNING);
        }
        
        // Giải phóng tham chiếu sau khi chạy xong
        luaL_unref_ptr(g_LuaState, LUA_REGISTRYINDEX, ref);
    } else {
        MessageBoxW(NULL, L"Không thể tạo Lua Thread mới!", L"Lỗi Exec", MB_OK | MB_ICONERROR);
    }

    if (p->code) free(p->code);
    free(p);
    return 0;
}

// Hàm Hook: Chạy 1 lần duy nhất để lấy State
int __cdecl hkLuaL_loadstring(lua_State* L, const char* s) {
    if (g_LuaState == NULL && L != NULL) {
        g_LuaState = L; 
        
        // Kích hoạt luồng gỡ hook ngay lập tức
        CreateThread(NULL, 0, UnhookThread, NULL, 0, NULL);
    }
    return oLuaL_loadstring_Hook(L, s);
}

DWORD WINAPI PipeThread(LPVOID param) {
    HANDLE hPipe;
    DWORD bytesRead;
    char* buffer = (char*)malloc(64 * 1024);
    if (!buffer) return 0;

    const char* SECRET_KEY = "235f08ec85de7c2b7abadba9c03ad5471aed8284b46398a7984849f217b52d2d";
    size_t keyLen = strlen(SECRET_KEY);

    while (1) {
        hPipe = CreateNamedPipeA(
            "\\\\.\\pipe\\80ea0cd854f6ea1642329af8bffa01dfbc7f789dabc918f5a09430410a797343",
            PIPE_ACCESS_DUPLEX, PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            1, 1024 * 1024, 1024 * 1024, 0, NULL
        );

        if (hPipe == INVALID_HANDLE_VALUE) { Sleep(1000); continue; }

        if (ConnectNamedPipe(hPipe, NULL) || GetLastError() == ERROR_PIPE_CONNECTED) {
            char* msgData = NULL;
            size_t totalSize = 0;
            BOOL errorOccurred = FALSE;

            do {
                if (!ReadFile(hPipe, buffer, 64 * 1024, &bytesRead, NULL)) {
                    if (GetLastError() != ERROR_MORE_DATA) break;
                }
                if (bytesRead > 0) {
                    if (totalSize + bytesRead > MAX_SCRIPT_SIZE) { errorOccurred = TRUE; break; }
                    char* tmp = (char*)realloc(msgData, totalSize + bytesRead + 1);
                    if (!tmp) { errorOccurred = TRUE; break; }
                    msgData = tmp;
                    memcpy(msgData + totalSize, buffer, bytesRead);
                    totalSize += bytesRead;
                    msgData[totalSize] = 0;
                }
            } while (GetLastError() == ERROR_MORE_DATA);

            if (!errorOccurred && msgData && totalSize > keyLen) {
                if (strncmp(msgData, SECRET_KEY, keyLen) == 0) {
                    if (g_LuaState) {
                        LuaExecParam* p = (LuaExecParam*)malloc(sizeof(LuaExecParam));
                        if (p) {
                            p->code = _strdup(msgData + keyLen);
                            CreateThread(NULL, 0, LuaExecutionThread, p, 0, NULL);
                        }
                    } else {
                        MessageBoxW(NULL, L"Chưa Hook được LuaState! Hãy tương tác game.", L"Cảnh báo", MB_OK | MB_ICONWARNING);
                    }
                } else {
                    MessageBoxW(NULL, L"Lỗi xác thực!", L"Security", MB_OK | MB_ICONERROR);
                }
            }
            if (msgData) free(msgData);
        }
        DisconnectNamedPipe(hPipe);
        CloseHandle(hPipe);
    }
    free(buffer);
    return 0;
}

DWORD WINAPI MainThread(LPVOID lpParam) {
    HMODULE hLua = GetModuleHandleA("liblua.dll");
    if (!hLua) hLua = GetModuleHandleA("lua51.dll");
    if (!hLua) {
        MessageBoxW(NULL, L"Không tìm thấy liblua.dll hoặc lua51.dll!", L"Lỗi Inject", MB_OK | MB_ICONERROR);
        return 0;
    }

    luaL_loadstring_ptr = (luaL_loadstring_Func)GetProcAddress(hLua, "luaL_loadstring");
    lua_vpcall_ptr      = (lua_vpcall_Func)     GetProcAddress(hLua, "lua_vpcall");
    lua_newthread_ptr   = (lua_newthread_Func)  GetProcAddress(hLua, "lua_newthread");
    luaL_ref_ptr        = (luaL_ref_Func)       GetProcAddress(hLua, "luaL_ref");
    luaL_unref_ptr      = (luaL_unref_Func)     GetProcAddress(hLua, "luaL_unref");

    if (!luaL_loadstring_ptr || !lua_vpcall_ptr || !lua_newthread_ptr || !luaL_ref_ptr || !luaL_unref_ptr) {
        MessageBoxW(NULL, L"Không tìm thấy đầy đủ các hàm Lua cần thiết!", L"Lỗi Inject", MB_OK | MB_ICONERROR);
        return 0;
    }

    g_pTargetFunction = (void*)luaL_loadstring_ptr;

    if (g_pTargetFunction && MH_Initialize() == MH_OK) {
        if (MH_CreateHook(g_pTargetFunction, (LPVOID)hkLuaL_loadstring, (LPVOID*)&oLuaL_loadstring_Hook) == MH_OK) {
            MH_EnableHook(g_pTargetFunction);
        } else {
             MessageBoxW(NULL, L"Không thể tạo Hook!", L"Lỗi MinHook", MB_OK | MB_ICONERROR);
             return 0;
        }
    } else {
        MessageBoxW(NULL, L"Không thể khởi tạo MinHook!", L"Lỗi MinHook", MB_OK | MB_ICONERROR);
        return 0;
    }
    CreateThread(NULL, 0, PipeThread, NULL, 0, NULL);
    MessageBoxW(NULL, L"Inject thành công!", L"LuaExecV4", MB_OK | MB_ICONINFORMATION);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)MainThread, NULL, 0, NULL);
    }
    return TRUE;
}