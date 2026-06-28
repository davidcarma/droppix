# TLS encryption + PIN pairing — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Encrypt the droppix stream with TLS and authenticate the PC to the tablet via a 6-digit pairing code derived from the PC's self-signed cert (cert pinning); keep approve-on-host; auto-trust localhost.

**Architecture:** The streaming TCP is wrapped in TLS — host = OpenSSL server (opt-in `--tls --cert --key`), tablet = `SSLSocket` + a pinning TrustManager. The pairing code = `SHA-256(cert DER)` → first 4 bytes BE u32 `% 1000000`, zero-padded to 6, computed identically both ends. The tablet verifies the PC via the PIN then pins the cert; the PC verifies the tablet via Part-1 approve-on-host. `127.0.0.1` is auto-trusted (USB).

**Tech Stack:** C++17 + OpenSSL (host), Qt6 (GUI), Kotlin/Android `SSLSocket`+`MessageDigest`, `openssl` CLI (cert gen).

**Spec:** `docs/superpowers/specs/2026-06-28-tls-pin-pairing-design.md`. **Builds on:** WiFi Part 2 (merged `e9dd134`).

## Global Constraints

- Host C++ builds/tests in the `droppix-dev` distrobox; build dir OFF the CIFS mount at
  `/home/Spinjitsudoomyt/droppix-build`. New source/test files need a `cmake -S host -B <build>` reconfigure.
- Android builds in the `droppix-android` distrobox: `ANDROID_SDK_ROOT=/home/Spinjitsudoomyt/android-sdk`,
  `bash gradlew --no-daemon` from `android/`; both `:app:assembleDebug` and `:app:testDebugUnitTest` pass.
- minSdk 21 (Nexus 10 / API 22). No Jetpack Compose — Material Views only. `SSLSocket` + `javax.net.ssl` (API 1+).
- Pairing code algorithm (BOTH ends MUST agree): `sha256 = SHA-256(der)`; `u32 = (sha256[0]<<24)|(sha256[1]<<16)|(sha256[2]<<8)|sha256[3]`; `code = u32 % 1000000`; format as 6 digits, zero-padded.
- OpenSSL is available on Bazzite; add `find_package(OpenSSL REQUIRED)` + link `OpenSSL::SSL OpenSSL::Crypto`.
- `git merge` on this mount intermittently errors `fatal: stash failed`; run each merge standalone with `--no-autostash`.
- Commit author `Claude <noreply@anthropic.com>`; body ends with `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`. Commit on the work branch; do not merge in tasks.

## File Structure

- `host/src/pairing_code.h` (new, pure) — `derive_pairing_code(der)`; `host/tests/test_pairing_code.cpp`.
- `host/src/transport_server.{h,cpp}` — optional TLS via OpenSSL.
- `host/src/stream_main.cpp` — `--tls --cert --key` flags.
- `host/CMakeLists.txt` — OpenSSL.
- `host/gui/cert_manager.{h,cpp}` (new) — ensure cert exists + read its DER + the pairing code.
- `host/gui/main_window.{h,cpp}` — show the pairing code; `host/gui/args_builder.cpp` — pass `--tls/--cert/--key`.
- `android/.../net/PairingCode.kt` (new) + test.
- `android/.../net/TlsTrust.kt` (new) — pin store + `SSLSocketFactory`/`X509TrustManager`.
- `android/.../net/TransportClient.kt` — use `SSLSocket`, verify pin.
- `android/.../ui/ConnectActivity.kt` — pairing probe + PIN dialog in `connectTo`.

---

### Task 1: Pairing-code derivation (host + Kotlin, byte-identical) + OpenSSL in CMake

**Files:**
- Create: `host/src/pairing_code.h`, `host/tests/test_pairing_code.cpp`
- Modify: `host/CMakeLists.txt` (OpenSSL; add the test)
- Create: `android/.../net/PairingCode.kt`, `android/.../test/.../net/PairingCodeTest.kt`

**Interfaces:**
- Host: `std::string derive_pairing_code(const std::vector<unsigned char>& der);` (6-char digit string).
- Kotlin: `PairingCode.derive(der: ByteArray): String`.

