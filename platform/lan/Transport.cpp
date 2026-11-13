#include "CryptoRuntime.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Transport.hpp"
#include "Resolver.hpp"
#include <algorithm>
#include <charconv>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/ssl.h>
#include <mbedtls/threading.h>
#include <mutex>
#include <psa/crypto.h>
#include <thread>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
namespace cliptiq::lan {
namespace {
constexpr size_t cap = 8u * 1024 * 1024;
using Clock = std::chrono::steady_clock;
constexpr unsigned retrySeconds(unsigned attempt) {
  return attempt >= 5 ? 30u : (1u << attempt);
}
static_assert(retrySeconds(0) == 1 && retrySeconds(1) == 2 &&
                  retrySeconds(2) == 4 && retrySeconds(3) == 8 &&
                  retrySeconds(4) == 16 && retrySeconds(5) == 30 &&
                  retrySeconds(100) == 30,
              "Reconnect backoff must increase and remain bounded");
bool numericAddress(const std::string &address) {
  auto host=address;auto zone=host.find('%');
  if(zone!=std::string::npos){
    auto scope=host.substr(zone+1);host.resize(zone);std::uint32_t index=0;auto parsed=std::from_chars(scope.data(),scope.data()+scope.size(),index);
    if(scope.empty()||parsed.ec!=std::errc{}||parsed.ptr!=scope.data()+scope.size()||index==0||host.find(':')==std::string::npos)return false;
  }
  unsigned char binary[sizeof(in6_addr)]{};
#ifdef _WIN32
  return InetPtonA(AF_INET, host.c_str(), binary) == 1 ||
         InetPtonA(AF_INET6, host.c_str(), binary) == 1;
#else
  return inet_pton(AF_INET, host.c_str(), binary) == 1 ||
         inet_pton(AF_INET6, host.c_str(), binary) == 1;
#endif
}
Error fail(const char *m) { return {ErrorCode::Io, m}; }
bool retry(int n) {
  return n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE;
}
} // namespace
struct Transport::Impl {
  struct Connection {
    Impl *owner;
    bool suppressFailure = false;
    const char *failureStatus = nullptr;
    mbedtls_net_context net;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config tls;
    TransportPeer *peer = nullptr;
    bool connecting = false, client = false, handshake = false, hello = false,
         dead = false;
    Clock::time_point born = Clock::now(), lastRead = Clock::now(),
                      lastWrite = Clock::now();
    std::array<unsigned char, 5> header{};
    size_t head = 0, need = 0;
    unsigned type = 0;
    Bytes body;
    std::string checkpoint;
    struct Out {
      std::array<unsigned char, 5> h{};
      std::shared_ptr<const Bytes> body;
      std::shared_ptr<int> budget;
      size_t offset = 0;
    };
    std::deque<Out> out;
    // Keep sent payloads until their FIFO ACK, so replacing a simultaneous
    // duplicate connection can replay them safely (Service deduplicates IDs).
    std::deque<Out> unacknowledged;
    Connection(Impl *o) : owner(o) {
      mbedtls_net_init(&net);
      mbedtls_ssl_init(&ssl);
      mbedtls_ssl_config_init(&tls);
    }
    ~Connection() {
      mbedtls_ssl_free(&ssl);
      mbedtls_ssl_config_free(&tls);
      mbedtls_net_free(&net);
      owner->incoming -= body.capacity();
    }
    void queue(unsigned t, std::shared_ptr<const Bytes> b,
               std::shared_ptr<int> budget = {}) {
      if (out.size() >= 32) {
        dead = true;
        return;
      }
      if (out.empty())
        lastWrite = Clock::now();
      Out x;
      size_t n = b ? b->size() : 0;
      x.h = {static_cast<unsigned char>(n >> 24),
             static_cast<unsigned char>(n >> 16),
             static_cast<unsigned char>(n >> 8), static_cast<unsigned char>(n),
             static_cast<unsigned char>(t)};
      x.body = std::move(b);
      x.budget = std::move(budget);
      out.push_back(std::move(x));
    }
  };
  TransportConfig config;
  TransportCallbacks callbacks;
  std::atomic<bool> running{false}, stopping{true};
  std::thread worker;
  std::mutex mutex;
  std::condition_variable cv;
  std::deque<std::function<void()>> jobs;
  std::shared_ptr<const Bytes> pending;
  // A separate lifetime token counts only transport references, not caller-held
  // buffers.
  std::weak_ptr<int> outbound;
  std::shared_ptr<int> pendingBudget;
  size_t incoming = 0;
  bool cryptoInitialized = false;
  mbedtls_entropy_context entropy{};
  mbedtls_ctr_drbg_context rng{};
  mbedtls_net_context listener;
  struct PeerState {
    std::unique_ptr<ResolverJob> resolver;
    std::vector<ResolvedAddress> addresses;
    size_t next = 0;
    Clock::time_point retry{};
    std::string lastStatus;
    unsigned attempts = 0;
    struct Pending {
      std::shared_ptr<const Bytes> body;
      std::shared_ptr<int> budget;
      Clock::time_point expires;
    };
    std::deque<Pending> outstanding;
  };
  std::vector<PeerState> peerStates;
  std::vector<std::unique_ptr<Connection>> connections;

