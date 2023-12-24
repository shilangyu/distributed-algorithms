#include <csignal>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_set>
#include <vector>
#include "common.hpp"
#include "lattice_agreement.hpp"
#include "parser.hpp"

struct Logger {
  inline auto reserve_decided_memory(const std::size_t bytes) -> void {
    _decided_buffer.reserve(bytes / sizeof(LatticeAgreement::AgreementType));
  }

  inline auto decide(
      const std::unordered_set<LatticeAgreement::AgreementType>& set) {
    std::lock_guard<std::mutex> lock(_mutex);
    // UB: we might be interrupted during a write. Then, we are in a very
    // bad state. In practice, we were promised that logs won't be larger
    // than 16MiB, so this write should never happen. Additionally getting
    // interrupted during a write is highly unlikely, as a write happens
    // about once every 2 million deliveries.
    if (_decided_buffer.capacity() < _decided_buffer.size() + set.size()) {
      // we are at full capacity, flush the buffer to the file
      write();
      _decided_buffer.clear();
      _decided_size = 0;
    }

    _decided_buffer.push_back(
        static_cast<LatticeAgreement::AgreementType>(set.size()));
    _decided_buffer.insert(_decided_buffer.end(), set.begin(), set.end());
    _decided_size.fetch_add(
        static_cast<LatticeAgreement::AgreementType>(set.size()) + 1);
  }

  inline auto write() -> void {
    if (_output.is_open()) {
      std::stringstream ss;
      const auto& decided_size =
          _frozen.value_or(static_cast<std::uint32_t>(_decided_size));

      for (size_t i = 0; i < decided_size; i++) {
        auto len = _decided_buffer[i];
        for (size_t j = 0; j < len; j++) {
          i += 1;
          if (j != 0) {
            ss << " ";
          }
          ss << _decided_buffer[i];
        }
        ss << '\n';
      }

      _output << ss.str();
    }
  }

  inline auto freeze() -> void {
    _frozen = _decided_size;
    _mutex.lock();
  }

  inline auto open(const std::string path) -> void { _output.open(path); }

 private:
  /// @brief The size of buffer won't actually indicate the size. We separately
  /// store an atomic size to update it after a write happened. When an
  /// interrupt happens we then know how many logs were actually fully written.
  std::atomic_uint32_t _decided_size = 0;
  /// A linear buffer of many decided sets. A sequence of decided numbers starts
  /// with an entry describing how many numbers there are.
  std::vector<LatticeAgreement::AgreementType> _decided_buffer;
  std::mutex _mutex;
  std::ofstream _output;
  std::optional<std::uint32_t> _frozen = std::nullopt;
};

Logger logger;

static void stop(int) {
  // freeze creation of new logs
  logger.freeze();

  // reset signal handlers to default
  perror_check<sig_t>([]() noexcept { return std::signal(SIGTERM, SIG_DFL); },
                      [](auto res) noexcept { return res == SIG_ERR; },
                      "reset SIGTERM signal handler", true);
  perror_check<sig_t>([]() noexcept { return std::signal(SIGINT, SIG_DFL); },
                      [](auto res) noexcept { return res == SIG_ERR; },
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
  perror_check<sig_t>([]() noexcept { return std::signal(SIGTERM, stop); },
                      [](auto res) noexcept { return res == SIG_ERR; },
                      "set SIGTERM signal handler", true);
  perror_check<sig_t>([]() noexcept { return std::signal(SIGINT, stop); },
                      [](auto res) noexcept { return res == SIG_ERR; },
                      "set SIGINT signal handler", true);

  Parser parser(argc, argv);
  parser.parse();

  auto config = parser.latticeAgreementConfig();

  logger.open(parser.outputPath());

  // create an agreement link and bind
  LatticeAgreement agreement{parser.id(), map_hosts(parser.hosts()),
                             config.unique_proposals,
                             [](auto& set) { logger.decide(set); }};
  if (auto myHost = parser.hostById(parser.id()); myHost.has_value()) {
    agreement.bind(myHost.value().ip, myHost.value().port);
  } else {
    throw std::runtime_error("Host not defined in the hosts file");
  }

  // preallocate about 16MiB for decided logs
  logger.reserve_decided_memory(16 * (1 << 20));

  // listen for deliveries
  auto listen_handle = std::thread([&] { agreement.listen(); });

  while (config.has_more_proposals()) {
    agreement.propose(config.next_proposal());
  }

  listen_handle.join();

  // after a process finishes broadcasting,
  // it waits forever for the delivery of messages
  while (true) {
    std::this_thread::sleep_for(std::chrono::hours(1));
  }

  return 0;
}

// TODO: stress run crashes
