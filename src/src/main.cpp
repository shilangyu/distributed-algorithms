#include <array>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>
#include <vector>
#include "common.hpp"
#include "parser.hpp"
#include "perfect_link.hpp"

using SendType = std::uint32_t;
using Delivered = std::tuple<PerfectLink::ProcessIdType, SendType>;

SendType sent_amount = 0;
std::vector<Delivered> delivered;
std::ofstream output;

static auto write_delivered() -> void {
  if (output.is_open()) {
    for (auto& [process_id, msg] : delivered) {
      output << "d " << +process_id << " " << msg << std::endl;
    }
    delivered.clear();
  }
}

static void stop(int) {
  // reset signal handlers to default
  perror_check(std::signal(SIGTERM, SIG_DFL) == SIG_ERR,
               "reset SIGTERM signal handler");
  perror_check(std::signal(SIGINT, SIG_DFL) == SIG_ERR,
               "reset SIGINT signal handler");

  // immediately stop network packet processing
  std::cout << "TODO Immediately stopping network packet processing.\n";

  // write output file
  if (output.is_open()) {
    for (SendType n = 1; n <= sent_amount; n++) {
      output << "b " << n << std::endl;
    }
  }
  write_delivered();

  // exit directly from signal handler
  exit(0);
}

int main(int argc, char** argv) {
  perror_check(std::signal(SIGTERM, stop) == SIG_ERR,
               "set SIGTERM signal handler");
  perror_check(std::signal(SIGINT, stop) == SIG_ERR,
               "set SIGINT signal handler");

  // `true` means that a config file is required.
  // Call with `false` if no config file is necessary.
  bool requireConfig = true;
  Parser parser(argc, argv, requireConfig);
  parser.parse();

  auto [m, i] = parser.perfectLinksConfig();

  output.open(parser.outputPath());

  // create link and bind
  PerfectLink link{parser.id()};
  auto myHost = parser.hostById(parser.id());
  if (myHost.has_value()) {
    link.bind(myHost.value().ip, myHost.value().port);
  } else {
    throw std::runtime_error("Host not defined in the hosts file");
  }

  if (parser.id() == i) {
    // we are the receiver process
    // preallocate about 16MiB for delivery logs
    delivered.reserve(16 * (1 << 20) / sizeof(Delivered));
    auto listen_handle = link.listen([](auto process_id, auto data) {
      SendType msg = 0;
      for (size_t i = 0; i < sizeof(SendType); i++) {
        msg |= static_cast<SendType>(data[i]) << (i * 8);
      }

      delivered.emplace_back(process_id, msg);
      if (delivered.capacity() == delivered.size()) {
        // we are at full capacity, flush the buffer to the file
        write_delivered();
      }
    });
    listen_handle.join();
  } else {
    auto receiverHost = parser.hostById(i);
    if (!receiverHost.has_value()) {
      throw std::runtime_error("Receiver host not defined in hosts file");
    }
    auto resend_handle = link.listen(
        []([[maybe_unused]] auto process_id, [[maybe_unused]] auto data) {});

    // we are a sender process
    // pack 8 datas in one message
    constexpr auto pack = 8;
    std::array<uint8_t, pack * sizeof(SendType)> msg;
    for (SendType n = pack; n <= m; n += 8) {
      for (size_t j = 1; j <= pack; j++) {
        for (size_t i = 0; i < sizeof(SendType); i++) {
          msg[(j - 1) * sizeof(SendType) + i] =
              ((n - pack + j) >> (i * 8)) & 0xff;
        }
      }

      link.send(
          receiverHost.value().ip, receiverHost.value().port,
          std::make_tuple(msg.data() + 0 * sizeof(SendType), sizeof(SendType)),
          std::make_tuple(msg.data() + 1 * sizeof(SendType), sizeof(SendType)),
          std::make_tuple(msg.data() + 2 * sizeof(SendType), sizeof(SendType)),
          std::make_tuple(msg.data() + 3 * sizeof(SendType), sizeof(SendType)),
          std::make_tuple(msg.data() + 4 * sizeof(SendType), sizeof(SendType)),
          std::make_tuple(msg.data() + 5 * sizeof(SendType), sizeof(SendType)),
          std::make_tuple(msg.data() + 6 * sizeof(SendType), sizeof(SendType)),
          std::make_tuple(msg.data() + 7 * sizeof(SendType), sizeof(SendType)));
      sent_amount = n;
    }
    // send rest individually
    for (SendType n = sent_amount + 1; n <= m; n++) {
      for (size_t i = 0; i < sizeof(SendType); i++) {
        msg[i] = (n >> (i * 8)) & 0xff;
      }

      link.send(receiverHost.value().ip, receiverHost.value().port,
                std::make_tuple(msg.data(), sizeof(SendType)));
      sent_amount = n;
    }

    resend_handle.join();
  }

  // after a process finishes broadcasting,
  // it waits forever for the delivery of messages
  while (true) {
    std::this_thread::sleep_for(std::chrono::hours(1));
  }

  return 0;
}
