#include "blocktopus/transport.h"

#include <thread>

#include <gtest/gtest.h>

namespace blocktopus {

TEST(Transport, LifecycleClientSmoke) {
  Transport::Config config{};
  Transport _transport(config);
}

TEST(TransportServer, LifecycleServerSmoke) {
  TransportServer::Config config;
  TransportServer _server(config);
}

// NOTE:  In the code below we set server ports per-test to avoid
// having to do SO_REUSEADDR shenanigans.

TEST(ClientServerPair, AcceptConnectionSmoke) {
  TransportServer server(TransportServer::Config{});
  auto server_port = server.GetPortNumber();
  Transport client(Transport::Config{
    .remote_addr = "localhost",
    .remote_port = server_port});
  int num_connections = 0;
  std::thread server_thread([&]() {
    server.AwaitIncomingConnection();
    num_connections++;
  });
  client.Start();
  server_thread.join();
  EXPECT_EQ(num_connections, 1);
}

TEST(ClientServerPair, AcceptMultiConnectionsSmoke) {
  int num_connections = 0;
  TransportServer server(TransportServer::Config{});
  auto server_port = server.GetPortNumber();
  std::thread server_thread([&]() {
    for (int i = 0; i < 3; ++i) {
      server.AwaitIncomingConnection();
      num_connections++;
    };
  });
  for (int i = 0; i < 3; ++i) {
    Transport(Transport::Config{
      .remote_addr = "localhost",
      .remote_port = server_port}).Start();
  }
  server_thread.join();
  EXPECT_EQ(num_connections, 3);
}

}  // namespace blocktopus
