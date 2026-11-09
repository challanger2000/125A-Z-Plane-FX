#pragma once
// 125A bipolar dual LFO: no audio allocations, sample and transport clock
// derived from explicit host state. The modulation never writes base params.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
namespace zplane {
class DualLfo final {
public:
 static constexpr unsigned kParameterCount=23,kFirst=11,kStride=6;
 using Params=std::array<double,kParameterCount>;
 struct Clock {double beatStart=0.,tempo=120.;};
 struct Result {double morph=.5,frequency=.5,transform=.5,x3Morph=.5,x3Q=.5;};
 enum Destination : unsigned {Off=0,Morph=1,Frequency=2,Transform=3,X3Q=4};
 static constexpr std::array<double,9> beatDivisions{.0625,.125,.25,.5,1.,2.,4.,8.,16.};
 static double normalized(double x) noexcept {
  return std::isfinite(x)?std::clamp(x,0.,1.):0.;
 }
 static unsigned index(double x,unsigned max) noexcept {
  return std::min(max,static_cast<unsigned>(std::floor(normalized(x)*max+.5)));
 }
 static bool active(Params const& p,unsigned n) noexcept {
  const auto b=kFirst+n*kStride;
  return p[b]>=.5&&normalized(p[b+2])>0.&&index(p[b+4],4)!=0;
 }
 static bool anyActive(Params const& p) noexcept {return active(p,0)||active(p,1);}
 static double freeHertz(double normalizedRate) noexcept {
  return .05*std::pow(240.,normalized(normalizedRate)); // 0.05...12 Hz
 }
 static double phase(double cycles) noexcept {
  return std::isfinite(cycles)?cycles-std::floor(cycles):0.;
 }
 static std::uint32_t hash(std::int64_t x) noexcept {
  auto v=static_cast<std::uint32_t>(x);
  v^=v>>16;v*=0x7feb352du;v^=v>>15;v*=0x846ca68bu;v^=v>>16;
  return v;
 }
 static double shape(unsigned type,double cycles) noexcept {
  const auto p=phase(cycles);
  switch(type){
   case 0:return std::sin(6.2831853071795864769*p);
   case 1:return 1.-4.*std::abs(p-.5);
   case 2:return 2.*p-1.;
   case 3:return p<.5?1.:-1.;
   case 4:{
    const auto cycle=std::floor(cycles);
    const auto bounded=std::clamp(cycle,-9007199254740991.,9007199254740991.);
    return 2.*static_cast<double>(hash(static_cast<std::int64_t>(bounded)))/
      static_cast<double>(std::numeric_limits<std::uint32_t>::max())-1.;
   }
   default:return 0.;
  }
 }
 static std::uint32_t quantum(double rate) noexcept {
  if(!std::isfinite(rate)||rate<=0.)rate=48000.;
  return static_cast<std::uint32_t>(std::clamp(std::floor(rate*.005+.5),1.,1024.));
 }
 static Result evaluate(Params const& p,std::uint64_t freeSamples,
                        double sampleRate,Clock clock,std::uint32_t blockOffset) noexcept {
  Result out{normalized(p[8]),normalized(p[9]),normalized(p[10]),
             normalized(p[1]),normalized(p[2])};
  if(!anyActive(p))return out;
  if(!std::isfinite(sampleRate)||sampleRate<=0.)sampleRate=48000.;
  const auto bpm=std::isfinite(clock.tempo)&&clock.tempo>=10.&&clock.tempo<=400.?
                   clock.tempo:120.;
  const auto seconds=static_cast<double>(freeSamples)/sampleRate;
  const auto beats=clock.beatStart+static_cast<double>(blockOffset)*bpm/(60.*sampleRate);
  for(unsigned n=0;n<2;++n){
   if(!active(p,n))continue;
   const unsigned b=kFirst+n*kStride;
   const auto cycles=p[b+5]>=.5 ?
       beats/beatDivisions[index(p[b+1],8)] : seconds*freeHertz(p[b+1]);
   const auto delta=.5*normalized(p[b+2])*shape(index(p[b+3],4),cycles);
   switch(index(p[b+4],4)){
    case Morph:out.morph+=delta;out.x3Morph+=delta;break;
    case Frequency:out.frequency+=delta;break;
    case Transform:out.transform+=delta;break;
    case X3Q:out.x3Q+=delta;break;
    default:break;
   }
  }
  out.morph=normalized(out.morph);out.frequency=normalized(out.frequency);
  out.transform=normalized(out.transform);
  out.x3Morph=normalized(out.x3Morph);out.x3Q=normalized(out.x3Q);
  return out;
 }
};
} // namespace zplane
