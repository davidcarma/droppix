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
