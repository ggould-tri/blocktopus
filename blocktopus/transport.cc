#include "transport.h"

#include "fmt/core.h"

#include <fcntl.h>
#include <iostream>
#include <sstream>
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
    const TransportServer::Config& config) {
  struct sockaddr_in server_addr;

  /* First call to socket() function */
  int sock_fd = HandleError("socket", socket(AF_INET, SOCK_STREAM, 0));

  /* Initialize socket structure */
  bzero(reinterpret_cast<char *>(&server_addr), sizeof(server_addr));

  server_addr.sin_family = AF_INET;
  // TODO(ggould) Should use config addr parsed as literal IP addr.
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(config.listen_port);

  HandleError(fmt::format("bind({})", config.listen_port),
              bind(sock_fd,
                   reinterpret_cast<struct sockaddr*>(&server_addr),
                   sizeof(server_addr)));

  HandleError("listen",
              listen(sock_fd, config.max_connection_queue_size));

  return sock_fd;
}

bool TryNonblockingReceive(
    int fd,
    Transport::RxBuffer* buffer) {
  buffer->data.reserve(4);
  while (buffer->bytes_received < Transport::kHeaderSize) {
    const ssize_t read_result = recv(
       fd, &buffer->data.data()[buffer->bytes_received],
       Transport::kHeaderSize - buffer->bytes_received,
       MSG_DONTWAIT);
    if (read_result < 0 && (errno == EWOULDBLOCK || errno == EAGAIN)) {
      return true;
    } else if (read_result == 0) {
      return false;  // The remote end disconnected.
    }
    buffer->bytes_received += HandleError("recv[header]", read_result);
  }
  buffer->payload_size =
    ntohl(*reinterpret_cast<uint32_t*>(buffer->data.data()));
  size_t message_length = buffer->payload_size + Transport::kHeaderSize;
  buffer->data.reserve(message_length);
  // TODO(ggould) enforce MTU here
  while (buffer->bytes_received < message_length) {
    const ssize_t read_result = recv(
       fd, &buffer->data.data()[buffer->bytes_received],
       message_length - buffer->bytes_received,
       MSG_DONTWAIT);
    if (read_result < 0 && (errno == EWOULDBLOCK || errno == EAGAIN)) {
      return true;
    } else if (read_result == 0) {
      return false;  // The remote end disconnected.
    }
    buffer->bytes_received += HandleError("recv[payload]", read_result);
  }
  return true;
}

bool TryNonblockingSend(
    int fd,
    Transport::TxBuffer* buffer) {
  uint8_t size_data[4];
  *reinterpret_cast<uint32_t*>(&size_data) = htonl(buffer->payload_size);
  while (buffer->bytes_sent < Transport::kHeaderSize) {
    const ssize_t send_result = send(
       fd, &size_data[buffer->bytes_sent],
       Transport::kHeaderSize - buffer->bytes_sent,
       MSG_DONTWAIT);
    if (send_result < 0 && (errno == EWOULDBLOCK || errno == EAGAIN)) {
      return true;
    } else if (send_result == 0) {
      return false;  // The remote end disconnected.
    }
    buffer->bytes_sent += HandleError("send[header]", send_result);
  }
  size_t message_length =
    buffer->payload_size + Transport::kHeaderSize;
  // TODO(ggould) enforce MTU here
  while (buffer->bytes_sent < message_length) {
    const ssize_t send_result = send(
       fd, &buffer->data.data()[buffer->bytes_sent],
       message_length - buffer->bytes_sent,
       MSG_DONTWAIT);
    if (send_result < 0 && (errno == EWOULDBLOCK || errno == EAGAIN)) {
      return true;
    } else if (send_result == 0) {
      return false;  // The remote end disconnected.
    }
    buffer->bytes_sent += HandleError("send[payload]", send_result);
  }
  return true;
}

}  // namespace

Transport::Transport(const Transport::Config& config)
    : config_(config) {}

Transport::~Transport() {
  if (sock_fd_ >= 0) {
    close(sock_fd_);
  }
}

