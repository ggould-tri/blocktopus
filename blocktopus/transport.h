#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

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
/// receive are B, error, or wait.
///
/// Clients are responsible for regularly servicing the queue, ideally via a
/// thread regularly calling ProcessIO.

namespace blocktopus {

class Transport final {
 public:
  /// Size of a datagram size header, in bytes.
  static constexpr size_t kHeaderSize = sizeof(uint32_t);

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
    size_t max_outbound_queue_size = 32;
  };

  /// @brief A container for the data and length of an outgoing datagram.
  struct TxBuffer {
    size_t payload_size;  // set to zero when empty.
    std::vector<uint8_t> data;
    size_t bytes_sent = 0;

    bool done() { return bytes_sent == payload_size + kHeaderSize; }
  };

  /// @brief A container for the data and length of an incoming datagram.
  struct RxBuffer {
    size_t payload_size;  // set to zero when empty.
    std::vector<uint8_t> data;
    size_t bytes_received = 0;

    bool done() { return bytes_received == payload_size + kHeaderSize; }
  };

  Transport(const Config& config);
  ~Transport();
  Transport(Transport&&) = default;

    /// (BLOCKING) Start the network connection for this service.
  void Start();

  void Send(std::vector<uint8_t> data) { SendBuffer({data.size(), data, 0}); }

  /// Send a datagram on this connnection.
  ///
  /// The passed-in data is copied; actual sending is deferred until the
  /// next call to ProcessIO.
  void SendBuffer(const TxBuffer& data);

  /// Receive all queued inbound datagrams on this connnection.
  ///
  /// Each returned handle holds a lock on its respective buffer, which
  /// will be unavailable to process futher incoming datagrams; as such, the
  /// caller should promptly process and discard these handles.
  std::vector<std::unique_ptr<RxBuffer>> ReceiveAll();

  /// (BLOCKING) The work unit function of this transport.
  ///
  /// @pre All calls to this function must be from the same thread
  /// @return `true` if the transport remains usable (not closed)
  ///
  /// Attempts to send all pending outbound datagrams and receive any pending
  /// incoming datagrams from the network.
  ///
  /// To use DatagramTransport as a nonblocking API, run this function in a
  /// loop on a thread; e.g.
  ///
  /// > std::thread([&](){ while(true) my_transport.ProcessIO(); });
  bool ProcessIO();

  /// @return the `Config` object this class was created with.
  Config config() const { return config_; }

 private:
  // Let factory class set private members.
  friend class TransportServer;

  const Config config_;

  int sock_fd_ = -1;

  std::optional<std::thread::id> io_thread_id_ = std::nullopt;

  std::vector<std::unique_ptr<RxBuffer>> inbound_buffers_;
  std::unique_ptr<RxBuffer> current_incoming_message_ = nullptr;

  std::deque<std::unique_ptr<TxBuffer>> outbound_buffers_;
  std::unique_ptr<TxBuffer> current_outgoing_message_ = nullptr;
};

/// A server that listens for incoming connections on a port in order to
/// create Transport objects for each one.
class TransportServer final {
 public:

  struct Config {
    std::string listen_addr = "0.0.0.0";
    uint16_t listen_port = 0;
    size_t max_connection_queue_size = 5;

    /// A prototype Config copied for each created Transport objects.
    /// End/addr/port will be ignored.
    Transport::Config transport_config_prototype;
  };

  TransportServer(const Config&);
  ~TransportServer();
  TransportServer(TransportServer&&) = default;

  /// @brief  (BLOCKING) Get one incoming connection, build a transport for it.
  /// @return A server-end DatagramTransport for the new connection.
  ///
  /// To use DatagramTransportServer as a nonblocking API, run this function
  /// in a loop on a thread; e.g.
  ///
  /// > std::thread([&](){ while(true) my_server.AwaitIncomingConnection(); });
  Transport AwaitIncomingConnection();

  /// @brief  (BLOCKING) Retrieve the server port number.
  ///
  /// If the configured port number was 0 (allowing the OS to choose an
  /// an unbound port, e.g. for unit testing; see `man 'bind(2)'` and
  /// `man 'ip(7)'`), this is the only way to determine what port the server
  /// is actually running on.
  ///
  /// Note that if `AwaitIncomingConnection` has not been called, this may
  /// block to bind a port.
  uint16_t GetPortNumber();

 private:
  /// @brief  (BLOCKING) Post-ctor initialization.
  void LazyInitialize();

  int sock_fd_ = -1;
  const Config config_;
};

}  // blocktopus