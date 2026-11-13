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
 DWORD owner=GetCurrentThreadId(); HWND window=nullptr,message=nullptr; HFONT font=nullptr,help_font=nullptr,heading_font=nullptr;
 bool embedded=false;lan::Config saved;
 LanSettingsDraft draft; Save save;std::function<Result<std::string>()> generate;std::function<Result<void>(std::string)> copy;
 struct Input { LanSettingsDraft::Field field;HWND window; };std::vector<Input> inputs;
 struct Action { int kind; std::string peer; };std::map<int,Action> actions;
 struct Placement { HWND window;int x,y,w,h;bool discovery=false; };std::vector<Placement> children;int offset=0,height=0,content_height=0;
 ~State(){if(prompt)DestroyWindow(prompt);if(window)DestroyWindow(window);if(font)DeleteObject(font);if(help_font)DeleteObject(help_font);if(heading_font)DeleteObject(heading_font);}
 HWND control(const wchar_t* cls,const std::string& value,DWORD style,int x,int y,int w,int h,int id=0){auto c=CreateWindowExW(cls==std::wstring(L"EDIT")?WS_EX_CLIENTEDGE:0,cls,wide(value).c_str(),WS_CHILD|WS_VISIBLE|style,px(x),px(y-offset),px(w),px(h),window,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);children.push_back({c,x,y,w,h});return c;}
 int px(int n)const{auto dpi=window?GetDpiForWindow(window):GetDpiForSystem();return MulDiv(n,dpi?dpi:96,96);}
 void layout(){RECT r{};GetClientRect(window,&r);SCROLLINFO si{sizeof(si),SIF_RANGE|SIF_PAGE|SIF_POS};si.nMax=height;si.nPage=MulDiv(r.bottom,96,GetDpiForWindow(window));si.nPos=offset;SetScrollInfo(window,SB_VERT,&si,TRUE);int available=MulDiv(r.right,96,GetDpiForWindow(window)),delta=0;for(auto& c:children){int x=c.x,w=c.w;if(embedded){if(c.x==380){x=available/2+8;w=available-x-16;}else if(c.w==350)w=available/2-28;else if(c.w>=704)w=available-32;else if(c.w==220){w=(available-64)/3;x=16+((c.x-16)/238)*(w+16);}}int h=c.h;int before=delta;if(GetPropW(c.window,L"Cliptiq.LanHelp")){auto dc=GetDC(window);auto old=SelectObject(dc,help_font);auto text=wide(utf(c.window));RECT measured{0,0,px(std::max(1,w)),0};DrawTextW(dc,text.c_str(),int(text.size()),&measured,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);SelectObject(dc,old);ReleaseDC(window,dc);h=std::max(20,MulDiv(measured.bottom,96,GetDpiForWindow(window)));delta+=h-c.h;}SetWindowPos(c.window,nullptr,px(x),px(c.y+before-offset),px(std::max(1,w)),px(h),SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOCOPYBITS|SWP_NOREDRAW);}content_height=height+delta;int maximum=std::max(0,content_height-int(si.nPage));if(offset>maximum){offset=maximum;layout();return;}si.nMax=content_height;SetScrollInfo(window,SB_VERT,&si,TRUE);
 // Transparent labels and moved siblings must repaint from their new positions.
 // Do not retain copied pixels from intermediate child-window moves.
 RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);
 }
 void status(std::string s){if(message)SetWindowTextW(message,wide(tr(s)).c_str());}
 Result<void> collect(){std::map<std::string,std::string> values;for(auto& i:inputs)values[i.field.key]=i.field.check?(SendMessageW(i.window,BM_GETCHECK,0,0)==BST_CHECKED?"1":"0"):utf(i.window);return draft.collect(values);}
 DiscoveryUiCallbacks discovery;lan::DiscoverySnapshot snapshot;std::string signature;bool manualExpanded=false,viewVisible=false;int discoveryHeight=0;HWND code_edit=nullptr,prompt=nullptr;std::vector<HWND> promptChildren;std::map<int,Action> discoveryActions;std::map<std::string,std::string> manualValues;int discoveryId=10000;
 HWND topControl(HWND host,const wchar_t* cls,const std::string& value,DWORD style,int x,int y,int w,int h,int id=0){
  if(host==window){auto c=control(cls,value,style,x,y,w,h,id);children.back().discovery=true;return c;}
  auto c=CreateWindowExW(std::wstring(cls)==L"EDIT"?WS_EX_CLIENTEDGE:0,cls,wide(value).c_str(),WS_CHILD|WS_VISIBLE|style,px(x),px(y),px(w),px(h),host,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(font?reinterpret_cast<HGDIOBJ>(font):GetStockObject(DEFAULT_GUI_FONT)),TRUE);promptChildren.push_back(c);return c;
 }
 HWND topButton(HWND host,const char* title,int kind,const std::string& value,int x,int y,int w=220,DWORD style=0){int id=discoveryId++;discoveryActions[id]={kind,value};return topControl(host,L"BUTTON",tr(title),WS_TABSTOP|style,x,y,w,32,id);}
 void discoverySection(const char* title,int& y,bool live=true){
  auto make=[&](const std::string& text,DWORD style,int top,int h){return live?topControl(window,L"STATIC",text,style,16,top,704,h):control(L"STATIC",text,style,16,top,704,h);};
  if(y>16){y+=20;make("",SS_ETCHEDHORZ,y,2);y+=16;}
  auto heading=make(tr(title),0,y,30);SendMessageW(heading,WM_SETFONT,reinterpret_cast<WPARAM>(heading_font),TRUE);y+=42;
 }
 void discoveryHelp(const std::string& text,int& y){auto h=topControl(window,L"STATIC",text,0,16,y,704,48);SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(help_font),TRUE);SetPropW(h,L"Cliptiq.LanHelp",reinterpret_cast<HANDLE>(1));y+=60;}
 int pairingControls(HWND host,int y){using Kind=lan::PairingEvent::Kind;if(snapshot.session_id.empty())return y;
  topControl(host,L"STATIC",snapshot.peer_name,0,16,y,450,28);y+=36;
  if(snapshot.pairing==Kind::Incoming){topControl(host,L"STATIC",tr("Allow this device to pair?"),0,16,y,450,36);y+=44;topButton(host,"Allow pairing",13,snapshot.session_id,16,y);y+=44;}
  if(snapshot.pairing==Kind::Code){topControl(host,L"STATIC",tr("Enter this code on the other computer. It expires in 2 minutes."),0,16,y,450,48);y+=56;auto code=topControl(host,L"STATIC",snapshot.code,0,16,y,220,36);if(heading_font)SendMessageW(code,WM_SETFONT,reinterpret_cast<WPARAM>(heading_font),TRUE);y+=44;}
  if(snapshot.pairing==Kind::EnterCode){topControl(host,L"STATIC",tr("Enter the 6-digit code shown on the other computer."),0,16,y,450,48);y+=56;code_edit=topControl(host,L"EDIT","",WS_TABSTOP|ES_AUTOHSCROLL|ES_NUMBER,16,y,180,28,8999);SendMessageW(code_edit,EM_SETLIMITTEXT,6,0);y+=36;topButton(host,"Pair device",14,snapshot.session_id,16,y);y+=44;}
  bool pending=snapshot.pairing!=Kind::Complete&&snapshot.pairing!=Kind::Failed;
  if(pending){topButton(host,"Cancel pairing",15,snapshot.session_id,16,y);y+=44;}
  if(host==prompt&&!snapshot.message.empty()){topControl(host,L"STATIC",tr(snapshot.message),0,16,y,450,64);y+=72;}return y;
 }
 static LRESULT CALLBACK promptProc(HWND w,UINT m,WPARAM wp,LPARAM lp){auto s=reinterpret_cast<State*>(GetWindowLongPtrW(w,GWLP_USERDATA));if(m==WM_NCCREATE){s=static_cast<State*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s));}if(s){if(m==WM_COMMAND&&HIWORD(wp)==BN_CLICKED){s->discoveryAction(LOWORD(wp));return 0;}if(m==WM_CLOSE){if(s->discovery.cancel&&s->snapshot.pairing!=lan::PairingEvent::Kind::Complete&&s->snapshot.pairing!=lan::PairingEvent::Kind::Failed)s->discovery.cancel(s->snapshot.session_id);ShowWindow(w,SW_HIDE);return 0;}if(m==WM_NCDESTROY){s->prompt=nullptr;s->promptChildren.clear();}}return DefWindowProcW(w,m,wp,lp);}
 void renderDiscovery(){if(!discovery.snapshot)return;std::string entered=code_edit?utf(code_edit):"";bool focus=code_edit&&GetFocus()==code_edit;code_edit=nullptr;
  for(auto it=children.begin();it!=children.end();)if(it->discovery){DestroyWindow(it->window);it=children.erase(it);}else ++it;
  for(auto c:promptChildren)DestroyWindow(c);promptChildren.clear();discoveryActions.clear();discoveryId=10000;int y=16;
  if(window){discoverySection("This device",y);if(!snapshot.local_name.empty()){topControl(window,L"STATIC",snapshot.local_name,0,16,y,704,28);y+=36;}auto availability=topButton(window,"Available for discovery",10,{},16,y,704,BS_AUTOCHECKBOX);SendMessageW(availability,BM_SETCHECK,snapshot.enabled?BST_CHECKED:BST_UNCHECKED,0);EnableWindow(availability,snapshot.available);y+=40;
   discoveryHelp(tr("Enable discovery once on each computer you want to connect."),y);
   discoverySection("Device search",y);
   discoveryHelp(tr("Search for nearby devices to connect. Your saved connections are listed separately below."),y);
   auto search=topButton(window,snapshot.searching?"Stop search":"Find devices on network",snapshot.searching?12:11,{},16,y,350);EnableWindow(search,snapshot.available);y+=44;
   for(const auto& device:snapshot.devices){topControl(window,L"STATIC",device.name+" · "+device.platform+" · "+device.address+" · "+device.device_id.substr(0,8),0,16,y+4,440,36);bool paired=std::any_of(snapshot.peers.begin(),snapshot.peers.end(),[&](const auto& p){return p.remote_device_id==device.device_id;});auto b=topButton(window,paired?"Paired":"Connect",17,device.device_id,492,y);EnableWindow(b,!paired&&!pairing_active(snapshot));y+=44;}
   discoverySection("Paired devices",y);
   if(snapshot.peers.empty())discoveryHelp(tr("No paired devices yet. Find a device above to create your first connection."),y);
   if(!snapshot.peers.empty()){for(const auto& peer:snapshot.peers){auto status=snapshot.peer_statuses.find(peer.id);topControl(window,L"STATIC",peer.name+(status==snapshot.peer_statuses.end()?"":" · "+tr(status->second)),0,16,y,440,36);auto remove=topButton(window,"Delete",23,peer.id,492,y);EnableWindow(remove,bool(discovery.remove_peer)&&!pairing_active(snapshot));y+=44;int x=16;for(int kind:{20,21,22}){auto b=topButton(window,kind==20?"Enable sharing":kind==21?"Send clipboard":"Receive clipboard",kind,peer.id,x,y,220,BS_AUTOCHECKBOX);SendMessageW(b,BM_SETCHECK,(kind==20?peer.enabled:kind==21?peer.send:peer.receive)?BST_CHECKED:BST_UNCHECKED,0);x+=238;}y+=60;}}
  }
  bool pending=pairing_active(snapshot);
  if((pending&&(!window||!IsWindowVisible(window)||prompt))||(prompt&&IsWindowVisible(prompt)&&snapshot.pairing)){
   if(!prompt){WNDCLASSW wc{};wc.lpfnWndProc=promptProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"CliptiqPairing";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassW(&wc);prompt=CreateWindowExW(WS_EX_TOOLWINDOW,wc.lpszClassName,lw(L"Pair device"),WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,CW_USEDEFAULT,CW_USEDEFAULT,px(510),px(390),nullptr,nullptr,wc.hInstance,this);}if(prompt){pairingControls(prompt,16);if(!IsWindowVisible(prompt)){ShowWindow(prompt,SW_SHOW);SetForegroundWindow(prompt);}}
  }else{if(prompt)ShowWindow(prompt,SW_HIDE);if(window)y=pairingControls(window,y);}
  if(code_edit){SetWindowTextW(code_edit,wide(entered).c_str());if(focus)SetFocus(code_edit);}
  if(window){if(!snapshot.message.empty()){topControl(window,L"STATIC",tr(snapshot.message),0,16,y,704,64);y+=72;}discoverySection("More connection options",y);topControl(window,L"BUTTON",tr("Advanced settings"),WS_TABSTOP,16,y,350,32,9000);y+=44;
   if(manualExpanded){
    topControl(window,L"STATIC",tr("Network interface"),0,16,y,704,28);y+=36;
    auto automatic=topButton(window,"Automatic (recommended)",16,{},16,y,704,BS_AUTORADIOBUTTON|WS_GROUP);SendMessageW(automatic,BM_SETCHECK,snapshot.interface_id.empty()?BST_CHECKED:BST_UNCHECKED,0);y+=40;
    for(const auto& iface:snapshot.interfaces){int id=discoveryId++;discoveryActions[id]={16,iface.id};auto b=topControl(window,L"BUTTON",iface.name+" ("+iface.address+")",WS_TABSTOP|BS_AUTORADIOBUTTON,16,y,704,32,id);SendMessageW(b,BM_SETCHECK,iface.id==snapshot.interface_id?BST_CHECKED:BST_UNCHECKED,0);y+=40;}
   }
   int delta=y-discoveryHeight;for(auto& child:children)if(!child.discovery)child.y+=delta;height+=delta;discoveryHeight=y;std::stable_sort(children.begin(),children.end(),[](const auto& a,const auto& b){return a.y<b.y;});layout();}
 }
 void discoveryAction(int id){auto it=discoveryActions.find(id);if(it==discoveryActions.end())return;auto a=it->second;Result<void> result;
  if(a.kind==10&&discovery.available)result=discovery.available(!snapshot.enabled);if(a.kind==11&&discovery.search)result=discovery.search();if(a.kind==12&&discovery.stop_search)discovery.stop_search();if(a.kind==13&&discovery.accept)result=discovery.accept(a.peer);if(a.kind==14&&discovery.enter_code)result=discovery.enter_code(a.peer,code_edit?utf(code_edit):"");if(a.kind==15&&discovery.cancel)discovery.cancel(a.peer);if(a.kind==16&&discovery.select_interface)result=discovery.select_interface(a.peer);if(a.kind==17&&discovery.connect)result=discovery.connect(a.peer);
  if(a.kind>=20&&a.kind<=22&&discovery.peer_options){auto p=std::find_if(snapshot.peers.begin(),snapshot.peers.end(),[&](const auto& p){return p.id==a.peer;});if(p!=snapshot.peers.end())result=discovery.peer_options(p->id,a.kind==20?!p->enabled:p->enabled,a.kind==21?!p->send:p->send,a.kind==22?!p->receive:p->receive);}
  if(a.kind==23&&discovery.remove_peer){auto removed=discovery.remove_peer(a.peer);if(!removed){result=removed.error();}else{
   remember();draft.remove(a.peer);saved=removed.value();draft.config.revision=saved.revision;
   auto prefix=a.peer+":";for(auto i=manualValues.begin();i!=manualValues.end();)if(i->first.compare(0,prefix.size(),prefix)==0)i=manualValues.erase(i);else ++i;
   build();return;
  }}
  snapshot=discovery.snapshot();if(!result)snapshot.message=result.error().message;signature=discovery_signature(snapshot);renderDiscovery();
 }
 void remember(){for(const auto& input:inputs)manualValues[input.field.key]=input.field.check?(SendMessageW(input.window,BM_GETCHECK,0,0)==BST_CHECKED?"1":"0"):utf(input.window);}
 void build(){for(auto& c:children)DestroyWindow(c.window);children.clear();inputs.clear();actions.clear();int y=16,id=100;discoveryHeight=0;height=0;
 if(discovery.snapshot){snapshot=discovery.snapshot();signature=discovery_signature(snapshot);renderDiscovery();y=discoveryHeight+8;}
 if(manualExpanded||!discovery.snapshot){
 control(L"STATIC",tr("Changes take effect only after Save. No peers means LAN is off."),0,16,y,720,40);y+=44;
 control(L"STATIC",tr("Listen on this device's LAN or VPN IP. Add the other device's address and use the same pairing key on both."),0,16,y,720,60);y+=68;
 for(auto f:draft.fields()){if(manualValues.count(f.key))f.value=manualValues[f.key];
 auto section=draft.section(f);if(!section.empty()){y+=20;control(L"STATIC","",SS_ETCHEDHORZ,16,y,720,2);y+=12;auto heading=control(L"STATIC",section,0,16,y,720,32);SendMessageW(heading,WM_SETFONT,reinterpret_cast<WPARAM>(heading_font),TRUE);y+=44;}
 control(L"STATIC",f.label,0,16,y+4,350,44);
 auto w=control(f.check?L"BUTTON":L"EDIT",f.check?"":f.value,WS_TABSTOP|(f.check?BS_AUTOCHECKBOX:ES_AUTOHSCROLL|(f.secret?ES_PASSWORD:0)),380,y,340,28);
 if(f.check)SendMessageW(w,BM_SETCHECK,f.value=="1"?BST_CHECKED:BST_UNCHECKED,0);else SendMessageW(w,EM_SETLIMITTEXT,f.secret?64:256,0);
 inputs.push_back({f,w});y+=48;
 auto help=control(L"STATIC",LanSettingsDraft::explanation(f),0,16,y,720,60);SendMessageW(help,WM_SETFONT,reinterpret_cast<WPARAM>(help_font),TRUE);SetPropW(help,L"Cliptiq.LanHelp",reinterpret_cast<HANDLE>(1));y+=76;
 if(f.secret){auto peer=f.key.substr(0,f.key.size()-4);int x=16;for(auto kind:{1,2,3}){auto label=tr(kind==1?"Generate key":kind==2?"Copy key":"Remove peer");actions[id]={kind,peer};control(L"BUTTON",label,WS_TABSTOP,x,y,220,30,id++);x+=238;}y+=48;}
 }
 actions[id]={4,{}};auto add=control(L"BUTTON",tr("Add peer"),WS_TABSTOP,16,y,220,32,id++);EnableWindow(add,draft.config.peers.size()<16);
 actions[id]={5,{}};control(L"BUTTON",tr("Save"),WS_TABSTOP|BS_DEFPUSHBUTTON,254,y,220,32,id++);
 actions[id]={6,{}};control(L"BUTTON",tr("Cancel"),WS_TABSTOP,492,y,220,32,id++);y+=44;
 }
 y+=12;control(L"STATIC","",SS_ETCHEDHORZ,16,y,704,2);y+=16;
 message=control(L"STATIC",tr("Ready. Pair keys are stored securely after Save."),0,16,y,704,48);SendMessageW(message,WM_SETFONT,reinterpret_cast<WPARAM>(help_font),TRUE);SetPropW(message,L"Cliptiq.LanHelp",reinterpret_cast<HANDLE>(1));height=y+64;layout();
 }
 void action(int id){if(id>=10000){discoveryAction(id);return;}if(id==9000){remember();manualExpanded=!manualExpanded;build();return;}auto a=actions.find(id);if(a==actions.end())return;auto kind=a->second.kind;auto peer=a->second.peer;if(kind==6){manualValues.clear();draft.keys.clear();if(embedded){draft.config=saved;build();}else ShowWindow(window,SW_HIDE);return;}
 auto result=collect();if(!result){status(result.error().message);return;}
 if(kind==1){auto key=generate?generate():Result<std::string>(Error{ErrorCode::Unsupported,"Key generation unavailable"});if(!key){status(key.error().message);return;}manualValues.clear();draft.keys[peer]=key.value();build();}
 if(kind==2){auto key=draft.keys.find(peer);if(key==draft.keys.end()){status(tr("Enter or generate a key to copy. Saved keys are not displayed."));return;}auto r=copy?copy(key->second):Result<void>(Error{ErrorCode::Unsupported,"Key copy unavailable"});status(r?tr("Key copied. Keep it private."):r.error().message);}
 if(kind==3){manualValues.clear();draft.remove(peer);build();}if(kind==4){manualValues.clear();draft.add();build();}
 if(kind==5){auto r=save?save(draft.config,draft.keys):Result<void>(Error{ErrorCode::Unsupported,"Save unavailable"});if(!r){status(r.error().message);return;}manualValues.clear();draft.keys.clear();saved=draft.config;if(embedded){build();status(tr("Saved. Unsupported functions remain disabled."));}else ShowWindow(window,SW_HIDE);}
 }
 static LRESULT CALLBACK proc(HWND w,UINT m,WPARAM wp,LPARAM lp){auto s=reinterpret_cast<State*>(GetWindowLongPtrW(w,GWLP_USERDATA));if(m==WM_NCCREATE){s=static_cast<State*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s));s->window=w;}
 if(s){if(m==WM_CTLCOLORSTATIC||m==WM_CTLCOLOREDIT){auto dc=reinterpret_cast<HDC>(wp);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,GetPropW(reinterpret_cast<HWND>(lp),L"Cliptiq.LanHelp")?RGB(100,105,112):GetSysColor(COLOR_WINDOWTEXT));SetBkColor(dc,GetSysColor(COLOR_WINDOW));return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));}if(m==WM_COMMAND&&HIWORD(wp)==BN_CLICKED){s->action(LOWORD(wp));return 0;}if(m==WM_CLOSE){ShowWindow(w,SW_HIDE);s->draft.keys.clear();s->viewVisible=false;if(s->discovery.close_view)s->discovery.close_view();return 0;}if(m==WM_SIZE){s->layout();return 0;}if(m==WM_VSCROLL||m==WM_MOUSEWHEEL){RECT r{};GetClientRect(w,&r);int d=m==WM_MOUSEWHEEL?-GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA*96:LOWORD(wp)==SB_LINEUP?-36:LOWORD(wp)==SB_LINEDOWN?36:LOWORD(wp)==SB_PAGEUP?-int(r.bottom):LOWORD(wp)==SB_PAGEDOWN?int(r.bottom):0;if(m==WM_VSCROLL&&(LOWORD(wp)==SB_THUMBTRACK||LOWORD(wp)==SB_THUMBPOSITION)){SCROLLINFO si{sizeof(si),SIF_TRACKPOS};GetScrollInfo(w,SB_VERT,&si);s->offset=si.nTrackPos;}else s->offset+=d;s->offset=std::clamp(s->offset,0,std::max(0,s->content_height-MulDiv(int(r.bottom),96,GetDpiForWindow(w))));s->layout();return 0;}if(m==WM_NCDESTROY){s->window=nullptr;s->message=nullptr;s->inputs.clear();s->children.clear();s->draft.keys.clear();}}
 return DefWindowProcW(w,m,wp,lp);}
};
NativeLanSettings::NativeLanSettings():state_(std::make_shared<State>()){}NativeLanSettings::~NativeLanSettings()=default;
Result<void> NativeLanSettings::show(lan::Config config,Save save,std::function<Result<std::string>()> generate,std::function<Result<void>(std::string)> copy,void* parent){auto s=state_;if(s->owner!=GetCurrentThreadId())return Error{ErrorCode::InvalidArgument,"LAN settings require owner GUI thread"};if(!save||!generate||!copy)return Error{ErrorCode::InvalidArgument,"LAN settings callbacks required"};if(s->window)DestroyWindow(s->window);s->children.clear();s->inputs.clear();s->embedded=parent!=nullptr;s->saved=config;s->draft={std::move(config),{}};s->save=std::move(save);s->generate=std::move(generate);s->copy=std::move(copy);s->offset=0;s->manualExpanded=!s->discovery.snapshot;s->manualValues.clear();
 WNDCLASSW wc{};wc.lpfnWndProc=State::proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"CliptiqLanSettings";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassW(&wc);
 if(!s->font)s->font=CreateFontW(-MulDiv(11,GetDpiForSystem(),72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
 if(!s->help_font)s->help_font=CreateFontW(-MulDiv(9,GetDpiForSystem(),72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
 if(!s->heading_font)s->heading_font=CreateFontW(-MulDiv(12,GetDpiForSystem(),72),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
 s->window=CreateWindowExW(WS_EX_CONTROLPARENT,wc.lpszClassName,lw(L"Advanced LAN settings"),(parent?WS_CHILD|WS_VISIBLE:WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX)|WS_VSCROLL|WS_CLIPCHILDREN, parent?0:CW_USEDEFAULT,parent?0:CW_USEDEFAULT,MulDiv(770,GetDpiForSystem(),96),MulDiv(720,GetDpiForSystem(),96),static_cast<HWND>(parent),parent?reinterpret_cast<HMENU>(303):nullptr,wc.hInstance,s.get());if(!s->window)return Error{ErrorCode::Unsupported,"Cannot create LAN settings window"};s->build();if(parent){RECT r{};GetClientRect(static_cast<HWND>(parent),&r);MoveWindow(s->window,0,0,r.right,r.bottom,TRUE);}else ShowWindow(s->window,SW_SHOW);s->viewVisible=IsWindowVisible(s->window);return {};}
Result<void> NativeLanSettings::process_events(bool changed){if(state_->owner!=GetCurrentThreadId())return Error{ErrorCode::InvalidArgument,"LAN settings require owner GUI thread"};auto s=state_;if(s->discovery.snapshot){bool visible=s->window&&IsWindowVisible(s->window);if(s->viewVisible&&!visible&&s->discovery.close_view)s->discovery.close_view();bool update=changed||visible!=s->viewVisible;s->viewVisible=visible;if(update){auto snapshot=s->discovery.snapshot();auto signature=discovery_signature(snapshot);if(signature!=s->signature){s->snapshot=std::move(snapshot);s->signature=std::move(signature);s->renderDiscovery();}}}MSG m{};for(int i=0;i<64&&state_->window&&PeekMessageW(&m,state_->window,0,0,PM_REMOVE);++i)if(!IsDialogMessageW(state_->window,&m)){TranslateMessage(&m);DispatchMessageW(&m);}for(int i=0;i<64&&s->prompt&&PeekMessageW(&m,s->prompt,0,0,PM_REMOVE);++i)if(!IsDialogMessageW(s->prompt,&m)){TranslateMessage(&m);DispatchMessageW(&m);}return {};}
void NativeLanSettings::discovery(DiscoveryUiCallbacks callbacks){state_->discovery=std::move(callbacks);}
void NativeLanSettings::status(std::string s){state_->status(std::move(s));}
}
