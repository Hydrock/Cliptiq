#include "cliptiq/settings.hpp"
#include "cliptiq/snippets.hpp"
#include <iostream>
using namespace cliptiq;
int main(int argc,char**argv){if(argc!=2)return 2;auto opened=open_store(argv[1]);if(!opened)return 3;auto db=std::move(opened.value());
 auto settings=default_settings(CapturePlatform::Windows);settings.values["auto_paste"]="0";
 for(auto key:{"type_text","type_rtf","type_rtfd","type_html","type_pdf","type_files","type_url","type_image"})settings.values[key]="0";
 for(auto& binding:settings.hotkeys)binding.second.reset();settings.hotkeys["settings"]=KeyChord{Control|Alt|Shift,"F11"};
 if(!save_settings(db,settings))return 4;
 const wchar_t* values[]={L"Cliptiq — история под рукой\r\nСкопированные тексты, ссылки и изображения хранятся локально.",L"План на сегодня\r\n• Завершить макет приложения\r\n• Проверить горячие клавиши\r\n• Сделать перерыв",L"https://github.com/Hydrock/Cliptiq"};
 for(auto text:values){Bytes bytes;for(;*text;++text){bytes.push_back(unsigned(*text)&255);bytes.push_back(unsigned(*text)>>8);}bytes.push_back(0);bytes.push_back(0);ClipboardSnapshot s;s.representations={{0,"windows","CF_UNICODETEXT",CanonicalKind::Text,std::move(bytes),TextEncoding::Utf16Le}};if(!ingest(db,std::move(s),{},1000+bytes.size()))return 5;}
 auto folder=create_folder(db,"Работа");if(!folder)return 6;Snippet s;s.folder_id=folder.value();s.title="Приветствие";s.content="Здравствуйте!\nСпасибо за обращение. Я посмотрю информацию и вернусь с ответом.";if(!save_snippet(db,s))return 7;
 std::cout<<"Created synthetic workspace preview profile\n";
}
