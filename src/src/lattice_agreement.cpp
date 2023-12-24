#include "lattice_agreement.hpp"
#include <array>
#include <cassert>

LatticeAgreement::LatticeAgreement(
    const PerfectLink::ProcessIdType id,
    const BestEffortBroadcast::AvailableProcesses processes,
    const std::size_t max_unique_values,
    ListenCallback callback)
    : _max_unique_values(max_unique_values),
      _link(id, processes),
      _callback(callback) {}

auto LatticeAgreement::bind(const in_addr_t host, const in_port_t port)
    -> void {
  _link.bind(host, port);
}

auto LatticeAgreement::propose(const std::vector<AgreementType>& values)
    -> void {
  _send_semaphore.acquire();

  std::lock_guard<std::mutex> lock(_agreements_mutex);

  auto& agreement = _agreements.try_emplace(_agreement_nr).first->second;
  agreement.proposed_value.insert(values.begin(), values.end());

  // we have the full set, no need to propose
  if (agreement.proposed_value.size() == _max_unique_values) {
    _decide(agreement);
  } else {
    _broadcast_proposal(agreement, _agreement_nr);
  }

  _agreement_nr += 1;
}

auto LatticeAgreement::listen() -> void {
  _link.listen([&](auto process_id, auto& data) {
    std::size_t offset = 0;

    auto message_kind = static_cast<MessageKind>(data[offset++]);

    PerfectLink::MessageIdType agreement_nr = 0;
    for (size_t i = 0; i < sizeof(PerfectLink::MessageIdType); i++) {
      agreement_nr |= static_cast<PerfectLink::MessageIdType>(data[offset++])
                      << (8 * i);
    }

    ProposalNumberType proposal_nr = 0;
    for (size_t i = 0; i < sizeof(ProposalNumberType); i++) {
      proposal_nr |= static_cast<ProposalNumberType>(data[offset++]) << (8 * i);
    }

    switch (message_kind) {
      case MessageKind::Proposal:
        _handle_proposal(process_id, agreement_nr, proposal_nr,
                         data.subslice(offset));
        break;
      case MessageKind::Ack:
        _handle_ack(agreement_nr, proposal_nr);
        break;
      case MessageKind::Nack:
        _handle_nack(agreement_nr, proposal_nr, data.subslice(offset));
        break;

      default:
        // poor man's std::unreachable();
        assert(false);
        break;
    }
  });
}

auto LatticeAgreement::_handle_proposal(
    const PerfectLink::ProcessIdType process_id,
    const PerfectLink::MessageIdType agreement_nr,
    const ProposalNumberType proposal_nr,
    const OwnedSlice<std::uint8_t>& message) -> void {
  std::array<std::uint8_t, PerfectLink::MAX_MESSAGE_SIZE> data;
  std::size_t size = 0;
  // if any of the values are outside of our current proposal, this
  // becomes a nack
  data[size++] = static_cast<std::uint8_t>(MessageKind::Ack);
  for (size_t i = 0; i < sizeof(agreement_nr); i++) {
    data[size++] = (agreement_nr >> (8 * i)) & 0xff;
  }
  for (size_t i = 0; i < sizeof(proposal_nr); i++) {
    data[size++] = (proposal_nr >> (8 * i)) & 0xff;
  }

  std::lock_guard<std::mutex> lock(_agreements_mutex);

  // might be the first time we see this agreement
  auto& agreement = _agreements.try_emplace(agreement_nr).first->second;

  assert(message.size() / sizeof(AgreementType) * sizeof(AgreementType) ==
         message.size());
  // read proposal values
  std::unordered_set<AgreementType> difference = agreement.accepted_value;
  std::size_t offset = 0;
  while (offset < message.size()) {
    AgreementType value = 0;
    for (size_t i = 0; i < sizeof(value); i++) {
      value |= static_cast<AgreementType>(message[offset++]) << (8 * i);
    }
    difference.erase(value);
    agreement.accepted_value.insert(value);
  }

  // we have values that the proposer does not, switch to sending a nack
  if (!difference.empty()) {
    data[0] = static_cast<std::uint8_t>(MessageKind::Nack);
    // send only the difference
    for (auto value : difference) {
      for (size_t i = 0; i < sizeof(value); i++) {
        data[size++] = (value >> (8 * i)) & 0xff;
      }
    }
  }

  // find who to send to a response
  auto& processes = _link.processes();
  auto target = processes.find(process_id);
  assert(target != processes.end());

  _link.send(target->second.host, target->second.port, std::nullopt,
             std::make_tuple(data.data(), size));
}

