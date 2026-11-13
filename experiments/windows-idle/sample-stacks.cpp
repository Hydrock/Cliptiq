#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#include <psapi.h>
#include <iostream>
#include <vector>
#include <map>
#include <string>
#include <algorithm>
#include <chrono>
int main(int argc,char**argv){if(argc!=3)return 2;DWORD pid=std::stoul(argv[1]),tid=std::stoul(argv[2]);auto p=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,FALSE,pid);if(!p)return 3;wchar_t path[32768];DWORD n=32768;if(!QueryFullProcessImageNameW(p,0,path,&n)||std::wstring(path).find(L"Cliptiq")==std::wstring::npos)return 4;auto t=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,tid);if(!t||GetProcessIdOfThread(t)!=pid)return 5;
 SymSetOptions(SYMOPT_UNDNAME|SYMOPT_DEFERRED_LOADS|SYMOPT_FAIL_CRITICAL_ERRORS|SYMOPT_NO_PROMPTS);if(!SymInitialize(p,"",TRUE))return 6;
 std::map<std::vector<DWORD64>,unsigned> stacks;
 for(int i=0;i<400;++i){
  if(SuspendThread(t)==DWORD(-1))return 7;
  CONTEXT ctx{};ctx.ContextFlags=CONTEXT_FULL;std::vector<DWORD64> frames;
  if(GetThreadContext(t,&ctx)){STACKFRAME64 frame{};frame.AddrPC={ctx.Rip,0,AddrModeFlat};frame.AddrStack={ctx.Rsp,0,AddrModeFlat};frame.AddrFrame={ctx.Rbp,0,AddrModeFlat};frames.push_back(ctx.Rip);for(int j=0;j<18;++j){if(!StackWalk64(IMAGE_FILE_MACHINE_AMD64,p,t,&frame,&ctx,nullptr,SymFunctionTableAccess64,SymGetModuleBase64,nullptr)||!frame.AddrPC.Offset)break;frames.push_back(frame.AddrPC.Offset);}}
  ResumeThread(t);++stacks[frames];Sleep(11+(i%7));
 }
 std::vector<std::pair<std::vector<DWORD64>,unsigned>> sorted(stacks.begin(),stacks.end());std::sort(sorted.begin(),sorted.end(),[](auto&a,auto&b){return a.second>b.second;});
 for(auto& [frames,count]:sorted){std::cout<<"SAMPLES "<<count<<'\n';for(auto address:frames){IMAGEHLP_MODULE64 module{};module.SizeOfStruct=sizeof module;SymGetModuleInfo64(p,address,&module);char buf[sizeof(SYMBOL_INFO)+512]{};auto symbol=reinterpret_cast<SYMBOL_INFO*>(buf);symbol->SizeOfStruct=sizeof(SYMBOL_INFO);symbol->MaxNameLen=511;DWORD64 offset=0;bool named=SymFromAddr(p,address,&offset,symbol);std::cout<<"  "<<module.ModuleName<<"!"<<(named?symbol->Name:"?")<<"+0x"<<std::hex<<(named?offset:address-module.BaseOfImage)<<std::dec<<'\n';}}
 SymCleanup(p);CloseHandle(t);CloseHandle(p);return 0;
}