  Impl() { (void)initialize_crypto_runtime();mbedtls_net_init(&listener); }
  ~Impl() {
    if (cryptoInitialized) {
      mbedtls_entropy_free(&entropy);
      mbedtls_ctr_drbg_free(&rng);
    }
  }
  bool liveHello(TransportPeer *peer) const {
    return std::any_of(connections.begin(), connections.end(),
                       [&](const auto &connection) {
                         return connection->peer == peer && connection->hello &&
                                !connection->dead;
                       });
  }
  PeerState &peer_state(TransportPeer *peer) {
    return peerStates[static_cast<size_t>(peer - config.peers.data())];
  }
  void expire_publications() {
    const auto now = Clock::now();
    for (auto &peer : peerStates)
      for (auto it = peer.outstanding.begin(); it != peer.outstanding.end();) {
        if (it->expires > now) { ++it; continue; }
        auto budget = it->budget;
        // ACKs carry no ID. Retire the old stream too, so its delayed ACK
        // cannot acknowledge a later publication on that connection.
        for (auto &connection : connections) {
          auto matches = [&](const Connection::Out &frame) { return frame.budget == budget; };
          if (std::any_of(connection->out.begin(), connection->out.end(), matches) ||
              std::any_of(connection->unacknowledged.begin(), connection->unacknowledged.end(), matches)) {
            connection->failureStatus = "Transfer timed out";
            connection->dead = true;
          }
        }
        it = peer.outstanding.erase(it);
      }
  }
  void status(std::string p, const char *s) {
    for (size_t index = 0; index < config.peers.size(); ++index)
      if (config.peers[index].id == p) {
        if (peerStates[index].lastStatus == s)
          return;
        peerStates[index].lastStatus = s;
        break;
      }
    if (callbacks.status)
      callbacks.status(std::move(p), s);
  }
  void deferRetry(PeerState &state) {
    const unsigned seconds = retrySeconds(state.attempts);
    if (state.attempts < 5)
      ++state.attempts;
    unsigned char random[2]{};
    unsigned jitter;
    if (mbedtls_ctr_drbg_random(&rng, random, sizeof(random)) == 0)
      jitter = ((unsigned(random[0]) << 8) | random[1]) % 251;
    else
      jitter = unsigned(Clock::now().time_since_epoch().count() % 251);
    state.retry = Clock::now() + std::chrono::seconds(seconds) +
                  std::chrono::milliseconds(jitter);
  }
  static int psk(void *ctx, mbedtls_ssl_context *ssl, const unsigned char *id,
                 size_t n) {
    auto *c = static_cast<Connection *>(ctx);
    for (auto &p : c->owner->config.peers)
      if (p.auth_id.size() == n && std::memcmp(p.auth_id.data(), id, n) == 0) {
        c->peer = &p;
        return mbedtls_ssl_set_hs_psk(ssl, p.key.data(), p.key.size());
      }
    return -1;
  }
  bool setup(Connection &c) {
    if (mbedtls_ssl_config_defaults(
            &c.tls, c.client ? MBEDTLS_SSL_IS_CLIENT : MBEDTLS_SSL_IS_SERVER,
            MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT))
      return false;
    mbedtls_ssl_conf_min_tls_version(&c.tls, MBEDTLS_SSL_VERSION_TLS1_3);
    mbedtls_ssl_conf_max_tls_version(&c.tls, MBEDTLS_SSL_VERSION_TLS1_3);
    mbedtls_ssl_conf_rng(&c.tls, mbedtls_ctr_drbg_random, &rng);
    mbedtls_ssl_conf_tls13_key_exchange_modes(
        &c.tls, MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_PSK_EPHEMERAL);
    if (c.client) {
      auto &p = *c.peer;
      if (mbedtls_ssl_conf_psk(
              &c.tls, p.key.data(), p.key.size(),
              reinterpret_cast<const unsigned char *>(p.auth_id.data()),
              p.auth_id.size()))
        return false;
    } else
      mbedtls_ssl_conf_psk_cb(&c.tls, psk, &c);
    if (mbedtls_ssl_setup(&c.ssl, &c.tls))
      return false;
    mbedtls_ssl_set_bio(&c.ssl, &c.net, mbedtls_net_send, mbedtls_net_recv,
                        nullptr);
    return mbedtls_net_set_nonblock(&c.net) == 0;
  }
  void process(Connection &c) {
    if (c.connecting) {
#ifndef _WIN32
      if (c.net.fd < 0 || c.net.fd >= FD_SETSIZE) {
        c.dead = true;
        return;
      }
#endif
      fd_set writefds;
      FD_ZERO(&writefds);
      FD_SET(c.net.fd, &writefds);
      timeval timeout{};
      int ready = select(c.net.fd + 1, nullptr, &writefds, nullptr, &timeout);
      if (ready <= 0)
        return;
      int error = 0;
#ifdef _WIN32
      int len = sizeof(error);
      getsockopt(c.net.fd, SOL_SOCKET, SO_ERROR,
                 reinterpret_cast<char *>(&error), &len);
#else
      socklen_t len = sizeof(error);
      getsockopt(c.net.fd, SOL_SOCKET, SO_ERROR, &error, &len);
#endif
      if (error) {
        c.dead = true;
        return;
      }
      c.connecting = false;
    }

    if (!c.handshake) {
      int r = mbedtls_ssl_handshake(&c.ssl);
      if (r) {
        if (!retry(r))
          c.dead = true;
        return;
      }
      c.handshake = true;
      auto b = std::make_shared<Bytes>(config.device_id.begin(),
                                       config.device_id.end());
      c.queue(1, b);
    }
    if (!c.out.empty()) {
      auto &x = c.out.front();
      const unsigned char *d;
      size_t n;
      if (x.offset < 5) {
        d = x.h.data() + x.offset;
        n = 5 - x.offset;
      } else {
        n = x.body ? x.body->size() - (x.offset - 5) : 0;
        d = n ? x.body->data() + x.offset - 5 : nullptr;
      }
      auto complete = [&] {
        if (x.h[4] == 2)
          c.unacknowledged.push_back(std::move(x));
        c.out.pop_front();
      };
      if (!n)
        complete();
      else {
        int r = mbedtls_ssl_write(&c.ssl, d, std::min(n, size_t(16384)));
        if (r > 0) {
          x.offset += r;
          c.lastWrite = Clock::now();
          // Register before reading an ACK that may arrive in this same pass.
          if (x.offset == 5 + (x.body ? x.body->size() : 0))
            complete();
        } else if (!retry(r))
          c.dead = true;
      }
    }
    for (int iterations = 0; iterations < 8 && !c.dead; ++iterations) {
      if (c.head < 5) {
        int r = mbedtls_ssl_read(&c.ssl, c.header.data() + c.head, 5 - c.head);
        if (r <= 0) {
          if (!retry(r))
            c.dead = true;
          return;
        }
        c.head += r;
        c.lastRead = Clock::now();
        if (c.head < 5)
          return;
        c.need = (size_t(c.header[0]) << 24) | (size_t(c.header[1]) << 16) |
                 (size_t(c.header[2]) << 8) | c.header[3];
        c.type = c.header[4];
        if ((c.type == 1 && (c.hello || c.need > 128 || !c.need)) ||
            (c.type == 2 &&
             (!c.hello || !c.peer->receive || !c.need || c.need > cap)) ||
            (c.type == 3 && c.need != 0) || c.type < 1 || c.type > 3) {
          c.dead = true;
          return;
        }
        if (c.need > cap - incoming) {
          c.dead = true;
          return;
        }
        if (c.type == 2) {
          if (!callbacks.receiving) {
            c.dead = true;
            return;
          }
          auto cp = callbacks.receiving(c.peer->id);
          if (!cp) {
            c.dead = true;
            return;
          }
          c.checkpoint = std::move(cp.value());
        }
        c.body.reserve(c.need);
        incoming += c.body.capacity();
      }
      if (c.body.size() < c.need) {
        unsigned char temp[16384];
        int r = mbedtls_ssl_read(
            &c.ssl, temp, std::min(sizeof(temp), c.need - c.body.size()));
        if (r <= 0) {
          if (!retry(r))
            c.dead = true;
          return;
        }
        c.body.insert(c.body.end(), temp, temp + r);
        c.lastRead = Clock::now();
        if (c.body.size() < c.need)
          continue;
      }
      if (c.type == 1) {
        std::string id(c.body.begin(), c.body.end());
        if (!c.peer || id == config.device_id ||
            (!c.peer->remote_device_id.empty() &&
             id != c.peer->remote_device_id) ||
            !callbacks.identity || !callbacks.identity(c.peer->id, id)) {
          c.dead = true;
          return;
        }
        for (auto &other : connections)
          if (other.get() != &c && other->peer == c.peer && other->hello &&
              !other->dead) {
            bool preferred = c.client == (config.device_id < id);
            if (preferred) {
              other->suppressFailure = true;
              other->dead = true;
            } else {
              c.suppressFailure = true;
              c.dead = true;
              return;
            }
          }
        c.hello = true;
        // The old socket may already have been erased before this HELLO.
        // Replay only retained live-peer publications within their original bound.
        for (const auto &frame : peer_state(c.peer).outstanding)
          if (frame.expires > Clock::now())
            c.queue(2, frame.body, frame.budget);
        for (size_t index = 0; index < config.peers.size(); ++index)
          if (&config.peers[index] == c.peer) {
            peerStates[index].attempts = 0;
            peerStates[index].retry = {};
            break;
          }
        status(c.peer->id, "Connected (TLS 1.3)");
      } else if (c.type == 2) {
        incoming -= c.body.capacity();
        Bytes received;
        received.swap(c.body);
        if (!callbacks.received ||
            !callbacks.received(c.peer->id, std::move(received),
                                std::move(c.checkpoint))) {
          c.dead = true;
          return;
        }
        c.queue(3, nullptr);
      } else if (c.type == 3 && !c.unacknowledged.empty()) {
        auto budget = c.unacknowledged.front().budget;
        auto &outstanding = peer_state(c.peer).outstanding;
        outstanding.erase(std::remove_if(outstanding.begin(), outstanding.end(),
            [&](const PeerState::Pending &frame) { return frame.budget == budget; }), outstanding.end());
        c.unacknowledged.pop_front();
      }
      incoming -= c.body.capacity();
      Bytes().swap(c.body);
      c.head = 0;
      c.need = 0;
    }
  }
  void run() {
    try {
      if (callbacks.started)
        callbacks.started();
      while (running) {
        std::deque<std::function<void()>> work;
        std::shared_ptr<const Bytes> event;
        std::shared_ptr<int> eventBudget;
        {
          std::lock_guard<std::mutex> l(mutex);
          work.swap(jobs);
          event.swap(pending);
          eventBudget.swap(pendingBudget);
        }
        for (auto &f : work) {
          if (!running)
            break;
          f();
        }
        if (!running)
          break;
        expire_publications();
        if (callbacks.tick)
          callbacks.tick();
        if (event) {
          const auto expires = Clock::now() + std::chrono::seconds(10);
          for (auto &c : connections)
            if (c->hello && !c->dead && c->peer->send) {
              auto &outstanding = peer_state(c->peer).outstanding;
              if (std::none_of(outstanding.begin(), outstanding.end(),
                  [&](const PeerState::Pending &frame) { return frame.budget == eventBudget; })) {
                outstanding.push_back({event, eventBudget, expires});
                c->queue(2, event, eventBudget);
              }
            }
        }
        unsigned pendingCount = 0;
        for (auto &c : connections)
          if (!c->hello)
            ++pendingCount;
        if (pendingCount < 4 && connections.size() < 20) {
          auto c = std::make_unique<Connection>(this);
          int r = mbedtls_net_accept(&listener, &c->net, nullptr, 0, nullptr);
          if (!r) {
            if (setup(*c))
              connections.push_back(std::move(c));
          }
        }
        for (size_t index = 0; index < config.peers.size() && running;
             ++index) {
          auto &p = config.peers[index];
          auto &state = peerStates[index];
          bool exists = false;
          unsigned pendingNow = 0;
          for (auto &connection : connections) {
            if (!connection->hello && !connection->dead)
              ++pendingNow;
            if (connection->peer == &p && !connection->dead)
              exists = true;
          }
          if (exists)
            continue;
          if (state.resolver) {
            auto answer = state.resolver->poll();
            if (answer) {
              if (*answer) {
                state.addresses = std::move(answer->value());
                if (state.addresses.size() > 16)
                  state.addresses.resize(16);
                state.next = 0;
                state.retry = {};
                if (state.addresses.empty())
                  deferRetry(state);
              } else {
                state.addresses.clear();
                state.next = 0;
                deferRetry(state);
                if (!liveHello(&p))
                  status(p.id, "Address unavailable");
              }
              state.resolver.reset();
            }
          }
          if (state.next >= state.addresses.size()) {
            if (!state.resolver && Clock::now() >= state.retry) {
              if (state.lastStatus.empty())
                status(p.id, "Connecting");
              state.retry = {};
              state.resolver = std::make_unique<ResolverJob>(p.host, p.port);
            }
            continue;
          }
          if (pendingNow >= 4 || connections.size() >= 20)
            continue;
          auto &address = state.addresses[state.next++];
          auto connection = std::make_unique<Connection>(this);
          connection->client = true;
          connection->connecting = true;
          connection->peer = &p;
          auto socketHandle =
              socket(address.address.ss_family, SOCK_STREAM, IPPROTO_TCP);
          if (socketHandle == decltype(socketHandle)(-1)) {
            if (!liveHello(&p))
              status(p.id, "TLS connection failed (check key/address)");
            if (state.next >= state.addresses.size())
              deferRetry(state);
            continue;
          }
          connection->net.fd = static_cast<int>(socketHandle);
          mbedtls_net_set_nonblock(&connection->net);
          ::connect(socketHandle,
                    reinterpret_cast<const sockaddr *>(&address.address),
                    address.length);
          if (setup(*connection))
            connections.push_back(std::move(connection));
          else {
            if (!liveHello(&p))
              status(p.id, "TLS connection failed (check key/address)");
            if (state.next >= state.addresses.size())
              deferRetry(state);
          }
        }
        for (auto &c : connections) {
          if (!c->dead)
            process(*c);
          if (!c->hello && Clock::now() - c->born > std::chrono::seconds(5))
            c->dead = true;
          if (c->hello && ((c->head && Clock::now() - c->lastRead >
                                           std::chrono::seconds(10)) ||
                           ((!c->out.empty() || !c->unacknowledged.empty()) && Clock::now() - c->lastWrite >
                                                   std::chrono::seconds(10)))) {
            c->failureStatus = "Transfer timed out";
            c->dead = true;
          }
        }
        for (auto &connection : connections)
          if (connection->dead && connection->peer &&
              !connection->suppressFailure && !liveHello(connection->peer)) {
            for (size_t index = 0; index < config.peers.size(); ++index)
              if (&config.peers[index] == connection->peer) {
                auto &state = peerStates[index];
                if (!state.resolver && state.next >= state.addresses.size() &&
                    state.retry == Clock::time_point{})
                  deferRetry(state);
                break;
              }
            status(connection->peer->id,
                   connection->failureStatus
                       ? connection->failureStatus
                       : (connection->hello
                              ? "Disconnected"
                              : "TLS connection failed (check key/address)"));
          }
        connections.erase(std::remove_if(connections.begin(), connections.end(),
                                         [](auto &c) { return c->dead; }),
                          connections.end());
        std::unique_lock<std::mutex> l(mutex);
        cv.wait_for(l, std::chrono::milliseconds(10),
                    [&] { return !running || pending || !jobs.empty(); });
      }
    } catch (...) {
      // Callback/allocation failures close the worker; no frame is ACKed.
      running = false;
    }
    peerStates.clear();
    connections.clear();
    mbedtls_net_free(&listener);
    {
      std::lock_guard<std::mutex> l(mutex);
      pending.reset();
      pendingBudget.reset();
      outbound.reset();
      jobs.clear();
    }
    for (auto &p : config.peers)
      mbedtls_platform_zeroize(p.key.data(), p.key.size());
    config.peers.clear();
    mbedtls_entropy_free(&entropy);
    mbedtls_ctr_drbg_free(&rng);
    cryptoInitialized = false;
    try {
      if (callbacks.stopped)
        callbacks.stopped();
    } catch (...) {
    }
  }
};
Transport::Transport() : impl_(std::make_unique<Impl>()) {}
Transport::~Transport() { stop(); }
Result<void> Transport::start(TransportConfig c, TransportCallbacks cb) {
  stop();
  if (c.peers.empty())
    return {};
  if (c.peers.size() > 16 || c.device_id.empty() || c.device_id.size() > 128 ||
      c.device_id.find('\0') != std::string::npos || !c.port ||
      c.bind_address.empty() || c.bind_address.size() > 253 ||
      c.bind_address.find('\0') != std::string::npos)
    return fail("Invalid transport configuration");
  if (!numericAddress(c.bind_address))
    return fail("Bind address must be a numeric IPv4 or IPv6 address");
  for (auto &p : c.peers)
    if (p.id.empty() || p.id.size() > 128 || p.host.empty() ||
        p.host.size() > 253 || p.host.find('\0') != std::string::npos ||
        !p.port || p.remote_device_id.size() > 128 ||
        p.auth_id.find('\0') != std::string::npos || p.key.size() != 32 ||
        p.auth_id.empty() || p.auth_id.size() > 128 ||
        std::all_of(p.key.begin(), p.key.end(),
                    [](unsigned char b) { return b == 0; }) ||
        (!p.send && !p.receive))
      return fail("Invalid peer credentials");
  for (size_t i = 0; i < c.peers.size(); ++i)
    for (size_t j = 0; j < i; ++j)
      if (c.peers[i].auth_id == c.peers[j].auth_id ||
          c.peers[i].id == c.peers[j].id || c.peers[i].key == c.peers[j].key)
        return fail("Duplicate peer credentials");
  if(!initialize_crypto_runtime())return fail("TLS crypto initialization failed");
  if (impl_->cryptoInitialized) {
    mbedtls_entropy_free(&impl_->entropy);
    mbedtls_ctr_drbg_free(&impl_->rng);
  }
  mbedtls_entropy_init(&impl_->entropy);
  mbedtls_ctr_drbg_init(&impl_->rng);
  impl_->cryptoInitialized = true;
  auto abortStart = [&](const char *message) -> Result<void> {
    mbedtls_net_free(&impl_->listener);
    mbedtls_entropy_free(&impl_->entropy);
    mbedtls_ctr_drbg_free(&impl_->rng);
    impl_->cryptoInitialized = false;
    for (auto &peer : c.peers)
      mbedtls_platform_zeroize(peer.key.data(), peer.key.size());
    return fail(message);
  };
  static const unsigned char label[] = "cliptiq-lan";
  if (mbedtls_ctr_drbg_seed(&impl_->rng, mbedtls_entropy_func, &impl_->entropy,
                            label, sizeof(label)))
    return abortStart("Secure random initialization failed");
  if (mbedtls_net_bind(&impl_->listener, c.bind_address.c_str(),
                       std::to_string(c.port).c_str(), MBEDTLS_NET_PROTO_TCP))
    return abortStart("LAN listener unavailable");
  mbedtls_net_set_nonblock(&impl_->listener);
  impl_->config = std::move(c);
  impl_->callbacks = std::move(cb);
  impl_->peerStates.resize(impl_->config.peers.size());
  impl_->stopping = false;
  impl_->running = true;
  impl_->worker = std::thread([this] { impl_->run(); });
  return {};
}
void Transport::stop() {
  impl_->stopping = true;
  impl_->running = false;
  impl_->cv.notify_all();
  if (impl_->worker.joinable())
    impl_->worker.join();
}
void Transport::publish(std::shared_ptr<const Bytes> b) {
  if (!b || b->empty() || b->size() > cap)
    return;
  std::lock_guard<std::mutex> l(impl_->mutex);
  if (impl_->running && impl_->outbound.expired()) {
    impl_->pendingBudget = std::make_shared<int>(0);
    impl_->outbound = impl_->pendingBudget;
    impl_->pending = std::move(b);
    impl_->cv.notify_all();
  }
}
Result<void> Transport::post(std::function<void()> f) {
  if (!f)
    return Error{ErrorCode::InvalidArgument, "Empty LAN job"};
  std::lock_guard<std::mutex> l(impl_->mutex);
  if (!impl_->running)
    return Error{ErrorCode::Busy, "LAN worker stopped"};
  if (impl_->jobs.size() >= 64)
    return Error{ErrorCode::Busy, "LAN worker queue full"};
  impl_->jobs.push_back(std::move(f));
  impl_->cv.notify_all();
  return {};
}
bool Transport::active() const { return impl_->running; }
bool Transport::ready_to_publish() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->running && impl_->outbound.expired() && !impl_->pending;
}
} // namespace cliptiq::lan
