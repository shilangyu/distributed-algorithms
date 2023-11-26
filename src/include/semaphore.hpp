#pragma once

#include <condition_variable>
#include <mutex>

struct Semaphore {
 public:
  Semaphore(std::size_t count);

  auto release() -> void;

  auto acquire() -> void;

 private:
  std::mutex _mutex;
  std::condition_variable _cv;
  std::size_t _count;
};
