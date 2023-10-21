#include <array>
#include <chrono>
#include <iostream>
#include <thread>

#include <signal.h>
#include "common.hpp"
#include "parser.hpp"
#include "perfect_link.hpp"

static void stop(int) {
  // reset signal handlers to default
  perror_check(signal(SIGTERM, SIG_DFL) == SIG_ERR,
               "reset SIGTERM signal handler");
  perror_check(signal(SIGINT, SIG_DFL) == SIG_ERR,
               "reset SIGINT signal handler");

  // immediately stop network packet processing
  std::cout << "TODO Immediately stopping network packet processing.\n";

  // write/flush output file if necessary
  std::cout << "TODO Writing output.\n";

  // exit directly from signal handler
  exit(0);
}

int main(int argc, char** argv) {
  perror_check(signal(SIGTERM, stop) == SIG_ERR, "set SIGTERM signal handler");
  perror_check(signal(SIGINT, stop) == SIG_ERR, "set SIGINT signal handler");

  // `true` means that a config file is required.
  // Call with `false` if no config file is necessary.
  bool requireConfig = true;
  Parser parser(argc, argv, requireConfig);
  parser.parse();
  parser.dumpInfo(Stage::perfect_links);

  auto [m, i] = parser.perfectLinksConfig();

  std::ofstream output(parser.outputPath());

  // create link and bind
  PerfectLink link{parser.id()};
  auto myHost = parser.hostById(parser.id());
  if (myHost.has_value()) {
    link.bind(myHost.value().ip, myHost.value().port);
  } else {
    throw std::runtime_error("Host not defined in the hosts file");
  }

  using SendType = size_t;

  if (parser.id() == i) {
    // we are the receiver process
    std::cout << "I am receiver" << std::endl;
    link.listen([&output](auto process_id, auto data) {
      SendType msg = 0;
      for (size_t i = 0; i < sizeof(SendType); i++) {
        msg |= static_cast<SendType>(data[i]) << i * 8;
      }
      output << "d " << process_id << " " << msg << std::endl;
    });
    // TODO: write to log
  } else {
    std::cout << "I am sender" << std::endl;
    auto receiverHost = parser.hostById(i);
    if (!receiverHost.has_value()) {
      throw std::runtime_error("Receiver host not defined in hosts file");
    }

    // we are a sender process
    std::array<uint8_t, sizeof(SendType)> msg;
    for (SendType n = 1; n <= m; n++) {
      for (size_t i = 0; i < sizeof(SendType); i++) {
        msg[i] = (n << i * 8) & 0xff;
      }
      link.send(receiverHost.value().ip, receiverHost.value().port, msg.data(),
                msg.size());
      output << "b " << n << std::endl;
    }
    link.listen(
        []([[maybe_unused]] auto process_id, [[maybe_unused]] auto data) {});
  }

  // After a process finishes broadcasting,
  // it waits forever for the delivery of messages.
  std::cout << "Done work" << std::endl;
  while (true) {
    std::this_thread::sleep_for(std::chrono::hours(1));
  }

  return 0;
}
