#include "../LanSettingsDraft.hpp"
#include "../LocaleNative.hpp"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <thread>
namespace cliptiq {
namespace {
std::wstring wide(const std::string& s){int n=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),nullptr,0);std::wstring r(n,0);MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),r.data(),n);return r;}
std::string utf(HWND w){int n=GetWindowTextLengthW(w);std::wstring r(n+1,0);GetWindowTextW(w,r.data(),n+1);int z=WideCharToMultiByte(CP_UTF8,0,r.data(),n,nullptr,0,nullptr,nullptr);std::string s(z,0);WideCharToMultiByte(CP_UTF8,0,r.data(),n,s.data(),z,nullptr,nullptr);return s;}
}
struct NativeLanSettings::State {
 DWORD owner=GetCurrentThreadId(); HWND window=nullptr,message=nullptr; HFONT font=nullptr;
 bool embedded=false;lan::Config saved;
 LanSettingsDraft draft; Save save;std::function<Result<std::string>()> generate;std::function<Result<void>(std::string)> copy;
 struct Input { LanSettingsDraft::Field field;HWND window; };std::vector<Input> inputs;
 struct Action { int kind; std::string peer; };std::map<int,Action> actions;
 struct Placement { HWND window;int x,y,w,h; };std::vector<Placement> children;int offset=0,height=0;
 ~State(){if(window)DestroyWindow(window);if(font)DeleteObject(font);}
 HWND control(const wchar_t* cls,const std::string& value,DWORD style,int x,int y,int w,int h,int id=0){auto c=CreateWindowExW(cls==std::wstring(L"EDIT")?WS_EX_CLIENTEDGE:0,cls,wide(value).c_str(),WS_CHILD|WS_VISIBLE|style,px(x),px(y-offset),px(w),px(h),window,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);children.push_back({c,x,y,w,h});return c;}
 int px(int n)const{auto dpi=window?GetDpiForWindow(window):GetDpiForSystem();return MulDiv(n,dpi?dpi:96,96);}
 void layout(){RECT r{};GetClientRect(window,&r);SCROLLINFO si{sizeof(si),SIF_RANGE|SIF_PAGE|SIF_POS};si.nMax=height;si.nPage=MulDiv(r.bottom,96,GetDpiForWindow(window));si.nPos=offset;SetScrollInfo(window,SB_VERT,&si,TRUE);int available=MulDiv(r.right,96,GetDpiForWindow(window));for(auto& c:children){int x=c.x,w=c.w;if(embedded){if(c.x==380){x=available/2+8;w=available-x-16;}else if(c.w==350)w=available/2-28;else if(c.w>=704)w=available-32;else if(c.w==220){w=(available-64)/3;x=16+((c.x-16)/238)*(w+16);}}MoveWindow(c.window,px(x),px(c.y-offset),px(std::max(1,w)),px(c.h),TRUE);}}
 void status(std::string s){if(message)SetWindowTextW(message,wide(tr(s)).c_str());}
 Result<void> collect(){std::map<std::string,std::string> values;for(auto& i:inputs)values[i.field.key]=i.field.check?(SendMessageW(i.window,BM_GETCHECK,0,0)==BST_CHECKED?"1":"0"):utf(i.window);return draft.collect(values);}
 void build(){for(auto& c:children)DestroyWindow(c.window);children.clear();inputs.clear();actions.clear();int y=16,id=100;
 control(L"STATIC",tr("Changes take effect only after Save. No peers means LAN is off."),0,16,y,720,40);y+=44;
 control(L"STATIC",tr("Listen on this device's LAN or VPN IP. Add the other device's address and use the same pairing key on both."),0,16,y,720,60);y+=68;
 for(auto& f:draft.fields()){
 control(L"STATIC",f.label,0,16,y+4,350,44);
 auto w=control(f.check?L"BUTTON":L"EDIT",f.check?"":f.value,WS_TABSTOP|(f.check?BS_AUTOCHECKBOX:ES_AUTOHSCROLL|(f.secret?ES_PASSWORD:0)),380,y,340,28);
 if(f.check)SendMessageW(w,BM_SETCHECK,f.value=="1"?BST_CHECKED:BST_UNCHECKED,0);else SendMessageW(w,EM_SETLIMITTEXT,f.secret?64:256,0);
 inputs.push_back({f,w});y+=52;
 if(f.secret){auto peer=f.key.substr(0,f.key.size()-4);int x=16;for(auto kind:{1,2,3}){auto label=tr(kind==1?"Generate key":kind==2?"Copy key":"Remove peer");actions[id]={kind,peer};control(L"BUTTON",label,WS_TABSTOP,x,y,220,30,id++);x+=238;}y+=48;}
 }
 actions[id]={4,{}};auto add=control(L"BUTTON",tr("Add peer"),WS_TABSTOP,16,y,220,32,id++);EnableWindow(add,draft.config.peers.size()<16);
 actions[id]={5,{}};control(L"BUTTON",tr("Save"),WS_TABSTOP|BS_DEFPUSHBUTTON,254,y,220,32,id++);
 actions[id]={6,{}};control(L"BUTTON",tr("Cancel"),WS_TABSTOP,492,y,220,32,id++);y+=44;
 message=control(L"STATIC",tr("Ready. Pair keys are stored securely after Save."),0,16,y,704,48);height=y+64;layout();
 }
 void action(int id){auto a=actions.find(id);if(a==actions.end())return;auto kind=a->second.kind;auto peer=a->second.peer;if(kind==6){draft.keys.clear();if(embedded){draft.config=saved;build();}else ShowWindow(window,SW_HIDE);return;}
 auto result=collect();if(!result){status(result.error().message);return;}
 if(kind==1){auto key=generate?generate():Result<std::string>(Error{ErrorCode::Unsupported,"Key generation unavailable"});if(!key){status(key.error().message);return;}draft.keys[peer]=key.value();build();}
 if(kind==2){auto key=draft.keys.find(peer);if(key==draft.keys.end()){status(tr("Enter or generate a key to copy. Saved keys are not displayed."));return;}auto r=copy?copy(key->second):Result<void>(Error{ErrorCode::Unsupported,"Key copy unavailable"});status(r?tr("Key copied. Keep it private."):r.error().message);}
 if(kind==3){draft.remove(peer);build();}if(kind==4){draft.add();build();}
 if(kind==5){auto r=save?save(draft.config,draft.keys):Result<void>(Error{ErrorCode::Unsupported,"Save unavailable"});if(!r){status(r.error().message);return;}draft.keys.clear();saved=draft.config;if(embedded){build();status(tr("Saved. Unsupported functions remain disabled."));}else ShowWindow(window,SW_HIDE);}
 }
 static LRESULT CALLBACK proc(HWND w,UINT m,WPARAM wp,LPARAM lp){auto s=reinterpret_cast<State*>(GetWindowLongPtrW(w,GWLP_USERDATA));if(m==WM_NCCREATE){s=static_cast<State*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s));s->window=w;}
 if(s){if(m==WM_COMMAND&&HIWORD(wp)==BN_CLICKED){s->action(LOWORD(wp));return 0;}if(m==WM_CLOSE){ShowWindow(w,SW_HIDE);s->draft.keys.clear();return 0;}if(m==WM_SIZE){s->layout();return 0;}if(m==WM_VSCROLL||m==WM_MOUSEWHEEL){RECT r{};GetClientRect(w,&r);int d=m==WM_MOUSEWHEEL?-GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA*96:LOWORD(wp)==SB_LINEUP?-36:LOWORD(wp)==SB_LINEDOWN?36:LOWORD(wp)==SB_PAGEUP?-int(r.bottom):LOWORD(wp)==SB_PAGEDOWN?int(r.bottom):0;if(m==WM_VSCROLL&&(LOWORD(wp)==SB_THUMBTRACK||LOWORD(wp)==SB_THUMBPOSITION)){SCROLLINFO si{sizeof(si),SIF_TRACKPOS};GetScrollInfo(w,SB_VERT,&si);s->offset=si.nTrackPos;}else s->offset+=d;s->offset=std::clamp(s->offset,0,std::max(0,s->height-MulDiv(int(r.bottom),96,GetDpiForWindow(w))));s->layout();return 0;}if(m==WM_NCDESTROY){s->window=nullptr;s->message=nullptr;s->inputs.clear();s->children.clear();s->draft.keys.clear();}}
 return DefWindowProcW(w,m,wp,lp);}
};
NativeLanSettings::NativeLanSettings():state_(std::make_shared<State>()){}NativeLanSettings::~NativeLanSettings()=default;
Result<void> NativeLanSettings::show(lan::Config config,Save save,std::function<Result<std::string>()> generate,std::function<Result<void>(std::string)> copy,void* parent){auto s=state_;if(s->owner!=GetCurrentThreadId())return Error{ErrorCode::InvalidArgument,"LAN settings require owner GUI thread"};if(!save||!generate||!copy)return Error{ErrorCode::InvalidArgument,"LAN settings callbacks required"};if(s->window)DestroyWindow(s->window);s->children.clear();s->inputs.clear();s->embedded=parent!=nullptr;s->saved=config;s->draft={std::move(config),{}};s->save=std::move(save);s->generate=std::move(generate);s->copy=std::move(copy);s->offset=0;
 WNDCLASSW wc{};wc.lpfnWndProc=State::proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"CliptiqLanSettings";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassW(&wc);
 if(!s->font)s->font=CreateFontW(-MulDiv(11,GetDpiForSystem(),72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
 s->window=CreateWindowExW(WS_EX_CONTROLPARENT,wc.lpszClassName,lw(L"Advanced LAN settings"),(parent?WS_CHILD|WS_VISIBLE:WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX)|WS_VSCROLL, parent?0:CW_USEDEFAULT,parent?0:CW_USEDEFAULT,MulDiv(770,GetDpiForSystem(),96),MulDiv(720,GetDpiForSystem(),96),static_cast<HWND>(parent),parent?reinterpret_cast<HMENU>(303):nullptr,wc.hInstance,s.get());if(!s->window)return Error{ErrorCode::Unsupported,"Cannot create LAN settings window"};s->build();if(parent){RECT r{};GetClientRect(static_cast<HWND>(parent),&r);MoveWindow(s->window,0,0,r.right,r.bottom,TRUE);}else ShowWindow(s->window,SW_SHOW);return {};}
Result<void> NativeLanSettings::process_events(){if(state_->owner!=GetCurrentThreadId())return Error{ErrorCode::InvalidArgument,"LAN settings require owner GUI thread"};MSG m{};for(int i=0;i<64&&state_->window&&PeekMessageW(&m,state_->window,0,0,PM_REMOVE);++i)if(!IsDialogMessageW(state_->window,&m)){TranslateMessage(&m);DispatchMessageW(&m);}return {};}
void NativeLanSettings::status(std::string s){state_->status(std::move(s));}
}
