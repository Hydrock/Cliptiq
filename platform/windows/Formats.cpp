#include "Formats.hpp"
#include <windows.h>
#include <shlobj.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstring>
#include <limits>
#include <string_view>
using Microsoft::WRL::ComPtr;
namespace cliptiq::windows {
namespace {
Error invalid(){return {ErrorCode::Corrupt,"Malformed native clipboard representation"};}
bool utf8(const std::uint8_t* data,std::size_t size) {
 return !size||(size<=INT_MAX&&MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,reinterpret_cast<const char*>(data),static_cast<int>(size),nullptr,0)>0);
}
bool utf16(const Bytes& data,std::size_t begin,std::size_t end) {
 if((end-begin)%2)return false;
 for(auto i=begin;i<end;i+=2){unsigned c=data[i]|(data[i+1]<<8);if(c>=0xd800&&c<=0xdbff){if(i+3>=end)return false;unsigned d=data[i+2]|(data[i+3]<<8);if(d<0xdc00||d>0xdfff)return false;i+=2;}else if(c>=0xdc00&&c<=0xdfff)return false;}
 return true;
}
std::size_t zero(const Bytes& data,std::size_t begin) {
 for(auto i=begin;i+1<data.size();i+=2)if(!data[i]&&!data[i+1])return i;
 return data.size();
}
struct Dib {std::uint32_t width,height,stride,offset;bool top,alpha;unsigned bits;};
Result<Dib> dib_layout(const Bytes& data,std::uint64_t max_pixels) {
 if(data.size()<sizeof(BITMAPINFOHEADER))return invalid();
 BITMAPINFOHEADER h{};memcpy(&h,data.data(),sizeof(h));
 if((h.biSize!=40&&h.biSize!=108&&h.biSize!=124)||h.biSize>data.size()||h.biWidth<=0||!h.biHeight||h.biHeight==LONG_MIN||h.biPlanes!=1||h.biClrUsed)return invalid();
 const auto width=static_cast<std::uint32_t>(h.biWidth),height=static_cast<std::uint32_t>(h.biHeight<0?-h.biHeight:h.biHeight);
 if(!max_pixels||std::uint64_t(width)*height>max_pixels||std::uint64_t(width)*height>40000000)return Error{ErrorCode::TooLarge,"Image dimensions exceed pixel limit"};
 bool alpha=false;
 if(h.biBitCount==32&&h.biCompression==BI_BITFIELDS&&h.biSize>=108){DWORD masks[4];memcpy(masks,data.data()+40,16);if(masks[0]!=0xff0000||masks[1]!=0xff00||masks[2]!=0xff||(masks[3]&&masks[3]!=0xff000000))return Error{ErrorCode::Unsupported,"Unsupported DIB channel masks"};alpha=masks[3]!=0;}
 else if(h.biCompression!=BI_RGB||(h.biBitCount!=24&&h.biBitCount!=32))return Error{ErrorCode::Unsupported,"Unsupported DIB compression or bit depth"};
 if(h.biSize==124){BITMAPV5HEADER v{};memcpy(&v,data.data(),sizeof(v));if(v.bV5ProfileData||v.bV5ProfileSize)return Error{ErrorCode::Unsupported,"Embedded/linked DIB profiles are not supported"};}
 const auto stride=((std::uint64_t(width)*h.biBitCount+31)/32)*4,amount=stride*height;
 if(stride>UINT_MAX||amount>data.size()-h.biSize)return invalid();
 return Dib{width,height,static_cast<std::uint32_t>(stride),h.biSize,h.biHeight<0,alpha,h.biBitCount};
}
}
Result<HtmlFragment> html_fragment(const Bytes& data) {
 if(data.empty()||data.size()>Limits{}.representation_bytes)return invalid();
 const std::string_view all(reinterpret_cast<const char*>(data.data()),data.size());
 const auto header=all.substr(0,std::min<std::size_t>(all.size(),65536));
 if(header.substr(0,12)!="Version:0.9\r"&&header.substr(0,12)!="Version:1.0\r"&&header.substr(0,12)!="Version:0.9\n"&&header.substr(0,12)!="Version:1.0\n")return invalid();
 auto number=[&](std::string_view key)->std::optional<std::int64_t>{
  const auto where=header.find(key);if(where==std::string_view::npos||(!where?false:header[where-1]!='\n'&&header[where-1]!='\r'))return {};
  auto i=where+key.size();bool negative=i<header.size()&&header[i]=='-';if(negative)++i;
  std::int64_t value=0;std::size_t digits=0;
  for(;i<header.size()&&header[i]>='0'&&header[i]<='9';++i){if(++digits>12)return {};value=value*10+(header[i]-'0');}
  if(!digits||i==header.size()||(header[i]!='\r'&&header[i]!='\n'))return {};
  return negative?-value:value;
 };
 auto sh=number("StartHTML:"),eh=number("EndHTML:"),sf=number("StartFragment:"),ef=number("EndFragment:");
 if(!sh||!eh||!sf||!ef||*sf<0||*ef<*sf||static_cast<std::uint64_t>(*ef)>data.size())return invalid();
 if((*sh==-1)!=(*eh==-1))return invalid();
 auto start=*sh==-1?*sf:*sh,end=*eh==-1?*ef:*eh;
 if(start<0||start>*sf||end<*ef||static_cast<std::uint64_t>(end)>data.size())return invalid();
 // Header fields must precede the data, never be discovered inside HTML.
 auto prefix=all.substr(0,static_cast<std::size_t>(start));
 for(auto field:{"StartHTML:","EndHTML:","StartFragment:","EndFragment:"})if(prefix.find(field)==std::string_view::npos)return invalid();
 if(!utf8(data.data()+start,static_cast<std::size_t>(end-start))||!utf8(data.data()+*sf,static_cast<std::size_t>(*ef-*sf)))return invalid();
 return HtmlFragment{static_cast<std::size_t>(*sf),static_cast<std::size_t>(*ef)};
}
Result<void> validate_native_payload(Representation& r) {
 auto& data=r.data;
 if(r.native_type=="CF_UNICODETEXT"||r.native_type=="UniformResourceLocatorW"){
  auto end=zero(data,0);if(end==data.size()||!utf16(data,0,end))return invalid();data.resize(end+2);
 }else if(r.native_type=="CF_HDROP"){
  if(data.size()<sizeof(DROPFILES))return invalid();DROPFILES h{};memcpy(&h,data.data(),sizeof(h));
  if(!h.fWide)return Error{ErrorCode::Unsupported,"ANSI DROPFILES paths are not supported"};
  if(h.pFiles<sizeof(h)||h.pFiles%2||h.pFiles+4ull>data.size())return invalid();
  auto pos=std::size_t(h.pFiles);unsigned count=0;
  for(;;){auto end=zero(data,pos);if(end==data.size())return invalid();if(end==pos){if(!count)return invalid();data.resize(end+2);break;}if(!utf16(data,pos,end)||++count>4096)return invalid();pos=end+2;}
 }else if(r.native_type=="HTML Format"){
  auto parsed=html_fragment(data);if(!parsed)return parsed.error();
 }else if(r.native_type=="CF_DIB"||r.native_type=="CF_DIBV5"){
  auto parsed=dib_layout(data,40000000);if(!parsed)return parsed.error();
  data.resize(parsed.value().offset+std::size_t(parsed.value().stride)*parsed.value().height);
 }else if(r.native_type=="Preferred DropEffect"){
  if(data.size()<4)return invalid();data.resize(4);
 }
 return {};
}
Result<DecodedImage> decode_image(const Representation& r,std::uint64_t max_pixels) {
 try{
  if(r.platform!="windows"||r.kind!=CanonicalKind::Image||r.data.empty()||r.data.size()>Limits{}.representation_bytes)return invalid();
  const auto init=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
  struct Uninit{HRESULT hr;~Uninit(){if(SUCCEEDED(hr))CoUninitialize();}} cleanup{init};
  if(FAILED(init)&&init!=RPC_E_CHANGED_MODE)return Error{ErrorCode::Io,"COM initialization failed"};
  ComPtr<IWICImagingFactory> factory;
  if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))))return Error{ErrorCode::Io,"WIC unavailable"};
  ComPtr<IWICBitmapSource> source;bool top=true;
  if(r.native_type=="CF_DIBV5"||r.native_type=="CF_DIB"){
   auto parsed=dib_layout(r.data,max_pixels);if(!parsed)return parsed.error();auto d=parsed.value();top=d.top;
   ComPtr<IWICBitmap> bitmap;
   const auto& format=d.bits==24?GUID_WICPixelFormat24bppBGR:(d.alpha?GUID_WICPixelFormat32bppBGRA:GUID_WICPixelFormat32bppBGR);
   if(FAILED(factory->CreateBitmapFromMemory(d.width,d.height,format,d.stride,d.stride*d.height,const_cast<BYTE*>(r.data.data()+d.offset),&bitmap)))return invalid();
   source=bitmap;
  }else if(r.native_type=="PNG"){
   ComPtr<IWICStream> stream;ComPtr<IWICBitmapDecoder> decoder;ComPtr<IWICBitmapFrameDecode> frame;
   if(FAILED(factory->CreateStream(&stream))||FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(r.data.data()),static_cast<DWORD>(r.data.size())))||FAILED(factory->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnDemand,&decoder))||FAILED(decoder->GetFrame(0,&frame)))return invalid();
   source=frame;
  }else return Error{ErrorCode::Unsupported,"Unsupported native image format"};
  UINT width=0,height=0;if(FAILED(source->GetSize(&width,&height))||!width||!height)return invalid();
  if(!max_pixels||std::uint64_t(width)*height>max_pixels||std::uint64_t(width)*height>40000000)return Error{ErrorCode::TooLarge,"Image dimensions exceed pixel limit"};
  ComPtr<IWICFormatConverter> converter;
  if(FAILED(factory->CreateFormatConverter(&converter))||FAILED(converter->Initialize(source.Get(),GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)))return invalid();
  DecodedImage out{width,height,width*4,Bytes(std::size_t(width)*height*4)};
  if(top){if(FAILED(converter->CopyPixels(nullptr,out.stride,static_cast<UINT>(out.bgra.size()),out.bgra.data())))return invalid();}
  else for(UINT y=0;y<height;++y){WICRect row{0,static_cast<INT>(height-1-y),static_cast<INT>(width),1};if(FAILED(converter->CopyPixels(&row,out.stride,out.stride,out.bgra.data()+std::size_t(y)*out.stride)))return invalid();}
  return out;
 }catch(const std::bad_alloc&){return Error{ErrorCode::TooLarge,"Image allocation failed"};}
}
}
