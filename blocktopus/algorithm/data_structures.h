#pragma once

#include <numbers>
#include <string>
#include <variant>
#include <vector>

/// @file See https://docs.google.com/document/d/1ZV2KArc6QP-IZnwKur7SrJR3ZStee5tOTrh-2N-Zbj4/edit

namespace blocktopus {
namespace algorithm {

// Primitive types
using SeqNum = double;
static constexpr SeqNum kFirstSeqNum = 0;
using ClientId = uint32_t;
using RxSelector = std::string;
using Payload = std::vector<uint8_t>;

// Utility types
using Critique = std::vector<std::string>;

/// Message representation:  A single message sent to multiple recipients.
struct Message {
  struct RxInfo {
    ClientId receipient;
    SeqNum receive_seq;
  };

  ClientId publisher;
  SeqNum publish_seq;
  std::vector<RxInfo> recipients;
  Payload payload;
};

struct PublishEvent {
  Message message;
};

struct ReceiveEvent {
  Message message;
  ClientId recipient;
};

struct SequenceEvent {
  SeqNum seq_num;
};

using Event = std::variant<PublishEvent, ReceiveEvent, SequenceEvent>;

using EventList = std::vector<Event>;

void Criticize(const Message& opus, Critique* critique);
void Criticize(const Event& opus, Critique* critique);
void Criticize(const EventList& opus, Critique* critique);

}  // algorithm
}  // blocktopus
