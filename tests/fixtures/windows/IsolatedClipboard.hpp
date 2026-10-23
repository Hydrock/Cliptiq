#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <stdexcept>
#include <string>

// Test/size-host utility ONLY. Fail before any clipboard access if isolation
// cannot be established. Never fall back to the interactive station.
struct IsolatedClipboard {
 HWINSTA previous=GetProcessWindowStation(), station=nullptr;
 HDESK previous_desktop=GetThreadDesktop(GetCurrentThreadId()),desktop=nullptr;
 IsolatedClipboard() {
  const auto name=L"Cliptiq-test-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
  station=CreateWindowStationW(name.c_str(),0,WINSTA_ACCESSCLIPBOARD|WINSTA_ACCESSGLOBALATOMS|WINSTA_CREATEDESKTOP|WINSTA_ENUMDESKTOPS|WINSTA_READATTRIBUTES|WINSTA_WRITEATTRIBUTES,nullptr);
  if(!station)throw std::runtime_error("CreateWindowStation failed: "+std::to_string(GetLastError()));
  if(!SetProcessWindowStation(station))throw std::runtime_error("SetProcessWindowStation failed: "+std::to_string(GetLastError()));
  desktop=CreateDesktopW(L"test",nullptr,nullptr,0,GENERIC_ALL,nullptr);
  if(!desktop||!SetThreadDesktop(desktop))throw std::runtime_error("Cannot isolate desktop; clipboard tests aborted");
 }
 ~IsolatedClipboard(){SetThreadDesktop(previous_desktop);SetProcessWindowStation(previous);if(desktop)CloseDesktop(desktop);if(station)CloseWindowStation(station);}
};
