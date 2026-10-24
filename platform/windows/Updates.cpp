#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>
#include <winhttp.h>
#include "../Updates.hpp"
#include <filesystem>
#include <fstream>
#include <chrono>
namespace cliptiq {
Result<std::string> native_update_feed(){
  auto session=WinHttpOpen(L"Cliptiq/0.1.0",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,nullptr,nullptr,0);
  if(!session)return Error{ErrorCode::Io,"Release check offline"};
  WinHttpSetTimeouts(session,5000,5000,5000,5000);
  auto connection=WinHttpConnect(session,L"github.com",443,0);
  auto request=connection?WinHttpOpenRequest(connection,L"GET",L"/Hydrock/Cliptiq/releases.atom",nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE):nullptr;
  DWORD redirect=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
  bool good=request&&WinHttpSetOption(request,WINHTTP_OPTION_REDIRECT_POLICY,&redirect,sizeof(redirect))&&WinHttpSendRequest(request,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0)&&WinHttpReceiveResponse(request,nullptr);
  DWORD status=0,bytes=sizeof(status);
  good=good&&WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,nullptr,&status,&bytes,nullptr)&&status==200;
  std::string body;char block[4096];auto started=std::chrono::steady_clock::now();
  while(good){DWORD n=0;if(std::chrono::steady_clock::now()-started>std::chrono::seconds(15)){good=false;break;}
    if(!WinHttpReadData(request,block,sizeof(block),&n)){good=false;break;}if(!n)break;
    if(body.size()+n>131072){good=false;break;}body.append(block,n);
  }
  if(request)WinHttpCloseHandle(request);if(connection)WinHttpCloseHandle(connection);WinHttpCloseHandle(session);
  if(!good)return Error{ErrorCode::Io,"Release check offline, timed out or exceeded size limit"};return body;
}
Result<void> NativeUpdates::initialize(){return {};}
bool NativeUpdates::manages_schedule()const{return false;}
std::int64_t NativeUpdates::last_check()const{return 0;}
bool NativeUpdates::automatic_available()const{return true;}
void NativeUpdates::preferences(bool,std::int64_t){}
std::string NativeUpdates::status()const{return metadata_status()+"; explicit signed installer/manager installation. Portable auto-install unavailable (no rollback helper).";}
Result<void> NativeUpdates::check_now(bool automatic){
  (void)automatic;return check_metadata();
}
Result<void> NativeUpdates::open_download(){
  auto r=ShellExecuteW(nullptr,L"open",L"https://github.com/Hydrock/Cliptiq/releases",nullptr,nullptr,SW_SHOWNORMAL);
  if(reinterpret_cast<INT_PTR>(r)<=32)return Error{ErrorCode::Io,"Cannot open project releases"};return {};
}
Result<void> NativeUpdates::export_diagnostics(){
  wchar_t path[32768]=L"Cliptiq-diagnostics.txt";
  OPENFILENAMEW f{};f.lStructSize=sizeof(f);f.lpstrFile=path;f.nMaxFile=32768;f.lpstrFilter=L"Text files\0*.txt\0\0";f.lpstrDefExt=L"txt";f.Flags=OFN_OVERWRITEPROMPT|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST;
  if(!GetSaveFileNameW(&f)){if(CommDlgExtendedError())return Error{ErrorCode::Io,"Cannot open diagnostics picker"};return {};}
  std::ofstream out(std::filesystem::path(path),std::ios::binary);out<<local_diagnostics();out.flush();
  if(!out)return Error{ErrorCode::Io,"Cannot export local diagnostics"};return {};
}
}
