# Lua inject mini world
gcc -shared -o LuaExecV3.dll LuaExecV3.c -Iinclude -Llib -lMinHook -m32 -Wall -DUNICODE -D_UNICODE -static-libgcc -static-libstdc++
gcc injectdll.c -o injectdll.exe -mwindows
