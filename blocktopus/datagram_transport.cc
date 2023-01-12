#include "datagram_transport.h"

#include "fmt/core.h"

#include <fcntl.h>
#include <iostream>
#include <mutex>
#include <netdb.h>
#include <unistd.h>

namespace blocktopus {

namespace {

/// @brief Perform standard unix return value handling.
///
/// If the potential error value is negative, raise an exception with its
/// return code's `strerror` or that of the value in `errno`.
///
/// Otherwise @return the non-error value.
int HandleError(const std::string& what, int maybe_error) {
  int error_to_print = maybe_error;
  if (maybe_error >= 0) return maybe_error;
  if (maybe_error == -1) error_to_print = errno;
  std::string error_text =
    fmt::format("ERROR[{} => {}/{}]: {}",
                what, maybe_error, error_to_print, strerror(error_to_print));
  // This will often be called outside of the main thread, in which case
  // the thrown text will not be output.  Write it directly before we throw.
  std::cerr << error_text << std::endl;
  throw std::logic_error(error_text);
}

/// @brief Return a bound, listening socket ready for accept() calls.
///
/// Creates and binds a new socket and puts it into listen mode
/// using relevant values from @p config.  The returned value is
/// a valid file descriptor (any failure causes an exception).
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

  HandleError(fmt::format("bind({})", config.remote_port),
              bind(sock_fd,
                   reinterpret_cast<struct sockaddr*>(&server_addr),
                   sizeof(server_addr)));

  HandleError("listen",
              listen(sock_fd, config.max_connection_queue_size));

  return sock_fd;
}

// Blockingly attempt to complete reading one message.  Return `true`
// if the fd remains open.
bool AdvanceRxBuffer(struct DatagramTransport::RxBuffer* buffer, int fd) {
  std::unique_lock lock(*buffer->mutex);
  while (buffer->bytes_read < DatagramTransport::kDatagramSizeHeaderSize) {
    int read_result = recv(
       fd, &buffer->data.data()[buffer->bytes_read],
       DatagramTransport::kDatagramSizeHeaderSize - buffer->bytes_read,
       0 /* no flags */);
    if (read_result == EAGAIN) {
      continue;
    } else if (read_result == 0) {
      return false;  // The remote end disconnected.
    }
    buffer->bytes_read += HandleError("recv[header]", read_result);
  }
  size_t payload_length =
    ntohl(*reinterpret_cast<uint32_t*>(buffer->data.data()));
  size_t message_length =
    payload_length + DatagramTransport::kDatagramSizeHeaderSize;
  // TODO(ggould) enforce MTU here
  while (buffer->bytes_read < message_length) {
    int read_result = recv(
       fd, &buffer->data.data()[buffer->bytes_read],
       message_length - buffer->bytes_read, 0 /* no flags */);
    if (read_result == EAGAIN) {
      continue;
    } else if (read_result == 0) {
      return false;  // The remote end disconnected.
    }
    buffer->bytes_read += HandleError("recv[payload]", read_result);
  }
}

// Blockingly send one message.  Return `true` if the fd remains open.
bool AdvanceTxBuffer(struct DatagramTransport::TxBuffer* buffer, int fd) {
  uint8_t size_data[4];
  *reinterpret_cast<uint32_t*>(&size_data) = htonl(buffer->payload_size);
  while (buffer->bytes_sent < DatagramTransport::kDatagramSizeHeaderSize) {
    int send_result = send(
       fd, &size_data[buffer->bytes_sent],
       DatagramTransport::kDatagramSizeHeaderSize - buffer->bytes_sent,
       0 /* no flags */);
    if (send_result == EAGAIN) {
      continue;
    } else if (send_result == 0) {
      return false;  // The remote end disconnected.
    }
    buffer->bytes_sent += HandleError("send[header]", send_result);
  }
  size_t message_length =
    buffer->payload_size + DatagramTransport::kDatagramSizeHeaderSize;
  // TODO(ggould) enforce MTU here
  while (buffer->bytes_sent < message_length) {
    int send_result = send(
       fd, &buffer->data.data()[buffer->bytes_sent],
       message_length - buffer->bytes_sent, 0 /* no flags */);
    if (send_result == EAGAIN) {
      continue;
    } else if (send_result == 0) {
      return false;  // The remote end disconnected.
    }
    buffer->bytes_sent += HandleError("send[payload]", send_result);
  }
}
}  // namespace

