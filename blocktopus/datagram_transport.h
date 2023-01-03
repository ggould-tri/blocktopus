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
  /// @brief Which end of a connection this DatagramTransport contains.
  enum class End : int {
    kServer = 1,
    kClient = 2,
  };

  /// @brief Universal constructor arguments for a DatagramTransport.
  ///
  /// Note that the fields are filled in differently in the client and server
  /// cases; a client must populate all members, while a server will discover
  /// the remote-end parameters at connection time.
  struct Config {
    End end = End::kClient;
    std::string remote_addr = "0.0.0.0";
    uint16_t remote_port = 30303;
    // TODO(ggould) There is no check that both ends agree about this!
    size_t mtu = 1024;
    size_t max_inbound_queue_size = 32;
    size_t max_connection_queue_size = 5;
  };

  /// @brief A container for the data and length of an outgoing datagram.
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

  /// @brief A threadsafe reference to datagram contents.
  ///
  /// This is the structure that is returned to callers of this API for a
  /// received datagram.  It pins the in-memory datagram in the queue while
  /// it exists, so it should be processed and discarded promptly to prevent
  /// unnecessary overflow and blocking.
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

    SharedBufferHandle(const SharedBufferHandle& orig)
        : lock(*orig.lock.mutex()),
          size(orig.size),
          data(orig.data) {}
  };

  /// Construct the transport object but DO NOT start networking yet.
  ///
  /// Note that this allocates the full maximum buffer capacity
  /// (mtu * max_inbound_queue_size) all at once to avoid future allocations.
  DatagramTransport(const Config& config);

  /// (BLOCKING) Start the network connection for this service.
  void Start();

  /// Send a datagram on this connnection.
  ///
  /// Takes ownership of the passed-in data.
  /// NOTE:  Actual sending is deferred until the next call to ProcessIO.
  void Send(std::unique_ptr<UnsharedBuffer> data);

  /// Receive all queued datagrams on this connnection.
  ///
  /// Each returned handle holds a lock on its respective buffer, which
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
  ///
  /// To use DatagramTransportServer as a nonblocking API, run this function
  /// in a loop on a thread; e.g.
  ///
  /// > std::thread([&](){ while(true) my_server.AwaitIncomingConnection(); });
  DatagramTransport AwaitIncomingConnection();

 private:
  int sock_fd_ = -1;
  const DatagramTransport::Config transport_config_prototype_;
};

}  // namespace blocktopus
