#pragma once
// Development-only adapter for user-supplied Emulator X3 filter-bank data.
// External research headers are downloaded from their origin at CMake time;
// they are NOT vendored or licensed for redistribution by this repository.
#include "emux_extended.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <limits>
#include <string>

namespace zplane {
class X3ResearchAdapter final {
 using Filter=emux::Filter;
 std::unique_ptr<emux::FactoryBank> factory_{};
 std::unique_ptr<emux::ExtendedBank> extended_{};
 Filter primaryL_{emux::Coefficients{}},primaryR_{emux::Coefficients{}};
 Filter nextL_{emux::Coefficients{}},nextR_{emux::Coefficients{}};
 struct Selection {
  unsigned index{0},rate{48000};
  float morph{0.5f},resonance{0.5f};
  bool operator==(const Selection& rhs) const noexcept {
   return index==rhs.index&&rate==rhs.rate&&morph==rhs.morph&&resonance==rhs.resonance;
  }
 };
 Selection primary_{},next_{},pending_{};
 std::uint32_t left_{0},total_{0};
 double mix_{1.},mixTarget_{1.},mixStep_{0.};
 std::uint32_t mixRemaining_{0};
 bool mixReady_{false};
 bool initialized_{false},queued_{false};
 static bool supported(unsigned rate) noexcept {
  return rate==44100||rate==48000||rate==96000||rate==192000;
 }
 static emux::MorphParameters programmed() noexcept {
  return {32,96,64,48,112,64};
 }
 static std::array<emux::DesignerSection,6> designer() noexcept {
  std::array<emux::DesignerSection,6> s{};
  s[0].shape=emux::Shape::eq;
  s[1].shape=emux::Shape::lowpass;
  return s;
 }
 bool coefficients(const Selection& selection,emux::Configuration& result) const {
  if(selection.index<1||selection.index>55||!supported(selection.rate))return false;
  if(selection.index<=33){
   if(!factory_)return false;
   result={factory_->coefficients(selection.index-1,selection.morph,selection.resonance,selection.rate),6};
  }else if(selection.index<=50){
   if(!extended_)return false;
   result=extended_->coefficients(selection.index-34,selection.morph,selection.resonance,selection.rate);
  }else{
   if(!extended_)return false;
   if(selection.index==55)
    result=extended_->designer_coefficients(designer(),selection.morph,selection.resonance,selection.rate);
   else{
    const auto kind=static_cast<emux::MorphKind>(selection.index-51);
    result=extended_->morph_coefficients(kind,programmed(),selection.morph,selection.resonance,selection.rate);
   }
  }
  return true;
 }
 void start(const Selection& request) {
  if(initialized_&&request==primary_)return;
  emux::Configuration cfg{};
  if(!coefficients(request,cfg))return;
  if(!initialized_){
   primaryL_.set_coefficients(cfg.coefficients,cfg.sections);
   primaryR_.set_coefficients(cfg.coefficients,cfg.sections);
   primary_=request;initialized_=true;return;
  }
  nextL_=primaryL_;nextR_=primaryR_;
  nextL_.set_coefficients(cfg.coefficients,cfg.sections);
  nextR_.set_coefficients(cfg.coefficients,cfg.sections);
  next_=request;
  left_=total_=std::max<std::uint32_t>(1,static_cast<std::uint32_t>(request.rate*0.005));
 }
public:
 X3ResearchAdapter()=default;
 // Control-thread only, before setActive. Never called from audio callback.
 bool load(const std::filesystem::path& directory,std::string& reason) {
  reason.clear();
  try {
   auto f=emux::FactoryBank::from_table_file((directory/"factory-bank.bin").string());
   auto e=emux::ExtendedBank::from_table_files(
      (directory/"standard-bank.bin").string(),
      (directory/"morph-support.bin").string());
   factory_=std::make_unique<emux::FactoryBank>(std::move(f));
   extended_=std::make_unique<emux::ExtendedBank>(std::move(e));
   reset();
   return true;
  }catch(const std::exception& e){
   factory_.reset();extended_.reset();reason=e.what();return false;
  }
 }
 bool hasBanks() const noexcept {return factory_&&extended_;}
 // The host's callback partitioning must NOT decide when a queued
 // coefficient transition starts. Give the outer processor the exact
 // remaining sample count so it can end a segment at the transition.
 std::uint32_t samplesToQueuedTransition() const noexcept {
  return queued_&&left_>0?left_:UINT32_MAX;
 }
 void reset() noexcept {
  primaryL_.reset();primaryR_.reset();nextL_.reset();nextR_.reset();
  left_=total_=mixRemaining_=0;queued_=initialized_=mixReady_=false;
  mix_=mixTarget_=1.;mixStep_=0.;
 }
 bool set(unsigned index,float morph,float resonance,unsigned sampleRate) {
  if(!hasBanks()||index<1||index>55||!supported(sampleRate))return false;
  if(!std::isfinite(morph)||!std::isfinite(resonance))return false;
  const Selection wanted{index,sampleRate,std::clamp(morph,0.f,1.f),std::clamp(resonance,0.f,1.f)};
  if(left_){
   if(wanted==next_)queued_=false;
   else{pending_=wanted;queued_=true;}
  }else start(wanted);
  return initialized_;
 }
 template<typename Sample>
 void process(const Sample* inL,const Sample* inR,Sample* outL,Sample* outR,
              std::uint32_t frames,double wet,bool bypass) noexcept {
  if(!initialized_||!inL||!inR||!outL||!outR)return;
  // A timed bypass and wet ramp avoids the discontinuity that was
  // previously audible on the first sample of each automated segment.
  const double desired=bypass?0.:std::clamp(std::isfinite(wet)?wet:0.,0.,1.);
  if(!mixReady_){
   mix_=mixTarget_=desired;mixReady_=true;
  }else if(desired!=mixTarget_){
   mixTarget_=desired;
   mixRemaining_=std::max<std::uint32_t>(1,
      static_cast<std::uint32_t>(primary_.rate*0.005));
   mixStep_=(mixTarget_-mix_)/static_cast<double>(mixRemaining_);
  }
  for(std::uint32_t i=0;i<frames;++i){
   // A completed crossfade must NOT clear queued_ here: the next
   // completeDeferred() at a host block boundary still needs to apply
   // the latest automation request. In the old version, the last
   // request was silently lost if a fade ended mid-block.
   const Sample dryL=std::isfinite(static_cast<double>(inL[i]))?inL[i]:static_cast<Sample>(0);
   const Sample dryR=std::isfinite(static_cast<double>(inR[i]))?inR[i]:static_cast<Sample>(0);
   // The external research core processes float32 even with 64-bit host
   // buffers. A finite double > FLT_MAX can overflow during conversion;
   // never pass infinity into stateful IIR sections. Dry/bypass retain
   // the exact original host precision.
   // Compare in double BEFORE narrowing: double->float overflow may
   // raise FE_OVERFLOW in a host with unmasked FP exceptions, before any
   // post-cast isfinite check can execute.
   constexpr double maxFloat=static_cast<double>(std::numeric_limits<float>::max());
   const double wideA=static_cast<double>(dryL),wideB=static_cast<double>(dryR);
   const float a=wideA>=-maxFloat&&wideA<=maxFloat?static_cast<float>(dryL):0.f;
   const float b=wideB>=-maxFloat&&wideB<=maxFloat?static_cast<float>(dryR):0.f;
   float pa=primaryL_.process_sample(a),pb=primaryR_.process_sample(b);
   // Upstream finite-but-extreme samples can still overflow the research
   // filter's INTERNAL float state. Reset only the affected channel and
   // output zero wet PCM for the faulting sample. This is fixed-capacity,
   // noexcept, allocation-free, and changes no ordinary valid audio.
   if(!std::isfinite(pa)){primaryL_.reset();pa=0.f;}
   if(!std::isfinite(pb)){primaryR_.reset();pb=0.f;}
   float wa=pa,wb=pb;
   if(left_){
    float na=nextL_.process_sample(a),nb=nextR_.process_sample(b);
    if(!std::isfinite(na)){nextL_.reset();na=0.f;}
    if(!std::isfinite(nb)){nextR_.reset();nb=0.f;}
    const double phase=double(total_-left_+1)/double(total_);
    wa=static_cast<float>(pa+(na-pa)*phase);
    wb=static_cast<float>(pb+(nb-pb)*phase);
    if(--left_==0){
     primaryL_=nextL_;primaryR_=nextR_;primary_=next_;
    }
   }
   // Guard the crossfade itself: subtracting two finite but very large
   // opposite-signed float values can overflow intermediate arithmetic.
   if(!std::isfinite(wa)){primaryL_.reset();nextL_.reset();wa=0.f;}
   if(!std::isfinite(wb)){primaryR_.reset();nextR_.reset();wb=0.f;}
   // At 100% wet, preserve the original float32 DSP output exactly.
   // A double-precision dry/wet subtraction+addition can otherwise
   // introduce a one-ULP difference for tiny impulses.
   if(mixRemaining_){
    mix_+=mixStep_;
    if(--mixRemaining_==0)mix_=mixTarget_;
   }
   const double amount=mix_;
   outL[i]=amount<=0.?dryL:amount>=1.?static_cast<Sample>(wa):
     static_cast<Sample>(static_cast<double>(dryL)+(double(wa)-static_cast<double>(dryL))*amount);
   outR[i]=amount<=0.?dryR:amount>=1.?static_cast<Sample>(wb):
     static_cast<Sample>(static_cast<double>(dryR)+(double(wb)-static_cast<double>(dryR))*amount);
  }
 }
 void completeDeferred() {
  // Called only after a sample-exact outer processing segment ends,
  // never from inside the per-sample filter loop.
  if(!left_&&queued_){const auto request=pending_;queued_=false;start(request);}
 }
};
} // namespace zplane
