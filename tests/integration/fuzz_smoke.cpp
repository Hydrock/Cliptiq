#include "../../platform/Media.hpp"
#include "cliptiq/xml.hpp"
#include "cliptiq/ocr.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace cliptiq;
int main(int argc,char**argv){try{
 if(argc!=2)return 2;std::ifstream file(argv[1],std::ios::binary);Bytes png((std::istreambuf_iterator<char>(file)),{});if(png.empty()||png.size()>65536)throw std::runtime_error("bounded PNG seed missing");
 const std::string xml="<folders><folder><title>Seed</title><snippets><snippet><title>A</title><content>text</content></snippet></snippets></folder></folders>";std::uint32_t state=0x170117u;
 for(unsigned n=0;n<512;++n){state^=state<<13;state^=state>>17;state^=state<<5;auto image=png;if(n%3==0)image.resize(state%(image.size()+1));else image[state%image.size()]^=static_cast<unsigned char>((state>>8)|1);auto framing=validate_screenshot_png(image);
  if(framing){auto decoded=decode_native_image({0,
#ifdef _WIN32
 "windows","PNG",
#elif defined(__APPLE__)
 "macos","public.png",
#else
 "x11","image/png",
#endif
 CanonicalKind::Image,std::move(image)});if(decoded&&std::uint64_t(decoded.value().width)*decoded.value().height>media_pixel_limit)throw std::runtime_error("codec exceeded pixel bound");}
  Bytes input(xml.begin(),xml.end());if(n%3==1)input.resize(state%(input.size()+1));else input[state%input.size()]=static_cast<unsigned char>(state);auto parsed=decode_clipy_xml(input,{65536,32,64,1024,4096,16384});if(parsed&&parsed.value().size()>32)throw std::runtime_error("XML folder bound exceeded");
  std::string text(input.begin(),input.end());auto ocr=truncate_ocr_text(text);if(ocr&&ocr.value().size()>1048576)throw std::runtime_error("OCR text bound exceeded");
 }
 // Real system codec receives an intact synthetic PNG as a positive control.
 auto valid=decode_native_image({0,
#ifdef _WIN32
 "windows","PNG",
#elif defined(__APPLE__)
 "macos","public.png",
#else
 "x11","image/png",
#endif
 CanonicalKind::Image,png});if(!valid)throw std::runtime_error("valid native PNG control failed");
 std::cout<<"PASS512 deterministic bounded XML/PNG/UTF8 mutations and native codec control\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
