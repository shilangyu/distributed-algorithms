#include "lattice_agreement.hpp"

LatticeAgreement::LatticeAgreement(
    const PerfectLink::ProcessIdType id,
    const BestEffortBroadcast::AvailableProcesses processes)
    : _link(id, processes) {}

auto LatticeAgreement::bind(const in_addr_t host, const in_port_t port)
    -> void {
  _link.bind(host, port);
}

auto LatticeAgreement::listen(ListenCallback callback) -> void {}

auto LatticeAgreement::propose(const std::vector<AgreementType>& values)
    -> void {}
