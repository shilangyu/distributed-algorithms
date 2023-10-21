#include "perfect_link.hpp"
#include <unistd.h>
#include <cstring>
#include "common.hpp"

// TODO: syscalls are interupted by signal and cause panics

const auto& socket_bind = bind;

PerfectLink::PerfectLink(const std::size_t id) : _id(id) {}

PerfectLink::~PerfectLink() {
  if (_sock_fd.has_value()) {
    perror_check(close(_sock_fd.value()) < 0, "failed to close socket");
  }
  _done = true;
}

auto PerfectLink::bind(const in_addr_t host, const in_port_t port) -> void {
  if (_sock_fd.has_value()) {
    throw std::runtime_error("Cannot bind a link twice");
  }

  int sock_fd = socket(PF_INET, SOCK_DGRAM, 0);
  perror_check(sock_fd < 0, "socket creation failure");

  sockaddr_in addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = host;
  addr.sin_port = port;

  perror_check(socket_bind(sock_fd, reinterpret_cast<sockaddr*>(&addr),
                           sizeof(addr)) < 0,
               "failed to bind socket");

  perror_check(setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, &RESEND_TIMEOUT,
                          sizeof(RESEND_TIMEOUT)) < 0,
               "failed to set socket timeout");

  _sock_fd = sock_fd;
}

inline auto PerfectLink::_prepare_message(const SeqType seq_nr,
                                          const uint8_t* data,
                                          const std::size_t data_length,
                                          const bool is_ack) const
    -> std::tuple<std::array<uint8_t, MAX_MESSAGE_SIZE>, std::size_t> {
  if (1 + sizeof(SeqType) + sizeof(IdType) + data_length > MAX_MESSAGE_SIZE) {
    throw std::runtime_error("Message is too large");
  }

  // message = [is_ack, ...seq_nr, ...process_id, ...data]
  std::array<uint8_t, MAX_MESSAGE_SIZE> message;
  message[0] = static_cast<uint8_t>(is_ack);
  for (size_t i = 0; i < sizeof(SeqType); i++) {
    message[i + 1] = (seq_nr << (8 * i)) & 0xff;
  }
  for (size_t i = 0; i < sizeof(IdType); i++) {
    message[i + 1 + sizeof(SeqType)] = (_id << (8 * i)) & 0xff;
  }
  if (data_length != 0) {
    std::memcpy(message.data() + 1 + sizeof(SeqType) + sizeof(IdType), data,
                data_length);
  }

  return {message, 1 + sizeof(SeqType) + sizeof(IdType) + data_length};
}

inline auto PerfectLink::_decode_message(
    const std::array<uint8_t, MAX_MESSAGE_SIZE> message,
    const ssize_t message_size) const
    -> std::tuple<bool, SeqType, IdType, std::vector<uint8_t>> {
  std::vector<uint8_t> data(
      message.data() + 1 + sizeof(SeqType) + sizeof(IdType),
      message.data() + message_size);

  SeqType seq_nr = 0;
  for (size_t i = 0; i < sizeof(SeqType); i++) {
    seq_nr |= static_cast<SeqType>(message[i + 1]) << (8 * i);
  }
  IdType process_id = 0;
  for (size_t i = 0; i < sizeof(IdType); i++) {
    process_id |= static_cast<IdType>(message[i + 1 + sizeof(SeqType)])
                  << (8 * i);
  }

  return {static_cast<bool>(message[0]), seq_nr, process_id, data};
}

auto PerfectLink::send(const in_addr_t host,
                       const in_port_t port,
                       const uint8_t* data,
                       const std::size_t data_length) -> void {
  if (!_sock_fd.has_value()) {
    throw std::runtime_error("Cannot send if not bound");
  }
  auto sock_fd = _sock_fd.value();

  auto [message, message_size] =
      _prepare_message(_seq_nr, data, data_length, false);

  sockaddr_in addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = host;
  addr.sin_port = port;

  perror_check(sendto(sock_fd, message.data(), message_size, 0,
                      reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0,
               "failed to send message");
  std::lock_guard<std::mutex> guard(_pending_for_ack_mutex);
  _pending_for_ack.try_emplace(_seq_nr, addr, message, message_size);
  _seq_nr += 1;
}

auto PerfectLink::listen(ListenCallback callback) -> std::thread {
  if (!_sock_fd.has_value()) {
    throw std::runtime_error("Cannot listen if not bound");
  }
  auto sock_fd = _sock_fd.value();

  std::thread listener([sock_fd, callback, this]() {
    std::array<uint8_t, MAX_MESSAGE_SIZE> message;

    sockaddr_in sender_addr;
    std::memset(&sender_addr, 0, sizeof(sender_addr));
    socklen_t sender_addr_len = sizeof(sender_addr);

    while (true) {
      // wait for a message
      auto message_size =
          recvfrom(sock_fd, message.data(), message.size(), MSG_WAITALL,
                   reinterpret_cast<sockaddr*>(&sender_addr), &sender_addr_len);

      if (_done) {
        return;
      }

      if (message_size < 0 && errno == EAGAIN) {
        // timed out, resend messages without ACKs
        std::lock_guard<std::mutex> guard(_pending_for_ack_mutex);
        for (auto& [seq_nr, pending] : _pending_for_ack) {
          perror_check(
              sendto(sock_fd, pending.message.data(), pending.message_size, 0,
                     reinterpret_cast<const sockaddr*>(&pending.addr),
                     sizeof(pending.addr)) < 0,
              "failed to resend message");
        }
        continue;
      }
      perror_check(message_size < 0, "failed to receive message");

      auto [is_ack, seq_nr, process_id, data] =
          _decode_message(message, message_size);

      if (is_ack) {
        // mark a sent message as being acknowledged, we will no longer be
        // sending it
        std::lock_guard<std::mutex> guard(_pending_for_ack_mutex);
        _pending_for_ack.erase(seq_nr);
      } else {
        // we received a potentially new message
        if (auto iter = _delivered.find({process_id, seq_nr});
            iter == _delivered.end()) {
          // we have not yet delivered the message, do it now
          callback(process_id, data);
          _delivered.emplace(process_id, seq_nr);
        }

        // send an ACK
        auto [ack_message, ack_message_size] =
            _prepare_message(seq_nr, nullptr, 0, true);
        perror_check(sendto(sock_fd, ack_message.data(), ack_message_size, 0,
                            reinterpret_cast<sockaddr*>(&sender_addr),
                            sender_addr_len) < 0,
                     "failed to send ack");
      }
    }
  });

  return listener;
}
