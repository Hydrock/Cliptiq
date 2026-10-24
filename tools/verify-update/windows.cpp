// Optional Authenticode/publisher verification utility. Never executes code.
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <winhttp.h>
#include <softpub.h>
#include <wintrust.h>
#include <wincrypt.h>
#include <cstdio>
#include <cwchar>
#pragma comment(lib,"winhttp.lib")
#pragma comment(lib,"wintrust.lib")
#pragma comment(lib,"crypt32.lib")
static bool verify(const wchar_t* path,const wchar_t* expected) {
    WINTRUST_FILE_INFO file{sizeof(file)}; file.pcwszFilePath=path;
    WINTRUST_DATA data{sizeof(data)}; data.dwUIChoice=WTD_UI_NONE;
    data.fdwRevocationChecks=WTD_REVOKE_WHOLECHAIN; data.dwUnionChoice=WTD_CHOICE_FILE;
    data.pFile=&file; data.dwStateAction=WTD_STATEACTION_VERIFY;
    data.dwProvFlags=WTD_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT;
    GUID action=WINTRUST_ACTION_GENERIC_VERIFY_V2;
    LONG status=WinVerifyTrust((HWND)INVALID_HANDLE_VALUE,&action,&data);
    bool match=false;
    if(status==ERROR_SUCCESS) {
        auto provider=WTHelperProvDataFromStateData(data.hWVTStateData);
        auto signer=WTHelperGetProvSignerFromChain(provider,0,FALSE,0);
        if(signer && signer->csCertChain) {
            BYTE hash[32]; DWORD size=sizeof(hash);
            if(CertGetCertificateContextProperty(signer->pasCertChain[0].pCert,CERT_SHA256_HASH_PROP_ID,hash,&size) && size==32) {
                wchar_t hex[65]{}; for(int i=0;i<32;i++) swprintf_s(hex+i*2,3,L"%02x",hash[i]);
                match=_wcsicmp(hex,expected)==0;
            }
        }
    }
    data.dwStateAction=WTD_STATEACTION_CLOSE; WinVerifyTrust((HWND)INVALID_HANDLE_VALUE,&action,&data);
    wprintf(L"trust_status=0x%08lx publisher_pin_match=%d\n",status,match);
    return status==ERROR_SUCCESS && match;
}

int wmain(int count,wchar_t** args) {
 if(count!=4 || wcscmp(args[1],L"--verify")!=0 || wcslen(args[3])!=64)return 2;
 for(auto p=args[3];*p;++p)if(!((*p>=L'0'&&*p<=L'9')||(*p>=L'a'&&*p<=L'f')||(*p>=L'A'&&*p<=L'F')))return 2;
 HANDLE file=CreateFileW(args[2],GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
 if(file==INVALID_HANDLE_VALUE)return 1;
 BY_HANDLE_FILE_INFORMATION info{};LARGE_INTEGER size{};
 bool regular=GetFileInformationByHandle(file,&info)&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))&&GetFileSizeEx(file,&size)&&size.QuadPart>0&&size.QuadPart<=5ll*1024*1024;
 bool good=regular&&verify(args[2],args[3]);CloseHandle(file);return good?0:1;
}
