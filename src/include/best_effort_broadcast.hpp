#pragma once

#include <netinet/in.h>
#include <tuple>
#include <vector>
#include "perfect_link.hpp"

/// Enforces 3 properties for broadcast communication:
/// 1. Validity - if pi and pj are correct, then every message broadcast by pi
/// 	 is eventually delivered to pj
/// 2. No duplication - no message is delivered more than once
/// 3. No creation - no message is delivered unless it was broadcast
class BestEffortBroadcast {
 public:
  BestEffortBroadcast(
      const PerfectLink::ProcessIdType id,
      const std::vector<std::tuple<in_addr_t, in_port_t>> processes);

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
  auto send(Data... datas) -> void;

 private:
  PerfectLink _link;
  const std::vector<std::tuple<in_addr_t, in_port_t>> _processes;
};

template <typename... Data, class, class>
auto BestEffortBroadcast::send(Data... datas) -> void {
  for (auto& [host, port] : _processes) {
    _link.send(host, port, datas...);
  }
}
