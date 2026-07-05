# AOA M1 — Host ByteChannel Refactor — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Make `TransportServer` transport-agnostic by running its framing/protocol over a `ByteChannel` abstraction, with `SocketChannel` preserving today's exact TCP+TLS behavior — so M2 can drop in an `AoaChannel` unchanged.

**Architecture:** Extract `TransportServer`'s inline byte ops (`conn_recv`/`conn_send_all`/`wait_readable`/`close_all`, incl. the TLS `SSL_pending` fast-path) into a `ByteChannel` interface. `SocketChannel` owns the accepted `{fd, ssl}`. `TransportServer` keeps `listen`/`accept_client`/the `MessageParser` and the protocol methods, delegating bytes to a held `ByteChannel` adopted via a new `adopt_channel(...)` (used by `accept_client` now, and by M2's AOA path later).

**Tech Stack:** C++17, OpenSSL, GoogleTest/ctest.

## Global Constraints

- **Behavior-identical:** the existing host suite — especially `test_transport_server.cpp` (`HandshakeThenVideo`, `RelistenClosesOldSocketAndSucceeds`) — MUST still pass unchanged. This milestone adds no protocol behavior.
- `ByteChannel` interface EXACTLY: `ssize_t recv(void*, size_t)`, `bool send_all(const unsigned char*, size_t)`, `bool wait_readable(int timeout_ms)`, `bool connected() const`, `void close()`.
- `SocketChannel::wait_readable` MUST return true when TLS holds already-decrypted buffered bytes (`SSL_pending(ssl) > 0`) — the fast-path currently inline in `poll_control`.
- `TransportServer` gains `void adopt_channel(std::unique_ptr<ByteChannel> ch, std::string peer = "")`; `accept_client` builds a `SocketChannel` and adopts it; `connected()` becomes `channel_ && channel_->connected()`.
- `listen`, `enable_tls`, the `SSL_CTX* ctx_`, and the `MessageParser parser_` stay in `TransportServer`. `adopt_channel` does NOT reset `parser_` (matches today's reconnect behavior).
- Host-only. The Android channel refactor is M4.
- Build/test in the `droppix-dev` distrobox at `/home/Spinjitsudoomyt/droppix-build`. Commits: `git -c user.name="Claude" -c user.email="noreply@anthropic.com"`.

---

## File Structure

- **Create** `host/src/byte_channel.h` — the `ByteChannel` interface (header-only).
- **Create** `host/src/socket_channel.h`, `host/src/socket_channel.cpp` — `SocketChannel` (TCP+TLS byte ops).
- **Create** `host/tests/test_socket_channel.cpp` — socketpair unit test of `SocketChannel`.
- **Modify** `host/src/transport_server.h`, `host/src/transport_server.cpp` — run over `channel_`; add `adopt_channel`.
- **Modify** `host/tests/test_transport_server.cpp` — add a `FakeChannel` framing test.
- **Modify** `host/CMakeLists.txt` — add `src/socket_channel.cpp` + the two test files.

---

### Task 1: `ByteChannel` interface + `SocketChannel`

**Files:**
- Create: `host/src/byte_channel.h`, `host/src/socket_channel.h`, `host/src/socket_channel.cpp`
- Create: `host/tests/test_socket_channel.cpp`
- Modify: `host/CMakeLists.txt`

**Interfaces:**
- Produces: `struct droppix::ByteChannel` (the 5 methods above); `class droppix::SocketChannel : public ByteChannel` constructed as `SocketChannel(int fd, SSL* ssl)` (ssl null = plaintext).
- Consumed by Task 2 (`TransportServer`) and Task 3 (test).

- [ ] **Step 1: Write the failing test**

Create `host/tests/test_socket_channel.cpp`:

```cpp
#include <gtest/gtest.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include "socket_channel.h"

using namespace droppix;

TEST(SocketChannel, SendRecvOverSocketpair) {
  int sv[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
  SocketChannel a(sv[0], nullptr);   // owns sv[0]

  const unsigned char msg[] = {1, 2, 3, 4, 5};
  ASSERT_TRUE(a.send_all(msg, sizeof(msg)));
  unsigned char got[5] = {0};
  ASSERT_EQ(::read(sv[1], got, 5), 5);
  EXPECT_EQ(0, memcmp(msg, got, 5));

  const unsigned char back[] = {9, 8, 7};
  ASSERT_EQ(::write(sv[1], back, 3), 3);
  EXPECT_TRUE(a.wait_readable(500));
  unsigned char rb[3] = {0};
  EXPECT_EQ(a.recv(rb, 3), 3);
  EXPECT_EQ(0, memcmp(back, rb, 3));

  ::close(sv[1]);
}

TEST(SocketChannel, WaitReadableTimesOutThenCloseDisconnects) {
  int sv[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
  SocketChannel a(sv[0], nullptr);
  EXPECT_FALSE(a.wait_readable(50));   // nothing written
  EXPECT_TRUE(a.connected());
  a.close();
  EXPECT_FALSE(a.connected());
  ::close(sv[1]);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `distrobox enter droppix-dev -- bash -lc 'cd /home/Spinjitsudoomyt/droppix-build && cmake . >/dev/null && cmake --build . --target droppix_tests -j$(nproc)'`
Expected: FAIL — `socket_channel.h` not found.

- [ ] **Step 3: Create the interface**

Create `host/src/byte_channel.h`:

```cpp
#pragma once
#include <cstddef>
#include <sys/types.h>  // ssize_t

namespace droppix {

// A connected, bidirectional byte stream that TransportServer runs its framing/protocol
// over. Implementations: SocketChannel (TCP + optional TLS); AoaChannel (USB bulk, M2).
struct ByteChannel {
  virtual ~ByteChannel() = default;
  // Read up to n bytes; returns >0 bytes read, or <=0 on close/error.
  virtual ssize_t recv(void* buf, size_t n) = 0;
  // Write all n bytes; returns true iff every byte was written.
  virtual bool send_all(const unsigned char* p, size_t n) = 0;
  // True iff data is readable within timeout_ms (0 = poll now). MUST also return true
  // when the implementation holds already-buffered readable bytes (e.g. TLS decrypted).
  virtual bool wait_readable(int timeout_ms) = 0;
  virtual bool connected() const = 0;
  virtual void close() = 0;
};

}  // namespace droppix
```

- [ ] **Step 4: Create `SocketChannel`**

Create `host/src/socket_channel.h`:

```cpp
#pragma once
#include <openssl/ssl.h>
#include "byte_channel.h"

namespace droppix {

// ByteChannel over an accepted TCP socket, optionally wrapped in TLS. Owns the fd and
// (when TLS) the SSL*; close() tears both down. Byte-for-byte the behavior TransportServer
// had inline before the ByteChannel refactor.
class SocketChannel : public ByteChannel {
 public:
  SocketChannel(int fd, SSL* ssl) : fd_(fd), ssl_(ssl) {}
  ~SocketChannel() override { close(); }
  ssize_t recv(void* buf, size_t n) override;
  bool send_all(const unsigned char* p, size_t n) override;
  bool wait_readable(int timeout_ms) override;
  bool connected() const override { return fd_ >= 0; }
  void close() override;

 private:
  int fd_ = -1;
  SSL* ssl_ = nullptr;  // null => plaintext
};

}  // namespace droppix
```

Create `host/src/socket_channel.cpp`:

```cpp
#include "socket_channel.h"
#include <poll.h>
#include <unistd.h>
#include <sys/socket.h>

namespace droppix {

ssize_t SocketChannel::recv(void* buf, size_t n) {
  return ssl_ ? static_cast<ssize_t>(SSL_read(ssl_, buf, static_cast<int>(n)))
              : ::recv(fd_, buf, n, 0);
}

bool SocketChannel::send_all(const unsigned char* p, size_t n) {
  while (n) {
    ssize_t w = ssl_ ? static_cast<ssize_t>(SSL_write(ssl_, p, static_cast<int>(n)))
                     : ::send(fd_, p, n, MSG_NOSIGNAL);
    if (w <= 0) return false;
    p += w;
    n -= static_cast<size_t>(w);
  }
  return true;
}

bool SocketChannel::wait_readable(int timeout_ms) {
  if (fd_ < 0) return false;
  if (ssl_ && SSL_pending(ssl_) > 0) return true;  // TLS already holds decrypted bytes
  pollfd pfd{fd_, POLLIN, 0};
  return ::poll(&pfd, 1, timeout_ms) > 0 && (pfd.revents & POLLIN);
}

void SocketChannel::close() {
  if (ssl_) { SSL_shutdown(ssl_); SSL_free(ssl_); ssl_ = nullptr; }
  if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
}

}  // namespace droppix
```

- [ ] **Step 5: Wire CMake**

In `host/CMakeLists.txt`: add `src/socket_channel.cpp` to the `droppix_core` sources (near `src/transport_server.cpp`); add `tests/test_socket_channel.cpp` to the `droppix_tests` executable (near `tests/test_transport_server.cpp`).

- [ ] **Step 6: Run to verify it passes**

Run: `distrobox enter droppix-dev -- bash -lc 'cd /home/Spinjitsudoomyt/droppix-build && cmake . >/dev/null && cmake --build . --target droppix_tests -j$(nproc) && ctest --output-on-failure -R SocketChannel'`
Expected: PASS — 2 `SocketChannel.*` tests.

- [ ] **Step 7: Commit**

```bash
git add host/src/byte_channel.h host/src/socket_channel.h host/src/socket_channel.cpp host/tests/test_socket_channel.cpp host/CMakeLists.txt
git -c user.name="Claude" -c user.email="noreply@anthropic.com" commit -m "feat(aoa-m1): ByteChannel interface + SocketChannel"
```

---

### Task 2: Refactor `TransportServer` to run over a `ByteChannel`

**Files:**
- Modify: `host/src/transport_server.h`
- Modify: `host/src/transport_server.cpp`

**Interfaces:**
- Consumes: `ByteChannel`, `SocketChannel` (Task 1).
- Produces: `void TransportServer::adopt_channel(std::unique_ptr<ByteChannel> ch, std::string peer = "")`; all protocol methods route bytes through the held `channel_`.

- [ ] **Step 1: Update the header**

In `host/src/transport_server.h`:
- Add includes near the top: `#include <memory>` and `#include "byte_channel.h"`.
- In the public section, after `bool accept_client(int timeout_ms);`, add:

```cpp
  // Adopt an already-connected byte stream (e.g. an AOA USB channel) instead of accepting
  // a TCP client. accept_client() uses this internally with a SocketChannel.
  void adopt_channel(std::unique_ptr<ByteChannel> ch, std::string peer = "");
```

- Replace `bool connected() const { return client_fd_ >= 0; }` with:

```cpp
  bool connected() const { return channel_ && channel_->connected(); }
```

- In the private section, DELETE these members and decls: `bool wait_readable(int fd, int timeout_ms);`, `ssize_t conn_recv(void* buf, size_t n);`, `bool conn_send_all(const unsigned char* p, size_t n);`, `int client_fd_ = -1;`, `SSL* ssl_ = nullptr;`. Keep `bool send_all(const std::vector<unsigned char>& bytes);`, `listen_fd_`, `port_`, `peer_ip_`, `parser_`, the two handlers, `tls_`, `cert_`, `key_`, `ctx_`. Add:

```cpp
  std::unique_ptr<ByteChannel> channel_;   // the live client connection (socket or AOA)
```

- [ ] **Step 2: Update the .cpp — connection lifecycle**

In `host/src/transport_server.cpp`:
- Keep `listen(...)` and `enable_tls(...)` unchanged.
- DELETE the standalone `TransportServer::wait_readable(int fd, int)`, `TransportServer::conn_recv(...)`, and `TransportServer::conn_send_all(...)` definitions (their logic now lives in `SocketChannel`). Add `#include "socket_channel.h"` and `#include <poll.h>` at the top.
- Add `adopt_channel`:

```cpp
void TransportServer::adopt_channel(std::unique_ptr<ByteChannel> ch, std::string peer) {
  channel_ = std::move(ch);
  peer_ip_ = std::move(peer);
}
```

- Replace `accept_client` with:

```cpp
bool TransportServer::accept_client(int timeout_ms) {
  close_all();  // drop any prior client so its fd can't leak on a new accept
  if (listen_fd_ < 0) return false;
  pollfd pfd{listen_fd_, POLLIN, 0};
  if (::poll(&pfd, 1, timeout_ms) <= 0 || !(pfd.revents & POLLIN)) return false;

  sockaddr_in cli{};
  socklen_t cli_len = sizeof(cli);
  int fd = ::accept(listen_fd_, (sockaddr*)&cli, &cli_len);
  if (fd < 0) return false;
  char buf[INET_ADDRSTRLEN] = {0};
  std::string peer = inet_ntop(AF_INET, &cli.sin_addr, buf, sizeof(buf)) ? buf : "";
  int yes = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));

  SSL* ssl = nullptr;
  if (tls_) {
    ssl = SSL_new(ctx_);
    if (!ssl) {
      std::fprintf(stderr, "tls: SSL_new failed\n"); ERR_print_errors_fp(stderr);
      ::close(fd); return false;
    }
    SSL_set_fd(ssl, fd);
    if (SSL_accept(ssl) <= 0) {
      std::fprintf(stderr, "tls: SSL_accept failed\n"); ERR_print_errors_fp(stderr);
      SSL_free(ssl); ::close(fd); return false;
    }
  }
  adopt_channel(std::make_unique<SocketChannel>(fd, ssl), std::move(peer));
  return true;
}
```

- Replace `close_all` with:

```cpp
void TransportServer::close_all() {
  if (channel_) { channel_->close(); channel_.reset(); }
}
```

- In the destructor, replace `close_all();` (unchanged) — it now resets the channel; keep the `listen_fd_` close and `SSL_CTX_free(ctx_)`.

- [ ] **Step 3: Update the .cpp — protocol methods route through `channel_`**

- `read_hello`: replace the guard and the two byte ops:

```cpp
bool TransportServer::read_hello(uint32_t& version, uint32_t& w, uint32_t& h, uint32_t& density,
                                 std::string& name, std::string& id, int timeout_ms) {
  if (!channel_) return false;
  unsigned char buf[1024];
  ParsedMessage m;
  for (;;) {
    if (parser_.next(m)) {
      if (m.type != MsgType::Hello) continue;
      return decode_hello(m.body, version, w, h, density, name, id);
    }
    if (!channel_->wait_readable(timeout_ms)) return false;
    ssize_t n = channel_->recv(buf, sizeof(buf));
    if (n <= 0) { close_all(); return false; }
    parser_.feed(buf, static_cast<size_t>(n));
  }
}
```

- `send_all`:

```cpp
bool TransportServer::send_all(const std::vector<unsigned char>& bytes) {
  if (!channel_ || !channel_->connected()) return false;
  if (!channel_->send_all(bytes.data(), bytes.size())) { close_all(); return false; }
  return true;
}
```

- `poll_control` (the `SSL_pending` fast-path now lives in `SocketChannel::wait_readable`):

```cpp
void TransportServer::poll_control() {
  if (!channel_ || !channel_->wait_readable(0)) return;
  unsigned char buf[1024];
  ssize_t n = channel_->recv(buf, sizeof(buf));
  if (n <= 0) { close_all(); return; }
  parser_.feed(buf, static_cast<size_t>(n));
  ParsedMessage m;
  while (parser_.next(m)) {
    if (m.type == MsgType::Ping) {
      send_all(encode_message(MsgType::Pong, m.body));
    } else if (m.type == MsgType::Touch && touch_handler_) {
      std::vector<TouchContact> contacts;
      if (decode_touch(m.body, contacts)) touch_handler_(contacts);
    } else if (m.type == MsgType::Input && touch_handler_) {
      uint8_t a; uint16_t x, y, p;
      if (decode_input(m.body, a, x, y, p)) {
        if (a == 2) touch_handler_({});
        else touch_handler_({TouchContact{0, x, y, p}});
      }
    } else if (m.type == MsgType::Orientation && orientation_handler_) {
      uint8_t code;
      if (decode_orientation(m.body, code)) orientation_handler_(code);
    }
  }
}
```

(`send_config`/`send_video`/`send_audio`/`send_overlay` are unchanged — they already call `send_all`.)

- [ ] **Step 4: Build + run the existing transport test (must still pass)**

Run: `distrobox enter droppix-dev -- bash -lc 'cd /home/Spinjitsudoomyt/droppix-build && cmake --build . --target droppix_tests -j$(nproc) && ctest --output-on-failure -R "TransportServer|SocketChannel"'`
Expected: PASS — `TransportServer.HandshakeThenVideo`, `TransportServer.RelistenClosesOldSocketAndSucceeds`, and the `SocketChannel.*` tests, all green (behavior-identical).

- [ ] **Step 5: Commit**

```bash
git add host/src/transport_server.h host/src/transport_server.cpp
git -c user.name="Claude" -c user.email="noreply@anthropic.com" commit -m "feat(aoa-m1): run TransportServer over a ByteChannel (SocketChannel); add adopt_channel"
```

---

### Task 3: `FakeChannel` framing test (framing without a socket)

**Files:**
- Modify: `host/tests/test_transport_server.cpp`

**Interfaces:**
- Consumes: `ByteChannel`, `TransportServer::adopt_channel` (Tasks 1–2).

- [ ] **Step 1: Write the failing test**

At the top of `host/tests/test_transport_server.cpp`, add includes: `#include <algorithm>`, `#include <cstring>`, `#include "byte_channel.h"`. Then add:

```cpp
namespace {
// In-memory ByteChannel: preload bytes recv() hands out; capture bytes send_all() writes.
class FakeChannel : public droppix::ByteChannel {
 public:
  std::vector<unsigned char> to_recv;   // bytes recv() returns, FIFO
  std::vector<unsigned char> sent;      // bytes send_all() captured
  size_t rpos = 0;
  ssize_t recv(void* buf, size_t n) override {
    size_t avail = to_recv.size() - rpos;
    if (avail == 0) return 0;
    size_t k = std::min(n, avail);
    std::memcpy(buf, to_recv.data() + rpos, k);
    rpos += k;
    return static_cast<ssize_t>(k);
  }
  bool send_all(const unsigned char* p, size_t n) override {
    sent.insert(sent.end(), p, p + n);
    return true;
  }
  bool wait_readable(int) override { return rpos < to_recv.size(); }
  bool connected() const override { return true; }
  void close() override {}
};
}  // namespace

TEST(TransportServer, FramingOverFakeChannel) {
  TransportServer s;
  auto fake = std::make_unique<FakeChannel>();
  FakeChannel* fp = fake.get();
  // preload a HELLO for read_hello to parse
  fp->to_recv = encode_message(
      MsgType::Hello, encode_hello(kProtocolVersion, 800, 600, 200, "Fake", "fid"));
  s.adopt_channel(std::move(fake), "test");

  uint32_t ver, w, h, d; std::string name, id;
  ASSERT_TRUE(s.read_hello(ver, w, h, d, name, id, 0));
  EXPECT_EQ(ver, kProtocolVersion);
  EXPECT_EQ(w, 800u); EXPECT_EQ(h, 600u);
  EXPECT_EQ(name, "Fake"); EXPECT_EQ(id, "fid");

  // send_video must push framed bytes into the fake's sent buffer, decodable back.
  ASSERT_TRUE(s.send_video(42, true, {0x65}));
  MessageParser p; ParsedMessage m;
  p.feed(fp->sent.data(), fp->sent.size());
  ASSERT_TRUE(p.next(m));
  EXPECT_EQ(m.type, MsgType::Video);
}
```

- [ ] **Step 2: Build + run**

Run: `distrobox enter droppix-dev -- bash -lc 'cd /home/Spinjitsudoomyt/droppix-build && cmake --build . --target droppix_tests -j$(nproc) && ctest --output-on-failure -R TransportServer'`
Expected: PASS — all `TransportServer.*` including the new `FramingOverFakeChannel`.

- [ ] **Step 3: Run the FULL host suite (no regressions)**

Run: `distrobox enter droppix-dev -- bash -lc 'cd /home/Spinjitsudoomyt/droppix-build && cmake --build . -j$(nproc) && ctest --output-on-failure'`
Expected: builds clean; 100% pass.

- [ ] **Step 4: Commit**

```bash
git add host/tests/test_transport_server.cpp
git -c user.name="Claude" -c user.email="noreply@anthropic.com" commit -m "test(aoa-m1): framing over a FakeChannel (adopt_channel, no socket)"
```

---

## Notes for the implementer

- This milestone is a pure refactor: **no protocol/behavior change**. The strongest signal of success is that `test_transport_server.cpp` (unchanged) still passes — `SocketChannel` must reproduce the old inline TCP+TLS behavior exactly.
- `adopt_channel` is not just a test seam: M2's streamer will call `tx.adopt_channel(make_aoa_channel(...))` instead of `accept_client()` to serve a tablet over USB.
- Do not reset `parser_` in `adopt_channel` — that preserves today's cross-reconnect behavior; changing it is out of scope for M1.
