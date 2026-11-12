#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <initializer_list>
#include <map>
namespace cliptiq::windows {
inline void set_application_icons(HWND window){
 struct Cache {
  std::map<int,HICON> icons;
  ~Cache(){for(auto& entry:icons)DestroyIcon(entry.second);}
 };
 static Cache cache;
 auto dpi=GetDpiForWindow(window);if(!dpi)dpi=96;
 for(auto kind:{ICON_SMALL,ICON_BIG}){
  int metric=kind==ICON_SMALL?SM_CXSMICON:SM_CXICON;
  int size=GetSystemMetricsForDpi(metric,dpi);
  auto found=cache.icons.find(size);
  if(found==cache.icons.end()){
   auto icon=reinterpret_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(1),IMAGE_ICON,size,size,0));
   if(icon)found=cache.icons.emplace(size,icon).first;
  }
  if(found!=cache.icons.end())SendMessageW(window,WM_SETICON,kind,reinterpret_cast<LPARAM>(found->second));
 }
}
}
