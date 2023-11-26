#include "semaphore.hpp"

Semaphore::Semaphore(std::size_t count) : _count(count) {}

auto Semaphore::release() -> void {
  std::lock_guard<decltype(_mutex)> lock(_mutex);
  _count += 1;
  _cv.notify_one();
}

auto Semaphore::acquire() -> void {
  std::unique_lock<decltype(_mutex)> lock(_mutex);
  _cv.wait(lock, [this]() { return _count > 0; });
  _count -= 1;
}
