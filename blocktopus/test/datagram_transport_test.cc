#include "blocktopus/datagram_transport.h"

#include <gtest/gtest.h>

namespace blocktopus {

TEST(DatagramTransport, LifecycleClientSmoke) {
  DatagramTransport::Config config{};
  DatagramTransport _transport(config);
}

TEST(DatagramTransportServer, LifecycleServerSmoke) {
  DatagramTransport::Config config{};
  DatagramTransportServer _server(config);
}

}  // namespace blocktopus
