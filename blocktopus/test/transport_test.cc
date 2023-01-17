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

TEST(Connection, SendReceive) {
  TransportServer server(TransportServer::Config{});
  auto server_port = server.GetPortNumber();
  Transport client_transport(Transport::Config{
    .remote_addr = "localhost",
    .remote_port = server_port});
  std::thread client_start([&](){ client_transport.Start(); });
  Transport server_transport = server.AwaitIncomingConnection();
  client_start.join();
  std::string data = "foo";

  // Send from server to client.
  server_transport.Send(std::vector<uint8_t>(data.begin(), data.end()));
  std::vector<std::unique_ptr<Transport::RxBuffer>> received;
  while (received.size() == 0) {
    server_transport.ProcessIO();
    client_transport.ProcessIO();
    received = client_transport.ReceiveAll();
  }
  EXPECT_EQ(received.size(), 1);

  // Send from client to server.
  client_transport.Send(std::vector<uint8_t>(data.begin(), data.end()));
  received.clear();
  while (received.size() == 0) {
    client_transport.ProcessIO();
    server_transport.ProcessIO();
    received = server_transport.ReceiveAll();
  }
  EXPECT_EQ(received.size(), 1);
}

}  // namespace blocktopus
