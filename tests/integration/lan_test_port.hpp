#pragma once
#include <stdexcept>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#endif

#define LAN_SOCKET_CHECK(x) do { if (!(x)) throw std::runtime_error("loopback port allocation: " #x); } while (false)
static unsigned short free_port(){
#ifdef _WIN32
 static bool initialized=[] {WSADATA data{};return WSAStartup(MAKEWORD(2,2),&data)==0;}();LAN_SOCKET_CHECK(initialized);
#endif
 auto s=socket(AF_INET,SOCK_STREAM,0);LAN_SOCKET_CHECK(s!=decltype(s)(-1));sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);LAN_SOCKET_CHECK(bind(s,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0);
#ifdef _WIN32
 int size=sizeof(address);
#else
 socklen_t size=sizeof(address);
#endif
 LAN_SOCKET_CHECK(getsockname(s,reinterpret_cast<sockaddr*>(&address),&size)==0);
#ifdef _WIN32
 closesocket(s);
#else
 close(s);
#endif
 return ntohs(address.sin_port);
}
#undef LAN_SOCKET_CHECK
