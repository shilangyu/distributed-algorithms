#pragma once

#include <cassert>
#include <cerrno>
#include <chrono>
#include <functional>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

/// @brief A helper for calling syscalls. Syscalls can be interrupted, and such
/// calls are restarted automatically. If a syscall fails, a corresponding error
/// is printed.
/// @param abort_on_error If true, a failed syscall causes an abort. In debug
/// mode it is by default true, otherwise false.
/// @return The returned value is the value returned from the syscall. Aborting
/// on error depends on the abort_on_error flag, the returned value might be an
/// error value.
template <typename T>
inline auto perror_check(const std::function<auto()->T> syscall,
                         const std::function<auto(T)->bool> error_condition,
                         const std::string_view message,
                         const bool abort_on_error =
#ifdef NDEBUG
                             false
#else
                             true
#endif
                         ) -> T {
  T res;
  do {
    res = syscall();
  } while (error_condition(res) && errno == EINTR);

  if (error_condition(res)) {
    perror(message.data());
    if (abort_on_error) {
      abort();
    }
  }

  return res;
}

template <typename T, typename... U>
using are_equal = std::conjunction<std::is_same<T, U>...>;

class Perf {
 public:
  Perf() {}

  template <class clock_t = std::chrono::steady_clock,
            class duration_t = std::chrono::nanoseconds>
  inline auto mark() -> std::chrono::time_point<clock_t, duration_t> {
    return clock_t::now();
  }

  template <class result_t = std::chrono::milliseconds,
            class clock_t = std::chrono::steady_clock,
            class duration_t = std::chrono::nanoseconds>
  inline auto since(std::string name,
                    std::chrono::time_point<clock_t, duration_t> const& start)
      -> void {
    auto end = clock_t::now();
    auto res = std::chrono::duration_cast<result_t>(end - start).count();

    std::lock_guard<std::mutex> guard(_lock);

    if (_cums.find(name) == _cums.end()) {
      _cums.insert({name, {0.0, 0.0}});
    }
    const auto [total, amount] = _cums[name];
    const auto new_total = total + static_cast<double>(res);
    const auto new_amount = amount + 1.0;
    _cums[name] = {new_total, new_amount};

    std::cout << name << "(avg): " << new_total / new_amount << std::endl;
  }

 private:
  std::mutex _lock;
  std::unordered_map<std::string, std::tuple<double, double>> _cums;
};

/// @brief Convenience type storing a pointer and size.
/// @tparam T
template <typename T>
struct Slice {
  Slice(const T* data, const std::size_t size) : _data(data), _size(size) {}

  /// Allocates to create an owned type of the underlying data.
  auto to_owned() const -> std::vector<T> {
    return std::vector(_data, _data + _size);
  }

  inline auto operator[](const std::size_t i) const -> const T& {
    return _data[i];
  }

  inline auto size() const -> std::size_t { return _size; }

  inline auto unsafe_raw() const -> std::tuple<T*, std::size_t> {
    return std::make_tuple(const_cast<T*>(_data), _size);
  }

  inline auto subslice(const std::size_t start) const -> Slice<T> {
    assert(start < _size);
    return Slice(_data + start, _size - start);
  }

 private:
  const T* _data;
  const std::size_t _size;
};

/// @brief An owned variant of a slice. Owned does not mean unique. It means
/// this slice cannot be moved or copied: there is a single owner of this
/// structure, not of the underlying data.
/// @tparam T
template <typename T>
struct OwnedSlice : public Slice<T> {
  OwnedSlice(const T* data, const std::size_t size) : Slice<T>(data, size) {}
  OwnedSlice(const Slice<T> slice) : Slice<T>(slice) {}

  OwnedSlice(const OwnedSlice&) = delete;
  OwnedSlice& operator=(const OwnedSlice&) = delete;
  OwnedSlice(OwnedSlice&&) = delete;
  OwnedSlice& operator=(OwnedSlice&&) = delete;

  inline auto subslice(const std::size_t start) const -> OwnedSlice<T> {
    return OwnedSlice(Slice<T>::subslice(start));
  }
};
