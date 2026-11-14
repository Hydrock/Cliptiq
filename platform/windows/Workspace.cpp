#include "Workspace.hpp"
#include <algorithm>
#include <string>
#include <stdexcept>
namespace cliptiq::windows {
namespace {
constexpr int History=7101,Snippets=7102,Settings=7103,Copy=7105,Erase=7106,Clear=7107,Filter=7109,List=7110;
LRESULT CALLBACK panel_proc(HWND w,UINT m,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR){
 if(m==WM_SIZE){if(auto child=GetWindow(w,GW_CHILD)){RECT r{};GetClientRect(w,&r);MoveWindow(child,0,0,r.right,r.bottom,TRUE);}}
 if(m==WM_NCDESTROY)RemoveWindowSubclass(w,panel_proc,id);
 return DefSubclassProc(w,m,wp,lp);
}
LRESULT CALLBACK preview_proc(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR){
 // Retain native focus, selection, scrolling and copying. The read-only EDIT
 // creates its caret during default focus handling; hide that caret once.
 auto result=DefSubclassProc(window,message,wp,lp);
 if(message==WM_SETFOCUS)HideCaret(window);
 if(message==WM_NCDESTROY)RemoveWindowSubclass(window,preview_proc,id);
 return result;
}
std::wstring wide(const std::string& s){int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);std::wstring r(n,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),r.data(),n);return r;}
std::wstring lower(std::wstring s){int n=LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,s.data(),int(s.size()),nullptr,0,nullptr,nullptr,0);if(n>0){std::wstring r(n,L'\0');LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,s.data(),int(s.size()),r.data(),n,nullptr,nullptr,0);return r;}return s;}
std::wstring multiline(const std::string& text){auto source=wide(text);std::wstring out;for(auto c:source){if(c==L'\n'&&(out.empty()||out.back()!=L'\r'))out+=L'\r';out+=c;}return out;}
bool high_contrast(){HIGHCONTRASTW value{sizeof(value)};return SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(value),&value,0)&&(value.dwFlags&HCF_HIGHCONTRASTON);}
void fill(HDC dc,RECT rect,COLORREF color){SetDCBrushColor(dc,color);FillRect(dc,&rect,reinterpret_cast<HBRUSH>(GetStockObject(DC_BRUSH)));}
const wchar_t* type(CanonicalKind k){switch(k){case CanonicalKind::Image:return L"Изображение";case CanonicalKind::Files:return L"Файлы";case CanonicalKind::Url:return L"Ссылка";case CanonicalKind::Rtf:return L"RTF";case CanonicalKind::Html:return L"HTML";case CanonicalKind::Pdf:return L"PDF";default:return L"Текст";}}
}
int Workspace::px(int v)const{return MulDiv(v,dpi_,96);}
HWND Workspace::control(const wchar_t* cls,const wchar_t* text,DWORD style,int id,DWORD ex){
 auto w=CreateWindowExW(ex,cls,text,WS_CHILD|WS_VISIBLE|style,0,0,1,1,owner_,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
 if(!w)throw std::runtime_error("Cannot create workspace control");
 SendMessageW(w,WM_SETFONT,reinterpret_cast<WPARAM>(font_),TRUE);children_.push_back(w);return w;
}
Workspace::Workspace(HWND owner,const NativeMenuOptions& options,std::function<void(MenuCommand)> action,MenuPreviewProvider provider)
 :owner_(owner),action_(std::move(action)),options_(options),provider_(std::move(provider)),dpi_(GetDpiForWindow(owner)){
 if(!dpi_)dpi_=96;
 high_contrast_=high_contrast();
 font_=CreateFontW(-MulDiv(10,dpi_,72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
 title_font_=CreateFontW(-MulDiv(16,dpi_,72),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
 bold_font_=CreateFontW(-MulDiv(11,dpi_,72),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
 auto brand=control(L"STATIC",L"Cliptiq",0,7190);SendMessageW(brand,WM_SETFONT,reinterpret_cast<WPARAM>(title_font_),TRUE);

 history_=control(L"BUTTON",L"История",BS_OWNERDRAW|WS_TABSTOP,History);
 snippets_=control(L"BUTTON",L"Сниппеты",BS_OWNERDRAW|WS_TABSTOP,Snippets);
 settings_=control(L"BUTTON",L"Настройки",BS_PUSHBUTTON|WS_TABSTOP,Settings);

 quit_=control(L"BUTTON",L"Выйти из Cliptiq",BS_PUSHBUTTON|WS_TABSTOP,WorkspaceQuit);
 heading_=control(L"STATIC",L"История буфера",0,7192);SendMessageW(heading_,WM_SETFONT,reinterpret_cast<WPARAM>(bold_font_),TRUE);
 summary_=control(L"STATIC",L"",0,7193);
 filter_=control(L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,Filter,WS_EX_CLIENTEDGE);
 SendMessageW(filter_,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"Фильтр записей · Ctrl+F"));SendMessageW(filter_,EM_SETLIMITTEXT,256,0);
 INITCOMMONCONTROLSEX init{sizeof(init),ICC_LISTVIEW_CLASSES};InitCommonControlsEx(&init);
 list_=control(WC_LISTVIEWW,L"",WS_TABSTOP|LVS_REPORT|LVS_OWNERDATA|LVS_SINGLESEL|LVS_SHOWSELALWAYS|LVS_NOSORTHEADER|LVS_NOCOLUMNHEADER|WS_BORDER,List);
 ListView_SetExtendedListViewStyle(list_,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_LABELTIP);
 row_images_=ImageList_Create(1,px(56),ILC_COLOR32,1,1);ListView_SetImageList(list_,row_images_,LVSIL_SMALL);
 LVCOLUMNW col{};col.mask=LVCF_TEXT|LVCF_WIDTH;col.pszText=const_cast<wchar_t*>(L"Содержимое");col.cx=px(300);ListView_InsertColumn(list_,0,&col);col.pszText=const_cast<wchar_t*>(L"Тип / папка");col.cx=px(110);ListView_InsertColumn(list_,1,&col);col.pszText=const_cast<wchar_t*>(L"Источник");col.cx=px(100);ListView_InsertColumn(list_,2,&col);
 preview_heading_=control(L"STATIC",L"Предпросмотр",0,7194);
 preview_=control(L"EDIT",L"Выберите запись слева.",ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL|WS_TABSTOP,7195);
 if(!SetWindowSubclass(preview_,preview_proc,1,0))throw std::runtime_error("Cannot configure workspace preview");
 image_=control(L"STATIC",L"",SS_OWNERDRAW,7196);ShowWindow(image_,SW_HIDE);
 copy_=control(L"BUTTON",L"Копировать",WS_TABSTOP|BS_OWNERDRAW,Copy);
 erase_=control(L"BUTTON",L"Удалить…",WS_TABSTOP,Erase);
 clear_=control(L"BUTTON",L"Очистить историю…",WS_TABSTOP,Clear);
 snippet_panel_=control(L"STATIC",L"",WS_CLIPCHILDREN,7198,WS_EX_CONTROLPARENT);
 SetWindowSubclass(snippet_panel_,panel_proc,1,0);ShowWindow(snippet_panel_,SW_HIDE);
 status_=control(L"STATIC",wide(options.workspace_status).c_str(),0,7197);
 layout(dpi_);
}
Workspace::~Workspace(){
 if(editor_mounted_&&options_.workspace_snippets)options_.workspace_snippets(0,{});
 if(options_.workspace_image)options_.workspace_image(std::nullopt);
 for(auto w:children_)if(IsWindow(w))DestroyWindow(w);
 if(bitmap_)DeleteObject(bitmap_);if(row_images_)ImageList_Destroy(row_images_);
 if(font_)DeleteObject(font_);if(title_font_)DeleteObject(title_font_);if(bold_font_)DeleteObject(bold_font_);
}
void Workspace::layout(unsigned dpi){
 if(dpi && dpi!=dpi_){
  auto make=[&](int size,int weight){return CreateFontW(-MulDiv(size,dpi,72),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");};
  auto normal=make(10,FW_NORMAL),title=make(16,FW_SEMIBOLD),bold=make(11,FW_SEMIBOLD);
  if(normal&&title&&bold){
   for(auto child:children_)SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(child==GetDlgItem(owner_,7190)?title:child==heading_?bold:normal),TRUE);
   DeleteObject(font_);DeleteObject(title_font_);DeleteObject(bold_font_);font_=normal;title_font_=title;bold_font_=bold;
  }else {if(normal)DeleteObject(normal);if(title)DeleteObject(title);if(bold)DeleteObject(bold);}
 }
 dpi_=dpi?dpi:96;RECT r{};GetClientRect(owner_,&r);const int w=MulDiv(r.right,96,dpi_),h=MulDiv(r.bottom,96,dpi_);
 auto place=[&](HWND c,int x,int y,int cw,int ch){MoveWindow(c,px(x),px(y),px(std::max(1,cw)),px(std::max(1,ch)),TRUE);};
 place(GetDlgItem(owner_,7190),20,16,144,32);
 place(history_,12,80,152,42);place(snippets_,12,128,152,42);place(settings_,12,h-104,152,34);place(quit_,12,h-62,152,34);
 const int left=200,body=std::max(460,w-left-24),listw=std::max(250,(body*55)/100),detail=left+listw+24,detailw=w-detail-24;
 place(snippet_panel_,left,80,w-left-24,h-116);
 place(heading_,left,20,body,28);place(summary_,left,50,listw,22);place(filter_,left,84,listw,32);
 place(list_,left,124,listw,h-220);place(preview_heading_,detail,84,detailw,28);place(preview_,detail,124,detailw,h-220);place(image_,detail,124,detailw,h-220);
 const int secondary=100,gap=12,primary=std::max(100,detailw-secondary-gap);
 place(copy_,detail,h-80,primary,40);place(erase_,detail+primary+gap,h-76,secondary,32);place(clear_,left,h-76,164,32);place(status_,left,h-31,w-left-24,23);
 if(row_images_){ImageList_SetIconSize(row_images_,1,px(56));}
 fit_column();
 InvalidateRect(owner_,nullptr,TRUE);
}
const WorkspaceRow* Workspace::selection()const{int n=ListView_GetNextItem(list_,-1,LVNI_SELECTED);return n>=0&&size_t(n)<visible_.size()?&rows_[visible_[n]]:nullptr;}
Result<void> Workspace::select_page(MenuKind page){
 if(page!=MenuKind::History&&page!=MenuKind::Snippets)return Error{ErrorCode::InvalidArgument,"Invalid workspace page"};
 if(page==MenuKind::Snippets && options_.workspace_snippets && !editor_mounted_){
  auto mounted=options_.workspace_snippets(reinterpret_cast<std::uintptr_t>(snippet_panel_),[this](const std::string& id){
   auto latest=options_.workspace_model?options_.workspace_model():Result<MenuModel>(model_);
   if(!latest){status(latest.error().message);return;}
   MenuCommand command{MenuAction::SelectSnippet,id,latest.value().revision};command.invocation.copy_only=true;action_(std::move(command));
  });
  if(!mounted){status(mounted.error().message);return mounted;}
  editor_mounted_=true;
 }
 page_=page;
 bool editing=page_==MenuKind::Snippets&&editor_mounted_;
 for(auto w:{summary_,filter_,list_,preview_heading_,preview_,image_,copy_,erase_,clear_})ShowWindow(w,editing?SW_HIDE:SW_SHOW);
 ShowWindow(snippet_panel_,editing?SW_SHOW:SW_HIDE);
 if(editing){SetWindowTextW(heading_,L"Сниппеты");if(options_.workspace_image)options_.workspace_image(std::nullopt);if(bitmap_){DeleteObject(bitmap_);bitmap_=nullptr;}if(auto child=GetWindow(snippet_panel_,GW_CHILD))SetFocus(GetDlgItem(child,501));}
 else filter(true);
 InvalidateRect(history_,nullptr,TRUE);InvalidateRect(snippets_,nullptr,TRUE);return {};
}
void Workspace::update(MenuModel m){model_=std::move(m);if(page_!=MenuKind::Snippets||!editor_mounted_)filter(true);}
void Workspace::fit_column(){
 RECT area{};GetClientRect(list_,&area);
 ListView_SetColumnWidth(list_,1,0);ListView_SetColumnWidth(list_,2,0);
 ListView_SetColumnWidth(list_,0,std::max(1,int(area.right)));
}
void Workspace::filter(bool rebuild){
 auto old=selection();std::string id=old?old->item.id:"";
 changing_=true;
 if(rebuild){
  rows_=workspace_rows(model_,page_);text_.clear();text_.reserve(rows_.size());
  for(const auto& row:rows_){
   RowText text;text.title=wide(row.item.label);
   text.metadata=(page_==MenuKind::Snippets?wide(row.group):std::wstring(type(row.item.kind)))+L" · "+(row.item.source_device.empty()?std::wstring(L"Это устройство"):wide(row.item.source_device));
   text.accessible=text.title+L" — "+text.metadata;text_.push_back(std::move(text));
  }
 }
 visible_.clear();SendMessageW(list_,WM_SETREDRAW,FALSE,0);ListView_SetItemCountEx(list_,0,0);
 wchar_t buffer[257]{};GetWindowTextW(filter_,buffer,257);auto query=lower(buffer);int selected_index=-1;
 for(size_t i=0;i<rows_.size();++i){const auto& row=rows_[i];
  // Keep only short display strings cached. Tooltips may contain 64 KiB of
  // payload; materialize their search text only while a nonempty filter runs.
  if(!query.empty()&&lower(wide(row.item.label+" "+row.item.tooltip+" "+row.group+" "+row.item.source_device)).find(query)==std::wstring::npos)continue;
  if(row.item.id==id)selected_index=int(visible_.size());visible_.push_back(i);
 }
 ListView_SetItemCountEx(list_,int(visible_.size()),LVSICF_NOINVALIDATEALL);
 if(selected_index<0&&!visible_.empty())selected_index=0;if(selected_index>=0)ListView_SetItemState(list_,selected_index,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
 fit_column();SendMessageW(list_,WM_SETREDRAW,TRUE,0);InvalidateRect(list_,nullptr,TRUE);changing_=false;
 auto count=std::to_wstring(visible_.size())+L" из "+std::to_wstring(rows_.size())+L" записей";SetWindowTextW(summary_,count.c_str());
 SetWindowTextW(heading_,page_==MenuKind::Snippets?L"Текстовые заготовки":L"История буфера");
 EnableWindow(clear_,page_==MenuKind::History&&!rows_.empty());InvalidateRect(history_,nullptr,TRUE);InvalidateRect(snippets_,nullptr,TRUE);selected();
}
void Workspace::selected(){
 if(changing_)return;const auto* row=selection();EnableWindow(copy_,row!=nullptr);EnableWindow(erase_,row&&page_==MenuKind::History);
 if(bitmap_){DeleteObject(bitmap_);bitmap_=nullptr;}ShowWindow(image_,SW_HIDE);ShowWindow(preview_,SW_SHOW);
 if(options_.workspace_image&&(!row||!row->item.preview))options_.workspace_image(std::nullopt);
 if(!row){SetWindowTextW(preview_,rows_.empty()?(page_==MenuKind::History?L"История пока пуста.\r\n\r\nСкопируйте текст, ссылку или изображение — запись появится здесь, если её тип разрешён в настройках.":L"Здесь будут ваши текстовые заготовки.\r\n\r\nОткройте редактор сниппетов, чтобы создать папку и первую заготовку."):L"По этому фильтру ничего не найдено.");return;}
 MenuCommand command{row->item.action,row->item.id,model_.revision};
 auto text=options_.workspace_preview?options_.workspace_preview(command):Result<std::string>(row->item.tooltip);
 SetWindowTextW(preview_,text?multiline(text.value().empty()?"У записи нет текстового представления. Кнопка копирования сохраняет доступные форматы.":text.value()).c_str():wide(text.error().message).c_str());
 if(row->item.preview&&(options_.workspace_image||provider_)){auto image=options_.workspace_image?options_.workspace_image(row->item.preview):provider_(*row->item.preview);if(image&&image->width&&image->height&&image->width<=512&&image->height<=512&&image->rgba.size()==size_t(image->width)*image->height*4){
  BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=image->width;info.bmiHeader.biHeight=-LONG(image->height);info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;void* pixels=nullptr;
  bitmap_=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&pixels,nullptr,0);if(bitmap_){auto out=static_cast<unsigned char*>(pixels);for(size_t i=0;i<image->rgba.size();i+=4){unsigned a=image->rgba[i+3];out[i]=(image->rgba[i+2]*a+255*(255-a))/255;out[i+1]=(image->rgba[i+1]*a+255*(255-a))/255;out[i+2]=(image->rgba[i]*a+255*(255-a))/255;out[i+3]=255;}ShowWindow(preview_,SW_HIDE);ShowWindow(image_,SW_SHOW);InvalidateRect(image_,nullptr,TRUE);}
 }}
}
void Workspace::status(const std::string& s){SetWindowTextW(status_,wide(s).c_str());}
void Workspace::execute(bool erase){
 if(page_==MenuKind::Snippets&&editor_mounted_)return;
 auto row=selection();if(!row||(erase&&page_!=MenuKind::History))return;
 if(erase&&MessageBoxW(owner_,L"Удалить эту запись из истории?",L"Cliptiq",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)!=IDYES)return;
 MenuCommand c{row->item.action,row->item.id,model_.revision};c.invocation.copy_only=true;c.invocation.delete_only=erase;action_(std::move(c));
}
bool Workspace::key(MSG& m){
 if(page_==MenuKind::Snippets&&editor_mounted_)return false;
 if(m.message!=WM_KEYDOWN)return false;
 if((GetKeyState(VK_CONTROL)&0x8000)&&m.wParam=='F'){SetFocus(filter_);return true;}
 if(m.hwnd==list_&&m.wParam==VK_RETURN){execute(false);return true;}
 if(m.hwnd==list_&&m.wParam==VK_DELETE){execute(true);return true;}
 if(m.hwnd==list_&&(GetKeyState(VK_CONTROL)&0x8000)&&m.wParam=='C'){execute(false);return true;}
 return false;
}
bool Workspace::message(UINT msg,WPARAM wp,LPARAM lp,LRESULT& result){
 if(msg==WM_SETTINGCHANGE||msg==WM_THEMECHANGED){high_contrast_=high_contrast();InvalidateRect(owner_,nullptr,TRUE);InvalidateRect(list_,nullptr,TRUE);InvalidateRect(copy_,nullptr,TRUE);}
 if(msg==WM_COMMAND){int id=LOWORD(wp);if(id==Filter&&HIWORD(wp)==EN_CHANGE){filter();result=0;return true;}
  if(HIWORD(wp)!=BN_CLICKED)return false;
  if(id==History||id==Snippets){select_page(id==History?MenuKind::History:MenuKind::Snippets);}
  else if(id==Copy)execute(false);else if(id==Erase)execute(true);
  else if(id==Settings||id==Clear||id==WorkspaceQuit){auto action=id==Settings?MenuAction::OpenSettings:id==Clear?MenuAction::ClearHistory:MenuAction::Quit;action_({action,"",model_.revision});}
  else return false;result=0;return true;
 }
 if(msg==WM_NOTIFY&&reinterpret_cast<NMHDR*>(lp)->hwndFrom==list_){
  auto h=reinterpret_cast<NMHDR*>(lp);
  if(h->code==LVN_ODFINDITEMW){
   const auto find=reinterpret_cast<NMLVFINDITEMW*>(lp);result=-1;
   if((find->lvfi.flags&LVFI_STRING)&&find->lvfi.psz&&!visible_.empty()){
    const auto length=wcslen(find->lvfi.psz);const size_t start=std::min(size_t(std::max(0,find->iStart)),visible_.size());
    const size_t count=(find->lvfi.flags&LVFI_WRAP)?visible_.size():visible_.size()-start;
    for(size_t offset=0;offset<count;++offset){
     const size_t index=(start+offset)%visible_.size();const auto& title=text_[visible_[index]].title;
     if(title.size()<length||(!(find->lvfi.flags&LVFI_PARTIAL)&&title.size()!=length))continue;
     if(CompareStringOrdinal(title.c_str(),int(length),find->lvfi.psz,int(length),TRUE)==CSTR_EQUAL){result=LRESULT(index);break;}
    }
   }
   return true;
  }
  if(h->code==LVN_GETDISPINFOW){
   auto info=reinterpret_cast<NMLVDISPINFOW*>(lp);auto index=size_t(info->item.iItem);
   if((info->item.mask&LVIF_TEXT)&&index<visible_.size()){
    const auto& text=text_[visible_[index]];
    info->item.pszText=const_cast<wchar_t*>(info->item.iSubItem==0?text.accessible.c_str():info->item.iSubItem==1?text.metadata.c_str():L"");
   }
   result=0;return true;
  }
  if(h->code==NM_CUSTOMDRAW){
   auto d=reinterpret_cast<NMLVCUSTOMDRAW*>(lp);
   if(high_contrast_){result=CDRF_DODEFAULT;return true;}
   if(d->nmcd.dwDrawStage==CDDS_PREPAINT){result=CDRF_NOTIFYITEMDRAW;return true;}
   if(d->nmcd.dwDrawStage==CDDS_ITEMPREPAINT){
    auto index=size_t(d->nmcd.dwItemSpec);
    if(index>=visible_.size()){result=CDRF_DODEFAULT;return true;}
    const auto& text=text_[visible_[index]];RECT r{};ListView_GetItemRect(list_,int(index),&r,LVIR_BOUNDS);
    const auto state=ListView_GetItemState(list_,int(index),LVIS_SELECTED|LVIS_FOCUSED);bool selected=(state&LVIS_SELECTED)!=0;
    fill(d->nmcd.hdc,r,selected?RGB(232,241,255):RGB(255,255,255));
    if(selected){RECT accent=r;accent.right=accent.left+px(3);fill(d->nmcd.hdc,accent,RGB(0,86,180));}
    int saved=SaveDC(d->nmcd.hdc);SetBkMode(d->nmcd.hdc,TRANSPARENT);SelectObject(d->nmcd.hdc,font_);
    RECT title{r.left+px(12),r.top+px(7),r.right-px(12),r.top+px(29)};
    SetTextColor(d->nmcd.hdc,RGB(32,34,38));DrawTextW(d->nmcd.hdc,text.title.c_str(),int(text.title.size()),&title,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
    RECT meta{title.left,r.top+px(29),title.right,r.bottom-px(5)};
    SetTextColor(d->nmcd.hdc,selected?RGB(44,73,109):RGB(86,96,110));DrawTextW(d->nmcd.hdc,text.metadata.c_str(),int(text.metadata.size()),&meta,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
    if((state&LVIS_FOCUSED)&&GetFocus()==list_){RECT focus=r;InflateRect(&focus,-2,-2);DrawFocusRect(d->nmcd.hdc,&focus);}
    RestoreDC(d->nmcd.hdc,saved);result=CDRF_SKIPDEFAULT;return true;
   }
   result=CDRF_DODEFAULT;return true;
  }
  if(h->code==LVN_ITEMCHANGED){auto change=reinterpret_cast<NMLISTVIEW*>(lp);if((change->uChanged&LVIF_STATE)&&((change->uOldState^change->uNewState)&LVIS_SELECTED)&&((change->uNewState&LVIS_SELECTED)||ListView_GetSelectedCount(list_)==0))selected();}
  else if(h->code==NM_DBLCLK)execute(false);result=0;return true;
 }
 if(msg==WM_DRAWITEM){auto d=reinterpret_cast<DRAWITEMSTRUCT*>(lp);
  if(d->CtlID==7196){
   FillRect(d->hDC,&d->rcItem,reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
   BITMAP b{};if(bitmap_&&GetObjectW(bitmap_,sizeof(b),&b)){
    int cw=d->rcItem.right-d->rcItem.left,ch=d->rcItem.bottom-d->rcItem.top;
    double scale=std::min(double(cw)/b.bmWidth,double(ch)/b.bmHeight);
    int w=std::max(1,int(b.bmWidth*scale)),h=std::max(1,int(b.bmHeight*scale));
    auto source=CreateCompatibleDC(d->hDC);auto old=SelectObject(source,bitmap_);
    int saved=SaveDC(d->hDC);SetStretchBltMode(d->hDC,HALFTONE);SetBrushOrgEx(d->hDC,0,0,nullptr);
    StretchBlt(d->hDC,d->rcItem.left+(cw-w)/2,d->rcItem.top+(ch-h)/2,w,h,source,0,0,b.bmWidth,b.bmHeight,SRCCOPY);
    RestoreDC(d->hDC,saved);SelectObject(source,old);DeleteDC(source);
   }result=TRUE;return true;
  }
  if(d->CtlID==Copy){
   bool enabled=(d->itemState&ODS_DISABLED)==0,pressed=(d->itemState&ODS_SELECTED)!=0,hc=high_contrast_;
   fill(d->hDC,d->rcItem,hc?GetSysColor(enabled?COLOR_HIGHLIGHT:COLOR_BTNFACE):!enabled?RGB(235,238,242):pressed?RGB(0,64,140):RGB(0,86,180));
   int saved=SaveDC(d->hDC);SetBkMode(d->hDC,TRANSPARENT);SetTextColor(d->hDC,hc?GetSysColor(enabled?COLOR_HIGHLIGHTTEXT:COLOR_GRAYTEXT):enabled?RGB(255,255,255):RGB(100,108,118));SelectObject(d->hDC,bold_font_);
   RECT label=d->rcItem;DrawTextW(d->hDC,L"Копировать",-1,&label,DT_SINGLELINE|DT_CENTER|DT_VCENTER|DT_NOPREFIX);
   if(d->itemState&ODS_FOCUS){InflateRect(&label,-4,-4);DrawFocusRect(d->hDC,&label);}RestoreDC(d->hDC,saved);result=TRUE;return true;
  }
  if(d->CtlID!=History&&d->CtlID!=Snippets)return false;
  bool active=(d->CtlID==History)==(page_==MenuKind::History);auto brush=CreateSolidBrush(active?RGB(224,237,255):RGB(246,247,249));FillRect(d->hDC,&d->rcItem,brush);DeleteObject(brush);SetBkMode(d->hDC,TRANSPARENT);SetTextColor(d->hDC,active?RGB(0,86,180):RGB(45,55,65));auto old=SelectObject(d->hDC,bold_font_);RECT r=d->rcItem;r.left+=px(14);DrawTextW(d->hDC,d->CtlID==History?L"История":L"Сниппеты",-1,&r,DT_SINGLELINE|DT_VCENTER);SelectObject(d->hDC,old);if(d->itemState&ODS_FOCUS)DrawFocusRect(d->hDC,&d->rcItem);result=TRUE;return true;}
 if(msg==WM_CTLCOLORSTATIC){SetTextColor(reinterpret_cast<HDC>(wp),RGB(45,55,65));SetBkColor(reinterpret_cast<HDC>(wp),RGB(255,255,255));result=reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));return true;}
 if(msg==WM_PAINT){PAINTSTRUCT ps;auto dc=BeginPaint(owner_,&ps);RECT r{};GetClientRect(owner_,&r);FillRect(dc,&r,reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));RECT nav{0,px(64),px(176),r.bottom};auto b=CreateSolidBrush(RGB(246,247,249));FillRect(dc,&nav,b);DeleteObject(b);auto pen=CreatePen(PS_SOLID,1,RGB(225,229,235));auto old=SelectObject(dc,pen);MoveToEx(dc,0,px(63),nullptr);LineTo(dc,r.right,px(63));SelectObject(dc,old);DeleteObject(pen);EndPaint(owner_,&ps);result=0;return true;}
 return false;
}
}
