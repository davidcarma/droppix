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
