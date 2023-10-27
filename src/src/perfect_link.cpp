#include "perfect_link.hpp"
#include <unistd.h>
#include "common.hpp"

// TODO: syscall interupts other threads which causes them to panic on
// perror_check

const auto& socket_bind = bind;

PerfectLink::PerfectLink(const ProcessIdType id) : _id(id) {}

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

inline auto PerfectLink::_decode_message(
    const std::array<uint8_t, MAX_MESSAGE_SIZE>& message,
    const size_t message_size,
    std::vector<Slice<uint8_t>>& data_buffer) const
    -> std::tuple<bool, MessageIdType, ProcessIdType> {
  bool is_ack = static_cast<bool>(message[0]);
  MessageIdType seq_nr = 0;
  for (size_t i = 0; i < sizeof(MessageIdType); i++) {
    seq_nr |= static_cast<MessageIdType>(message[i + 1]) << (8 * i);
  }
  ProcessIdType process_id = message[1 + sizeof(MessageIdType)];

  data_buffer.clear();
  auto offset = 1 + sizeof(MessageIdType) + sizeof(ProcessIdType);
  while (offset < message_size) {
    size_t length = 0;
    for (size_t i = 0; i < sizeof(MessageSizeType); i++) {
      length |= static_cast<size_t>(message[offset++]) << (8 * i);
    }
    data_buffer.emplace_back(message.data() + offset, length);
    offset += length;
  }

  return {is_ack, seq_nr, process_id};
}

auto PerfectLink::listen(ListenCallback callback) -> std::thread {
  if (!_sock_fd.has_value()) {
    throw std::runtime_error("Cannot listen if not bound");
  }
  auto sock_fd = _sock_fd.value();

  std::thread listener([sock_fd, callback, this]() {
    std::array<uint8_t, MAX_MESSAGE_SIZE> message;
    std::vector<Slice<std::uint8_t>> data_buffer;
    data_buffer.reserve(MAX_MESSAGE_COUNT_IN_PACKET);

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

      auto [is_ack, seq_nr, process_id] = _decode_message(
          message, static_cast<size_t>(message_size), data_buffer);

      if (is_ack) {
        // mark a sent message as being acknowledged, we will no longer be
        // sending it
        std::lock_guard<std::mutex> guard(_pending_for_ack_mutex);
        _pending_for_ack.erase(seq_nr);
      } else {
        // we received a potentially new message
        if (_delivered.find({process_id, seq_nr}) == _delivered.end()) {
          // we have not yet delivered the message, do it now
          for (auto& data : data_buffer) {
            OwnedSlice owned = data;
            callback(process_id, owned);
          }
          _delivered.emplace(process_id, seq_nr);
        }

        // send an ACK
        auto [ack_message, ack_message_size] = _prepare_message(seq_nr, true);
        perror_check(sendto(sock_fd, ack_message.data(), ack_message_size, 0,
                            reinterpret_cast<sockaddr*>(&sender_addr),
                            sender_addr_len) < 0,
                     "failed to send ack");
      }
    }
  });

  return listener;
}
