#include "Secrets.hpp"
#include <array>
#include <algorithm>
#ifdef _WIN32
#include <windows.h>
#include <wincred.h>
#include <bcrypt.h>
#elif defined(__APPLE__)
#include <Security/Security.h>
#else
#include <sys/random.h>
#include <cerrno>
#endif
namespace cliptiq::lan {
#if !defined(_WIN32)
namespace native {
Result<void> write(const std::string&, const std::string&);
Result<std::string> read(const std::string&);
Result<void> erase(const std::string&);
}
#endif
namespace {
Error io() { return {ErrorCode::Io,"Native credential operation failed"}; }
bool valid_ref(const std::string& s) { return !s.empty() && s.size()<=128 && std::all_of(s.begin(),s.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_';}); }
bool valid_key(const std::string& s) { return s.size()==64 && std::all_of(s.begin(),s.end(),[](unsigned char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');}); }
#ifdef _WIN32
std::wstring target(const std::string& ref) { return L"Cliptiq/LAN/"+std::wstring(ref.begin(),ref.end()); }
#endif
}
Result<std::string> generate_key() {
 std::array<unsigned char,32> bytes{};
#ifdef _WIN32
 if(BCryptGenRandom(nullptr,bytes.data(),static_cast<ULONG>(bytes.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)!=0) return io();
#elif defined(__APPLE__)
 if(SecRandomCopyBytes(kSecRandomDefault,bytes.size(),bytes.data())!=errSecSuccess) return io();
#else
 size_t pos=0; while(pos<bytes.size()) { auto n=getrandom(bytes.data()+pos,bytes.size()-pos,0); if(n<0&&errno==EINTR) continue; if(n<=0) return io(); pos+=static_cast<size_t>(n); }
#endif
 constexpr char hex[]="0123456789abcdef"; std::string out; out.reserve(64); for(auto b:bytes) {out+=hex[b>>4];out+=hex[b&15];} std::fill(bytes.begin(),bytes.end(),0); return out;
}
Result<void> write_secret(const std::string& ref,const std::string& key) {
 if(!valid_ref(ref)||!valid_key(key)) return Error{ErrorCode::InvalidArgument,"Invalid credential reference or key"};
#ifdef _WIN32
 auto name=target(ref); CREDENTIALW c{}; c.Type=CRED_TYPE_GENERIC;c.TargetName=name.data();c.CredentialBlobSize=static_cast<DWORD>(key.size());c.CredentialBlob=reinterpret_cast<LPBYTE>(const_cast<char*>(key.data()));c.Persist=CRED_PERSIST_LOCAL_MACHINE;
 if(!CredWriteW(&c,0)) return io(); return {};
#else
 return native::write(ref,key);
#endif
}
Result<std::string> read_secret(const std::string& ref) {
 if(!valid_ref(ref)) return Error{ErrorCode::InvalidArgument,"Invalid credential reference"};
#ifdef _WIN32
 PCREDENTIALW c=nullptr; auto name=target(ref); if(!CredReadW(name.c_str(),CRED_TYPE_GENERIC,0,&c)) { if(GetLastError()==ERROR_NOT_FOUND) return Error{ErrorCode::NotFound,"Credential not found"}; return io(); }
 std::string key(reinterpret_cast<char*>(c->CredentialBlob),c->CredentialBlobSize); CredFree(c); if(!valid_key(key)) return Error{ErrorCode::Corrupt,"Invalid stored credential"};return key;
#else
 auto key=native::read(ref); if(key && !valid_key(key.value())) return Error{ErrorCode::Corrupt,"Invalid stored credential"}; return key;
#endif
}
Result<void> erase_secret(const std::string& ref) {
 if(!valid_ref(ref)) return Error{ErrorCode::InvalidArgument,"Invalid credential reference"};
#ifdef _WIN32
 auto name=target(ref);if(!CredDeleteW(name.c_str(),CRED_TYPE_GENERIC,0)&&GetLastError()!=ERROR_NOT_FOUND) return io();return {};
#else
 return native::erase(ref);
#endif
}
}
