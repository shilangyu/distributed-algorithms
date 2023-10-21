#include "perfect_link.hpp"
#include "common.hpp"

const auto& socket_bind = bind;

PerfectLink::PerfectLink(std::size_t id) : _id(id) {}

PerfectLink::~PerfectLink() {
  if (_sock_fd.has_value()) {
    perror_check(close(_sock_fd.value()) < 0, "failed to close socket");
  }
}

auto PerfectLink::bind(in_addr_t host, in_port_t port) -> void {
  if (_sock_fd.has_value()) {
    throw std::runtime_error("Cannot bind a link twice");
  }

  int sock_fd = socket(PF_INET, SOCK_DGRAM, 0);
  perror_check(sock_fd < 0, "socket creation failure");

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(struct sockaddr_in));
  addr.sin_family = AF_INET;
  addr.sin_port = port;
  addr.sin_addr.s_addr = host;

  perror_check(socket_bind(sock_fd, reinterpret_cast<sockaddr*>(&addr),
                           sizeof(addr)) < 0,
               "failed to bind socket");
  _sock_fd = sock_fd;
}
