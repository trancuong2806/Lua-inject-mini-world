import win32pipe, win32file

PIPE_NAME = r"\\.\pipe\80ea0cd854f6ea1642329af8bffa01dfbc7f789dabc918f5a09430410a797343"
SECRET_KEY = "235f08ec85de7c2b7abadba9c03ad5471aed8284b46398a7984849f217b52d2d"

def exec_lua(code: str):
    try:
        full_msg = SECRET_KEY + code
        handle = win32file.CreateFile(
            PIPE_NAME,
            win32file.GENERIC_READ | win32file.GENERIC_WRITE,
            0,
            None,
            win32file.OPEN_EXISTING,
            0,
            None
        )
        win32file.WriteFile(handle, full_msg.encode("utf-8"))
        handle.close()
    except Exception:
        print("Chưa inject")
        pass

script_lua = r'''
local function uid(id)
return id + 1000000000
end
for i=1,100 do
local msg = {}
id = 71666036
msg.role_id   = uid(id)
msg.itemid    = 1
msg.itemnum   = 1

SandboxLuaMsg.sendToHost(
    _G.SANDBOX_LUAMSG_NAME.GLOBAL.DEVELOPERSTORE_EXTRASTOREITEM_TOHOST,
    msg
)
end 
'''
exec_lua(script_lua)
