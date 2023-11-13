#include "uniform_reliable_broadcast.hpp"
#include <cassert>
#include <limits>

UniformReliableBroadcast::UniformReliableBroadcast(
    const PerfectLink::ProcessIdType id,
    const BestEffortBroadcast::AvailableProcesses processes)
    : _link(id, processes) {}

auto UniformReliableBroadcast::bind(const in_addr_t host, const in_port_t port)
    -> void {
  _link.bind(host, port);
}

auto UniformReliableBroadcast::listen(PerfectLink::ListenCallback callback)
    -> void {
  _link.listen_batch([&](auto process_id, auto& metadata, auto& datas) {
    MessageIdType message_id = 0;
    for (size_t i = 0; i < sizeof(MessageIdType); i++) {
      message_id |= static_cast<MessageIdType>(metadata[i]) << (8 * i);
    }

    // TODO: protect with mutex
    // mark that process_id has received this message
    auto acks = _acknowledged.try_emplace(message_id).first->second;
    assert(!acks[process_id] && "Aready acked");
    acks[process_id] = true;

    // check if majority has acked, if so, we can deliver. We don't need to keep
    // track of a delivered structure: the moment where we reach majority will
    // happen only once due to the no duplication property.
    auto should_deliver = acks.count() == (_link.processes().size() / 2 + 1);

    if (should_deliver) {
      PerfectLink::ProcessIdType author_id =
          message_id & std::numeric_limits<PerfectLink::ProcessIdType>::max();
      for (auto& data : datas) {
        OwnedSlice owned = data;
        callback(author_id, owned);
      }
    }

    // if we have not yet received this message from that process, note and
    // broadcast
    if (_pending.find(message_id) == _pending.end()) {
      _pending.insert(message_id);
      // paying for the stupid decision of compile-time known datas amount...
      switch (datas.size()) {
        case 0:
          _link.broadcast(metadata.unsafe_raw());
          break;
        case 1:
          _link.broadcast(metadata.unsafe_raw(), datas[0].unsafe_raw());
          break;
        case 2:
          _link.broadcast(metadata.unsafe_raw(), datas[0].unsafe_raw(),
                          datas[1].unsafe_raw());
          break;
        case 3:
          _link.broadcast(metadata.unsafe_raw(), datas[0].unsafe_raw(),
                          datas[1].unsafe_raw(), datas[2].unsafe_raw());
          break;
        case 4:
          _link.broadcast(metadata.unsafe_raw(), datas[0].unsafe_raw(),
                          datas[1].unsafe_raw(), datas[2].unsafe_raw(),
                          datas[3].unsafe_raw());
          break;
        case 5:
          _link.broadcast(metadata.unsafe_raw(), datas[0].unsafe_raw(),
                          datas[1].unsafe_raw(), datas[2].unsafe_raw(),
                          datas[3].unsafe_raw(), datas[4].unsafe_raw());
          break;
        case 6:
          _link.broadcast(metadata.unsafe_raw(), datas[0].unsafe_raw(),
                          datas[1].unsafe_raw(), datas[2].unsafe_raw(),
                          datas[3].unsafe_raw(), datas[4].unsafe_raw(),
                          datas[5].unsafe_raw());
          break;
        case 7:
          _link.broadcast(metadata.unsafe_raw(), datas[0].unsafe_raw(),
                          datas[1].unsafe_raw(), datas[2].unsafe_raw(),
                          datas[3].unsafe_raw(), datas[4].unsafe_raw(),
                          datas[5].unsafe_raw(), datas[6].unsafe_raw());
          break;
        case 8:
          _link.broadcast(metadata.unsafe_raw(), datas[0].unsafe_raw(),
                          datas[1].unsafe_raw(), datas[2].unsafe_raw(),
                          datas[3].unsafe_raw(), datas[4].unsafe_raw(),
                          datas[5].unsafe_raw(), datas[6].unsafe_raw(),
                          datas[7].unsafe_raw());
          break;
          static_assert(PerfectLink::MAX_MESSAGE_COUNT_IN_PACKET == 8);
      }
    }
  });
}
