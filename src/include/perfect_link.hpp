#pragma once

#include <netinet/in.h>
#include <sys/socket.h>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstring>
#include <functional>
#include <limits>
#include <mutex>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include "common.hpp"

/// Enforces 3 properties for point-to-point communication:
/// 1. Validity - if p1 and p2 are correct, every message sent by p1 is
///    eventually delivered by p2
/// 2. No duplication - no message is delivered more than once
/// 3. No creation - no message is delivered unless sent
class PerfectLink {
 public:
  /// @brief The type used to store ID of a process.
  using ProcessIdType = std::uint8_t;

  static constexpr std::uint8_t MAX_MESSAGE_COUNT_IN_PACKET = 8;

  PerfectLink(const ProcessIdType id);

  /// @brief If the link was bound to a socket, destructor will close the
  /// socket.
  ~PerfectLink();

  /// @brief Binds this link to a host and port. Once done cannot be done again.
  auto bind(const in_addr_t host, const in_port_t port) -> void;

  using ListenCallback = std::function<
      auto(ProcessIdType process_id, OwnedSlice<std::uint8_t>& data)->void>;

  /// @brief Starts listening to incoming messages. Sends ACKs for new messages.
  /// Receives ACKs and resends messages with missing ACKs. Thread safe.
  /// @param callback Function that will be called when a message is delivered.
  auto listen(ListenCallback callback) -> void;

  /// @brief Sends a message from this link to a chosen host and port. The
  /// data has to be smaller than about 64KiB. Sending is possible only
  /// after performing a bind. At most 8 messages can be packed in
  /// a single packet.
  template <
      typename... Data,
      class = std::enable_if_t<
          are_equal<std::tuple<std::uint8_t*, std::size_t>, Data...>::value>,
      class =
          std::enable_if_t<(sizeof...(Data) <= MAX_MESSAGE_COUNT_IN_PACKET)>>
  auto send(const in_addr_t host, const in_port_t port, Data... datas) -> void;

 private:
  /// @brief The type used to store ID of a message.
  using MessageIdType = std::uint32_t;

  /// @brief The type used to store the size of data.
  using MessageSizeType = std::uint16_t;

  static constexpr std::size_t MAX_MESSAGE_SIZE =
      std::numeric_limits<MessageSizeType>::max();
  static constexpr timeval RESEND_TIMEOUT = {0, 200000};
  static constexpr std::uint16_t MAX_IN_FLIGHT = 64;

  /// @brief Data structure to hold temporary data of a message that was sent
  /// but where no ACK for it was yet received.
  struct PendingMessage {
    PendingMessage(const sockaddr_in addr,
                   const std::array<uint8_t, MAX_MESSAGE_SIZE> message,
                   const std::size_t message_size)
        : addr(addr), message(message), message_size(message_size) {}
    const struct sockaddr_in addr;
    const std::array<uint8_t, MAX_MESSAGE_SIZE> message;
    const std::size_t message_size;
  };

  /// @brief Id of this process.
  const ProcessIdType _id;

  /// @brief Hash function for `_delivered`.
  struct hash_delivered {
    size_t operator()(
        const std::tuple<ProcessIdType, MessageIdType>& arg) const noexcept {
      return std::get<0>(arg) ^ std::get<1>(arg);
    }
  };

  /// @brief Bound socket file descriptor. None if no bind was performed.
  std::optional<int> _sock_fd = std::nullopt;
  /// @brief Current sequence number of messages.
  MessageIdType _seq_nr = 1;
  /// @brief Map of sent messages that have not yet sent back an ACK.
  std::unordered_map<MessageIdType, PendingMessage> _pending_for_ack = {};
  std::mutex _pending_for_ack_mutex;
  std::condition_variable _pending_for_ack_cv;
  /// @brief A map of messages that have been delivered.
  std::unordered_set<std::tuple<ProcessIdType, MessageIdType>, hash_delivered>
      _delivered = {};
  std::mutex _delivered_mutex;
  /// @brief Flag indicating whether this link should do no more work.
  std::atomic_bool _done = false;

  /// @brief Prepares a message to be sent.
  /// @return Encoded message with its real length.
  template <
      typename... Data,
      class = std::enable_if_t<
          are_equal<std::tuple<std::uint8_t*, std::size_t>, Data...>::value>>
  inline auto _prepare_message(const MessageIdType seq_nr,
                               const bool is_ack,
                               Data... datas) const
      -> std::tuple<std::array<uint8_t, MAX_MESSAGE_SIZE>, std::size_t>;

  /// @brief Given a message from network decodes it to data. `data_buffer` will
  /// contain pointers into `message`.
  /// @return is_ack, seq_nr, process_id, filled data_buffer
  static inline auto _decode_message(
      const std::array<uint8_t, MAX_MESSAGE_SIZE>& message,
      const size_t message_size,
      std::vector<Slice<uint8_t>>& data_buffer)
      -> std::tuple<bool, MessageIdType, ProcessIdType>;
};

template <typename... Data, class>
inline auto PerfectLink::_prepare_message(const MessageIdType seq_nr,
                                          const bool is_ack,
                                          Data... datas) const
    -> std::tuple<std::array<uint8_t, MAX_MESSAGE_SIZE>, std::size_t> {
  const auto message_size = 1 + sizeof(MessageIdType) + sizeof(ProcessIdType) +
                            (std::get<1>(datas) + ... + 0) +
                            (sizeof...(Data) * sizeof(MessageSizeType));
  if (message_size > MAX_MESSAGE_SIZE) {
    throw std::runtime_error("Message is too large");
  }

  // message = [is_ack, ...seq_nr, ...process_id, ...[data_length, ...data]]
  std::array<uint8_t, MAX_MESSAGE_SIZE> message;
  message[0] = static_cast<uint8_t>(is_ack);
  for (size_t i = 0; i < sizeof(MessageIdType); i++) {
    message[i + 1] = (seq_nr >> (8 * i)) & 0xff;
  }
  message[1 + sizeof(MessageIdType)] = _id;
  auto offset = 1 + sizeof(MessageIdType) + sizeof(ProcessIdType);

  if constexpr (sizeof...(Data) > 0) {
    for (const auto& [data, length] : {datas...}) {
      for (size_t i = 0; i < sizeof(MessageSizeType); i++) {
        message[offset++] = (length >> (8 * i)) & 0xff;
      }
      std::memcpy(message.data() + offset, data, length);
      offset += length;
    }
  }

  return {message, message_size};
}

template <typename... Data, class, class>
auto PerfectLink::send(const in_addr_t host,
                       const in_port_t port,
                       Data... datas) -> void {
  if (!_sock_fd.has_value()) {
    throw std::runtime_error("Cannot send if not bound");
  }
  auto sock_fd = _sock_fd.value();

  auto [message, message_size] = _prepare_message(_seq_nr, false, datas...);

  sockaddr_in addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = host;
  addr.sin_port = port;

  {
    std::unique_lock lock(_pending_for_ack_mutex);
    _pending_for_ack_cv.wait(
        lock, [this] { return _pending_for_ack.size() < MAX_IN_FLIGHT; });
  }

  perror_check<ssize_t>(
      [&, &message = message, &message_size = message_size] {
        return sendto(sock_fd, message.data(), message_size, 0,
                      reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
      },
      [](auto res) { return res < 0; }, "failed to send message", true);

  std::lock_guard lock(_pending_for_ack_mutex);
  _pending_for_ack.try_emplace(_seq_nr, addr, message, message_size);
  _seq_nr += 1;
}
