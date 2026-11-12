#include "Images.hpp"
#include "../windows/Clipboard.hpp"
#define NOMINMAX
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <objidl.h>
using Microsoft::WRL::ComPtr;
namespace cliptiq::lan {
Result<Bytes> network_png(const Representation& offered) {
 try {
  auto r=offered;r.platform="windows";
  if(r.native_type=="image/png"||r.native_type=="public.png")r.native_type="PNG";
  auto decoded=windows::decode_image(r,8000000);
  if(!decoded)return decoded.error();
  auto &i=decoded.value();if(i.width>8192||i.height>8192)return Error{ErrorCode::TooLarge,"LAN image dimensions exceed limit"};
  if(r.native_type=="PNG") {if(r.data.size()>8*1024*1024)return Error{ErrorCode::TooLarge,"LAN image exceeds 8 MiB"};return std::move(r.data);}
  auto initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
  struct Uninit{HRESULT result;~Uninit(){if(SUCCEEDED(result))CoUninitialize();}} uninit{initialized};
  if(FAILED(initialized)&&initialized!=RPC_E_CHANGED_MODE)return Error{ErrorCode::Io,"Cannot initialize image encoder"};
  ComPtr<IWICImagingFactory> factory;ComPtr<IStream> stream;ComPtr<IWICBitmapEncoder> encoder;ComPtr<IWICBitmapFrameEncode> frame;
  if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)))||FAILED(CreateStreamOnHGlobal(nullptr,TRUE,&stream))||FAILED(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder))||FAILED(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache))||FAILED(encoder->CreateNewFrame(&frame,nullptr))||FAILED(frame->Initialize(nullptr))||FAILED(frame->SetSize(i.width,i.height)))return Error{ErrorCode::Io,"Cannot create PNG encoder"};
  auto format=GUID_WICPixelFormat32bppBGRA;
  if(FAILED(frame->SetPixelFormat(&format))||format!=GUID_WICPixelFormat32bppBGRA||FAILED(frame->WritePixels(i.height,i.stride,static_cast<UINT>(i.bgra.size()),i.bgra.data()))||FAILED(frame->Commit())||FAILED(encoder->Commit()))return Error{ErrorCode::Io,"PNG encoding failed"};
  STATSTG stat{};if(FAILED(stream->Stat(&stat,STATFLAG_NONAME))||stat.cbSize.QuadPart>8*1024*1024)return Error{ErrorCode::TooLarge,"LAN PNG exceeds 8 MiB"};
  HGLOBAL memory=nullptr;if(FAILED(GetHGlobalFromStream(stream.Get(),&memory)))return Error{ErrorCode::Io,"PNG stream unavailable"};
  auto bytes=static_cast<const unsigned char*>(GlobalLock(memory));if(!bytes)return Error{ErrorCode::Io,"PNG stream unavailable"};
  struct Unlock{HGLOBAL memory;~Unlock(){GlobalUnlock(memory);}} unlock{memory};
  return Bytes(bytes,bytes+static_cast<size_t>(stat.cbSize.QuadPart));
 }catch(const std::bad_alloc&){return Error{ErrorCode::TooLarge,"LAN image allocation limit"};}
}
}
