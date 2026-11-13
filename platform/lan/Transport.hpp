#pragma once
#include "cliptiq/types.hpp"
#include <functional>
#include <memory>
namespace cliptiq::lan {
struct TransportPeer {
  std::string id, host, remote_device_id, auth_id;
  std::uint16_t port = 0;
  Bytes key;
  bool send = false, receive = false;
};
struct TransportConfig {
  std::string device_id, bind_address;
  std::uint16_t port = 0;
  std::vector<TransportPeer> peers;
  bool probe_only=false;
};
struct TransportCallbacks {
  std::function<Result<void>(std::string, std::string)> identity;
  std::function<Result<std::string>(std::string)> receiving;
  std::function<Result<void>(std::string, Bytes, std::string)> received;
  std::function<void(std::string, std::string)> status;
  std::function<void()> started, stopped, tick;
};
class Transport {
public:
  Transport();
  ~Transport();
  Transport(const Transport &) = delete;
  Transport &operator=(const Transport &) = delete;
  Result<void> start(TransportConfig, TransportCallbacks);
  void stop();
  void publish(std::shared_ptr<const Bytes>);
  Result<void> post(std::function<void()>);
  bool active() const;
  bool ready_to_publish() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace cliptiq::lan
