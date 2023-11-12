#include "uniform_reliable_broadcast.hpp"

UniformReliableBroadcast::UniformReliableBroadcast(
    const PerfectLink::ProcessIdType id,
    const BestEffortBroadcast::AvailableProcesses processes)
    : _link(id, processes) {}

auto UniformReliableBroadcast::bind(const in_addr_t host, const in_port_t port)
    -> void {
  _link.bind(host, port);
}
