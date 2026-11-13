#include "platform/lan/SocketWait.hpp"
#include <chrono>
#include <thread>
#include <iostream>
#include <stdexcept>
using namespace cliptiq::lan;
using Clock=std::chrono::steady_clock;
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(){try{
 WSADATA data{};check(WSAStartup(MAKEWORD(2,2),&data)==0,"WSAStartup");
 {
 SocketWait wait;check(wait.valid(),"wake event");
 auto begin=Clock::now();check(wait.wait({},100),"idle wait failed");
 check(Clock::now()-begin>=std::chrono::milliseconds(80),"idle wait spun");
 std::thread sender([&]{std::this_thread::sleep_for(std::chrono::milliseconds(25));wait.signal();});
 begin=Clock::now();auto woke=wait.wait({},1000);auto elapsed=Clock::now()-begin;sender.join();
 check(woke&&elapsed<std::chrono::milliseconds(700),"command wake delayed");
 SOCKET server=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);check(server!=INVALID_SOCKET,"server");
 sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
 check(bind(server,reinterpret_cast<sockaddr*>(&address),sizeof address)==0&&listen(server,1)==0,"listen");
 int size=sizeof address;check(getsockname(server,reinterpret_cast<sockaddr*>(&address),&size)==0,"port");
 SOCKET client=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);check(connect(client,reinterpret_cast<sockaddr*>(&address),size)==0,"connect");
 SOCKET peer=accept(server,nullptr,nullptr);check(peer!=INVALID_SOCKET,"accept");
 std::vector<SocketWait::Interest> sockets{{peer,FD_READ|FD_CLOSE}};
 std::thread writer([&]{std::this_thread::sleep_for(std::chrono::milliseconds(25));send(client,"x",1,0);});
 begin=Clock::now();auto readable=wait.wait(sockets,1000);elapsed=Clock::now()-begin;writer.join();
 check(readable&&elapsed<std::chrono::milliseconds(700),"socket read delayed");char c;check(recv(peer,&c,1,0)==1&&c=='x',"payload");
 begin=Clock::now();check(wait.wait(sockets,100),"second idle wait");
 check(Clock::now()-begin>=std::chrono::milliseconds(80),"read readiness not reset");
 closesocket(client);begin=Clock::now();check(wait.wait(sockets,1000)&&Clock::now()-begin<std::chrono::milliseconds(700),"close wake delayed");
 closesocket(peer);closesocket(server);
 }
 WSACleanup();std::cout<<"PASS idle block, command wake, socket read/close and reset\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
