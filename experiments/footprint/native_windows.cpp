#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <psapi.h>
#include "core.h"
#include "native.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>

static HWND window = nullptr;
static bool hotkey = false;
static bool tray = false;
static std::wstring wide(const char* text, int len) {
    int count = MultiByteToWideChar(CP_UTF8, 0, text, len, nullptr, 0);
    std::wstring result(count, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, len, result.data(), count); return result;
}
static void set_data(UINT format, const void* bytes, size_t count) {
    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, count);
    if (!handle) return;
    void* memory = GlobalLock(handle);
    if (!memory) { GlobalFree(handle); return; }
    std::memcpy(memory, bytes, count); GlobalUnlock(handle);
    if (!SetClipboardData(format, handle)) GlobalFree(handle);
}
static void copy_entry(int id) {
    if (!OpenClipboard(window)) return;
    EmptyClipboard();
    unsigned char data[4096]; int len = probe_get(id, "text/plain", data, sizeof data);
    if (len >= 0) { auto text = wide(reinterpret_cast<char*>(data), len); set_data(CF_UNICODETEXT, text.c_str(), (text.size()+1)*sizeof(wchar_t)); }
    len = probe_get(id, "image/png", data, sizeof data);
    if (len >= 0) set_data(RegisterClipboardFormatW(L"PNG"), data, len);
    len = probe_get(id, "text/rtf", data, sizeof data);
    if (len >= 0) { data[len] = 0; set_data(RegisterClipboardFormatW(L"Rich Text Format"), data, len+1); }
    len = probe_get(id, "text/html", data, sizeof data);
    if (len >= 0) {
        const std::string prefix = "<html><body><!--StartFragment-->";
        const std::string suffix = "<!--EndFragment--></body></html>";
        const char* pattern = "Version:0.9\r\nStartHTML:%010d\r\nEndHTML:%010d\r\nStartFragment:%010d\r\nEndFragment:%010d\r\n";
        char header[256]; int head = std::snprintf(header,sizeof header,pattern,0,0,0,0);
        std::snprintf(header,sizeof header,pattern,head,head+int(prefix.size())+len+int(suffix.size()),head+int(prefix.size()),head+int(prefix.size())+len);
        std::string html = std::string(header)+prefix+std::string(reinterpret_cast<char*>(data),len)+suffix;
        set_data(RegisterClipboardFormatW(L"HTML Format"),html.c_str(),html.size()+1);
    }
    CloseClipboard();
}
static void popup() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, L"Copy text fixture");
    AppendMenuW(menu, MF_STRING, 2, L"Copy PNG fixture");
    AppendMenuW(menu, MF_STRING, 3, L"Copy HTML fixture");
    AppendMenuW(menu, MF_STRING, 4, L"Copy RTF fixture");
    AppendMenuW(menu, MF_STRING, 5, L"Settings / footprint prototype");
    AppendMenuW(menu, MF_STRING, 6, L"Quit");
    POINT pos; GetCursorPos(&pos); SetForegroundWindow(window);
    int action = TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,pos.x,pos.y,0,window,nullptr);
    if (action >= 1 && action <= 4) copy_entry(action == 1 ? 0 : action + 28);
    if (action == 5) ShowWindow(window, SW_SHOW);
    if (action == 6) DestroyWindow(window);
    DestroyMenu(menu);
}
static LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
    if (message == WM_HOTKEY || (message == WM_APP+1 && (lp == WM_RBUTTONUP || lp == WM_LBUTTONUP))) { popup(); return 0; }
    if (message == WM_COMMAND && LOWORD(wp)==10) { popup(); return 0; }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(hwnd,message,wp,lp);
}
bool native_setup(bool visible) {
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW cls{}; cls.lpfnWndProc=procedure; cls.hInstance=instance; cls.lpszClassName=L"CliptiqFootprint";
    cls.hCursor=LoadCursorW(nullptr,IDC_ARROW); cls.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
    if (!RegisterClassW(&cls)) return false;
    window=CreateWindowW(cls.lpszClassName,L"Cliptiq — measurement prototype",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,480,220,nullptr,nullptr,instance,nullptr);
    if (!window) return false;
    CreateWindowW(L"STATIC",L"33 fixture entries / 35 representations\nSame Win32 shell for both cores.\nNo real clipboard history is monitored.",WS_CHILD|WS_VISIBLE,20,20,420,80,window,nullptr,instance,nullptr);
    CreateWindowW(L"BUTTON",L"Fixture menu",WS_CHILD|WS_VISIBLE,20,115,160,30,window,(HMENU)10,instance,nullptr);
    hotkey=RegisterHotKey(window,1,MOD_CONTROL|MOD_ALT|MOD_SHIFT|MOD_NOREPEAT,VK_F12)!=0;
    if (visible) {
        NOTIFYICONDATAW icon{}; icon.cbSize=sizeof icon; icon.hWnd=window; icon.uID=1;
        icon.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP; icon.uCallbackMessage=WM_APP+1;
        icon.hIcon=LoadIconW(nullptr,IDI_APPLICATION); lstrcpyW(icon.szTip,L"Cliptiq footprint prototype");
        tray=Shell_NotifyIconW(NIM_ADD,&icon)!=0; ShowWindow(window,SW_SHOW);
    }
    return true;
}
void native_loop() { MSG msg; while (GetMessageW(&msg,nullptr,0,0)>0) { TranslateMessage(&msg); DispatchMessageW(&msg); } }
void native_schedule_close() { PostMessageW(window,WM_CLOSE,0,0); }
void native_cleanup() {
    if (tray) { NOTIFYICONDATAW icon{}; icon.cbSize=sizeof icon; icon.hWnd=window; icon.uID=1; Shell_NotifyIconW(NIM_DELETE,&icon); }
    if (hotkey) UnregisterHotKey(window,1);
    if (IsWindow(window)) DestroyWindow(window);
}
size_t native_memory() { PROCESS_MEMORY_COUNTERS counts{}; counts.cb=sizeof counts; return GetProcessMemoryInfo(GetCurrentProcess(),&counts,sizeof counts) ? counts.WorkingSetSize : 0; }
