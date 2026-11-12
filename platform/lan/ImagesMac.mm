#include "Images.hpp"
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
namespace cliptiq::lan {
Result<Bytes> network_png(const Representation& r) { @autoreleasepool { try {
 if(r.kind!=CanonicalKind::Image||r.data.empty()||r.data.size()>32*1024*1024)return Error{ErrorCode::TooLarge,"Invalid LAN image"};
 auto data=[NSData dataWithBytesNoCopy:const_cast<unsigned char*>(r.data.data()) length:r.data.size() freeWhenDone:NO];
 auto source=CGImageSourceCreateWithData((__bridge CFDataRef)data,nullptr);if(!source)return Error{ErrorCode::Corrupt,"ImageIO rejected LAN image"};
 struct Source {CGImageSourceRef value;~Source(){CFRelease(value);}} sourceGuard{source};
 NSDictionary *properties=CFBridgingRelease(CGImageSourceCopyPropertiesAtIndex(source,0,nullptr));
 auto w=[properties[(__bridge NSString*)kCGImagePropertyPixelWidth] unsignedLongLongValue],h=[properties[(__bridge NSString*)kCGImagePropertyPixelHeight] unsignedLongLongValue];
 if(!w||!h||w>8192||h>8192||w*h>8000000)return Error{ErrorCode::TooLarge,"LAN image dimensions exceed limit"};
 auto image=CGImageSourceCreateImageAtIndex(source,0,nullptr);if(!image)return Error{ErrorCode::Corrupt,"ImageIO decode failed"};
 struct Image {CGImageRef value;~Image(){CGImageRelease(value);}} imageGuard{image};
 if(r.native_type=="image/png"||r.native_type=="public.png"){if(r.data.size()>8*1024*1024)return Error{ErrorCode::TooLarge,"LAN PNG exceeds 8 MiB"};return r.data;}
 auto output=[NSMutableData data];auto destination=CGImageDestinationCreateWithData((__bridge CFMutableDataRef)output,CFSTR("public.png"),1,nullptr);
 if(!destination)return Error{ErrorCode::Io,"PNG encoder unavailable"};CGImageDestinationAddImage(destination,image,nullptr);bool encoded=CGImageDestinationFinalize(destination);CFRelease(destination);
 if(!encoded)return Error{ErrorCode::Io,"PNG encoding failed"};if(output.length>8*1024*1024)return Error{ErrorCode::TooLarge,"LAN PNG exceeds 8 MiB"};
 auto bytes=static_cast<const unsigned char*>(output.bytes);return Bytes(bytes,bytes+output.length);
 }catch(const std::bad_alloc&){return Error{ErrorCode::TooLarge,"LAN image allocation limit"};} } }
}
