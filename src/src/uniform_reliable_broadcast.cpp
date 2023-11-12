#include "uniform_reliable_broadcast.hpp"

UniformReliableBroadcast::UniformReliableBroadcast(
    const PerfectLink::ProcessIdType id,
    const std::vector<std::tuple<in_addr_t, in_port_t>> processes)
    : _link(id, processes) {}

auto UniformReliableBroadcast::bind(const in_addr_t host, const in_port_t port)
    -> void {
  _link.bind(host, port);
}