- [ ] **Step 1: CMake OpenSSL.** In `host/CMakeLists.txt` add `find_package(OpenSSL REQUIRED)` and
`target_link_libraries(droppix_core PUBLIC ... OpenSSL::SSL OpenSSL::Crypto)`. Add `tests/test_pairing_code.cpp` to `droppix_tests`.

- [ ] **Step 2: Host impl** (`pairing_code.h`):

```cpp
#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <cstdio>
#include <openssl/sha.h>
namespace droppix {
inline std::string derive_pairing_code(const std::vector<unsigned char>& der) {
  unsigned char h[SHA256_DIGEST_LENGTH];
  SHA256(der.data(), der.size(), h);
  uint32_t u = (uint32_t(h[0])<<24)|(uint32_t(h[1])<<16)|(uint32_t(h[2])<<8)|uint32_t(h[3]);
  char buf[8]; std::snprintf(buf, sizeof buf, "%06u", u % 1000000u);
  return std::string(buf);
}
}  // namespace droppix
```

- [ ] **Step 3: Host test** (`test_pairing_code.cpp`). Use a FIXED der and lock the value via TDD:

```cpp
#include "pairing_code.h"
using namespace droppix;
TEST(PairingCode, DeterministicSixDigits) {
  std::vector<unsigned char> der = {0xDE,0xAD,0xBE,0xEF,0x01,0x02,0x03,0x04};
  std::string code = derive_pairing_code(der);
  EXPECT_EQ(code.size(), 6u);
  for (char c : code) EXPECT_TRUE(c >= '0' && c <= '9');
  EXPECT_EQ(code, derive_pairing_code(der));            // deterministic
  // Lock the exact value so the Kotlin side must match it byte-for-byte:
  EXPECT_EQ(code, EXPECTED);   // EXPECTED = the 6-digit string this prints on first run
}
```
Reconfigure + build + run; read the actual `code` value (e.g. from a temporary `std::printf`), then
replace `EXPECTED` with that literal (e.g. `"482913"`) in this test. This SAME literal goes in the Kotlin test (Step 5), which is how byte-identity is enforced.

- [ ] **Step 4: Kotlin impl** (`PairingCode.kt`):

```kotlin
package com.droppix.app.net
import java.security.MessageDigest
object PairingCode {
    fun derive(der: ByteArray): String {
        val h = MessageDigest.getInstance("SHA-256").digest(der)
        val u = ((h[0].toLong() and 0xFF) shl 24) or ((h[1].toLong() and 0xFF) shl 16) or
                ((h[2].toLong() and 0xFF) shl 8) or (h[3].toLong() and 0xFF)
        return "%06d".format(u % 1000000L)
    }
}
```

- [ ] **Step 5: Kotlin test** (`PairingCodeTest.kt`): same der `byteArrayOf(0xDE.toByte(),0xAD.toByte(),0xBE.toByte(),0xEF.toByte(),1,2,3,4)`; `assertEquals(EXPECTED, PairingCode.derive(der))` using the SAME literal locked in Step 3. (If Kotlin differs, this fails — proving a byte mismatch.)

- [ ] **Step 6: Build/run both** — host `ctest -R PairingCode`; Android `:app:testDebugUnitTest`. Both green with the identical literal.

- [ ] **Step 7: Commit** — `feat(security): pairing-code derivation (host+android) + OpenSSL`.

---

### Task 2: Host TLS server (optional, OpenSSL)

**Files:**
- Modify: `host/src/transport_server.h`, `host/src/transport_server.cpp`, `host/src/stream_main.cpp`

**Interfaces:**
- `TransportServer::enable_tls(const std::string& cert_path, const std::string& key_path);` — call BEFORE `accept_client`; sets up a process `SSL_CTX`. When enabled, `accept_client` performs `SSL_accept` and all subsequent byte I/O uses the TLS session.
- `stream_main` gains `--tls`, `--cert <path>`, `--key <path>`.

