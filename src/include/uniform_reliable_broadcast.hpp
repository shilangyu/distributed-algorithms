#pragma once

#include <netinet/in.h>
#include <array>
#include <bitset>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "best_effort_broadcast.hpp"
#include "perfect_link.hpp"

/// Enforces 4 properties for broadcast communication:
/// 1. Validity - if pi and pj are correct, then every message broadcast by pi
/// 	 is eventually delivered to pj
/// 2. No duplication - no message is delivered more than once
/// 3. No creation - no message is delivered unless it was broadcast
/// 4. Uniform agreement - if any process delivers m, then all correct processes
///    eventually deliver m
class UniformReliableBroadcast {
 public:
  UniformReliableBroadcast(
      const PerfectLink::ProcessIdType id,
      const BestEffortBroadcast::AvailableProcesses processes);

  /// @brief Binds this broadcast link to a host and port. Once done cannot be
  /// done again.
  auto bind(const in_addr_t host, const in_port_t port) -> void;

  /// @brief Starts listening to incoming broadcast messages. Sends ACKs for new
  /// messages. Receives ACKs and resends messages with missing ACKs. Thread
  /// safe.
  /// @param callback Function that will be called when a message is delivered.
  auto listen(PerfectLink::ListenCallback callback) -> void;

  /// @brief Broadcasts a message to all processes. The data has to be smaller
  /// than about 64KiB. Sending is possible only after performing a bind. At
  /// most 8 messages can be packed in a single packet.
  template <typename... Data,
            class = std::enable_if_t<
                are_equal<PerfectLink::MessageData, Data...>::value>,
            class = std::enable_if_t<
                (sizeof...(Data) <= PerfectLink::MAX_MESSAGE_COUNT_IN_PACKET)>>
  auto broadcast(Data... datas) -> void;

  /// @brief Id of this process.
  inline auto id() const -> PerfectLink::ProcessIdType { return _link.id(); }

 private:
  /// @brief A broadcasted message is identified by its source process and a
  /// message ID for that process. Together they fit in a 64bit integer.
  using MessageIdType = std::uint64_t;
  static_assert(sizeof(MessageIdType) >=
                sizeof(PerfectLink::ProcessIdType) +
                    sizeof(PerfectLink::MessageIdType));

  BestEffortBroadcast _link;
  // TODO: find a way to garbage collect
  /// @brief Messages that have been acknowledges. Acknowledgement is indicated
  /// by a set bit in the bitset.
  std::unordered_map<MessageIdType, std::bitset<PerfectLink::MAX_PROCESSES>>
      _acknowledged;
  // TODO: find a way to garbage collect
  /// @brief Messages pending for delivery. Cannot be yet delivered because of
  /// missing acknowledgements. The actual message is not stored here. Together
  /// with an ack we will receive the message, we can use that to deliver.
  std::unordered_set<MessageIdType> _pending;

  /// @brief Current sequence number of messages.
  PerfectLink::MessageIdType _seq_nr = 1;
};

template <typename... Data, class, class>
auto UniformReliableBroadcast::broadcast(Data... datas) -> void {
  MessageIdType message_id = 0;
  std::array<std::uint8_t, sizeof(MessageIdType)> message_id_data;

  for (size_t i = 0; i < sizeof(PerfectLink::ProcessIdType); i++) {
    message_id |= static_cast<MessageIdType>(id() & (0xff << (8 * i)));
    message_id_data[i] = (id() >> (i * 8)) & 0xff;
  }
  for (size_t i = 0; i < sizeof(PerfectLink::MessageIdType); i++) {
    message_id |= (_seq_nr & static_cast<MessageIdType>(0xff << (8 * i)))
                  << (8 * sizeof(PerfectLink::ProcessIdType));
    message_id_data[i + sizeof(PerfectLink::ProcessIdType)] =
        (_seq_nr >> (i * 8)) & 0xff;
  }

  // TODO: protect with mutex
  _pending.insert(message_id);
  _link.broadcast(
      std::make_tuple(message_id_data.data(), message_id_data.size()),
      datas...);

  _seq_nr += 1;
}
