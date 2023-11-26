#include "perfect_link.hpp"
#include <unistd.h>
#include "common.hpp"

const auto& socket_bind = bind;

PerfectLink::PerfectLink(const ProcessIdType id) : _id(id) {}

PerfectLink::~PerfectLink() {
  if (_sock_fd.has_value()) {
    perror_check<int>([this] { return close(_sock_fd.value()); },
                      [](auto res) { return res < 0; },
                      "failed to close socket");
  }
  _done = true;
}

auto PerfectLink::bind(const in_addr_t host, const in_port_t port) -> void {
  if (_sock_fd.has_value()) {
    throw std::runtime_error("Cannot bind a link twice");
  }

  int sock_fd = perror_check<int>(
      []() { return socket(PF_INET, SOCK_DGRAM, 0); },
      [](auto res) { return res < 0; }, "socket creation failure", true);

  sockaddr_in addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = host;
  addr.sin_port = port;

  perror_check<int>(
      [&] {
        return socket_bind(sock_fd, reinterpret_cast<sockaddr*>(&addr),
                           sizeof(addr));
      },
      [](auto res) { return res < 0; }, "failed to bind socket", true);

  perror_check<int>(
      [sock_fd] {
        return setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, &RESEND_TIMEOUT,
                          sizeof(RESEND_TIMEOUT));
      },
      [](auto res) { return res < 0; }, "failed to set socket timeout", true);

  _sock_fd = sock_fd;
}

inline auto PerfectLink::_decode_message(
    const std::array<uint8_t, MAX_MESSAGE_SIZE>& message,
    const size_t message_size,
    std::vector<Slice<uint8_t>>& data_buffer)
    -> std::tuple<bool, MessageIdType, ProcessIdType, Slice<std::uint8_t>> {
  bool is_ack = static_cast<bool>(message[0]);
  MessageIdType seq_nr = 0;
  for (size_t i = 0; i < sizeof(MessageIdType); i++) {
    seq_nr |= static_cast<MessageIdType>(message[i + 1]) << (8 * i);
  }
  ProcessIdType process_id = message[1 + sizeof(MessageIdType)];
  auto offset = 1 + sizeof(MessageIdType) + sizeof(ProcessIdType);

  size_t metadata_length = 0;
  for (size_t i = 0; i < sizeof(MessageSizeType); i++) {
    metadata_length |= static_cast<size_t>(message[offset++]) << (8 * i);
  }
  Slice<uint8_t> metadata(message.data() + offset, metadata_length);
  offset += metadata_length;

  data_buffer.clear();
  while (offset < message_size) {
    size_t length = 0;
    for (size_t i = 0; i < sizeof(MessageSizeType); i++) {
      length |= static_cast<size_t>(message[offset++]) << (8 * i);
    }
    data_buffer.emplace_back(message.data() + offset, length);
    offset += length;
  }

  return {is_ack, seq_nr, process_id, metadata};
}

auto PerfectLink::listen(ListenCallback callback) -> void {
  listen_batch(
      [&](auto process_id, [[maybe_unused]] auto& metadata, auto& datas) {
        for (auto& data : datas) {
          OwnedSlice owned = data;
          callback(process_id, owned);
        }
      });
}

auto PerfectLink::listen_batch(ListenBatchCallback callback) -> void {
  if (!_sock_fd.has_value()) {
    throw std::runtime_error("Cannot listen if not bound");
  }
  auto sock_fd = _sock_fd.value();

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

    if (message_size < 0 && errno == EINTR) {
      // got interrupted, try again
      continue;
    }

    if (message_size < 0 && errno == EAGAIN) {
      // TODO: consider scoping resends to a single process
      // timed out, resend messages without ACKs
      std::lock_guard<std::mutex> guard(_pending_for_ack_mutex);
      for (auto& [seq_nr, pending] : _pending_for_ack) {
        perror_check<ssize_t>(
            [&, &seq_nr = seq_nr, &pending = pending] {
              return sendto(sock_fd, pending.message.data(),
                            pending.message_size, 0,
                            reinterpret_cast<const sockaddr*>(&pending.addr),
                            sizeof(pending.addr));
            },
            [](auto res) { return res < 0; }, "failed to resend message");
      }
      continue;
    }

    if (message_size < 0) {
      perror("failed to receive message");
      continue;
    }

    auto [is_ack, seq_nr, process_id, metadata] = _decode_message(
        message, static_cast<size_t>(message_size), data_buffer);

    if (is_ack) {
      // mark a sent message as being acknowledged, we will no longer be
      // sending it
      {
        std::lock_guard<std::mutex> guard(_pending_for_ack_mutex);
        _pending_for_ack.erase(seq_nr);
      }
    } else {
      // we received a potentially new message
      _delivered_mutex.lock();
      auto has_not_been_delivered =
          _delivered.emplace(process_id, seq_nr).second;
      _delivered_mutex.unlock();

      if (has_not_been_delivered) {
        // we have not yet delivered the message, do it now
        OwnedSlice m = metadata;
        callback(process_id, m, data_buffer);
      }

      // send an ACK
      auto [ack_message, ack_message_size] =
          _prepare_message(seq_nr, true, std::nullopt);
      perror_check<ssize_t>(
          [&, &ack_message = ack_message,
           &ack_message_size = ack_message_size] {
            return sendto(
                sock_fd, ack_message.data(), ack_message_size, MSG_NOSIGNAL,
                reinterpret_cast<sockaddr*>(&sender_addr), sender_addr_len);
          },
          [](auto res) { return res < 0 && errno != EPIPE; },
          "failed to send ack");
    }
  }
}
