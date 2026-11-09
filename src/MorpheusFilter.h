#pragma once
// 125A Morpheus/UltraProteus seven-stage cube engine.
// CubeData.h is the recovered Rossum Morpheus 289-record update table.
// The DSP is a mathematical reconstruction, not a bit-exact hardware claim.
#include "CubeData.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace zplane {
class MorpheusFilter final {
public:
 static constexpr unsigned kCubes=289;
 static constexpr unsigned kStages=7;
 static constexpr double kNativeRate=1250000.0/27.0;
 struct Control {
  unsigned index=0;
  double morph=0.5, frequency=0.5, transform=0.5;
  bool operator==(const Control&) const = default;
 };
 struct Section {
  double a=0.,radius=0.,z1=0.,z2=0.,scale=1.;
 };
 struct Description {
  std::array<Section,kStages> stages{};
  double gain=1.;
  bool valid=true;
  bool nullFilter=true;
 };
 static const char* name(unsigned index) noexcept {
  return morphosis::cube_data::kCubes[std::min(index,kCubes-1u)].name;
 }
 static Description describe(Control setting,double rate) noexcept {
  Description desc;
  if(setting.index==0)return desc;
  if(setting.index>=kCubes||!std::isfinite(rate)||rate<=0.){desc.valid=false;return desc;}
  if(!std::isfinite(setting.morph)||!std::isfinite(setting.frequency)||
     !std::isfinite(setting.transform)){desc.valid=false;return desc;}
  desc.nullFilter=false;
  const auto& record=morphosis::cube_data::kCubes[setting.index];
  const auto norm=[](double p) noexcept {
   return static_cast<int>(std::lround(std::clamp(p,0.,1.)*32768.));
  };
  // Recovered encoded register order: Transform / Frequency / Morph.
  const std::array<int,3> coordinates{norm(setting.transform),
                                       norm(setting.frequency),norm(setting.morph)};
  const auto ratio=kNativeRate/rate;
  for(unsigned stage=0;stage<kStages;++stage){
   const unsigned offset=stage*16;
   const auto poleAngle=decode(record,offset,coordinates,true);
   const auto poleDecay=decode(record,offset+4,coordinates,false);
   const auto zeroAngle=stage<kStages-1?decode(record,offset+8,coordinates,true):0.;
   const auto zeroDecay=stage<kStages-1?decode(record,offset+12,coordinates,false):1.;
   const auto radius=std::pow(std::clamp(1.-poleDecay,1.e-9,.999999),ratio);
   const auto zeroRadius=stage<kStages-1?
     std::pow(std::clamp(1.-zeroDecay,0.,.999999),ratio):0.;
   const auto angle=poleAngle*ratio;
   auto& c=desc.stages[stage];
   c.radius=std::clamp(radius,1.e-9,.999999);
   c.a=c.radius*std::cos(angle);
   c.z1=-2.*zeroRadius*std::cos(zeroAngle*ratio);
   c.z2=zeroRadius*zeroRadius;
   const bool normalize=(record.words[offset+8]&0x80000000u)!=0u;
   c.scale=normalize?
      1.+c.radius*c.radius-2.*c.a:1.;
   if(!std::isfinite(c.a)||!std::isfinite(c.z1)||
      !std::isfinite(c.scale)||c.scale<0.)desc.valid=false;
  }
  const std::array<std::uint32_t,4> gains{record.words[112],record.words[113],
                                        record.words[114],record.words[115]};
  const auto expanded=static_cast<float>(expand(interpolate(gains,coordinates),false));
  desc.gain=static_cast<float>(static_cast<double>(expanded)*
                              static_cast<double>(std::bit_cast<float>(0x338007ffu)));
  desc.valid=desc.valid&&std::isfinite(desc.gain)&&desc.gain>=0.&&desc.gain<1.e4;
  return desc;
 }
 static double magnitude(Description const& d,double hz,double rate) noexcept {
  if(!d.valid||!std::isfinite(hz)||!std::isfinite(rate)||rate<=0.)return 0.;
  if(d.nullFilter)return 1.;
  const auto w=2.*3.14159265358979323846*hz/rate;
  const auto cs=std::cos(w),sn=std::sin(w);
  const auto cs2=std::cos(2*w),sn2=std::sin(2*w);
  auto value=d.gain;
  for(const auto& s:d.stages){
   const auto numerator=std::hypot(1.+s.z1*cs+s.z2*cs2,
                                  -s.z1*sn-s.z2*sn2);
   const auto denominator=std::hypot(1.-2.*s.a*cs+s.radius*s.radius*cs2,
                                    2.*s.a*sn-s.radius*s.radius*sn2);
   value*=s.scale*numerator/std::max(denominator,1.e-15);
  }
  return value;
 }
 void reset(double sampleRate=48000.) noexcept {
  rate_=(std::isfinite(sampleRate)&&sampleRate>0.)?sampleRate:48000.;
  main_={};next_={};previous_={};
  fadeLeft_=0;pending_=false;hasMain_=false;
  wet_=wetTarget_=1.;wetStep_=0.;wetLeft_=0;wetReady_=false;
 }
 void set(Control requested,double wet,bool bypass) noexcept {
  requested.index=std::min(requested.index,kCubes-1u);
  requested.morph=clean(requested.morph);
  requested.frequency=clean(requested.frequency);
  requested.transform=clean(requested.transform);
  const auto requestedWet=bypass?0.:clean(wet);
  if(!wetReady_){wet_=wetTarget_=requestedWet;wetReady_=true;}
  else if(wetTarget_!=requestedWet){
   wetTarget_=requestedWet;wetLeft_=fadeSamples();
   wetStep_=(wetTarget_-wet_)/static_cast<double>(wetLeft_);
  }
  if(!hasMain_){
   main_.d=describe(requested,rate_);
   main_.control=requested;hasMain_=true;return;
  }
  if(fadeLeft_!=0){
   if(requested==next_.control){pending_=false;return;}
   waiting_=requested;pending_=true;return;
  }
  if(!(requested==main_.control))start(requested);
 }
 bool hasCurrent() const noexcept {return hasMain_;}
 std::uint32_t samplesToQueuedTransition() const noexcept {
  return fadeLeft_?fadeLeft_:std::numeric_limits<std::uint32_t>::max();
 }
 void completeDeferred() noexcept {
  if(fadeLeft_==0&&pending_){
   const auto wanted=waiting_;
   pending_=false;
   if(!(wanted==main_.control))start(wanted);
  }
 }
 template<typename Sample>
 void process(const Sample* left,const Sample* right,Sample* outL,Sample* outR,
              std::uint32_t frames) noexcept {
  for(std::uint32_t n=0;n<frames;++n){
   const double inL=sanitize(static_cast<double>(left[n]));
   const double inR=sanitize(static_cast<double>(right[n]));
   const double oldL=hasMain_?tick(main_,0,inL):inL;
   const double oldR=hasMain_?tick(main_,1,inR):inR;
   double filteredL=oldL,filteredR=oldR;
   if(fadeLeft_){
    const auto newL=tick(next_,0,inL),newR=tick(next_,1,inR);
    const double alpha=1.-static_cast<double>(fadeLeft_-1)/
         static_cast<double>(fadeSamples());
    filteredL=oldL+(newL-oldL)*alpha;
    filteredR=oldR+(newR-oldR)*alpha;
    if(--fadeLeft_==0)main_=next_;
   }
   if(wetLeft_){
    wet_+=wetStep_;
    if(--wetLeft_==0)wet_=wetTarget_;
   }
   const auto render=[this](double dry,double filtered) noexcept {
    const double result=wet_<=0.?dry:wet_>=1.?filtered:
                        dry+(filtered-dry)*wet_;
    return std::isfinite(result)&&std::abs(result)<1.e9?result:0.;
   };
   outL[n]=static_cast<Sample>(render(inL,filteredL));
   outR[n]=static_cast<Sample>(render(inR,filteredR));
  }
 }
private:
 struct StageState {double w1=0.,w2=0.;};
 struct Bank {
  Control control{};
  Description d{};
  std::array<std::array<StageState,kStages>,2> state{};
 };
 Bank main_{},next_{};
 Control waiting_{},previous_{};
 bool pending_{false},hasMain_{false};
 double rate_{48000.};
 std::uint32_t fadeLeft_{0};
 double wet_{1.},wetTarget_{1.},wetStep_{0.};
 std::uint32_t wetLeft_{0};
 bool wetReady_{false};
 std::uint32_t fadeSamples() const noexcept {
  return static_cast<std::uint32_t>(std::clamp(rate_*.005,1.,48000.));
 }
 static double clean(double value) noexcept {
  return std::isfinite(value)?std::clamp(value,0.,1.):0.5;
 }
 static double sanitize(double input) noexcept {
  return std::isfinite(input)&&std::abs(input)<1.e6?input:0.;
 }
 void start(Control setting) noexcept {
  next_={};
  next_.control=setting;next_.d=describe(setting,rate_);
  fadeLeft_=fadeSamples();
 }
 static double tick(Bank& bank,int channel,double input) noexcept {
  if(!bank.d.valid||bank.d.nullFilter)return input;
  auto x=input;
  auto& states=bank.state[static_cast<std::size_t>(channel)];
  for(unsigned i=0;i<kStages;++i){
   auto& st=states[i];const auto& c=bank.d.stages[i];
   const double w=c.scale*x+2.*c.a*st.w1-c.radius*c.radius*st.w2;
   const double y=w+c.z1*st.w1+c.z2*st.w2;
   st.w2=st.w1;st.w1=w;
   if(!std::isfinite(y)||std::abs(y)>1.e7){
    for(auto& xstate:states)xstate={};
    return 0.;
   }
   x=y;
  }
  const auto y=x*bank.d.gain;
  if(!std::isfinite(y)||std::abs(y)>1.e7){
   for(auto& xstate:states)xstate={};
   return 0.;
  }
  return y;
 }
 static std::int64_t signedField(std::uint32_t value,int bits) noexcept {
  if(bits==16)return static_cast<std::int16_t>(value&0xffffu);
  return std::bit_cast<std::int32_t>(value);
 }
 static std::uint32_t weights(int coordinate) noexcept {
  const auto q=std::clamp(coordinate,0,32768);
  return ((static_cast<std::uint32_t>(-q)&0xffffu)<<16)|
         (static_cast<std::uint32_t>(q-32768)&0xffffu);
 }
 static std::uint32_t dot(std::uint32_t a,std::uint32_t b) noexcept {
  const auto v=signedField(a,16)*signedField(b,16)+
               signedField(a>>16,16)*signedField(b>>16,16);
  return static_cast<std::uint32_t>(v);
 }
 static std::uint32_t pack(std::uint32_t a,std::uint32_t b) noexcept {
  const auto shifted=std::bit_cast<std::int32_t>(a)>>15;
  return ((b<<1)&0xffff0000u)|
         (static_cast<std::uint32_t>(shifted)&0xffffu);
 }
 static std::uint32_t interpolate(const std::array<std::uint32_t,4>& words,
                                  const std::array<int,3>& xyz) noexcept {
  const auto w1=weights(xyz[0]),w4=weights(xyz[1]),w5=weights(xyz[2]);
  const std::array<std::uint32_t,4> d{
   dot(w1,words[0]),dot(w1,words[1]),dot(w1,words[2]),dot(w1,words[3])
  };
  const auto a=dot(w4,pack(d[0],d[1]));
  const auto b=dot(w4,pack(d[2],d[3]));
  return 0u-dot(w5,pack(a,b));
 }
 static std::uint32_t expand(std::uint32_t value,bool angle) noexcept {
  const auto exponent=(value>>26)&15u, mantissa=(value>>15)&2047u;
  if(angle)return (mantissa|2048u)<<exponent;
  const auto nonzero=exponent?1u:0u;
  return (mantissa|(nonzero<<11))<<(exponent-nonzero);
 }
 static double decode(const morphosis::cube_data::CubeRecord& record,
                      unsigned offset,const std::array<int,3>& xyz,bool angle) noexcept {
  std::array<std::uint32_t,4> words{};
  for(unsigned i=0;i<4;++i)
   words[i]=record.words[offset+i]&(i==0?0x7fffffffu:0xffffffffu);
  const auto expanded=static_cast<float>(expand(interpolate(words,xyz),angle));
  const auto factor=std::bit_cast<float>(angle?0x32c90fdbu:0x32800800u);
  return static_cast<float>(static_cast<double>(expanded)*static_cast<double>(factor));
 }
};
} // namespace zplane
