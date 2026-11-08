#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace zplane {
enum class Mode : unsigned { LowPass=0, HighPass=1, BandPass=2, Notch=3 };
struct Settings {
  Mode mode{Mode::LowPass};
  double frequency{1000.0};
  double quality{0.7071067811865476};
  double wet{1.0};
  bool bypass{false};
};
class Biquad {
  double b0_{1.},b1_{0.},b2_{0.},a1_{0.},a2_{0.};
  double x1_{0.},x2_{0.},y1_{0.},y2_{0.};
public:
  void reset() noexcept {x1_=x2_=y1_=y2_=0.;}
  void configure(Mode mode,double rate,double freq,double q) noexcept {
    if(!std::isfinite(rate) || rate<=0. || !std::isfinite(freq) || !std::isfinite(q) || q<=0.) {b0_=1.; b1_=b2_=a1_=a2_=0.; return;}
    const double nyquist=rate*0.5;
    const double f=std::clamp(freq,1.e-5,std::nextafter(nyquist,0.));
    const double w=2.0*3.141592653589793238462643383279502884*f/rate;
    const double co=std::cos(w), si=std::sin(w), alpha=si/(2.*std::clamp(q,0.05,40.0));
    const double norm=1.0/(1.0+alpha);
    double n0=1.,n1=0.,n2=0.;
    switch(mode) {
      case Mode::LowPass: n0=(1.-co)*0.5;n1=1.-co;n2=n0;break;
      case Mode::HighPass:n0=(1.+co)*0.5;n1=-(1.+co);n2=n0;break;
      case Mode::BandPass:n0=alpha;n1=0.;n2=-alpha;break;
      case Mode::Notch:n0=1.;n1=-2.*co;n2=1.;break;
    }
    b0_=n0*norm;b1_=n1*norm;b2_=n2*norm;a1_=-2.*co*norm;a2_=(1.-alpha)*norm;
  }
  double tick(double x) noexcept {
    if(!std::isfinite(x)){reset();return 0.;}
    // Direct Form I: separate x/y delay lines; bounded deterministic work.
    const double y=b0_*x+b1_*x1_+b2_*x2_-a1_*y1_-a2_*y2_;
    x2_=x1_;x1_=x;y2_=y1_;y1_=std::isfinite(y)?y:0.;
    if(std::abs(y1_)<1.e-290){y1_=0.;}
    if(std::abs(y2_)<1.e-290){y2_=0.;}
    return y1_;
  }
};
class StereoFilter {
  Biquad l_,r_;
  Settings last_{};
  double rate_{48000.};
public:
  void reset() noexcept {l_.reset();r_.reset();}
  void configure(double rate,const Settings& s) noexcept {
    if(std::isfinite(rate)&&rate>0.)rate_=rate;
    last_=s;last_.wet=std::clamp(std::isfinite(s.wet)?s.wet:0.,0.,1.);
    l_.configure(s.mode,rate_,s.frequency,s.quality);
    r_.configure(s.mode,rate_,s.frequency,s.quality);
  }
  template<typename Sample>
  void process(const Sample* inL,const Sample* inR,Sample* outL,Sample* outR,std::uint32_t frames) noexcept {
    if(!inL||!inR||!outL||!outR)return;
    for(std::uint32_t i=0;i<frames;++i){
      const double xL=inL[i],xR=inR[i];
      // Keep filter states warm when dry/bypassed, for seamless transitions.
      const double yL=l_.tick(xL),yR=r_.tick(xR);
      const double wet=last_.bypass?0.0:last_.wet;
      outL[i]=static_cast<Sample>(wet==0.0?xL:xL+(yL-xL)*wet);
      outR[i]=static_cast<Sample>(wet==0.0?xR:xR+(yR-xR)*wet);
    }
  }
};
} // namespace zplane
