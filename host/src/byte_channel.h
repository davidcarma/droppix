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