DatagramTransport::DatagramTransport(
  const DatagramTransport::Config& config)
    : config_(config) {
  for (size_t i = 0; i < config_.max_inbound_queue_size; ++i) {
    inbound_buffers_.emplace_back(config_.mtu);
  }
}

DatagramTransport::~DatagramTransport() {
  if (sock_fd_ >= 0) {
    close(sock_fd_);
  }
}

void DatagramTransport::Start() {
  switch (config_.end) {
    case DatagramTransport::End::kClient: {
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
      HandleError(fmt::format("connect({})", config_.remote_port),
                  connect_error);
      freeaddrinfo(addr_list);
      break;
    }
    case DatagramTransport::End::kServer: {
      break;
    }
    default:
      throw std::runtime_error(fmt::format("Invalid server end {}",
                                           static_cast<int>(config_.end)));
  }
}

void DatagramTransport::Send(const DatagramTransport::TxBuffer& data) {

}

std::vector<DatagramTransport::RxBufferHandle>
DatagramTransport::ReceiveAll() {
  std::vector<DatagramTransport::RxBufferHandle> result;
  for (auto& buffer : inbound_buffers_) {
    if (!buffer.has_been_returned) {
      result.emplace_back(&buffer);
    }
  }
  return result;
}

void DatagramTransport::ProcessIO() {
  for (auto& buffer : inbound_buffers_) {
    std::unique_lock lock;
    lock.try_lock
  }
}

DatagramTransportServer::DatagramTransportServer(
  const DatagramTransport::Config& transport_config_prototype)
    : transport_config_prototype_(transport_config_prototype) {
  if (transport_config_prototype_.end != DatagramTransport::End::kServer) {
    throw std::logic_error(
      "Tried to create a DatagramTransportServer with a client config");
  }
  // NOP:  We will lazily initialize via `BoundListeningSocket` in the first
  // `AwaitIncomingConnection` to avoid doing blocking work in the ctor (even
  // though in practice that setup rarely/never blocks).
}

DatagramTransportServer::~DatagramTransportServer() {
  if (sock_fd_ >= 0) {
    close(sock_fd_);
  }
}

void DatagramTransportServer::LazyInitialize() {
  if (sock_fd_ <= 0) {
    sock_fd_ = BoundListeningSocket(transport_config_prototype_);
  }
}

DatagramTransport DatagramTransportServer::AwaitIncomingConnection() {
  LazyInitialize();
  struct sockaddr_in client_addr;
  unsigned int client_addr_len = sizeof(struct sockaddr_in);
  int new_fd = HandleError(
    "accept",
    accept(sock_fd_,
           reinterpret_cast<struct sockaddr*>(&client_addr),
           &client_addr_len));
  DatagramTransport::Config result_config = transport_config_prototype_;
  result_config.remote_addr = client_addr.sin_addr.s_addr;
  result_config.remote_port = ntohs(client_addr.sin_port);

  DatagramTransport result(result_config);
  result.sock_fd_ = new_fd;
  return result;
}

uint16_t DatagramTransportServer::GetPortNumber() {
  LazyInitialize();
  struct sockaddr_in addr;
  socklen_t socklen = sizeof(struct sockaddr_in);
  HandleError("getsockname", getsockname(
      sock_fd_,
      reinterpret_cast<struct sockaddr*>(&addr),
      &socklen));
  return ntohs(addr.sin_port);
}

}  // namespace blocktopus
