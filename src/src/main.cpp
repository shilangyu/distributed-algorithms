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
  std::cout << "Immediately stopping network packet processing.\n";

  // write/flush output file if necessary
  std::cout << "Writing output.\n";

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

  std::cout << std::endl;

  std::cout << "My PID: " << getpid() << "\n";
  std::cout << "From a new terminal type `kill -SIGINT " << getpid()
            << "` or `kill -SIGTERM " << getpid()
            << "` to stop processing packets\n\n";

  std::cout << "My ID: " << parser.id() << "\n\n";

  std::cout << "List of resolved hosts is:\n";
  std::cout << "==========================\n";
  auto hosts = parser.hosts();
  for (auto& host : hosts) {
    std::cout << host.id << "\n";
    std::cout << "Human-readable IP: " << host.ipReadable() << "\n";
    std::cout << "Machine-readable IP: " << host.ip << "\n";
    std::cout << "Human-readable Port: " << host.portReadable() << "\n";
    std::cout << "Machine-readable Port: " << host.port << "\n";
    std::cout << "\n";
  }
  std::cout << "\n";

  std::cout << "Path to output:\n";
  std::cout << "===============\n";
  std::cout << parser.outputPath() << "\n\n";

  std::cout << "Path to config:\n";
  std::cout << "===============\n";
  std::cout << parser.configPath() << "\n\n";
  std::cout << "Perfect links config:\n";
  std::cout << "m=" << std::get<0>(parser.perfectLinksConfig())
            << ", i=" << std::get<1>(parser.perfectLinksConfig()) << "\n\n";

  std::cout << "Doing some initialization...\n\n";

  PerfectLink link{parser.id()};
  auto myHost = parser.hostById(parser.id());
  if (myHost.has_value()) {
    link.bind(myHost.value().ip, myHost.value().port);
  } else {
    throw std::runtime_error("Host not defined in the hosts file");
  }

  std::cout << "Broadcasting and delivering messages...\n\n";

  // After a process finishes broadcasting,
  // it waits forever for the delivery of messages.
  while (true) {
    std::this_thread::sleep_for(std::chrono::hours(1));
  }

  return 0;
}
