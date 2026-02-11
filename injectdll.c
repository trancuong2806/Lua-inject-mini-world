#include <windows.h>
#include <stdio.h>

int InjectDLL(DWORD pid, const char* dllPath) {
    HANDLE hProcess, hThread;
    LPVOID pRemoteBuf;
    HMODULE hKernel32;
    FARPROC pLoadLibraryA;
    char fullPath[MAX_PATH];
    SIZE_T dllPathLen;

    // Lấy đường dẫn tuyệt đối trước
    GetFullPathNameA(dllPath, MAX_PATH, fullPath, NULL);
    dllPathLen = strlen(fullPath) + 1;

    // Mở process với quyền đầy đủ
    hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProcess) {
        printf("[-] OpenProcess thất bại! Lỗi: %lu\n", GetLastError());
        return -1;
    }

    // Cấp phát bộ nhớ trong process target
    pRemoteBuf = VirtualAllocEx(hProcess, NULL, dllPathLen, MEM_COMMIT, PAGE_READWRITE);
    if (!pRemoteBuf) {
        printf("[-] VirtualAllocEx thất bại! Lỗi: %lu\n", GetLastError());
        CloseHandle(hProcess);
        return -1;
    }

    // Ghi đường dẫn DLL tuyệt đối vào bộ nhớ target
    if (!WriteProcessMemory(hProcess, pRemoteBuf, (LPVOID)fullPath, dllPathLen, NULL)) {
        printf("[-] WriteProcessMemory thất bại! Lỗi: %lu\n", GetLastError());
        VirtualFreeEx(hProcess, pRemoteBuf, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return -1;
    }

    // Lấy địa chỉ LoadLibraryA trong Kernel32.dll
    hKernel32 = GetModuleHandleA("kernel32.dll");
    pLoadLibraryA = GetProcAddress(hKernel32, "LoadLibraryA");

    // Tạo remote thread để gọi LoadLibraryA(fullPath)
    hThread = CreateRemoteThread(hProcess, NULL, 0,
                                 (LPTHREAD_START_ROUTINE)pLoadLibraryA,
                                 pRemoteBuf, 0, NULL);

    if (!hThread) {
        printf("[-] CreateRemoteThread thất bại! Lỗi: %lu\n", GetLastError());
        VirtualFreeEx(hProcess, pRemoteBuf, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return -1;
    }

    printf("[+] DLL đã inject thành công!\n");

    // Đợi thread kết thúc
    WaitForSingleObject(hThread, INFINITE);

    // Dọn dẹp
    VirtualFreeEx(hProcess, pRemoteBuf, 0, MEM_RELEASE);
    CloseHandle(hThread);
    CloseHandle(hProcess);

    return 0;
}

int main(int argc, char* argv[]) {
    SetConsoleOutputCP(CP_UTF8);

    if (argc != 3) {
        printf("Cách dùng: %s <PID> <Đường dẫn DLL>\n", argv[0]);
        return -1;
    }

    DWORD pid = (DWORD)atoi(argv[1]);
    const char* dllPath = argv[2];

    InjectDLL(pid, dllPath);
    return 0;
}
