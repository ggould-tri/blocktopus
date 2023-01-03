#include "blocktopus/datagram_transport.h"

#include <thread>

#include <gtest/gtest.h>

namespace blocktopus {

TEST(DatagramTransport, LifecycleClientSmoke) {
  DatagramTransport::Config config{};
  DatagramTransport _transport(config);
}

TEST(DatagramTransportServer, LifecycleServerSmoke) {
  DatagramTransport::Config config{};
  config.end = DatagramTransport::End::kServer;
  DatagramTransportServer _server(config);
}

// NOTE:  In the code below we set server ports per-test to avoid
// having to do SO_REUSEADDR shenanigans.

TEST(ClientServerPair, AcceptConnectionSmoke) {
  DatagramTransportServer server(DatagramTransport::Config{
    .end = DatagramTransport::End::kServer,
    .remote_port = 0});
  auto server_port = server.GetPortNumber();
  DatagramTransport client(DatagramTransport::Config{
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
  DatagramTransportServer server(DatagramTransport::Config{
    .end = DatagramTransport::End::kServer,
    .remote_port = 0});
  auto server_port = server.GetPortNumber();
  std::thread server_thread([&]() {
    for (int i = 0; i < 3; ++i) {
      server.AwaitIncomingConnection();
      num_connections++;
    };
  });
  for (int i = 0; i < 3; ++i) {
    DatagramTransport(DatagramTransport::Config{
      .remote_addr = "localhost",
      .remote_port = server_port}).Start();
  }
  server_thread.join();
  EXPECT_EQ(num_connections, 3);
}

}  // namespace blocktopus
