#include <array>
#include <chrono>
#include <csignal>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
#include "common.hpp"
#include "parser.hpp"
#include "perfect_link.hpp"
#include "uniform_reliable_broadcast.hpp"

using SendType = std::uint32_t;
using Delivered = std::tuple<PerfectLink::ProcessIdType, SendType>;

struct Logger {
  inline auto reserve_delivered_memory(const std::size_t bytes) -> void {
    _delivered_buffer.reserve(bytes / sizeof(Delivered));
  }

  inline auto deliver(PerfectLink::ProcessIdType process_id, SendType msg) {
    std::lock_guard<std::mutex> lock(_mutex);
    _delivered_buffer.emplace_back(process_id, msg);
    // UB: we might be interrupted during a write. Then, we are in a very bad
    // state. In practice, we were promised that logs won't be larger than
    // 16MiB, so this write should never happen. Additionally getting
    // interrupted during a write is highly unlikely, as a write happend about
    // once every 2 million deliveries.
    if (_delivered_buffer.capacity() == _delivered_buffer.size()) {
      // we are at full capacity, flush the buffer to the file
      write();
      _delivered_buffer.clear();
      _delivered_size = 0;
    } else {
      _delivered_size.fetch_add(1);
    }
  }

  inline auto write() -> void {
    if (_output.is_open()) {
      const auto& [sent_amount, delivered_size] = _frozen.value_or(
          std::make_tuple(static_cast<std::uint32_t>(_sent_amount),
                          static_cast<std::uint32_t>(_delivered_size)));

      for (SendType n = _sent_amount_logged + 1; n <= sent_amount; n++) {
        _output << "b " << n << std::endl;
      }
      _sent_amount_logged = sent_amount;

      for (size_t i = 0; i < delivered_size; i++) {
        auto& [process_id, msg] = _delivered_buffer[i];
        _output << "d " << +process_id << " " << msg << std::endl;
      }
    }
  }

  inline auto freeze() -> void {
    _frozen = {_sent_amount, _delivered_size};
    _mutex.lock();
  }

  inline auto set_sent_amount(const SendType sent_amount) -> void {
    _sent_amount = sent_amount;
  }

  inline auto sent_amount() const -> SendType { return _sent_amount; }

  inline auto open(const std::string path) -> void { _output.open(path); }

 private:
  /// @brief The size of buffer won't actually indicate the size. We separately
  /// store an atomic size to update it after a write happened. When an
  /// interrupt happens we then know how many logs where actually fully written.
  std::atomic_uint32_t _delivered_size = 0;
  std::vector<Delivered> _delivered_buffer;
  std::mutex _mutex;
  std::ofstream _output;
  std::atomic_uint32_t _sent_amount = 0;
  std::atomic_uint32_t _sent_amount_logged = 0;
  std::optional<std::tuple<SendType, std::uint32_t>> _frozen = std::nullopt;
};

Logger logger;

static void stop(int) {
  // freeze creation of new logs
  logger.freeze();

  // reset signal handlers to default
  perror_check<sig_t>([] { return std::signal(SIGTERM, SIG_DFL); },
                      [](auto res) { return res == SIG_ERR; },
                      "reset SIGTERM signal handler", true);
  perror_check<sig_t>([] { return std::signal(SIGINT, SIG_DFL); },
                      [](auto res) { return res == SIG_ERR; },
                      "reset SIGINT signal handler", true);

  // write output file
  logger.write();

  // exit directly from signal handler
  exit(0);
}

static auto map_hosts(std::vector<Parser::Host> hosts)
    -> BestEffortBroadcast::AvailableProcesses {
  BestEffortBroadcast::AvailableProcesses result;
  result.reserve(hosts.size());

  for (const auto& host : hosts) {
    result[host.id] = {host.ip, host.port};
  }

  return result;
}

int main(int argc, char** argv) {
  perror_check<sig_t>([] { return std::signal(SIGTERM, stop); },
                      [](auto res) { return res == SIG_ERR; },
                      "set SIGTERM signal handler", true);
  perror_check<sig_t>([] { return std::signal(SIGINT, stop); },
                      [](auto res) { return res == SIG_ERR; },
                      "set SIGINT signal handler", true);

  Parser parser(argc, argv);
  parser.parse();

  auto m = parser.fifoBroadcastConfig();

  logger.open(parser.outputPath());

  // create broadcast link and bind
  UniformReliableBroadcast link{parser.id(), map_hosts(parser.hosts())};
  if (auto myHost = parser.hostById(parser.id()); myHost.has_value()) {
    link.bind(myHost.value().ip, myHost.value().port);
  } else {
    throw std::runtime_error("Host not defined in the hosts file");
  }

  // preallocate about 16MiB for delivery logs
  logger.reserve_delivered_memory(16 * (1 << 20));

  // listen for deliveries
  auto listen_handle = std::thread([&] {
    link.listen([](auto process_id, auto& data) {
      SendType msg = 0;
      for (size_t i = 0; i < sizeof(SendType); i++) {
        msg |= static_cast<SendType>(data[i]) << (i * 8);
      }

      logger.deliver(process_id, msg);
    });
  });

  // pack 8 datas in one message
  constexpr auto pack = 8;
  std::array<uint8_t, pack * sizeof(SendType)> msg;
  for (SendType n = pack; n <= m; n += 8) {
    logger.set_sent_amount(n);
    for (size_t j = 1; j <= pack; j++) {
      for (size_t i = 0; i < sizeof(SendType); i++) {
        msg[(j - 1) * sizeof(SendType) + i] =
            ((n - pack + j) >> (i * 8)) & 0xff;
      }
    }

    link.broadcast(
        std::make_tuple(msg.data() + 0 * sizeof(SendType), sizeof(SendType)),
        std::make_tuple(msg.data() + 1 * sizeof(SendType), sizeof(SendType)),
        std::make_tuple(msg.data() + 2 * sizeof(SendType), sizeof(SendType)),
        std::make_tuple(msg.data() + 3 * sizeof(SendType), sizeof(SendType)),
        std::make_tuple(msg.data() + 4 * sizeof(SendType), sizeof(SendType)),
        std::make_tuple(msg.data() + 5 * sizeof(SendType), sizeof(SendType)),
        std::make_tuple(msg.data() + 6 * sizeof(SendType), sizeof(SendType)),
        std::make_tuple(msg.data() + 7 * sizeof(SendType), sizeof(SendType)));
  }
  // send rest individually
  for (SendType n = logger.sent_amount() + 1; n <= m; n++) {
    logger.set_sent_amount(n);
    for (size_t i = 0; i < sizeof(SendType); i++) {
      msg[i] = (n >> (i * 8)) & 0xff;
    }

    link.broadcast(std::make_tuple(msg.data(), sizeof(SendType)));
  }

  listen_handle.join();

  // after a process finishes broadcasting,
  // it waits forever for the delivery of messages
  while (true) {
    std::this_thread::sleep_for(std::chrono::hours(1));
  }

  return 0;
}
