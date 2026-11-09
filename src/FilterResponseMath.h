#pragma once
// GUI-thread-only deterministic steady-state transfer and graph domain.
// Graph is deliberately a 48 kHz reference preview until the processor's
// actual sample rate is explicitly transported to the controller.
#include "MorpheusFilter.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace zplane::response {
constexpr double kRate=48000.;
constexpr std::size_t kPoints=256;
constexpr double kMinHz=20., kMaxHz=20000.;
using Complex=std::complex<double>;
using Params=std::array<double,11>;
using Curve=std::array<float,kPoints>;
inline double clamp01(double x) noexcept {
 return std::isfinite(x)?std::clamp(x,0.,1.):0.;
}
inline double frequency(std::size_t i) noexcept {
 return kMinHz*std::pow(kMaxHz/kMinHz,
   static_cast<double>(std::min(i,kPoints-1))/(kPoints-1));
}
inline Complex z(double hz,double rate=kRate) noexcept {
 const auto omega=-2.*3.1415926535897932384626433832795*
      std::clamp(hz,0.,rate*.5)/rate;
 return {std::cos(omega),std::sin(omega)};
}
inline Complex morpheusTransfer(MorpheusFilter::Description const& d,double hz,
                               double rate=kRate) noexcept {
 if(!d.valid)return {std::numeric_limits<double>::quiet_NaN(),0.};
 if(d.nullFilter)return {1.,0.};
 const auto zz=z(hz,rate),zz2=zz*zz;
 Complex h{d.gain,0.};
 for(auto const& s:d.stages){
  const auto denominator=Complex{1.,0.}-2.*s.a*zz+
      (s.a*s.a+s.b*s.b)*zz2;
  if(std::abs(denominator)<1.e-18)return {1.e9,0.};
  h*=s.scale*(Complex{1.,0.}+s.z1*zz+s.z2*zz2)/denominator;
 }
 return h;
}
inline Complex legacyTransfer(Params const& p,double hz,double rate=kRate) noexcept {
 const auto shape=std::min(3,static_cast<int>(std::floor(clamp01(p[0])*4.)));
 const auto freq=std::clamp(20.*std::pow(1000.,clamp01(p[1])),
                   1.e-5,std::nextafter(rate*.5,0.));
 const auto q=std::clamp(.2*std::pow(100.,clamp01(p[2])),.05,40.);
 const auto w=2.*3.14159265358979323846*freq/rate;
 const auto co=std::cos(w),si=std::sin(w),alpha=si/(2.*q);
 double b0=1.,b1=0.,b2=0.;
 switch(shape){
  case 0:b0=(1.-co)*.5;b1=1.-co;b2=b0;break;
  case 1:b0=(1.+co)*.5;b1=-(1.+co);b2=b0;break;
  case 2:b0=alpha;b1=0.;b2=-alpha;break;
  case 3:b0=1.;b1=-2.*co;b2=1.;break;
 }
 const auto norm=1./(1.+alpha);
 const auto zz=z(hz,rate);
 const Complex numerator=(b0+b1*zz+b2*zz*zz)*norm;
 const Complex denominator=Complex{1.,0.}-2.*co*norm*zz+
      (1.-alpha)*norm*zz*zz;
 return numerator/denominator;
}
inline Complex wetTransfer(Complex h,Params const& p) noexcept {
 const auto wet=p[4]>=.5?0.:clamp01(p[3]);
 return Complex{1.-wet,0.}+wet*h;
}
inline float levelDb(Complex h) noexcept {
 const auto magnitude=std::abs(h);
 if(!std::isfinite(magnitude))return 24.f;
 return static_cast<float>(std::clamp(20.*std::log10(std::max(magnitude,1.e-12)),
                                      -48.,24.));
}
inline MorpheusFilter::Control cubeControl(Params const& p) noexcept {
 return {static_cast<unsigned>(std::clamp(std::floor(clamp01(p[7])*288.+.5),0.,288.)),
         clamp01(p[8]),clamp01(p[9]),clamp01(p[10])};
}
inline Curve computeAnalytic(Params const& p,bool& valid) noexcept {
 Curve result{};
 valid=p[6]>=.5 || p[5]<1./110.;
 MorpheusFilter::Description d;
 if(p[6]>=.5){
  d=MorpheusFilter::describe(cubeControl(p),kRate);
  valid=d.valid;
 }
 if(!valid)return result; // active X3 requires measured transfer, never a fake legacy curve
 for(std::size_t i=0;i<result.size();++i){
  const auto hz=frequency(i);
  auto h=p[6]>=.5?morpheusTransfer(d,hz):legacyTransfer(p,hz);
  result[i]=levelDb(wetTransfer(h,p));
 }
 return result;
}
} // namespace zplane::response
