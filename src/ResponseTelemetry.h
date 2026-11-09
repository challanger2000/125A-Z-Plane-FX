#pragma once
// Host-controlled transfer of the last DSP-computed LFO target to VSTGUI.
// Plain packet is copied into preallocated Steinberg Data Exchange blocks.
// Receiving Controller owns the mailbox; no shared Processor/GUI pointers.
#include "DualLfo.h"
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace zplane {
struct ResponseTelemetry final {
 static constexpr std::uint32_t kMagic=0x5A504C46u;
 static constexpr std::uint32_t kVersion=1;
 std::uint32_t magic{kMagic},version{kVersion};
 std::uint64_t renderedFrames{0};
 double sampleRate{48000.};
 std::array<double,11> base{};
 DualLfo::Result effective{};
 std::uint32_t modulationActive{0};
 std::uint32_t reserved{0};
};
static_assert(std::is_trivially_copyable_v<ResponseTelemetry>);
inline bool validTelemetry(const ResponseTelemetry& v) noexcept {
 if(v.magic!=ResponseTelemetry::kMagic||v.version!=ResponseTelemetry::kVersion||
    !std::isfinite(v.sampleRate)||v.sampleRate<=0.||
    v.modulationActive>1||v.reserved!=0)return false;
 for(const double value:v.base)
  if(!std::isfinite(value)||value<0.||value>1.)return false;
 const std::array<double,5> values{v.effective.morph,v.effective.frequency,
   v.effective.transform,v.effective.x3Morph,v.effective.x3Q};
 for(const double value:values)
  if(!std::isfinite(value)||value<0.||value>1.)return false;
 return true;
}
class ResponseTelemetryMailbox final {
 std::atomic<std::uint64_t> sequence_{0},frames_{0};
 std::atomic<double> rate_{48000.};
 std::array<std::atomic<double>,11> base_{};
 std::array<std::atomic<double>,5> effective_{};
 std::atomic<std::uint32_t> active_{0};
 std::atomic<bool> available_{false};
public:
 void clear() noexcept {available_.store(false,std::memory_order_release);}
 void publish(const ResponseTelemetry& packet) noexcept {
  if(!validTelemetry(packet))return;
  sequence_.fetch_add(1,std::memory_order_acq_rel);
  frames_.store(packet.renderedFrames,std::memory_order_relaxed);
  rate_.store(packet.sampleRate,std::memory_order_relaxed);
  for(std::size_t i=0;i<base_.size();++i)
   base_[i].store(packet.base[i],std::memory_order_relaxed);
  const std::array<double,5> v{packet.effective.morph,packet.effective.frequency,
    packet.effective.transform,packet.effective.x3Morph,packet.effective.x3Q};
  for(std::size_t i=0;i<effective_.size();++i)
   effective_[i].store(v[i],std::memory_order_relaxed);
  active_.store(packet.modulationActive,std::memory_order_relaxed);
  sequence_.fetch_add(1,std::memory_order_release);
  available_.store(true,std::memory_order_release);
 }
 bool read(ResponseTelemetry& out) const noexcept {
  if(!available_.load(std::memory_order_acquire))return false;
  for(unsigned attempt=0;attempt<16;++attempt){
   const auto before=sequence_.load(std::memory_order_acquire);
   if(before&1u)continue;
   ResponseTelemetry next{};
   next.renderedFrames=frames_.load(std::memory_order_relaxed);
   next.sampleRate=rate_.load(std::memory_order_relaxed);
   for(std::size_t i=0;i<base_.size();++i)
    next.base[i]=base_[i].load(std::memory_order_relaxed);
   std::array<double,5> v{};
   for(std::size_t i=0;i<v.size();++i)
    v[i]=effective_[i].load(std::memory_order_relaxed);
   next.effective={v[0],v[1],v[2],v[3],v[4]};
   next.modulationActive=active_.load(std::memory_order_relaxed);
   if(before==sequence_.load(std::memory_order_acquire)&&
      available_.load(std::memory_order_acquire)){
    out=next;return validTelemetry(out);
   }
  }
  return false;
 }
};
} // namespace zplane
