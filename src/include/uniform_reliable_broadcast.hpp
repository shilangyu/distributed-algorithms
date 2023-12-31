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
#include "semaphore.hpp"

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

  using ListenCallback =
      std::function<auto(PerfectLink::ProcessIdType process_id,
                         PerfectLink::MessageIdType seq_nr,
                         OwnedSlice<std::uint8_t>& data)
                        ->void>;

  /// @brief Binds this broadcast link to a host and port. Once done cannot be
  /// done again.
  auto bind(const in_addr_t host, const in_port_t port) -> void;

  /// @brief Starts listening to incoming broadcast messages. Sends ACKs for new
  /// messages. Receives ACKs and resends messages with missing ACKs. Thread
  /// safe.
  /// @param callback Function that will be called when a message is delivered.
  auto listen(ListenCallback callback) -> void;

  /// @brief Broadcasts a message to all processes. The data has to be smaller
  /// than about 64KiB. Sending is possible only after performing a bind. At
  /// most 8 messages can be packed in a single packet. Thread safe.
  template <typename... Data,
            class = std::enable_if_t<
                are_equal<PerfectLink::MessageData, Data...>::value>,
            class = std::enable_if_t<
                (sizeof...(Data) <= PerfectLink::MAX_MESSAGE_COUNT_IN_PACKET)>>
  auto broadcast(Data... datas) -> void;

  /// @brief Id of this process.
  inline auto id() const -> PerfectLink::ProcessIdType { return _link.id(); }

  static constexpr PerfectLink::MessageIdType INITIAL_SEQ_NR = 1;

 private:
  /// @brief Amount of in-flight broadcast messages of this process.
  static constexpr std::size_t MAX_IN_FLIGHT = 1;

  /// @brief A broadcasted message is identified by its source process and a
  /// message ID for that process. Together they fit in a 64bit integer.
  using MessageIdType = std::uint64_t;
  static_assert(sizeof(MessageIdType) >=
                sizeof(PerfectLink::ProcessIdType) +
                    sizeof(PerfectLink::MessageIdType));

  BestEffortBroadcast _link;
  /// @brief Messages that have been acknowledges. Acknowledgement is indicated
  /// by a set bit in the bitset. If a map entry exists, then this message is
  /// pending for delivery. Once enough acks are collected the message is
  /// delivered. The actual message is not stored here. Together with an ack we
  /// will receive the message, we can use that to deliver.
  std::unordered_map<MessageIdType, std::bitset<PerfectLink::MAX_PROCESSES>>
      _acknowledged;
  std::mutex _acknowledged_mutex;

  /// @brief Current sequence number of messages.
  PerfectLink::MessageIdType _seq_nr = INITIAL_SEQ_NR;

  Semaphore _send_semaphore{MAX_IN_FLIGHT};
};

template <typename... Data, class, class>
auto UniformReliableBroadcast::broadcast(Data... datas) -> void {
  MessageIdType message_id = 0;
  std::array<std::uint8_t, sizeof(MessageIdType)> message_id_data;

  for (size_t i = 0; i < sizeof(PerfectLink::ProcessIdType); i++) {
    message_id |= static_cast<MessageIdType>(id() & (0xff << (8 * i)));
    message_id_data[i] = static_cast<std::uint8_t>((id() >> (i * 8)) & 0xff);
  }
  for (size_t i = 0; i < sizeof(PerfectLink::MessageIdType); i++) {
    message_id |= (_seq_nr & static_cast<MessageIdType>(0xff << (8 * i)))
                  << (8 * sizeof(PerfectLink::ProcessIdType));
    message_id_data[i + sizeof(PerfectLink::ProcessIdType)] =
        (_seq_nr >> (i * 8)) & 0xff;
  }

  _send_semaphore.acquire();

  {
    std::lock_guard lock(_acknowledged_mutex);
    // add map entry to indicate this message is pending
    _acknowledged.try_emplace(message_id);
    _seq_nr += static_cast<PerfectLink::MessageIdType>(sizeof...(Data));
  }

  _link.broadcast(
      std::make_tuple(message_id_data.data(), message_id_data.size()),
      datas...);
}
