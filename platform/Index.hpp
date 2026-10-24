#pragma once
#include "cliptiq/index.hpp"
#include <future>
namespace cliptiq {
class IndexService {
  Store &store_;
  std::future<Result<void>> worker_;
  std::uint64_t indexed_ = UINT64_MAX, pending_ = 0;

public:
  explicit IndexService(Store &s) : store_(s) {}
  ~IndexService() {
    if (worker_.valid())
      worker_.wait();
  }
  Result<void> poll();
};
} // namespace cliptiq
