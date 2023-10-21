#pragma once

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstddef>
#include <optional>

/// Enforces 3 properties for point-to-point communication:
/// 1. Validity - if p1 and p2 are correct, every message sent by p1 is
///    eventually delivered by p2
/// 2. No duplication - no message is delivered more than once
/// 3. No creation - no message is delivered unless sent
class PerfectLink {
 public:
  PerfectLink(const std::size_t id);
  ~PerfectLink();

  auto bind(const in_addr_t host, const in_port_t port) -> void;

  auto send(const in_addr_t host,
            const in_port_t port,
            const uint8_t* message,
            const std::size_t message_length) -> void;

 private:
  const std::size_t _id = 1;
  std::optional<int> _sock_fd = std::nullopt;
  std::size_t _seq_nr = 1;
  static constexpr std::size_t MAX_MESSAGE_SIZE = 64 * (1 << 10) - 1;
};
