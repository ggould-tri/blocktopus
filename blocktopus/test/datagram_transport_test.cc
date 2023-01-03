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

TEST(ClientServerPair, AcceptConnectionSmoke) {
  DatagramTransport::Config client_config{};
  client_config.remote_addr = "localhost";
  DatagramTransport::Config server_config{};
  server_config.end = DatagramTransport::End::kServer;
  DatagramTransportServer server(server_config);
  DatagramTransport client(client_config);
  std::thread server_thread([&]() {
    server.AwaitIncomingConnection();
  });
  client.Start();
  server_thread.join();
}

}  // namespace blocktopus
