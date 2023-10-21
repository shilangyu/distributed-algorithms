#pragma once

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstddef>
#include <mutex>
#include <optional>
#include <unordered_map>

/// Enforces 3 properties for point-to-point communication:
/// 1. Validity - if p1 and p2 are correct, every message sent by p1 is
///    eventually delivered by p2
/// 2. No duplication - no message is delivered more than once
/// 3. No creation - no message is delivered unless sent
class PerfectLink {
 public:
  PerfectLink(const std::size_t id);

  /// @brief If the link was bound to a socket, destructor will close the
  /// socket.
  ~PerfectLink();

  /// @brief Binds this link to a host and port. Once done cannot be done again.
  auto bind(const in_addr_t host, const in_port_t port) -> void;

  /// @brief Sends a message from this link to a chosen host and port. The
  /// message has to be smaller than about 64KiB. Sending is possible only after
  /// performing a bind.
  auto send(const in_addr_t host,
            const in_port_t port,
            const uint8_t* message,
            const std::size_t message_length) -> void;

 private:
  static constexpr std::size_t MAX_MESSAGE_SIZE = 64 * (1 << 10) - 1;

  /// @brief Data structor to hold temporary data of a message that was sent but
  /// where no ACK for it was yet received.
  struct PendingMessage {
    PendingMessage(const sockaddr_in addr,
                   const std::array<uint8_t, MAX_MESSAGE_SIZE> message,
                   const std::size_t message_size)
        : addr(addr), message(message), message_size(message_size) {}
    const struct sockaddr_in addr;
    const std::array<uint8_t, MAX_MESSAGE_SIZE> message;
    const std::size_t message_size;
  };

  using SeqType = std::size_t;

  /// @brief Id of this process.
  const std::size_t _id;
  /// @brief Bound socket file descriptor. None if no bind was performed.
  std::optional<int> _sock_fd = std::nullopt;
  /// @brief Current sequence number of messages.
  SeqType _seq_nr = 1;
  /// @brief Map of sent messages that have not yet sent back an ACK.
  std::unordered_map<SeqType, PendingMessage> _pending_for_ack = {};
  std::mutex _pending_for_ack_mutex;

  /// @brief Prepares a message to be sent.
  inline auto _prepare_message(const SeqType seq_nr,
                               const uint8_t* message,
                               const std::size_t message_length,
                               const bool is_ack) const
      -> std::tuple<std::array<uint8_t, MAX_MESSAGE_SIZE>, std::size_t>;
};
