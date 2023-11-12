#pragma once

#include <netinet/in.h>
#include <tuple>
#include <vector>
#include "best_effort_broadcast.hpp"
#include "perfect_link.hpp"

/// Enforces 3 properties for broadcast communication:
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
  template <
      typename... Data,
      class = std::enable_if_t<
          are_equal<std::tuple<std::uint8_t*, std::size_t>, Data...>::value>,
      class = std::enable_if_t<(sizeof...(Data) <=
                                PerfectLink::MAX_MESSAGE_COUNT_IN_PACKET)>>
  auto broadcast(Data... datas) -> void;

  /// @brief Id of this process.
  inline auto id() const -> PerfectLink::ProcessIdType;

 private:
  BestEffortBroadcast _link;
};
