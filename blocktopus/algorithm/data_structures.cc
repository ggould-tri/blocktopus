#include "blocktopus/algorithm/data_structures.h"

#include <limits>

#include "fmt/format.h"

namespace blocktopus {
namespace algorithm {

namespace {
  // Obnoxious variant helper boilerplate.
  template<class... Ts>
  struct overloaded : Ts... { using Ts::operator()...; };
}  // namespace

void Criticize(const Message& opus, Critique* critique) {
  std::string message_name =
    fmt::format("{}_pub_at_{}", opus.publisher, opus.publish_seq);
  for (const auto& receipt : opus.receipients) {
    if (receipt.receive_seq <= opus.publish_seq) {
      critique->push_back(
        fmt::format("{} noncausal message pub_seq {} rx_seq {}",
                    message_name, opus.publish_seq, receipt.receive_seq));
    }
  }
}

void Criticize(const Event& opus, Critique* critique) {
  std::visit(overloaded {
    [&](PublishEvent evt) { Criticize(evt.message, critique); },
    [&](ReceiveEvent evt) { Criticize(evt.message, critique); },
    [&](SequenceEvent evt) {},
  }, opus);
}

void Criticize(const EventList& opus, Critique* critique){
  // Front-load ill-formed event criticism, as it's easier to fix and probably
  // the source of any subsequent errors.
  for (const auto& event : opus) Criticize(event, critique);
  SeqNum last_causal_point = std::numeric_limits<SeqNum>::min();
  for (const auto& event : opus) {
    std::visit(overloaded {
      [&](PublishEvent evt) { 
        std::string message_name =
          fmt::format("{}_pub_at_{}",
                      evt.message.publisher, evt.message.publish_seq);
        if (last_causal_point >= evt.message.publish_seq) {
          critique->push_back(fmt::format("Event {} after causal sequence {}",
                                          message_name, last_causal_point));
        }
        last_causal_point = evt.message.publish_seq;
      },
      [&](ReceiveEvent evt) { 
        std::string message_name =
          fmt::format("{}_pub_at_{}",
                      evt.message.publisher, evt.message.publish_seq);
        for (const auto& rx : evt.message.recipients) {
          if (last_causal_point >= rx.receive_seq) {
          critique->push_back(fmt::format("Event {} after causal sequence {}",
                                          message_name, last_causal_point));
          }
        }
      },
      [&](SequenceEvent evt) {
        if (last_causal_point >= evt.seq_num) {
          critique->push_back(fmt::format(
            "Sequence point {} after causal sequence {}",
            evt.seq_num, last_causal_point));
        }
        last_causal_point = evt.seq_num;
      },
    }, event);
  }
}

}  // algorithm
}  // blocktopus
