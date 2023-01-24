#pragma once

#include <memory>
#include <vector>

#include "common.h"
#include "transport.h"

/// @file
/// This is a placeholder file showing the API we would like this feature to
/// present.

namespace blocktopus {

/// A client of a deterministic pub-sub networking system.
///
/// Every interaction with the system has a sequence number.  A client must
/// always use monotonically increasing sequence numbers in its calls, and in
/// return the system will provide the client with data with correspondingly
/// interleaved sequence numbers.
///
/// We use the term "sequence number" instead of "timestamp" in order to
/// discourage any confusion with wall-clock time.  However the most obvious
/// sequence number would be the timestamp of a distributed simulation.
///
/// The invariant is this:
///
/// Within a client, considering all of its API calls in order, the
/// following sequence numbers are nondecreaasing:
///  * The returned sequence number of all Subscribe and Unsubscribe calls,
///  * The message.receive_seq values of all members of Receive() returns.
///
///
class DeterministicClient {
 public:
  DeterministicClient(std::unique_ptr<Transport> transport);

  DeterministicClient(const DeterministicClient&) = delete;
  DeterministicClient(DeterministicClient&&) = delete;
  ~DeterministicClient() = default;

  /// @brief (BLOCKING) Perform blocking intialization of this client.
  ClientId Start();

  /// @brief Subscribe to a message channel.
  ///
  /// If @p channel is `std::nullopt` then this subscribes to all channels;
  /// note that such a subscription is inefficient not only for this client
  /// but the system as a whole since more client sequence numbers must be
  /// processed.
  ///
  /// There is one subtlety around subscription start times, analogous to the
  /// "lagging subscription" problem of all pub/sub architectures:
  ///
  /// * The passed-in sequence number indicates that this client does not
  ///   wish to receive messages on this subscription with lower sequence
  ///   numbers.
  /// * The returned sequence number indicates that the server guarantees that
  ///   messages with greater sequence numbers than this will in fact be
  ///   delivered.
  ///
  /// This is meant to handle the subtlety that this client does not know
  /// what sequence numbers the server has fully cleared.
  Seq Subscribe(std::optional<std::string> channel, Seq);

  /// @brief Exact opposite of Subscribe, with the same sequence semantics.
  Seq Unsubscribe(std::optional<std::string> channel, Seq);

  /// @brief Publish a message.
  /// * `message.sender` will be ignored and replaced with this client's ID.
  /// * `message.receive_seq` must be greater than `message.send_seq`.
  ///
  /// This implies `ClearToAdvance(message.send_seq)` and therefore
  /// this client may no longer mention any lower sequence number.
  void Publish(Message&& message);

  /// @brief Inform the server that this client will not publish with any
  /// sequence number lower than `clear_until`.  This client is henceforth
  /// prohibited from mentioning any lower sequence number.
  void ClearToAdvance(Seq clear_until);

  /// @brief (BLOCKING) Advance the sequence number of this client.
  ///
  /// Wait for the server end to advance this client's sequence number by
  /// any amount.
  Seq AwaitAdvance();

  /// @brief Receive some messages.
  /// This returns some of the messages sent to this client prior to its
  /// clear-to-advance time.  The messages will be in a reliable order that is
  /// nondescending in sequence number.  This will also return a sequence
  /// number than which no future message sequence number will be lower.
  std::tuple<std::vector<std::unique_ptr<Message>>, Seq> ReceiveMessages();

  /// @brief (BLOCKING) Convenience method to advance the sequence number.
  ///
  /// Sugar for the following pseudocode:
  ///  * ClearToAdvance(clear_until)
  ///  * while minimum_receive_sequence() < clear_until:
  ///    * ReceiveMessages()
  ///    * AwaitAdvance()
  std::tuple<std::vector<std::unique_ptr<Message>>, Seq>
  ReceiveUntil(Seq clear_until);

  /// @brief @return the sequence number of the last `ClearToAdvance`.
  Seq minimum_send_sequence();

  /// @brief @return the sequence number last returned by `AwaitAdvance`;
  /// the sequence number that the server understands this client to be
  /// using.
  Seq server_sequence_number();

  /// @brief @return the last sequence number received from the server.
  Seq minimum_receive_sequence();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // blocktopus
