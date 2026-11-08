#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>

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
  // Stable insertion by time and equal-time host queue order.
  auto at=size_;
  while(at>0&&entries_[at-1].offset>offset){
   entries_[at]=entries_[at-1];--at;
  }
  entries_[at]={offset,param,value,static_cast<std::uint32_t>(size_)};
  ++size_;return true;
 }
 bool overflow() const noexcept {return overflow_;}
 std::size_t size() const noexcept {return size_;}
 const AutomationEvent& operator[](std::size_t index) const noexcept {return entries_[index];}
};
} // namespace zplane
