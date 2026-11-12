#include "cliptiq/settings.hpp"
#include <iostream>
using namespace cliptiq;
int main(int argc, char** argv) {
  if(argc!=3) return 2;
  auto opened=open_store(argv[1]);
  if(!opened) return 3;
  auto store=std::move(opened.value());
  auto s=default_settings(CapturePlatform::Windows);
  s.values["auto_paste"]="0";
  s.values["history_count"]="1000";
  // Keep the real capture listener running, but do not retain ambient clipboard
  // payload during a benchmark. History is populated with synthetic text only.
  s.values["unknown_source"]="2";
  for(auto key:{"type_text","type_rtf","type_rtfd","type_html","type_pdf","type_files","type_url","type_image"}) s.values[key]="0";
  for(auto& k:s.hotkeys) k.second.reset();
  s.hotkeys["settings"]=KeyChord{Control|Alt|Shift,"F11"};
  auto saved=save_settings(store,s);
  if(!saved){std::cerr<<saved.error().message;return 4;}
  auto n=std::stoi(argv[2]);
  for(int i=0;i<n;++i){
    std::string text="Cliptiq synthetic idle fixture "+std::to_string(i)+std::string(1024,'x');
    ClipboardSnapshot snapshot;
    snapshot.representations={{0,"windows","CF_UNICODETEXT",CanonicalKind::Text,Bytes(text.begin(),text.end())}};
    auto result=ingest(store,std::move(snapshot),HistoryPolicy{1000},1000+i);
    if(!result)return 5;
  }
  std::cout<<"Prepared "<<n<<" synthetic entries\n";
}