void Transport::Start() {
  switch (config_.end) {
    case Transport::End::kClient: {
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
    case Transport::End::kServer: {
      break;
    }
    default:
      throw std::runtime_error(fmt::format("Invalid server end {}",
                                           static_cast<int>(config_.end)));
  }
}

void Transport::SendBuffer(const Transport::TxBuffer& data) {
  outbound_buffers_.push_back(std::make_unique<Transport::TxBuffer>(data));
}

std::vector<std::unique_ptr<Transport::RxBuffer>>
Transport::ReceiveAll() {
  std::vector<std::unique_ptr<Transport::RxBuffer>> result;
  for (auto& buffer_handle : inbound_buffers_) {
    result.push_back(std::move(buffer_handle));
  }
  inbound_buffers_.clear();
  return result;
}

bool Transport::ProcessIO() {
  if (!io_thread_id_.has_value()) {
    io_thread_id_ = std::this_thread::get_id();
  } else if (*io_thread_id_ != std::this_thread::get_id()) {
    std::ostringstream err;
    err << "ProcessIO() called on thread " << *io_thread_id_
        << " but has also been called from thread " <<
        std::this_thread::get_id();
    throw std::runtime_error(err.str());
  }

  // Repeatedly send buffers without blocking.
  if (current_outgoing_message_ == nullptr && !outbound_buffers_.empty()) {
    current_outgoing_message_ = std::move(outbound_buffers_.front());
    outbound_buffers_.pop_front();
  }
  while (current_outgoing_message_ != nullptr) {
    bool still_open = TryNonblockingSend(sock_fd_,
                                     current_outgoing_message_.get());
    if (!still_open) { return false; }
    if (current_outgoing_message_ == nullptr ||
        current_outgoing_message_->done()) {
      if (!outbound_buffers_.empty()) {
        current_outgoing_message_ = std::move(outbound_buffers_.front());
        outbound_buffers_.pop_front();
      } else {
        current_outgoing_message_ = nullptr;
      }
    }
  }

  // Repeatedly receive buffers without blocking.
  if (current_incoming_message_ == nullptr) {
    current_incoming_message_ = std::make_unique<Transport::RxBuffer>();
  }
  while (current_incoming_message_ != nullptr) {
    bool still_open = TryNonblockingReceive(sock_fd_,
                                            current_incoming_message_.get());
    if (!still_open) { return false; }
    if (current_incoming_message_->done()) {
      inbound_buffers_.push_back(std::move(current_incoming_message_));
      current_incoming_message_ = std::make_unique<Transport::RxBuffer>();
    } else {
      break;  // We couldn't recieve a full message without blocking.
    }
  }

  // TODO(ggould) Ideally we would at this point wait on a blocking operation
  // and a condition variable on the queues.  We don't bother with this yet.
  return true;
}

TransportServer::TransportServer(
  const TransportServer::Config& config)
    : config_(config) {
  // NOP:  We will lazily initialize via `BoundListeningSocket` in the first
  // `AwaitIncomingConnection` to avoid doing blocking work in the ctor (even
  // though in practice that setup rarely/never blocks).
}

TransportServer::~TransportServer() {
  if (sock_fd_ >= 0) {
    close(sock_fd_);
  }
}

void TransportServer::LazyInitialize() {
  if (sock_fd_ <= 0) {
    sock_fd_ = BoundListeningSocket(config_);
  }
}

Transport TransportServer::AwaitIncomingConnection() {
  LazyInitialize();
  struct sockaddr_in client_addr;
  unsigned int client_addr_len = sizeof(struct sockaddr_in);
  int new_fd = HandleError(
    "accept",
    accept(sock_fd_,
           reinterpret_cast<struct sockaddr*>(&client_addr),
           &client_addr_len));
  Transport::Config result_config = config_.transport_config_prototype;
  result_config.remote_addr = client_addr.sin_addr.s_addr;
  result_config.remote_port = ntohs(client_addr.sin_port);

  Transport result(result_config);
  result.sock_fd_ = new_fd;
  return result;
}

uint16_t TransportServer::GetPortNumber() {
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
