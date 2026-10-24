#include "Updates.hpp"
#include <future>
#include <chrono>
namespace cliptiq {
Result<std::string> native_update_feed();
struct NativeUpdates::State {
  std::future<Result<std::string>> work;
  std::string message="Version 0.1.0; update metadata not checked";
};
NativeUpdates::NativeUpdates():state_(std::make_shared<State>()){}
Result<void> NativeUpdates::check_metadata(){
  if(state_->work.valid())return Error{ErrorCode::Busy,"Update check in progress"};
  state_->message="Checking project release metadata (not signature verification)";
  try{state_->work=std::async(std::launch::async,[]()->Result<std::string>{try{auto feed=native_update_feed();if(!feed)return feed.error();return release_hint(feed.value(),"0.1.0");}catch(...){return Error{ErrorCode::Io,"Release metadata check failed"};}});}
  catch(...){return Error{ErrorCode::Io,"Cannot start update worker"};}
  return {};
}
bool NativeUpdates::poll(){
  if(!state_->work.valid()||state_->work.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return false;
  auto r=state_->work.get();
  state_->message=r?"Version "+r.value()+" advertised; authenticity not verified. Install through the signed native/package route.":r.error().message;
  return true;
}
std::string NativeUpdates::metadata_status()const{return state_->message;}
}
