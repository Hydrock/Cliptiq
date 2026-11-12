#include "Secrets.hpp"
#include <Security/Security.h>
namespace cliptiq::lan::native {
namespace {
Error failure(OSStatus status) {
 if(status==errSecItemNotFound) return {ErrorCode::NotFound,"Credential not found"};
 if(status==errSecInteractionNotAllowed || status==errSecAuthFailed || status==errSecUserCanceled)
  return {ErrorCode::Io,"Unlock the macOS login Keychain to access LAN credentials"};
 return {ErrorCode::Io,"macOS Keychain credential operation failed"};
}
CFMutableDictionaryRef query(const std::string& ref) {
 auto q=CFDictionaryCreateMutable(nullptr,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
 CFDictionarySetValue(q,kSecClass,kSecClassGenericPassword);
 CFDictionarySetValue(q,kSecAttrService,CFSTR("Cliptiq/LAN"));
 auto account=CFStringCreateWithBytes(nullptr,reinterpret_cast<const UInt8*>(ref.data()),ref.size(),kCFStringEncodingASCII,false);
 CFDictionarySetValue(q,kSecAttrAccount,account); CFRelease(account);
 // Never leave the clipboard worker waiting for an authentication dialog.
 CFDictionarySetValue(q,kSecUseAuthenticationUI,kSecUseAuthenticationUIFail);
 return q;
}
}
Result<void> write(const std::string& ref,const std::string& key) {
 auto q=query(ref); auto data=CFDataCreate(nullptr,reinterpret_cast<const UInt8*>(key.data()),key.size());
 auto attrs=CFDictionaryCreateMutable(nullptr,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
 CFDictionarySetValue(attrs,kSecValueData,data);
 auto status=SecItemUpdate(q,attrs);
 if(status==errSecItemNotFound) {
  CFDictionarySetValue(q,kSecValueData,data);
  CFDictionarySetValue(q,kSecAttrAccessible,kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly);
  status=SecItemAdd(q,nullptr);
 }
 CFRelease(attrs);CFRelease(data);CFRelease(q);
 if(status!=errSecSuccess) return failure(status);return {};
}
Result<std::string> read(const std::string& ref) {
 auto q=query(ref);CFDictionarySetValue(q,kSecReturnData,kCFBooleanTrue);
 CFTypeRef result=nullptr;auto status=SecItemCopyMatching(q,&result);CFRelease(q);
 if(status!=errSecSuccess) return failure(status);
 if(!result || CFGetTypeID(result)!=CFDataGetTypeID()) {if(result)CFRelease(result);return Error{ErrorCode::Corrupt,"Invalid stored credential"};}
 auto data=static_cast<CFDataRef>(result);
 std::string key(reinterpret_cast<const char*>(CFDataGetBytePtr(data)),CFDataGetLength(data));CFRelease(result);return key;
}
Result<void> erase(const std::string& ref) {
 auto q=query(ref);auto status=SecItemDelete(q);CFRelease(q);
 if(status!=errSecSuccess && status!=errSecItemNotFound)return failure(status);return {};
}
}
