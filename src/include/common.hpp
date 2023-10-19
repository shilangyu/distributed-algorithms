#include <cerrno>
#include <stdexcept>
#include <string>

auto perror_check(const bool error_condition, const std::string message)
    -> void {
  if (error_condition) {
    perror(message.c_str());
    throw std::runtime_error("panic");
  }
}
