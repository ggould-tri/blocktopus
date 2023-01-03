#include "datagram_transport.h"

#include "fmt/core.h"

#include <fcntl.h>
#include <netdb.h>
#include <unistd.h>

namespace {

/// @brief Perform standard unix return value handling:  If the potential
///        error value is negative, raise an exception with its strerror or
///        with the value in erron.  Otherwise return the non-error value.
int HandleError(const std::string& what, int maybe_error) {
  if (maybe_error >= 0) return maybe_error;
  if (maybe_error == -1) maybe_error = errno;
  throw std::logic_error(
    fmt::format("ERROR[{}]: {}", what, strerror(maybe_error)));
}

/// @brief Return a bound, listening socket ready for accept() calls
///
///        Creates and binds a new socket and puts it into listen mode
///        using relevant values from @p config.  The returned value is
///        a valid file descriptor (any failure causes an exception).
int BoundListeningSocket(
    const blocktopus::DatagramTransport::Config& config) {
  struct sockaddr_in server_addr;

  /* First call to socket() function */
  int sock_fd = HandleError("socket", socket(AF_INET, SOCK_STREAM, 0));

  /* Initialize socket structure */
  bzero(reinterpret_cast<char *>(&server_addr), sizeof(server_addr));

  server_addr.sin_family = AF_INET;
  // TODO(ggould) Should use config addr parsed as literal IP addr.
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(config.remote_port);

  HandleError("bind",
              bind(sock_fd,
                   reinterpret_cast<struct sockaddr*>(&server_addr),
                   sizeof(server_addr)));

  HandleError("listen",
              listen(sock_fd, config.max_connection_queue_size));

  return sock_fd;
}
}  // namespace

namespace blocktopus {

DatagramTransport::DatagramTransport(
  const DatagramTransport::Config& config) 
    : config_(config) {
  for (int i = 0; i < config_.max_inbound_queue_size; ++i) {
    inbound_buffers_.emplace_back(config_.mtu);
  }
}

void DatagramTransport::Start() {
  switch (config_.end) {
    case DatagramTransport::End::CLIENT: {
      struct addrinfo hints;
      struct addrinfo* addr_list;
      std::string port = fmt::format("{}", config_.remote_port);
      memset(&hints, 0, sizeof(hints));
      hints.ai_family = PF_UNSPEC;
      hints.ai_socktype = SOCK_STREAM;
      HandleError("getaddrinfo",
                  getaddrinfo(config_.remote_addr.c_str(), port.c_str(),
                              &hints, &addr_list));
      sock_fd_ = -1;
      int connect_error = 0;
      // Try each returned addrinfo; discard all but the last error.
      for (struct addrinfo* addr = addr_list;
           addr != NULL;
           addr = addr->ai_next) {
        sock_fd_ = socket(addr->ai_family,
                          addr->ai_socktype,
                          addr->ai_protocol);
        if (sock_fd_ == -1) continue;
        connect_error = 0;
        connect_error = connect(sock_fd_, addr->ai_addr, addr->ai_addrlen);
        if (connect_error == 0) break;
      }
      HandleError("socket", sock_fd_);
      HandleError("connect", connect_error);
      freeaddrinfo(addr_list);
      close(sock_fd_);
      break;
    }
    case DatagramTransport::End::SERVER: {
      break;
    }
    default:
      throw std::runtime_error(fmt::format("Invalid server end {}",
                                           static_cast<int>(config_.end)));
  }
}

void DatagramTransport::Send(
    std::unique_ptr<DatagramTransport::UnsharedBuffer> data) {
  outbound_buffers_.push_back(data);
}

std::vector<DatagramTransport::SharedBufferHandle>
DatagramTransport::ReceiveAll() {
  std::vector<DatagramTransport::SharedBufferHandle> result;
  for (auto& buffer : inbound_buffers_) {
    if (!buffer.returned) {
      result.emplace_back(&buffer);
    }
  }
  return result;
}

void DatagramTransport::ProcessIO() {
  // XXX
}

DatagramTransportServer::DatagramTransportServer(
  const DatagramTransport::Config& transport_config_prototype)
    : transport_config_prototype_(transport_config_prototype) {
  // NOP:  We will lazily initialize in the first `AwaitIncomingConnection`
  // to avoid doing blocking work in the ctor (even though in practice that
  // setup rarely blocks).
}

DatagramTransport DatagramTransportServer::AwaitIncomingConnection() {
  if (sock_fd_ <= 0) {
    sock_fd_ = BoundListeningSocket(transport_config_prototype_);
  }
  struct sockaddr_in client_addr;
  unsigned int client_addr_len = sizeof(struct sockaddr_in);
  int new_fd = HandleError(
    "accept",
    accept(sock_fd_,
           reinterpret_cast<struct sockaddr*>(&client_addr),
           &client_addr_len));
  DatagramTransport::Config result_config = transport_config_prototype_;
  result_config.remote_addr = client_addr.sin_addr.s_addr;
  result_config.remote_port = client_addr.sin_port;
  result_config.end = DatagramTransport::End::SERVER;

  DatagramTransport result(result_config);
  result.sock_fd_ = new_fd;
  return result;
}

}  // namespace blocktopus
