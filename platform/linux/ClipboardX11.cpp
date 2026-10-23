#include "Clipboard.hpp"
#include "Transfer.hpp"
#include <xcb/xcb.h>
#include <xcb/xcbext.h>
#include <xcb/xfixes.h>
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <map>
#include <poll.h>
#include <thread>
namespace cliptiq::linux_backend {
namespace {
template<class T>using Reply=std::unique_ptr<T,decltype(&free)>;
Error busy(){return {ErrorCode::Busy,"X11 selection changed, owner exited or transfer timed out"};}
Error corrupt(){return {ErrorCode::Corrupt,"Malformed X11 selection reply"};}
}
struct ClipboardX11::State{
 ClipboardOptions options;std::thread::id thread=std::this_thread::get_id();xcb_connection_t* connection=nullptr;xcb_window_t root=0,window=0,owner=0;xcb_timestamp_t stamp=0,write_time=0;int event_base=0;
 std::map<std::string,xcb_atom_t> atoms;std::map<xcb_atom_t,std::string> names;
 std::uint64_t epoch=1,observed=0;std::atomic<bool> active{false};std::atomic<std::uint64_t> monitor_generation{0};CaptureCallback callback;std::string failure;
 std::shared_ptr<ClipboardSnapshot> replay;
 struct Send{std::shared_ptr<const Bytes> data;std::size_t offset=0;xcb_window_t requestor;xcb_atom_t property,type;Clock::time_point deadline;};std::vector<Send> sends;
 enum class Phase{Targets,Source,Data,Finish};
 struct Capture{
  xcb_window_t requestor,owner;std::uint64_t epoch,monitor;bool sync;Clock::time_point deadline;
  ClipboardSnapshot snapshot;std::vector<xcb_atom_t> targets,selected;std::size_t index=0;std::uint64_t total=0;
  std::unique_ptr<StreamBuffer> buffer;bool incr=false;std::uint64_t minimum=0;
  unsigned pending=0;Phase phase=Phase::Targets;
 };
 std::unique_ptr<Capture> capture;std::optional<Result<ClipboardSnapshot>> sync_result;
 std::vector<std::pair<std::uint64_t,Result<ClipboardSnapshot>>> deliveries;
 explicit State(ClipboardOptions o):options(std::move(o)){
  if(!options.timeout_ms||options.timeout_ms>10000){failure="Invalid X11 transfer deadline";return;}
  int screen=0;connection=xcb_connect(options.display_name.empty()?nullptr:options.display_name.c_str(),&screen);if(!connection||xcb_connection_has_error(connection)){failure="X11 display connection unavailable";return;}
  auto iterator=xcb_setup_roots_iterator(xcb_get_setup(connection));while(screen--&&iterator.rem)xcb_screen_next(&iterator);if(!iterator.rem){failure="X11 screen unavailable";return;}root=iterator.data->root;
  auto extension=xcb_get_extension_data(connection,&xcb_xfixes_id);if(!extension||!extension->present){failure="XFixes selection notifications unavailable";return;}event_base=extension->first_event;
  auto version=xcb_xfixes_query_version(connection,5,0);auto reply=wait_reply<xcb_xfixes_query_version_reply_t>(version.sequence);if(!reply){failure="XFixes version query failed";return;}
  for(const auto* name:{"CLIPBOARD","TARGETS","TIMESTAMP","INCR","WM_CLASS","CLIPTIQ_TRANSFER","CLIPTIQ_TIME","org.nspasteboard.TransientType","org.nspasteboard.ConcealedType"})atom(name);
  for(const auto& type:native_types(true))atom(type.name);
  window=xcb_generate_id(connection);std::uint32_t mask=XCB_EVENT_MASK_PROPERTY_CHANGE|XCB_EVENT_MASK_STRUCTURE_NOTIFY;xcb_create_window(connection,0,window,root,0,0,1,1,0,XCB_WINDOW_CLASS_INPUT_ONLY,0,XCB_CW_EVENT_MASK,&mask);
  xcb_xfixes_select_selection_input(connection,window,atom("CLIPBOARD"),XCB_XFIXES_SELECTION_EVENT_MASK_SET_SELECTION_OWNER|XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_WINDOW_DESTROY|XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_CLIENT_CLOSE);
  xcb_flush(connection);auto cookie=xcb_get_selection_owner(connection,atom("CLIPBOARD"));auto current=wait_reply<xcb_get_selection_owner_reply_t>(cookie.sequence);if(current)owner=current->owner;else failure="X11 selection owner query failed";
 }
 ~State(){if(connection){cancel(false);if(window)xcb_destroy_window(connection,window);xcb_flush(connection);xcb_disconnect(connection);}}
 bool connected()const{return connection&&!xcb_connection_has_error(connection)&&failure.empty()&&window;}
 bool own_thread()const{return thread==std::this_thread::get_id();}
 template<class T>Reply<T> wait_reply(unsigned sequence){xcb_flush(connection);void* value=nullptr;xcb_generic_error_t* error=nullptr;auto end=Clock::now()+std::chrono::milliseconds(500);while(Clock::now()<end){if(xcb_poll_for_reply(connection,sequence,&value,&error)){free(error);return Reply<T>(error?nullptr:static_cast<T*>(value),free);}if(xcb_connection_has_error(connection))break;pollfd fd{xcb_get_file_descriptor(connection),POLLIN,0};poll(&fd,1,5);}xcb_discard_reply(connection,sequence);return Reply<T>(nullptr,free);}
 xcb_atom_t atom(const std::string& name){auto known=atoms.find(name);if(known!=atoms.end())return known->second;auto cookie=xcb_intern_atom(connection,0,static_cast<std::uint16_t>(name.size()),name.c_str());xcb_flush(connection);auto r=wait_reply<xcb_intern_atom_reply_t>(cookie.sequence);if(!r)return 0;atoms[name]=r->atom;names[r->atom]=name;return r->atom;}
 std::string name(xcb_atom_t value){auto known=names.find(value);if(known!=names.end())return known->second;auto cookie=xcb_get_atom_name(connection,value);xcb_flush(connection);auto r=wait_reply<xcb_get_atom_name_reply_t>(cookie.sequence);if(!r)return {};auto length=xcb_get_atom_name_name_length(r.get());if(length<=0||length>4096)return {};std::string n(xcb_get_atom_name_name(r.get()),length);names[value]=n;return n;}
 void cancel(bool report=true){if(!capture)return;bool sync=capture->sync;auto generation=capture->monitor;if(capture->pending)xcb_discard_reply(connection,capture->pending);xcb_destroy_window(connection,capture->requestor);capture.reset();if(report){if(sync)sync_result=busy();else deliveries.emplace_back(generation,busy());}}
 void complete(Result<ClipboardSnapshot> result){if(!capture)return;bool sync=capture->sync;auto generation=capture->monitor;xcb_destroy_window(connection,capture->requestor);capture.reset();if(sync)sync_result=std::move(result);else{observed=epoch;deliveries.emplace_back(generation,std::move(result));}}
 void changed(xcb_window_t next,xcb_timestamp_t time){if(next==owner&&time==stamp)return;owner=next;stamp=time;++epoch;if(owner!=window)replay.reset();cancel();}
 void begin(bool sync){
  if(capture)return;if(!owner){if(sync)sync_result=Error{ErrorCode::NotFound,"X11 clipboard has no owner"};return;}
  auto requestor=xcb_generate_id(connection);std::uint32_t mask=XCB_EVENT_MASK_PROPERTY_CHANGE|XCB_EVENT_MASK_STRUCTURE_NOTIFY;xcb_create_window(connection,0,requestor,root,0,0,1,1,0,XCB_WINDOW_CLASS_INPUT_ONLY,0,XCB_CW_EVENT_MASK,&mask);
  capture=std::make_unique<Capture>();capture->requestor=requestor;capture->owner=owner;capture->epoch=epoch;capture->monitor=monitor_generation;capture->sync=sync;capture->deadline=Clock::now()+std::chrono::milliseconds(options.timeout_ms);
  capture->snapshot.sequence_token="x11:"+std::to_string(owner)+":"+std::to_string(stamp)+":"+std::to_string(epoch);
  xcb_convert_selection(connection,requestor,atom("CLIPBOARD"),atom("TARGETS"),atom("CLIPTIQ_TRANSFER"),stamp);xcb_flush(connection);
 }
 void property(Phase phase){if(!capture)return;capture->phase=phase;auto budget=phase==Phase::Targets?16384u:static_cast<unsigned>(std::min(options.limits.representation_bytes,options.limits.entry_bytes-capture->total));auto cookie=xcb_get_property(connection,0,capture->requestor,atom("CLIPTIQ_TRANSFER"),XCB_GET_PROPERTY_TYPE_ANY,0,(budget+3)/4);capture->pending=cookie.sequence;xcb_flush(connection);}
 void next(){if(!capture)return;auto& c=*capture;if(c.index==c.selected.size()){c.phase=Phase::Finish;c.pending=xcb_get_selection_owner(connection,atom("CLIPBOARD")).sequence;xcb_flush(connection);return;}c.buffer=std::make_unique<StreamBuffer>(std::min(options.limits.representation_bytes,options.limits.entry_bytes-c.total),c.deadline,c.epoch);c.incr=false;c.minimum=0;xcb_delete_property(connection,c.requestor,atom("CLIPTIQ_TRANSFER"));xcb_convert_selection(connection,c.requestor,atom("CLIPBOARD"),c.selected[c.index],atom("CLIPTIQ_TRANSFER"),stamp);xcb_flush(connection);}
 void select(){if(!capture)return;auto& c=*capture;auto caps=native_capabilities(true,true);c.snapshot.flags=native_flags(c.snapshot.offered_types);
  for(auto target:c.targets){auto n=name(target);auto t=std::find_if(native_types(true).begin(),native_types(true).end(),[&](const auto& type){return n==type.name;});if(t==native_types(true).end())continue;
   auto has=[&](const char* value){return std::any_of(c.snapshot.offered_types.begin(),c.snapshot.offered_types.end(),[&](const auto& o){return o.native_type==value;});};
   if(n=="STRING"&&(has("UTF8_STRING")||has("text/plain;charset=utf-8")||has("text/plain")))continue;if(n=="text/plain;charset=utf-8"&&has("UTF8_STRING"))continue;if(n=="text/plain"&&(has("UTF8_STRING")||has("text/plain;charset=utf-8")))continue;if(n=="image/tiff"&&has("image/png"))continue;
   c.snapshot.representations.push_back({0,"x11",n,t->kind,{},t->encoding});}
  auto decision=evaluate_capture(c.snapshot,options.policy,caps);c.snapshot.representations.clear();if(decision.disposition==CaptureDisposition::Reject){c.phase=Phase::Finish;c.pending=xcb_get_selection_owner(connection,atom("CLIPBOARD")).sequence;xcb_flush(connection);return;}
  for(const auto& r:decision.snapshot->representations)c.selected.push_back(atom(r.native_type));if(c.selected.size()>options.limits.max_representations){complete(Error{ErrorCode::TooLarge,"Too many X11 representations"});return;}next();
 }
 void reply(){if(!capture||!capture->pending)return;auto sequence=capture->pending;void* value=nullptr;xcb_generic_error_t* error=nullptr;if(!xcb_poll_for_reply(connection,sequence,&value,&error))return;Reply<void> memory(value,free);Reply<xcb_generic_error_t> error_owner(error,free);capture->pending=0;if(error||!value){complete(busy());return;}
  auto& c=*capture;if(c.phase==Phase::Finish){auto r=static_cast<xcb_get_selection_owner_reply_t*>(value);if(r->owner!=c.owner||c.epoch!=epoch){complete(busy());return;}complete(std::move(c.snapshot));return;}
  auto r=static_cast<xcb_get_property_reply_t*>(value);auto length=xcb_get_property_value_length(r);auto bytes=static_cast<const std::uint8_t*>(xcb_get_property_value(r));
  if(c.phase==Phase::Targets){if(r->type!=XCB_ATOM_ATOM||r->format!=32||r->bytes_after||length%4||length/4>4096){complete(corrupt());return;}auto data=reinterpret_cast<const xcb_atom_t*>(bytes);c.targets.assign(data,data+length/4);for(auto target:c.targets){auto n=name(target);if(n.empty()){complete(corrupt());return;}c.snapshot.offered_types.push_back({"x11",std::move(n)});}xcb_delete_property(connection,c.requestor,atom("CLIPTIQ_TRANSFER"));c.phase=Phase::Source;c.pending=xcb_get_property(connection,0,c.owner,atom("WM_CLASS"),XCB_ATOM_STRING,0,1024).sequence;xcb_flush(connection);return;}
  if(c.phase==Phase::Source){if(r->format==8&&r->type==XCB_ATOM_STRING&&!r->bytes_after&&length>1){std::string hints(reinterpret_cast<const char*>(bytes),length);auto split=hints.find('\0');if(split!=std::string::npos){auto end=hints.find('\0',split+1);auto klass=hints.substr(split+1,end==std::string::npos?std::string::npos:end-split-1);if(!klass.empty()){for(auto& ch:klass)if(ch>='A'&&ch<='Z')ch+=32;c.snapshot.source={"x11.wm_class:"+klass,SourceConfidence::BestEffort};}}}select();return;}
  if(r->type==atom("INCR")){if(c.incr||r->format!=32||length!=4||r->bytes_after){complete(corrupt());return;}std::uint32_t minimum;memcpy(&minimum,bytes,4);if(minimum>options.limits.representation_bytes||minimum>options.limits.entry_bytes-c.total){complete(Error{ErrorCode::TooLarge,"X11 INCR announcement exceeds byte budget"});return;}c.minimum=minimum;c.incr=true;xcb_delete_property(connection,c.requestor,atom("CLIPTIQ_TRANSFER"));xcb_flush(connection);return;}
  auto target=c.selected[c.index];if(r->format!=8||r->bytes_after||r->type!=target){complete(r->bytes_after?Error{ErrorCode::TooLarge,"X11 property exceeds byte budget"}:corrupt());return;}
  auto appended=c.buffer->append(bytes,static_cast<std::size_t>(length),epoch);if(!appended){complete(appended.error());return;}xcb_delete_property(connection,c.requestor,atom("CLIPTIQ_TRANSFER"));xcb_flush(connection);
  if(!c.incr||!length){auto data=c.buffer->finish(epoch);if(!data){complete(data.error());return;}if(data.value().size()<c.minimum){complete(corrupt());return;}auto n=name(target);auto t=std::find_if(native_types(true).begin(),native_types(true).end(),[&](const auto& t){return n==t.name;});Representation representation{0,"x11",n,t->kind,std::move(data.value()),t->encoding};auto valid=validate_payload(representation);if(!valid){complete(valid.error());return;}if(representation.data.size()>options.limits.representation_bytes||representation.data.size()>options.limits.entry_bytes-c.total){complete(Error{ErrorCode::TooLarge,"Converted X11 payload exceeds budget"});return;}c.total+=representation.data.size();c.snapshot.representations.push_back(std::move(representation));++c.index;next();}
 }
 void send_notify(const xcb_selection_request_event_t& request,xcb_atom_t property){xcb_selection_notify_event_t reply{};reply.response_type=XCB_SELECTION_NOTIFY;reply.time=request.time;reply.requestor=request.requestor;reply.selection=request.selection;reply.target=request.target;reply.property=property;xcb_send_event(connection,0,request.requestor,0,reinterpret_cast<const char*>(&reply));}
 void request(const xcb_selection_request_event_t& r){auto property=r.property?r.property:r.target;if(!replay||r.owner!=window||r.selection!=atom("CLIPBOARD")){send_notify(r,0);return;}
  if(r.target==atom("TARGETS")){std::vector<xcb_atom_t> offers={atom("TARGETS"),atom("TIMESTAMP")};for(const auto& value:replay->representations)offers.push_back(atom(value.native_type));if(replay->flags.transient==Flag::Yes)offers.push_back(atom("org.nspasteboard.TransientType"));if(replay->flags.concealed==Flag::Yes)offers.push_back(atom("org.nspasteboard.ConcealedType"));xcb_change_property(connection,XCB_PROP_MODE_REPLACE,r.requestor,property,XCB_ATOM_ATOM,32,offers.size(),offers.data());send_notify(r,property);return;}
  if(r.target==atom("TIMESTAMP")){xcb_change_property(connection,XCB_PROP_MODE_REPLACE,r.requestor,property,XCB_ATOM_INTEGER,32,1,&write_time);send_notify(r,property);return;}
  auto n=name(r.target);auto found=std::find_if(replay->representations.begin(),replay->representations.end(),[&](const auto& item){return item.native_type==n;});if(found==replay->representations.end()){if(n=="org.nspasteboard.TransientType"||n=="org.nspasteboard.ConcealedType"){xcb_change_property(connection,XCB_PROP_MODE_REPLACE,r.requestor,property,r.target,8,0,nullptr);send_notify(r,property);}else send_notify(r,0);return;}
  if(found->data.size()<=65536){xcb_change_property(connection,XCB_PROP_MODE_REPLACE,r.requestor,property,r.target,8,found->data.size(),found->data.data());send_notify(r,property);return;}
  std::uint64_t retained=0;for(const auto& s:sends)retained+=s.data->size();if(sends.size()>=8||retained+found->data.size()>128ull*1024*1024){send_notify(r,0);return;}std::uint32_t mask=XCB_EVENT_MASK_PROPERTY_CHANGE|XCB_EVENT_MASK_STRUCTURE_NOTIFY;xcb_change_window_attributes(connection,r.requestor,XCB_CW_EVENT_MASK,&mask);auto data=std::shared_ptr<const Bytes>(replay,&found->data);sends.push_back({data,0,r.requestor,property,r.target,Clock::now()+std::chrono::milliseconds(options.timeout_ms)});std::uint32_t size=data->size();xcb_change_property(connection,XCB_PROP_MODE_REPLACE,r.requestor,property,atom("INCR"),32,1,&size);send_notify(r,property);
 }
 void event(xcb_generic_event_t* generic){auto type=generic->response_type&127;
  if(type==event_base+XCB_XFIXES_SELECTION_NOTIFY){auto e=reinterpret_cast<xcb_xfixes_selection_notify_event_t*>(generic);if(e->selection==atom("CLIPBOARD"))changed(e->owner,e->selection_timestamp);return;}
  if(type==XCB_SELECTION_REQUEST){request(*reinterpret_cast<xcb_selection_request_event_t*>(generic));return;}
  if(type==XCB_SELECTION_NOTIFY){auto e=reinterpret_cast<xcb_selection_notify_event_t*>(generic);if(capture&&e->requestor==capture->requestor&&e->selection==atom("CLIPBOARD")){auto expected=capture->phase==Phase::Targets?atom("TARGETS"):capture->selected[capture->index];if(e->target!=expected)return;if(!e->property){complete(Error{ErrorCode::Unsupported,"X11 owner refused a declared selection format"});return;}property(capture->phase==Phase::Targets?Phase::Targets:Phase::Data);}return;}
  if(type==XCB_PROPERTY_NOTIFY){auto e=reinterpret_cast<xcb_property_notify_event_t*>(generic);if(e->window==window&&e->atom==atom("CLIPTIQ_TIME"))write_time=e->time;
   if(capture&&capture->incr&&!capture->pending&&e->window==capture->requestor&&e->atom==atom("CLIPTIQ_TRANSFER")&&e->state==XCB_PROPERTY_NEW_VALUE)property(Phase::Data);
   if(e->state==XCB_PROPERTY_DELETE)for(auto it=sends.begin();it!=sends.end();++it)if(it->requestor==e->window&&it->property==e->atom){auto amount=std::min<std::size_t>(65536,it->data->size()-it->offset);xcb_change_property(connection,XCB_PROP_MODE_REPLACE,it->requestor,it->property,it->type,8,amount,it->data->data()+it->offset);it->offset+=amount;if(!amount)sends.erase(it);break;}return;}
  if(type==XCB_DESTROY_NOTIFY){auto e=reinterpret_cast<xcb_destroy_notify_event_t*>(generic);sends.erase(std::remove_if(sends.begin(),sends.end(),[&](const auto& s){return s.requestor==e->window;}),sends.end());}
 }
 void drain(){while(auto event_ptr=xcb_poll_for_event(connection)){Reply<xcb_generic_event_t> held(event_ptr,free);event(event_ptr);}}
 Result<void> pump(unsigned timeout){if(!own_thread())return Error{ErrorCode::InvalidArgument,"X11 backend requires owner event thread"};if(!connected())return Error{ErrorCode::ReadOnly,failure.empty()?"X11 display disconnected":failure};
  drain();for(unsigned i=0;i<64&&capture&&capture->pending;++i){auto old=capture->pending;reply();if(capture&&capture->pending==old)break;drain();}
  if(capture&&Clock::now()>=capture->deadline)complete(busy());sends.erase(std::remove_if(sends.begin(),sends.end(),[](const auto& s){return Clock::now()>=s.deadline;}),sends.end());
  if(active&&!capture&&owner&&owner!=window&&observed!=epoch)begin(false);xcb_flush(connection);
  if(timeout){pollfd fd{xcb_get_file_descriptor(connection),POLLIN,0};poll(&fd,1,std::min(timeout,10u));drain();reply();}
  auto ready=std::move(deliveries);deliveries.clear();for(auto& item:ready)if(active&&item.first==monitor_generation&&callback){auto cb=callback;cb(std::move(item.second));}return {};
 }
};
ClipboardX11::ClipboardX11(ClipboardOptions o):state_(std::make_shared<State>(std::move(o))){}
ClipboardX11::~ClipboardX11(){stop();}
Capabilities ClipboardX11::capabilities()const{return native_capabilities(true,state_->connected(),state_->failure);}
int ClipboardX11::connection_fd()const{return state_->connected()?xcb_get_file_descriptor(state_->connection):-1;}
Result<void> ClipboardX11::dispatch(unsigned timeout){auto state=state_;return state->pump(timeout);}
Result<void> ClipboardX11::set_capture_policy(CapturePolicy policy){auto s=state_;if(!s->own_thread())return Error{ErrorCode::InvalidArgument,"X11 policy requires owner thread"};if(evaluate_capture({},policy,capabilities()).reason==CaptureReason::InvalidInput)return Error{ErrorCode::InvalidArgument,"Invalid capture policy"};s->options.policy=std::move(policy);return {};}
Result<void> ClipboardX11::start(CaptureCallback callback){auto s=state_;if(!s->own_thread()||!callback||s->active)return Error{ErrorCode::InvalidArgument,"Invalid X11 monitor start"};if(!s->connected())return Error{ErrorCode::ReadOnly,"X11 capture unavailable"};s->cancel(false);++s->monitor_generation;s->callback=std::move(callback);s->active=true;return {};}
void ClipboardX11::stop(){auto s=state_;s->active=false;++s->monitor_generation;if(s->own_thread()){s->callback={};s->cancel(false);}}
Result<ClipboardSnapshot> ClipboardX11::read_snapshot(){auto s=state_;if(!s->own_thread())return Error{ErrorCode::InvalidArgument,"X11 read requires owner thread"};auto pumped=s->pump(0);if(!pumped)return pumped.error();if(s->capture)return Error{ErrorCode::Busy,"An X11 capture is already pending"};auto cookie=xcb_get_selection_owner(s->connection,s->atom("CLIPBOARD"));xcb_flush(s->connection);auto current=s->wait_reply<xcb_get_selection_owner_reply_t>(cookie.sequence);s->drain();if(!current)return busy();if(current->owner!=s->owner)s->changed(current->owner,0);s->sync_result.reset();s->begin(true);while(!s->sync_result){auto r=s->pump(5);if(!r){s->cancel(false);return r.error();}}auto result=std::move(*s->sync_result);s->sync_result.reset();return result;}
Result<std::string> ClipboardX11::write_snapshot(const ClipboardSnapshot& input){auto s=state_;if(!s->own_thread())return Error{ErrorCode::InvalidArgument,"X11 replay requires owner thread"};if(!s->connected())return Error{ErrorCode::ReadOnly,"X11 replay unavailable"};auto checked=prepare_replay(input,s->options,true);if(!checked)return checked.error();auto prepared=std::make_shared<ClipboardSnapshot>(std::move(checked.value()));s->write_time=0;std::uint8_t marker=1;xcb_change_property(s->connection,XCB_PROP_MODE_REPLACE,s->window,s->atom("CLIPTIQ_TIME"),XCB_ATOM_INTEGER,8,1,&marker);xcb_flush(s->connection);auto end=Clock::now()+std::chrono::milliseconds(500);while(!s->write_time&&Clock::now()<end){s->drain();pollfd fd{xcb_get_file_descriptor(s->connection),POLLIN,0};poll(&fd,1,2);}if(!s->write_time)return busy();xcb_set_selection_owner(s->connection,s->window,s->atom("CLIPBOARD"),s->write_time);xcb_flush(s->connection);auto cookie=xcb_get_selection_owner(s->connection,s->atom("CLIPBOARD"));auto reply=s->wait_reply<xcb_get_selection_owner_reply_t>(cookie.sequence);if(!reply||reply->owner!=s->window)return busy();s->drain();s->owner=s->window;s->replay=std::move(prepared);s->observed=s->epoch;return "x11:"+std::to_string(s->window)+":"+std::to_string(s->write_time)+":"+std::to_string(s->epoch);}
}
