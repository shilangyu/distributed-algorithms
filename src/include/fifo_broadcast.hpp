#include <queue>
#include "best_effort_broadcast.hpp"
#include "perfect_link.hpp"
#include "uniform_reliable_broadcast.hpp"

/// Filters delivers to make sure they are FIFO. Specialized for the data we use
/// to save needless allocations.
struct FifoBroadcast {
  FifoBroadcast(const PerfectLink::ProcessIdType id,
                const BestEffortBroadcast::AvailableProcesses processes)
      : _link(id, processes) {}

  using SendType = std::uint32_t;

  using ListenCallback = std::function<
      auto(PerfectLink::ProcessIdType process_id, SendType msg)->void>;

  auto bind(const in_addr_t host, const in_port_t port) -> void {
    _link.bind(host, port);
  }

  template <typename... Data,
            class = std::enable_if_t<
                are_equal<PerfectLink::MessageData, Data...>::value>,
            class = std::enable_if_t<
                (sizeof...(Data) <= PerfectLink::MAX_MESSAGE_COUNT_IN_PACKET)>>
  auto broadcast(Data... datas) -> void {
    _link.broadcast(datas...);
  }

  /// @brief NOT thread safe.
  auto listen(ListenCallback callback) -> void {
    _link.listen([&](auto process_id, auto seq_nr, auto& data) {
      SendType msg = 0;
      for (size_t i = 0; i < sizeof(SendType); i++) {
        msg |= static_cast<SendType>(data[i]) << (i * 8);
      }

      auto& buffer = _buffered[process_id - 1];

      if (buffer.next_seq_nr == seq_nr) {
        callback(process_id, msg);
        buffer.next_seq_nr += 1;
        // deliver all next messages
        for (; !buffer.buffer.empty(); buffer.buffer.pop()) {
          auto [top_seq_nr, top_msg] = buffer.buffer.top();
          if (top_seq_nr != buffer.next_seq_nr) {
            break;
          }
          callback(process_id, top_msg);
          buffer.next_seq_nr += 1;
        }
      } else {
        buffer.buffer.emplace(seq_nr, msg);
      }
    });
  }

 private:
  struct BufferedMessages {
    struct BufferedMessage {
      BufferedMessage(const PerfectLink::MessageIdType seq_nr,
                      const SendType msg)
          : seq_nr(seq_nr), msg(msg) {}
      PerfectLink::MessageIdType seq_nr;
      SendType msg;

      friend auto operator<(BufferedMessage const& left,
                            BufferedMessage const& right) -> bool {
        return left.seq_nr > right.seq_nr;
      }
    };

    // min heap
    std::priority_queue<BufferedMessage> buffer;
    PerfectLink::MessageIdType next_seq_nr =
        UniformReliableBroadcast::INITIAL_SEQ_NR;
  };
  UniformReliableBroadcast _link;
  std::array<BufferedMessages, PerfectLink::MAX_PROCESSES> _buffered;
};
