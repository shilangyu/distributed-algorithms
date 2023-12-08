#pragma once

#include <netinet/in.h>
#include <unordered_set>
#include <vector>
#include "best_effort_broadcast.hpp"
#include "perfect_link.hpp"
#include "semaphore.hpp"

/// Enforces 3 properties for agreement:
/// 1. Validity - let a process Pi decide the set Oi. Then:
///    - Ii is a subset of Oi
///    - Oi is a subset of the union of all Ij
/// 2. Consistency - Oi is a subset of Oj or Oj is a subset of Oi
/// 3. Termination - Every correct process eventually decides
class LatticeAgreement {
 public:
  LatticeAgreement(const PerfectLink::ProcessIdType id,
                   const BestEffortBroadcast::AvailableProcesses processes);

  using AgreementType = std::uint32_t;

  using ListenCallback =
      std::function<auto(const std::unordered_set<AgreementType>& data)->void>;

  /// @brief Binds this agreement link to a host and port. Once done cannot be
  /// done again.
  auto bind(const in_addr_t host, const in_port_t port) -> void;

  /// @brief Starts listening to decided sets. Sends ACKs for new
  /// messages. Receives ACKs and resends messages with missing ACKs.
  /// @param callback Function that will be called when a set is decided.
  auto listen(ListenCallback callback) -> void;

  /// @brief Starts a new agreement with the proposed values. Assumes given
  /// values are unique.
  auto propose(const std::vector<AgreementType>& values) -> void;

  /// @brief Id of this process.
  inline auto id() const -> PerfectLink::ProcessIdType { return _link.id(); }

 private:
  /// @brief Amount of in-flight agreements of this process.
  static constexpr std::size_t MAX_IN_FLIGHT = 1;

  BestEffortBroadcast _link;

  /// @brief The current agreement number used to differentiate agreements.
  PerfectLink::MessageIdType _agreement_nr = 0;

  Semaphore _send_semaphore{MAX_IN_FLIGHT};
};
