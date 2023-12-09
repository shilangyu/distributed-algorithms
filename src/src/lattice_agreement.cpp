#include "lattice_agreement.hpp"
#include <array>
#include <cassert>

LatticeAgreement::LatticeAgreement(
    const PerfectLink::ProcessIdType id,
    const BestEffortBroadcast::AvailableProcesses processes)
    : _link(id, processes) {}

auto LatticeAgreement::bind(const in_addr_t host, const in_port_t port)
    -> void {
  _link.bind(host, port);
}

auto LatticeAgreement::propose(const std::vector<AgreementType>& values)
    -> void {
  // TODO: consider moving semaphore after all data for sending is prepared.
  // This is quite annoying to pull off
  _send_semaphore.acquire();

  std::lock_guard<std::mutex> lock(_agreements_mutex);

  auto& agreement = _agreements.try_emplace(_agreement_nr).first->second;
  _broadcast_proposal(agreement, _agreement_nr, values.begin(), values.end());
  _agreement_nr += 1;
}

auto LatticeAgreement::listen(ListenCallback callback) -> void {
  // TODO: check if templating the lambda helps performance
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
        _handle_ack(agreement_nr, proposal_nr, callback);
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

  // TODO: it is possible to do without allocations, but it is quite involved:
  // - the proposal would have to be given in a sorted order
  // - you can take compute the set difference in a yielding way (see
  //   std::set_difference)
  // TODO: test how things will perform when using std::set

  std::lock_guard<std::mutex> lock(_agreements_mutex);

  // might be the first time we see this agreement/proposal
  auto& agreement = _agreements.try_emplace(agreement_nr).first->second;
  auto& proposal =
      agreement.proposals.try_emplace(agreement.proposal_nr).first->second;

  assert(message.size() / sizeof(AgreementType) * sizeof(AgreementType) ==
         message.size());
  // read proposal values
  std::unordered_set<AgreementType> difference = proposal.proposed_value;
  std::size_t offset = 0;
  while (offset < message.size()) {
    AgreementType value = 0;
    for (size_t i = 0; i < sizeof(value); i++) {
      value |= static_cast<AgreementType>(message[offset++]) << (8 * i);
    }
    difference.erase(value);
    proposal.proposed_value.insert(value);
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
    const ProposalNumberType proposal_nr,
    ListenCallback callback) -> void {
  std::lock_guard<std::mutex> lock(_agreements_mutex);

  auto agreement_entry = _agreements.find(agreement_nr);
  // got an ack, so we had to start this agreement already
  assert(agreement_entry != _agreements.end());
  auto& agreement = agreement_entry->second;

  // if has already decided we don't care about the ack
  if (agreement.has_decided) {
    return;
  }

  auto proposal_entry = agreement.proposals.find(proposal_nr);
  // got an ack, so we had to start this proposal already
  assert(proposal_entry != agreement.proposals.end());
  auto& proposal = proposal_entry->second;

  proposal.ack_count++;

  // check if we can decide immediately
  if (2 * static_cast<std::size_t>(proposal.ack_count) >=
      _link.processes().size()) {
    callback(proposal.proposed_value);
    agreement.has_decided = true;
    _send_semaphore.release();
    return;
  }

  _check_nacks(agreement, agreement_nr, proposal);
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

  // if has already decided we don't care about the nack
  if (agreement.has_decided) {
    return;
  }

  auto proposal_entry = agreement.proposals.find(proposal_nr);
  // got an nack, so we had to start this proposal already
  assert(proposal_entry != agreement.proposals.end());
  auto& proposal = proposal_entry->second;

  assert(message.size() / sizeof(AgreementType) * sizeof(AgreementType) ==
         message.size());
  // read difference set values
  std::size_t offset = 0;
  while (offset < message.size()) {
    AgreementType value = 0;
    for (size_t i = 0; i < sizeof(value); i++) {
      value |= static_cast<AgreementType>(message[offset++]) << (8 * i);
    }
    proposal.proposed_value.insert(value);
  }

  proposal.nack_count++;

  _check_nacks(agreement, agreement_nr, proposal);
}

auto LatticeAgreement::_check_nacks(
    Agreement& agreement,
    const PerfectLink::MessageIdType agreement_nr,
    const Agreement::Proposal& proposal) -> void {
  if (2 * (static_cast<std::size_t>(proposal.ack_count) +
           static_cast<std::size_t>(proposal.nack_count)) >=
      _link.processes().size()) {
    // we have to start a new proposal
    agreement.proposal_nr += 1;
    _broadcast_proposal(agreement, agreement_nr,
                        proposal.proposed_value.begin(),
                        proposal.proposed_value.end());
  }
}
