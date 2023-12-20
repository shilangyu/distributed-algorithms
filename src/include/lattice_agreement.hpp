#pragma once

#include <netinet/in.h>
#include <mutex>
#include <unordered_map>
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
  /// @brief Type used to encode individual proposal rounds in a single
  /// agreement.
  using ProposalNumberType = std::uint32_t;

  struct Agreement {
    PerfectLink::ProcessIdType ack_count = 0;
    PerfectLink::ProcessIdType nack_count = 0;
    std::unordered_set<AgreementType> proposed_value;
    std::unordered_set<AgreementType> accepted_value;

    ProposalNumberType proposal_nr = 0;
    bool has_decided = false;
  };

  /// @brief Handles incoming proposals.
  auto _handle_proposal(const PerfectLink::ProcessIdType process_id,
                        const PerfectLink::MessageIdType agreement_nr,
                        const ProposalNumberType proposal_nr,
                        const OwnedSlice<std::uint8_t>& data) -> void;

  /// @brief Handles incoming ACKs.
  auto _handle_ack(const PerfectLink::MessageIdType agreement_nr,
                   const ProposalNumberType proposal_nr,
                   ListenCallback callback) -> void;

  /// @brief Handles incoming NACKs.
  auto _handle_nack(const PerfectLink::MessageIdType agreement_nr,
                    const ProposalNumberType proposal_nr,
                    const OwnedSlice<std::uint8_t>& data) -> void;

  /// @brief Check if the accumulated acks/nacks warrant a new proposal.
  /// @return
  auto _check_nacks(Agreement& agreement,
                    const PerfectLink::MessageIdType agreement_nr) -> void;

  template <class Iter>
  auto _broadcast_proposal(Agreement& agreement,
                           PerfectLink::MessageIdType agreement_nr,
                           Iter begin,
                           Iter end) -> void;

  /// @brief Amount of in-flight agreements of this process.
  static constexpr std::size_t MAX_IN_FLIGHT = 1;

  enum class MessageKind : std::uint8_t {
    Proposal = 0,
    Ack = 1,
    Nack = 2,
  };

  BestEffortBroadcast _link;

  /// @brief The current agreement number used to differentiate agreements.
  PerfectLink::MessageIdType _agreement_nr = 0;

  Semaphore _send_semaphore{MAX_IN_FLIGHT};

  // TODO: consider storing it in a dense representation (vector)
  std::unordered_map<PerfectLink::MessageIdType, Agreement> _agreements;
  std::mutex _agreements_mutex;
};

template <class Iter>
auto LatticeAgreement::_broadcast_proposal(
    Agreement& agreement,
    PerfectLink::MessageIdType agreement_nr,
    Iter begin,
    Iter end) -> void {
  agreement.proposed_value.insert(begin, end);

  std::array<std::uint8_t, PerfectLink::MAX_MESSAGE_SIZE> data;
  std::size_t size = 0;

  data[size++] = static_cast<std::uint8_t>(MessageKind::Proposal);

  for (size_t i = 0; i < sizeof(agreement_nr); i++) {
    data[size++] = (agreement_nr >> (8 * i)) & 0xff;
  }

  for (size_t i = 0; i < sizeof(agreement.proposal_nr); i++) {
    data[size++] = (agreement.proposal_nr >> (8 * i)) & 0xff;
  }

  // make sure we can fit the message
  assert(size + agreement.proposed_value.size() * sizeof(AgreementType) <
         data.size());

  for (auto& value : agreement.proposed_value) {
    for (size_t i = 0; i < sizeof(value); i++) {
      data[size++] = (value >> (8 * i)) & 0xff;
    }
  }

  _link.broadcast(std::nullopt, std::make_tuple(data.data(), size));
}
