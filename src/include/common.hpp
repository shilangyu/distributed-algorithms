#pragma once

#include <cerrno>
#include <stdexcept>
#include <string_view>
#include <type_traits>

inline auto perror_check(const bool error_condition,
                         const std::string_view message) -> void {
  if (error_condition) {
    perror(message.data());
    throw std::runtime_error("panic");
  }
}

template <typename T, typename... U>
using are_equal = std::conjunction<std::is_same<T, U>...>;
