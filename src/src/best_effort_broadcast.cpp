#include "best_effort_broadcast.hpp"
#include "perfect_link.hpp"

BestEffortBroadcast::BestEffortBroadcast(
    const PerfectLink::ProcessIdType id,
    const std::vector<std::tuple<in_addr_t, in_port_t>> processes)
    : _link(id), _processes(processes) {}

auto BestEffortBroadcast::bind(const in_addr_t host, const in_port_t port)
    -> void {
  _link.bind(host, port);
}

auto BestEffortBroadcast::listen(PerfectLink::ListenCallback callback) -> void {
  _link.listen(callback);
}

auto BestEffortBroadcast::processes() const
    -> const std::vector<std::tuple<in_addr_t, in_port_t>>& {
  return _processes;
}
