// Measurement spike: HTTPS staging + Authenticode/publisher gate. Never executes an installer.
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
static bool download(const wchar_t* url,const wchar_t* destination) {
    URL_COMPONENTS parts{sizeof(parts)}; wchar_t host[256],path[4096];
    parts.lpszHostName=host; parts.dwHostNameLength=256; parts.lpszUrlPath=path; parts.dwUrlPathLength=4096;
    if(!WinHttpCrackUrl(url,0,0,&parts) || parts.nScheme!=INTERNET_SCHEME_HTTPS) return false;
    HINTERNET session=WinHttpOpen(L"Cliptiq-T01/1",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,0,0,0);
    if(!session) return false;
    WinHttpSetTimeouts(session,15000,15000,15000,15000);
    HINTERNET connection=WinHttpConnect(session,host,parts.nPort,0);
    HINTERNET request=connection?WinHttpOpenRequest(connection,L"GET",path,0,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE):nullptr;
    DWORD redirect=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    if(request) WinHttpSetOption(request,WINHTTP_OPTION_REDIRECT_POLICY,&redirect,sizeof(redirect));
    bool ok=request && WinHttpSendRequest(request,0,0,0,0,0,0) && WinHttpReceiveResponse(request,0);
    DWORD status=0,size=sizeof(status);
    ok=ok && WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,0,&status,&size,0) && status==200;
    HANDLE file=ok?CreateFileW(destination,GENERIC_WRITE,0,0,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,0):INVALID_HANDLE_VALUE;
    ok=ok && file!=INVALID_HANDLE_VALUE;
    if(ok) { BYTE buffer[32768]; DWORD got,written; unsigned long long total=0;
        for(;;) { if(!WinHttpReadData(request,buffer,sizeof(buffer),&got)){ok=false;break;} if(!got)break;
            total+=got; if(total>256ull*1024*1024 || !WriteFile(file,buffer,got,&written,0) || written!=got){ok=false;break;} }
    }
    if(file!=INVALID_HANDLE_VALUE) {CloseHandle(file); if(!ok)DeleteFileW(destination);}
    if(request)WinHttpCloseHandle(request); if(connection)WinHttpCloseHandle(connection); WinHttpCloseHandle(session);
    return ok;
}
int wmain(int count,wchar_t** args) {
    if(count==4 && wcscmp(args[1],L"--verify")==0) return verify(args[2],args[3])?0:1;
    if(count==5 && wcscmp(args[1],L"--download")==0) {
        if(!download(args[2],args[3]))return 2;
        if(!verify(args[3],args[4])){DeleteFileW(args[3]);return 1;} return 0;
    }
    fwprintf(stderr,L"--verify FILE SHA256_SIGNER_CERT | --download HTTPS_URL NEW_FILE SHA256_SIGNER_CERT\n");return 2;
}