auto LatticeAgreement::_handle_ack(
    const PerfectLink::MessageIdType agreement_nr,
    const ProposalNumberType proposal_nr) -> void {
  std::lock_guard<std::mutex> lock(_agreements_mutex);

  auto agreement_entry = _agreements.find(agreement_nr);
  // got an ack, so we had to start this agreement already
  assert(agreement_entry != _agreements.end());
  auto& agreement = agreement_entry->second;

  // if has already decided or the proposal number does not match then we don't
  // care about the nack
  if (agreement.has_decided || agreement.proposal_nr != proposal_nr) {
    return;
  }

  agreement.ack_count++;

  // check if we can decide immediately
  if (2 * static_cast<std::size_t>(agreement.ack_count) >=
      _link.processes().size()) {
    _decide(agreement);
    return;
  }

  _check_nacks(agreement, agreement_nr);
}

auto LatticeAgreement::_handle_nack(
    const PerfectLink::MessageIdType agreement_nr,
    const ProposalNumberType proposal_nr,
    const OwnedSlice<std::uint8_t>& message) -> void {
  std::lock_guard<std::mutex> lock(_agreements_mutex);

  auto agreement_entry = _agreements.find(agreement_nr);
  // got an nack, so we had to start this agreement already
  assert(agreement_entry != _agreements.end());
  auto& agreement = agreement_entry->second;

  // if has already decided or the proposal number does not match then we don't
  // care about the nack
  if (agreement.has_decided || agreement.proposal_nr != proposal_nr) {
    return;
  }

  assert(message.size() / sizeof(AgreementType) * sizeof(AgreementType) ==
         message.size());
  // read difference set values
  std::size_t offset = 0;
  while (offset < message.size()) {
    AgreementType value = 0;
    for (size_t i = 0; i < sizeof(value); i++) {
      value |= static_cast<AgreementType>(message[offset++]) << (8 * i);
    }
    agreement.proposed_value.insert(value);
  }

  agreement.nack_count++;

  // we have the full set, no need to check nacks
  if (agreement.proposed_value.size() == _max_unique_values) {
    _decide(agreement);
  } else {
    _check_nacks(agreement, agreement_nr);
  }
}

auto LatticeAgreement::_check_nacks(
    Agreement& agreement,
    const PerfectLink::MessageIdType agreement_nr) -> void {
  if (2 * (static_cast<std::size_t>(agreement.ack_count) +
           static_cast<std::size_t>(agreement.nack_count)) >=
      _link.processes().size()) {
    // we have to start a new proposal
    agreement.proposal_nr += 1;
    agreement.ack_count = 0;
    agreement.nack_count = 0;
    _broadcast_proposal(agreement, agreement_nr);
  }
}

auto LatticeAgreement::_broadcast_proposal(
    Agreement& agreement,
    PerfectLink::MessageIdType agreement_nr) -> void {
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

auto LatticeAgreement::_decide(Agreement& agreement) -> void {
  _callback(agreement.proposed_value);
  agreement.has_decided = true;
  // if we decided the full set, we remember this set in accepted_value. Then,
  // when a different process sends us their proposal, we can immediately give
  // them the full set.
  if (agreement.proposed_value.size() == _max_unique_values) {
    agreement.accepted_value.insert(agreement.proposed_value.begin(),
                                    agreement.proposed_value.end());
  }
  _send_semaphore.release();
}