- [ ] **Step 1: transport_server TLS members + ctx.** Add to the class: `bool tls_=false; std::string cert_,key_; SSL_CTX* ctx_=nullptr; SSL* ssl_=nullptr;` and:

```cpp
void TransportServer::enable_tls(const std::string& cert, const std::string& key) {
  cert_ = cert; key_ = key; tls_ = true;
  ctx_ = SSL_CTX_new(TLS_server_method());
  SSL_CTX_use_certificate_file(ctx_, cert_.c_str(), SSL_FILETYPE_PEM);
  SSL_CTX_use_PrivateKey_file(ctx_, key_.c_str(), SSL_FILETYPE_PEM);
}
```
Include `<openssl/ssl.h>`. In `accept_client`, after a successful `::accept` (and the existing
`TCP_NODELAY`/peer_ip capture), if `tls_`: `ssl_ = SSL_new(ctx_); SSL_set_fd(ssl_, client_fd_); if (SSL_accept(ssl_) <= 0) { /* log, close, free, return false */ }`.

- [ ] **Step 2: Route byte I/O through SSL.** Add private helpers and use them wherever the class
currently calls `::recv(client_fd_, ...)` / `::send(...)`/`send_all`:

```cpp
ssize_t TransportServer::conn_recv(void* b, size_t n) {
  return tls_ ? SSL_read(ssl_, b, (int)n) : ::recv(client_fd_, b, n, 0);
}
bool TransportServer::conn_send_all(const unsigned char* p, size_t n) {
  while (n) { ssize_t w = tls_ ? SSL_write(ssl_, p, (int)n) : ::send(client_fd_, p, n, MSG_NOSIGNAL);
              if (w <= 0) return false; p += w; n -= (size_t)w; }
  return true;
}
```
Replace the recv/send sites in `read_hello`, `poll_control`, and `send_all` (used by send_config/send_video) with these. In `poll_control`, before `wait_readable`, if `tls_ && SSL_pending(ssl_) > 0` treat as readable (so buffered TLS records aren't missed).

- [ ] **Step 3: Teardown.** In `close_all` (and the destructor): if `ssl_` `{ SSL_shutdown(ssl_); SSL_free(ssl_); ssl_=nullptr; }` before closing the fd. Free `ctx_` in the destructor.

- [ ] **Step 4: stream_main flags.** Parse `--tls` (bool), `--cert`/`--key` (string via the existing
`sval()`); after `tx.listen(...)` and before the reconnect loop, if tls: `tx.enable_tls(cert, key);` (once; the ctx is reused across sessions, but `ssl_` is per-accept — ensure `accept_client` makes a fresh `SSL` each session and `close_all` frees it).

- [ ] **Step 5: Build + manual TLS handshake check** (no app needed):
```bash
# generate a throwaway cert, run the streamer with --tls --test-pattern, handshake with s_client
cd /tmp && openssl req -x509 -newkey rsa:2048 -nodes -keyout k.pem -out c.pem -days 1 -subj "/CN=droppix" 2>/dev/null
/home/Spinjitsudoomyt/droppix-build/droppix_stream --test-pattern --tls --cert /tmp/c.pem --key /tmp/k.pem --port 27995 &
sleep 1; echo | openssl s_client -connect 127.0.0.1:27995 -brief 2>&1 | grep -iE "protocol|cipher|verify" ; kill %1
```
Expected: a completed TLS handshake (protocol/cipher lines). Also run full `ctest` (plaintext tests stay green, 85+/...).

- [ ] **Step 6: Commit** — `feat(host): optional TLS (OpenSSL) for the stream socket`.

---

### Task 3: GUI cert generation + pairing-code display + --tls args

**Files:**
- Create: `host/gui/cert_manager.h`, `host/gui/cert_manager.cpp` (+ CMake to `droppix_gui`)
- Modify: `host/gui/main_window.{h,cpp}`, `host/gui/args_builder.cpp`, `host/gui/args_builder.h`

**Interfaces:**
- `class CertManager { explicit CertManager(QString dir); bool ensure(); QString certPath() const; QString keyPath() const; QString pairingCode() const; };`
  — `ensure()` generates `<dir>/cert.pem`+`key.pem` via `openssl` if absent; `pairingCode()` reads the
  cert, converts to DER, and returns `derive_pairing_code(der)`.

- [ ] **Step 1: CertManager.** `ensure()`: if either file missing, run (via `QProcess::execute`)
`openssl req -x509 -newkey rsa:2048 -nodes -keyout <dir>/key.pem -out <dir>/cert.pem -days 3650 -subj "/CN=droppix"`,
then `chmod 600` the key. `pairingCode()`: read `cert.pem`, get DER via `QSslCertificate(pem).toDer()`
(Qt6::Network, already linked), `std::vector<unsigned char>` from it, `derive_pairing_code(...)` (include `../src/pairing_code.h`). Cache the code.

- [ ] **Step 2: MainWindow.** Own a `CertManager cert_{configDir()};`; call `cert_.ensure()` in the
ctor; add a `QLabel "Pairing code: <code>"` (objectName "caption") in the Settings/Status area showing
`cert_.pairingCode()`. (If `ensure()` fails — no openssl — show "Pairing code: unavailable" and proceed; TLS will fail to start, surfaced via the streamer log.)

- [ ] **Step 3: args_builder.** `build_command` signature gains the cert+key paths (or read them from a
new `Settings` field set by MainWindow before building). Simplest: add `Settings.tls=true`, `Settings.certPath`,
`Settings.keyPath`; MainWindow's `collectSettings()` fills certPath/keyPath from `cert_`. In the Evdi
branch (and test-pattern too, so WiFi-to-test-pattern works), if `s.tls && !certPath.isEmpty()`:
`a.push_back("--tls"); a.push_back("--cert"); a.push_back(certPath.toStdString()); a.push_back("--key"); a.push_back(keyPath.toStdString());`. Update `test_args_builder.cpp` with a test asserting `--tls`/`--cert` present when set.

- [ ] **Step 4: Build droppix_gui clean + full ctest green.** Manual: launch the GUI (human) shows
"Pairing code: NNNNNN"; the generated `~/.config/droppix_gui/cert.pem` exists. (Build-only verify here;
the implementer can also run `CertManager`-equivalent: `openssl x509 -in ~/.config/droppix_gui/cert.pem -outform der | sha256sum` and confirm the first 4 bytes → code matches the host `derive_pairing_code`.)

- [ ] **Step 5: Commit** — `feat(gui): generate TLS cert, show pairing code, pass --tls`.

---

### Task 4: Android TLS client + cert pinning

**Files:**
- Create: `android/.../net/TlsTrust.kt`
- Modify: `android/.../net/TransportClient.kt`

**Interfaces:**
- `class TlsTrust(ctx: Context) { fun isPaired(host: String): Boolean; fun pinnedFp(host: String): String?; fun pin(host: String, fp: String); fun clear(host: String); fun socketFactory(captured: (cert: java.security.cert.X509Certificate) -> Unit): SSLSocketFactory }`
  — the factory builds an `SSLContext` with an `X509TrustManager` whose `checkServerTrusted` records
  `chain[0]` (via `captured`) and returns without throwing (pinning decided by the caller).
- `fun certFingerprint(cert: X509Certificate): String` (hex `SHA-256(cert.encoded)`).

- [ ] **Step 1: TlsTrust** — pin store in `SharedPreferences("droppix_pins")` (`host -> fp`).
`socketFactory(captured)` returns an `SSLSocketFactory` from an `SSLContext.getInstance("TLS")` initialized
with a single `X509TrustManager` that, in `checkServerTrusted`, calls `captured(chain[0])` and returns
(no CA check). `certFingerprint` = lowercase hex of `MessageDigest.getInstance("SHA-256").digest(cert.encoded)`.

- [ ] **Step 2: TransportClient uses SSLSocket.** Replace `Socket()` with an `SSLSocket` from
`tlsTrust.socketFactory{ cert -> serverCert = cert }`; after `connect` call `startHandshake()`. Then enforce
the pin (the host param tells us which host): if `host == "127.0.0.1"` skip; else `val fp = certFingerprint(serverCert!!)`;
`val pinned = tlsTrust.pinnedFp(host)`; if `pinned == null` → caller should have paired first (treat as error
"not paired"); if `pinned != fp` → close + throw a distinct `CertChangedException` (StreamActivity surfaces
"PC identity changed — re-pair"). `run(...)` gains a `tlsTrust: TlsTrust` param (StreamActivity passes one).

- [ ] **Step 3: Build** `:app:assembleDebug :app:testDebugUnitTest` → SUCCESSFUL.

- [ ] **Step 4: Commit** — `feat(android): TLS SSLSocket + cert pinning`.

---

### Task 5: Android pairing probe + PIN dialog in ConnectActivity

**Files:**
- Modify: `android/.../ui/ConnectActivity.kt` (and `StreamActivity.kt` for the CertChanged surface)

**Interfaces:** consumes `TlsTrust`, `PairingCode.derive`, `certFingerprint`.

- [ ] **Step 1: Pairing in connectTo.** Change `ConnectActivity.connectTo(host, port)` to:
  - `host == "127.0.0.1"` → `launchStream(host, port)` (unchanged behaviour).
  - `tlsTrust.isPaired(host)` → `launchStream(host, port)`.
  - else → run `pairThenConnect(host, port)` on a background thread: open an `SSLSocket` via
    `tlsTrust.socketFactory{ c -> cert = c }`, `startHandshake()`, close it; on the main thread show an
    `AlertDialog` with an `EditText` (numeric, 6 digits) titled "Pair with `<host>`" / message "Enter the
    6-digit code shown on the PC"; on OK compare the entry to `PairingCode.derive(cert.encoded)` — match →
    `tlsTrust.pin(host, certFingerprint(cert))` + `launchStream(host, port)`; mismatch → toast "Wrong code"
    and stay. Handshake failure → toast "Could not reach `<host>`".
  - Add `launchStream(host,port)` = the old body of `connectTo` (save prefs + start StreamActivity).

- [ ] **Step 2: StreamActivity passes TlsTrust + handles CertChanged.** `StreamActivity` builds a
`TlsTrust(this)` and passes it to `client.run(...)`; if the connect loop catches `CertChangedException`,
`runOnUiThread` an `AlertDialog` "PC identity changed" with "Re-pair" (→ `tlsTrust.clear(host)`, finish back
to ConnectActivity) / "Cancel".

- [ ] **Step 3: Build** `:app:assembleDebug :app:testDebugUnitTest` → SUCCESSFUL. Manual e2e (human):
pair over WiFi with the PIN, reconnect silently, wrong PIN rejected, USB connects with no PIN, regenerate
the host cert → re-pair prompt.

- [ ] **Step 4: Commit** — `feat(android): PIN pairing probe + dialog + cert-change re-pair`.

---

## Self-Review

- **Spec coverage:** pairing-code derivation both ends (T1); host TLS server opt-in (T2); GUI cert gen +
  code display + `--tls` args (T3); Android `SSLSocket` + pinning (T4); PIN pairing probe + dialog +
  localhost auto-trust + cert-change re-pair (T5). approve-on-host is untouched (Part 1). USB auto-trust =
  the `127.0.0.1` branch in T4/T5.
- **Placeholder scan:** the only deferred literal is the pairing-code test vector `EXPECTED` (T1), which is
  a deterministic hash output the implementer locks via the TDD red-run and reuses verbatim in the Kotlin
  test — that shared literal is precisely what proves byte-identity. Everything else is concrete code/commands.
- **Type consistency:** `derive_pairing_code`/`PairingCode.derive` (T1) used by T3 (`CertManager.pairingCode`)
  and T5 (PIN compare); `enable_tls` (T2) called by `stream_main` (T2) and fed by the GUI's `--cert/--key`
  (T3); `TlsTrust`/`certFingerprint` (T4) used by T5; `CertChangedException` defined T4, handled T5.

## Execution

Subagent-driven, T1→T5. After T5, final whole-branch review, then merge.
