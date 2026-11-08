#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace zplane {
struct AutomationEvent {
 std::uint32_t offset{};
 std::uint32_t parameter{};
 double value{};
 std::uint32_t order{};
};
class AutomationTimeline {
public:
 static constexpr std::size_t kCapacity=4096;
private:
 std::array<AutomationEvent,kCapacity> entries_{};
 std::size_t size_{0};
 bool overflow_{false};
public:
 void clear() noexcept {size_=0;overflow_=false;}
 bool add(std::uint32_t offset,std::uint32_t param,double value) noexcept {
  if(param>=5||!std::isfinite(value))return false;
  if(size_==kCapacity){overflow_=true;return false;}
  // O(1) enqueue, rather than quadratic realtime insertion shifting.
  entries_[size_]={offset,param,value,static_cast<std::uint32_t>(size_)};
  ++size_;return true;
 }
 void finalize() noexcept {
  // std::sort has bounded stack usage and never heap-allocates. Including
  // insertion-order tie breaking preserves deterministic equal-offset edits.
  std::sort(entries_.begin(),entries_.begin()+static_cast<std::ptrdiff_t>(size_),
    [](const AutomationEvent& lhs,const AutomationEvent& rhs) noexcept {
      if(lhs.offset!=rhs.offset)return lhs.offset<rhs.offset;
      return lhs.order<rhs.order;
    });
 }
 bool overflow() const noexcept {return overflow_;}
 std::size_t size() const noexcept {return size_;}
 const AutomationEvent& operator[](std::size_t index) const noexcept {return entries_[index];}
};
} // namespace zplane
