#include "blocktopus/datagram_transport.h"

#include <gtest/gtest.h>

namespace blocktopus {

// Demonstrate some basic assertions.
TEST(DatagramTransport, LifecycleSmoke) {
  {  // Create and destroy with default args.
    DatagramTransport::Config config{};
    DatagramTransport _transport(config);
  }
}

}  // namespace blocktopus