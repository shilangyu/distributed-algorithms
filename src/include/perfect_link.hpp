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

  /// @brief The type used to store ID of a message.
  using MessageIdType = std::uint32_t;

  using MessageData = std::tuple<std::uint8_t*, std::size_t>;

  static constexpr std::uint8_t MAX_MESSAGE_COUNT_IN_PACKET = 8;
  static constexpr ProcessIdType MAX_PROCESSES = 128;
  static constexpr std::size_t MAX_MESSAGE_SIZE = 64;

  PerfectLink(const ProcessIdType id);

  /// @brief If the link was bound to a socket, destructor will close the
  /// socket.
  ~PerfectLink();

  /// @brief Binds this link to a host and port. Once done cannot be done again.
  auto bind(const in_addr_t host, const in_port_t port) -> void;

  using ListenCallback = std::function<
      auto(ProcessIdType process_id, OwnedSlice<std::uint8_t>& data)->void>;
  using ListenBatchCallback =
      std::function<auto(ProcessIdType process_id,
                         OwnedSlice<std::uint8_t>& metadata,
                         std::vector<Slice<std::uint8_t>>& datas)
                        ->void>;

  /// @brief Starts listening to incoming messages. Sends ACKs for new messages.
  /// Receives ACKs and resends messages with missing ACKs. Thread safe.
  /// @param callback Function that will be called when a message is delivered.
  auto listen(ListenCallback callback) -> void;

  /// @brief Same as `listen` but receives many messages at once if the send was
  /// batched. This will also recover metadata.
  auto listen_batch(ListenBatchCallback callback) -> void;

  /// @brief Sends a message from this link to a chosen host and port. The
  /// data has to be smaller than about 64KiB. Sending is possible only
  /// after performing a bind. At most 8 messages can be packed in
  /// a single packet. Thread safe.
  template <typename... Data,
            class = std::enable_if_t<are_equal<MessageData, Data...>::value>,
            class = std::enable_if_t<(sizeof...(Data) <=
                                      MAX_MESSAGE_COUNT_IN_PACKET)>>
  auto send(const in_addr_t host,
            const in_port_t port,
            const std::optional<MessageData> metadata,
            Data... datas) -> void;

  /// @brief Id of this process.
  inline auto id() const -> ProcessIdType { return _id; }

 private:
  /// @brief The type used to store the size of data.
  using MessageSizeType = std::uint16_t;

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

  // TODO: std::tuple<ProcessIdType, MessageIdType> fits in a single uint64.
  //       Could be compressed to avoid hashing.
  /// @brief Hash function for `_delivered`.
  struct hash_delivered {
    inline size_t operator()(
        const std::tuple<ProcessIdType, MessageIdType>& arg) const noexcept {
      return std::get<0>(arg) ^ std::get<1>(arg);
    }
  };

  /// @brief Bound socket file descriptor. None if no bind was performed.
  std::optional<int> _sock_fd;
  /// @brief Current sequence number of messages.
  MessageIdType _seq_nr = 1;
  /// @brief Map of sent messages that have not yet sent back an ACK.
  std::unordered_map<MessageIdType, PendingMessage> _pending_for_ack;
  std::mutex _pending_for_ack_mutex;
  /// @brief A map of messages that have been delivered.
  std::unordered_set<std::tuple<ProcessIdType, MessageIdType>, hash_delivered>
      _delivered = {};
  std::mutex _delivered_mutex;
  /// @brief Flag indicating whether this link should do no more work.
  std::atomic_bool _done = false;

  /// @brief Prepares a message to be sent.
  /// @return Encoded message with its real length.
  template <typename... Data,
            class = std::enable_if_t<are_equal<MessageData, Data...>::value>>
  inline auto _prepare_message(const MessageIdType seq_nr,
                               const bool is_ack,
                               const std::optional<MessageData> metadata,
                               Data... datas) const
      -> std::tuple<std::array<uint8_t, MAX_MESSAGE_SIZE>, std::size_t>;

  /// @brief Given a message from network decodes it to data. `data_buffer` will
  /// contain pointers into `message`.
  /// @return is_ack, seq_nr, process_id, metadata
  static inline auto _decode_message(
      const std::array<uint8_t, MAX_MESSAGE_SIZE>& message,
      const size_t message_size,
      std::vector<Slice<uint8_t>>& data_buffer)
      -> std::tuple<bool, MessageIdType, ProcessIdType, Slice<std::uint8_t>>;
};

template <typename... Data, class>
inline auto PerfectLink::_prepare_message(
    const MessageIdType seq_nr,
    const bool is_ack,
    const std::optional<MessageData> metadata,
    Data... datas) const
    -> std::tuple<std::array<uint8_t, MAX_MESSAGE_SIZE>, std::size_t> {
  auto metadata_value = metadata.value_or(std::make_tuple(nullptr, 0));

  const auto message_size = 1 + sizeof(MessageIdType) + sizeof(ProcessIdType) +
                            std::get<1>(metadata_value) +
                            sizeof(MessageSizeType) +
                            (std::get<1>(datas) + ... + 0) +
                            (sizeof...(Data) * sizeof(MessageSizeType));
  if (message_size > MAX_MESSAGE_SIZE) {
    throw std::runtime_error("Message is too large");
  }

  // message = [is_ack, ...seq_nr, ...process_id,
  //            ...metadata_length, ...metadata,
  //            ...[data_length, ...data]]
  std::array<uint8_t, MAX_MESSAGE_SIZE> message;
  message[0] = static_cast<uint8_t>(is_ack);
  for (size_t i = 0; i < sizeof(MessageIdType); i++) {
    message[i + 1] = (seq_nr >> (8 * i)) & 0xff;
  }
  message[1 + sizeof(MessageIdType)] = _id;
  auto offset = 1 + sizeof(MessageIdType) + sizeof(ProcessIdType);

  auto& [data, length] = metadata_value;
  for (size_t i = 0; i < sizeof(MessageSizeType); i++) {
    message[offset++] = (length >> (8 * i)) & 0xff;
  }
  std::memcpy(message.data() + offset, data, length);
  offset += length;

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
                       const std::optional<MessageData> metadata,
                       Data... datas) -> void {
  if (!_sock_fd.has_value()) {
    throw std::runtime_error("Cannot send if not bound");
  }
  auto sock_fd = _sock_fd.value();

  auto [message, message_size] =
      _prepare_message(_seq_nr, false, metadata, datas...);

  sockaddr_in addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = host;
  addr.sin_port = port;

  {
    std::unique_lock lock(_pending_for_ack_mutex);
    _pending_for_ack.try_emplace(_seq_nr, addr, message, message_size);
    _seq_nr += 1;
  }

  perror_check<ssize_t>(
      [&, &message = message, &message_size = message_size] {
        return sendto(sock_fd, message.data(), message_size, MSG_NOSIGNAL,
                      reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
      },
      [](auto res) { return res < 0 && errno != EPIPE; },
      "failed to send message");
}
