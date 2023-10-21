#include "perfect_link.hpp"
#include <array>
#include "common.hpp"

const auto& socket_bind = bind;

PerfectLink::PerfectLink(const std::size_t id) : _id(id) {}

PerfectLink::~PerfectLink() {
  if (_sock_fd.has_value()) {
    perror_check(close(_sock_fd.value()) < 0, "failed to close socket");
  }
}

auto PerfectLink::bind(const in_addr_t host, const in_port_t port) -> void {
  if (_sock_fd.has_value()) {
    throw std::runtime_error("Cannot bind a link twice");
  }

  int sock_fd = socket(PF_INET, SOCK_DGRAM, 0);
  perror_check(sock_fd < 0, "socket creation failure");

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = host;
  addr.sin_port = port;

  perror_check(socket_bind(sock_fd, reinterpret_cast<sockaddr*>(&addr),
                           sizeof(addr)) < 0,
               "failed to bind socket");
  _sock_fd = sock_fd;
}

auto PerfectLink::send(const in_addr_t host,
                       const in_port_t port,
                       const uint8_t* message,
                       const std::size_t message_length) -> void {
  if (!_sock_fd.has_value()) {
    throw std::runtime_error("Cannot send if not binded");
  }
  auto sock_fd = _sock_fd.value();
  if (sizeof(_seq_nr) + message_length > MAX_MESSAGE_SIZE) {
    throw std::runtime_error("Message is too large");
  }

  std::array<uint8_t, MAX_MESSAGE_SIZE> data;
  for (size_t i = 0; i < sizeof(_seq_nr); i++) {
    data[i] = (_seq_nr << 8 * i) & 0xff;
  }
  memcpy(data.data() + sizeof(_seq_nr), message, message_length);

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = host;
  addr.sin_port = port;

  perror_check(sendto(sock_fd, data.data(), sizeof(_seq_nr) + message_length, 0,
                      reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0,
               "failed to send message");
  _seq_nr += 1;

  //  TODO: wait for ACK
}
