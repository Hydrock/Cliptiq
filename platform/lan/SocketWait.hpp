#pragma once
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#include <map>
#include <vector>
#include <algorithm>
namespace cliptiq::lan {
// Worker-owned socket subscriptions; signal() alone is safe from other threads.
// Remove a subscription before closing its socket (Windows reuses socket IDs).
class SocketWait {
 struct Watch { WSAEVENT event=WSA_INVALID_EVENT; long mask=0; };
 HANDLE wake_=CreateEventW(nullptr,FALSE,FALSE,nullptr);
 std::map<SOCKET,Watch> watches_;
public:
 struct Interest { SOCKET socket; long mask; };
 SocketWait()=default;
 SocketWait(const SocketWait&)=delete;
 SocketWait& operator=(const SocketWait&)=delete;
 ~SocketWait(){for(auto& item:watches_)WSACloseEvent(item.second.event);if(wake_)CloseHandle(wake_);}
 bool valid()const{return wake_!=nullptr;}
 void signal(){if(wake_)SetEvent(wake_);}
 void remove(SOCKET socket){auto it=watches_.find(socket);if(it!=watches_.end()){WSAEventSelect(socket,nullptr,0);WSACloseEvent(it->second.event);watches_.erase(it);}}
 // false requests the old bounded polling fallback on resource/API failure.
 bool wait(const std::vector<Interest>& sockets,unsigned timeout){
  if(!wake_||sockets.size()+1>MAXIMUM_WAIT_OBJECTS)return false;
  for(auto it=watches_.begin();it!=watches_.end();){auto socket=it->first;++it;if(std::none_of(sockets.begin(),sockets.end(),[&](auto& s){return s.socket==socket;}))remove(socket);}
  std::vector<HANDLE> events{wake_};
  for(auto interest:sockets){
   auto& watch=watches_[interest.socket];
   if(watch.event==WSA_INVALID_EVENT)watch.event=WSACreateEvent();
   if(watch.event==WSA_INVALID_EVENT)return false;
   if(watch.mask!=interest.mask){if(WSAEventSelect(interest.socket,watch.event,interest.mask)!=0)return false;watch.mask=interest.mask;}
   events.push_back(watch.event);
  }
  auto result=WaitForMultipleObjects(static_cast<DWORD>(events.size()),events.data(),FALSE,timeout);
  if(result==WAIT_FAILED)return false;
  // Consume readiness only after waiting; the next worker pass drains I/O.
  for(auto& [socket,watch]:watches_){WSANETWORKEVENTS ready{};if(WSAEnumNetworkEvents(socket,watch.event,&ready)!=0)return false;}
  return true;
 }
};
}
#endif
