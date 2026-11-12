#include "lan/Transport.hpp"
#include <atomic>
#include <chrono>
#include <iostream>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <stdexcept>
#include <thread>
using namespace cliptiq;
using namespace cliptiq::lan;
using namespace std::chrono_literals;
static void require(bool ok, const char *why) {
  if (!ok)
    throw std::runtime_error(why);
}
template <class F> static bool wait(F predicate, int ms = 4000) {
  auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  while (!predicate() && std::chrono::steady_clock::now() < until)
    std::this_thread::sleep_for(10ms);
  return predicate();
}
// An independent real TLS endpoint also exercises hostile framing and short
// I/O.
struct RawPeer {
  mbedtls_net_context net;
  mbedtls_ssl_context ssl;
  mbedtls_ssl_config conf;
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context rng;
  RawPeer(unsigned short port) {
    mbedtls_net_init(&net);
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&conf);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&rng);
    require(!mbedtls_ctr_drbg_seed(&rng, mbedtls_entropy_func, &entropy,
                                   nullptr, 0),
            "raw RNG");
    require(!mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT,
                                         MBEDTLS_SSL_TRANSPORT_STREAM,
                                         MBEDTLS_SSL_PRESET_DEFAULT),
            "raw TLS config");
    mbedtls_ssl_conf_min_tls_version(&conf, MBEDTLS_SSL_VERSION_TLS1_3);
    mbedtls_ssl_conf_max_tls_version(&conf, MBEDTLS_SSL_VERSION_TLS1_3);
    mbedtls_ssl_conf_tls13_key_exchange_modes(
        &conf, MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_PSK_EPHEMERAL);
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &rng);
    Bytes key(32, 42);
    const std::string identity = "shared-auth-id";
    require(!mbedtls_ssl_conf_psk(
                &conf, key.data(), key.size(),
                reinterpret_cast<const unsigned char *>(identity.data()),
                identity.size()),
            "raw PSK");
    require(!mbedtls_net_connect(&net, "127.0.0.1",
                                 std::to_string(port).c_str(),
                                 MBEDTLS_NET_PROTO_TCP),
            "raw socket");
    require(!mbedtls_ssl_setup(&ssl, &conf), "raw TLS setup");
    mbedtls_ssl_conf_read_timeout(&conf, 1000);
    mbedtls_ssl_set_bio(&ssl, &net, mbedtls_net_send, mbedtls_net_recv,
                        mbedtls_net_recv_timeout);
    require(!mbedtls_ssl_handshake(&ssl), "raw TLS handshake");
  }
  ~RawPeer() {
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_net_free(&net);
    mbedtls_ctr_drbg_free(&rng);
    mbedtls_entropy_free(&entropy);
  }
  void send(const Bytes &bytes, bool fragmented = false) {
    size_t offset = 0;
    while (offset < bytes.size()) {
      int n = mbedtls_ssl_write(&ssl, bytes.data() + offset,
                                fragmented ? 1 : bytes.size() - offset);
      require(n > 0, "raw write");
      offset += n;
      if (fragmented)
        std::this_thread::sleep_for(1ms);
    }
  }
  Bytes read(size_t count) {
    Bytes bytes(count);
    size_t offset = 0;
    while (offset < count) {
      int n = mbedtls_ssl_read(&ssl, bytes.data() + offset, count - offset);
      require(n > 0, "raw read");
      offset += n;
    }
    return bytes;
  }
  void hello() {
    std::string id = "cccccccc-cccc-cccc-cccc-cccccccccccc";
    Bytes frame{0, 0, 0, static_cast<unsigned char>(id.size()), 1};
    frame.insert(frame.end(), id.begin(), id.end());
    send(frame, true);
    auto header = read(5);
    require(header[4] == 1, "server hello");
    read(header[3]);
  }
};
int main() {
  try {
    Transport a, b;
    require(!a.active(), "constructor active");
    require(!a.post([] {}), "off post accepted");
    a.stop();
    std::atomic<int> connected{0}, arrivals{0}, preflights{0}, started{0},
        stopped{0}, posted{0}, disconnected{0};
    std::atomic<bool> persist{true};
    std::atomic<size_t> expectedSize{3};
    TransportCallbacks cb;
    cb.started = [&] { ++started; };
    cb.stopped = [&] { ++stopped; };
    cb.identity = [](std::string, std::string) -> Result<void> { return {}; };
    cb.status = [&](std::string, std::string s) {
      if (s.find("Connected") != std::string::npos)
        ++connected;
      if (s=="Disconnected")++disconnected;
    };
    cb.receiving = [&](std::string) -> Result<std::string> {
      ++preflights;
      return std::string("token");
    };
    cb.received = [&](std::string, Bytes bytes,
                      std::string token) -> Result<void> {
      require(preflights > 0 && token == "token", "checkpoint missing");
      require(bytes.size() == expectedSize, "payload size differs");
      if (bytes.size() == 3)
        require(bytes == Bytes({1, 2, 3}), "payload differs");
      ++arrivals;
      if (!persist)
        return Error{ErrorCode::Io, "test persistence failure"};
      return {};
    };
    TransportPeer pa;
    pa.id = "pair";
    pa.host = "localhost";
    pa.port = 49882;
    pa.auth_id = "shared-auth-id";
    pa.key = Bytes(32, 42);
    pa.send = pa.receive = true;
    TransportPeer pb = pa;
    pb.port = 49881;
    TransportConfig ca{
        "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa", "127.0.0.1", 49881, {pa}};
    TransportConfig cc{
        "bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb", "127.0.0.1", 49882, {pb}};
    require(bool(a.start(ca, cb)), "a start");
    require(bool(b.start(cc, cb)), "b start");
    require(wait([&] { return connected >= 2; }, 8000), "TLS handshake failed");
    std::this_thread::sleep_for(150ms);
    auto retained = std::make_shared<Bytes>(Bytes{1, 2, 3});
    a.publish(retained);
    require(wait([&] { return arrivals == 1; }), "TLS delivery failed");
    std::this_thread::sleep_for(50ms);
    // Retaining the caller's shared_ptr must not consume transport queue
    // budget.
    a.publish(retained);
    require(wait([&] { return arrivals == 2; }),
            "retained payload blocked publish");
    std::this_thread::sleep_for(50ms);
    expectedSize = 1024 * 1024;
    a.publish(std::make_shared<Bytes>(expectedSize.load(), 7));
    require(wait([&] { return arrivals == 3; }, 6000),
            "large fragmented TLS record delivery failed");
    require(bool(a.post([&] { ++posted; })), "post failed");
    require(wait([&] { return posted == 1; }, 1000), "post not executed");
    auto before = std::chrono::steady_clock::now();
    a.stop();
    b.stop();
    require(std::chrono::steady_clock::now() - before < 1s, "stop slow");
    require(started == 2 && stopped == 2, "lifecycle callbacks");
    connected = 0;
    arrivals = 0;
    expectedSize = 3;
    cc.peers[0].key[0] ^= 1;
    require(bool(a.start(ca, cb)) && bool(b.start(cc, cb)), "wrong-key start");
    std::this_thread::sleep_for(1500ms);
    a.publish(retained);
    std::this_thread::sleep_for(200ms);
    a.stop();
    b.stop();
    require(connected == 0 && arrivals == 0, "wrong PSK authenticated");
    // Unknown/mismatched authenticated device identities are rejected.
    auto pinned = ca;
    pinned.peers[0].remote_device_id = "expected-other-device";
    require(bool(a.start(pinned, cb)), "pinned start");
    {
      RawPeer raw(49881);
      std::string id = "cccccccc-cccc-cccc-cccc-cccccccccccc";
      Bytes frame{0, 0, 0, 36, 1};
      frame.insert(frame.end(), id.begin(), id.end());
      raw.send(frame);
      std::this_thread::sleep_for(100ms);
    }
    a.stop();
    require(connected == 0, "wrong device identity authenticated");
    // A socket may disappear before the replacement HELLO is received.
    // Keep the publication until ACK, including across this ordered reconnect.
    connected=0;disconnected=0;
    require(bool(a.start(ca, cb)), "replay server start");
    {
      RawPeer raw(49881);raw.hello();
      require(wait([&]{return connected>0;}),"replay peer hello");
      a.publish(retained);
      require(raw.read(5)==Bytes({0,0,0,3,2}) && raw.read(3)==*retained,"initial replay frame missing");
      require(!wait([&]{return a.ready_to_publish();},100),"publication released before ACK");
    }
    require(wait([&]{return disconnected>0;}),"old socket not removed");
    {
      RawPeer raw(49881);raw.hello();
      require(raw.read(5)==Bytes({0,0,0,3,2}) && raw.read(3)==*retained,"unacknowledged frame lost across reconnect");
      raw.send({0,0,0,0,3});
      require(wait([&]{return a.ready_to_publish();}),"ACK retained publication budget");
      a.publish(retained);
      require(raw.read(5)==Bytes({0,0,0,3,2}) && raw.read(3)==*retained,"expiry frame missing");
      require(wait([&]{return a.ready_to_publish();},11000),"expired publication retained budget");
    }
    a.stop();
    // Short reads and ACK only after successful persistence.
    require(bool(a.start(ca, cb)), "raw server start");
    {
      RawPeer raw(49881);
      raw.hello();
      raw.send({0, 0, 0, 3, 2, 1, 2, 3}, true);
      auto ack = raw.read(5);
      require(ack == Bytes({0, 0, 0, 0, 3}), "persistence ACK missing");
      persist = false;
      raw.send({0, 0, 0, 3, 2, 1, 2, 3}, true);
      require(wait([&] { return arrivals == 2; }),
              "persistence failure not observed");
      unsigned char byte = 0;
      require(mbedtls_ssl_read(&raw.ssl, &byte, 1) <= 0,
              "ACK sent before persistence");
      persist = true;
    }
    a.stop();
    arrivals = 0;
    preflights = 0;
    require(bool(a.start(ca, cb)), "oversize server start");
    {
      RawPeer raw(49881);
      raw.hello();
      raw.send({0, 128, 0, 1, 2});
      std::this_thread::sleep_for(100ms);
    }
    a.stop();
    require(arrivals == 0 && preflights == 0,
            "oversize allocated or reached callbacks");
    // An abandoned maximum frame releases the global receive reservation.
    require(bool(a.start(ca, cb)), "stalled server start");
    {
      RawPeer raw(49881);
      raw.hello();
      raw.send({0, 128, 0, 0, 2});
      std::this_thread::sleep_for(10500ms);
    }
    {
      RawPeer raw(49881);
      raw.hello();
      raw.send({0, 0, 0, 3, 2, 1, 2, 3});
      require(raw.read(5) == Bytes({0, 0, 0, 0, 3}),
              "stalled frame retained budget");
    }
    a.stop();
    arrivals = 0;
    connected = 0;
    // An event published while disconnected must not replay upon reconnect.
    cc.peers[0].key = pa.key;
    require(bool(a.start(ca, cb)), "offline server start");
    a.publish(retained);
    std::this_thread::sleep_for(100ms);
    require(bool(b.start(cc, cb)), "reconnect server start");
    require(wait([&] { return connected >= 2; }, 8000), "reconnect handshake");
    std::this_thread::sleep_for(200ms);
    require(arrivals == 0, "offline event replayed");
    a.publish(retained);
    require(wait([&] { return arrivals == 1; }),
            "new event after reconnect missing");
    a.stop();
    b.stop();
    // Queued jobs are cancelled once stop begins, while in-flight work drains.
    std::atomic<bool> entered{false}, release{false}, stopEntered{false};
    require(bool(a.start(ca, cb)), "post-stop start");
    require(bool(a.post([&] {
              entered = true;
              while (!release)
                std::this_thread::sleep_for(1ms);
            })),
            "blocking post");
    require(wait([&] { return entered.load(); }, 1000), "blocking job missing");
    require(bool(a.post([&] { ++posted; })), "queued post");
    std::thread stopper([&] {
      stopEntered = true;
      a.stop();
    });
    wait([&] { return stopEntered.load(); }, 1000);
    std::this_thread::sleep_for(30ms);
    release = true;
    stopper.join();
    require(posted == 1, "queued post ran after stop");
    require(!a.post([] {}), "stopped post accepted");
    std::cout << "TLS 1.3 PSK: loopback, wrong key/identity, short I/O, 1MiB "
                 "payload, persistence ACK, oversize and queued-stop passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
