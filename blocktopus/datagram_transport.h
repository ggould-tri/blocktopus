#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>

namespace blocktopus {

/// @file The datagram transport layer of the library, which abstracts away
/// the boring TCP stuff.  Note that this is all written as the functions a
/// thread would loop over, but does not spawn any actual threads -- that
/// is for the caller to do.
///
/// The client end of the connection is very easy to understand -- punch some
/// server info into the config struct, Start(), and loop on Receive().
///
/// The server end is slightly more complex:  A DatagramTransportServer
/// listens for client connections and creates a DatagramTransport when
/// such a connection comes in.
///
/// In both cases, you will ultimately want one thread per DatagramTransport;
/// what thread entry point and loop and error checking and daemon-mode you
/// want is up to you and no threads are provided at this level.

/// A simple wrapper around unix networking to provide a minimal reliable,
/// sequential datagram service.  Currently built around raw TCP but should
/// in the future either use sctp or just zmq outright.
///
/// This service is strictly reliable and in-order, i.e. if messages A and B
/// are sent, and A is received, then the only possible results of the next
/// receive are B, error, or wait.  As such it is MUST be vulnerable to queue
/// overflow on any finite machine.  Clients are responsible for regularly
/// servicing the queue, ideally via a thread regularly calling ProcessIO.
class DatagramTransport {
 public:
  enum class End : int {
    SERVER = 1,
    CLIENT = 2,
  };

  struct Config {
    End end;
    std::string remote_addr;
    uint16_t remote_port;
    size_t mtu;
    size_t max_inbound_queue_size;
    size_t max_connection_queue_size;
  };

  struct UnsharedBuffer {
    size_t size;  // set to zero when empty.
    std::vector<uint8_t> data;
  };

  struct SharedBuffer {
    // NOTE:  Mutex is wrapped in order to get a move ctor (a mutex itself is
    // never movable and std::vector elements must be movable).
    std::unique_ptr<std::shared_mutex> mutex;
    bool returned;
    size_t size;
    std::vector<uint8_t> data;

    SharedBuffer(size_t max_size)
      : mutex(std::make_unique<std::shared_mutex>()),
        returned(false),
        size(0),
        data(max_size, 0) {}
  };

  struct SharedBufferHandle {
    // NOTE:  We use a `shared_lock` and a bareptr rather than the more
    // obvious strategy of a `shared_ptr` because C++ shared pointer reference
    // counts other than zero are irretrievably thread unsafe (consider, e.g.,
    // `weak_ptr` promotion on another thread) and are being deprecated.
    std::shared_lock<std::shared_mutex> lock;
    size_t size;
    const std::vector<uint8_t>* data;

    SharedBufferHandle(SharedBuffer* buffer) 
        : lock(*buffer->mutex) {
      buffer->returned = true;
      size = buffer->size;
      data = &buffer->data;
    }
  };

  /// Construct the transport object but DO NOT start networking yet.
  ///
  /// Note that this allocates the full maximum buffer capacity
  /// (mtu * sum(max_*_queue_size)) all at once to avoid future allocations.
  DatagramTransport(const Config& config);

  /// (Blocking) Start the network connection for this service.
  void Start();

  /// Send a datagram on this connnection.
  ///
  /// Takes ownership of the passed-in data.
  /// NOTE:  Actual sending is deferred until the next call to ProcessIO.
  void Send(std::unique_ptr<UnsharedBuffer> data);

  /// Receive all queued datagrams on this connnection.
  ///
  /// The returned handles each hold a lock on its respective buffer, which
  /// will be unavailable to process futher incoming datagrams; as such, the
  /// caller should promptly process and discard these handles.
  std::vector<SharedBufferHandle> ReceiveAll();

  /// (BLOCKING) The work unit function of this transport.
  ///
  /// Attempts to send all pending outbound datagrams and receive any pending
  /// incoming datagrams from the network.
  ///
  /// To use DatagramTransport as a nonblocking API, run this function in a
  /// loop on a thread; e.g.
  ///
  /// > std::thread([&](){ while(true) my_transport.ProcessIO(); });
  void ProcessIO();

 private:
  // Let factory class set private members.
  friend class DatagramTransportServer;

  static SharedBufferHandle MakeBufferHandle(SharedBuffer* buffer);

  const Config config_;

  int sock_fd_;
  std::vector<SharedBuffer> inbound_buffers_;
  std::vector<std::unique_ptr<UnsharedBuffer>> outbound_buffers_;
};

/// A server that listens for incoming connections on a port in order to
/// create DatagramTransport objects for each one.
class DatagramTransportServer {
 public:
  /// @brief  Create a new server.
  /// @param transport_config A prototype Config copied for each created
  ///        DatagramTransport objects.  End/addr/port will be ignored.
  DatagramTransportServer(
    const DatagramTransport::Config& transport_config_prototype);

  /// @brief  Get one incoming connection, build a transport for it.
  /// @return A server-end DatagramTransport for the new connection.
  DatagramTransport AwaitIncomingConnection();

 private:
  int sock_fd_;
  const DatagramTransport::Config transport_config_prototype_;

};

}  // namespace blocktopus
