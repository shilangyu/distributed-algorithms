#pragma once

#include <netinet/in.h>
#include <optional>
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
  struct ProcessAddress {
    in_addr_t host;
    in_port_t port;
  };

  using AvailableProcesses =
      std::unordered_map<PerfectLink::ProcessIdType, ProcessAddress>;

  BestEffortBroadcast(const PerfectLink::ProcessIdType id,
                      const AvailableProcesses processes);

  /// @brief Binds this broadcast link to a host and port. Once done cannot be
  /// done again.
  auto bind(const in_addr_t host, const in_port_t port) -> void;

  /// @brief Starts listening to incoming broadcast messages. Sends ACKs for new
  /// messages. Receives ACKs and resends messages with missing ACKs. Thread
  /// safe.
  /// @param callback Function that will be called when a message is delivered.
  auto listen(PerfectLink::ListenCallback callback) -> void;

  /// @brief Same as `listen` but receives many messages at once if the send was
  /// batched. This will also recover metadata.
  auto listen_batch(PerfectLink::ListenBatchCallback callback) -> void;

  /// @brief Broadcasts a message to all processes. The data has to be smaller
  /// than about 64KiB. Sending is possible only after performing a bind. At
  /// most 8 messages can be packed in a single packet. Thread safe.
  template <typename... Data,
            class = std::enable_if_t<
                are_equal<PerfectLink::MessageData, Data...>::value>,
            class = std::enable_if_t<
                (sizeof...(Data) <= PerfectLink::MAX_MESSAGE_COUNT_IN_PACKET)>>
  auto broadcast(const std::optional<PerfectLink::MessageData> metadata,
                 Data... datas) -> void;

  /// @brief Sending a message to a single host.
  template <typename... Data,
            class = std::enable_if_t<
                are_equal<PerfectLink::MessageData, Data...>::value>,
            class = std::enable_if_t<
                (sizeof...(Data) <= PerfectLink::MAX_MESSAGE_COUNT_IN_PACKET)>>
  auto send(const in_addr_t host,
            const in_port_t port,
            const std::optional<PerfectLink::MessageData> metadata,
            Data... datas) -> void;

  /// @brief A list of processes this broadcast link knowns.
  auto processes() const -> const AvailableProcesses&;

  /// @brief Id of this process.
  inline auto id() const -> PerfectLink::ProcessIdType { return _link.id(); }

 private:
  PerfectLink _link;
  const AvailableProcesses _processes;
};

template <typename... Data, class, class>
auto BestEffortBroadcast::broadcast(
    const std::optional<PerfectLink::MessageData> metadata,
    Data... datas) -> void {
  for (const auto& [_, address] : _processes) {
    _link.send(address.host, address.port, metadata, datas...);
  }
}

template <typename... Data, class, class>
auto BestEffortBroadcast::send(
    const in_addr_t host,
    const in_port_t port,
    const std::optional<PerfectLink::MessageData> metadata,
    Data... datas) -> void {
  _link.send(host, port, metadata, datas...);
}
