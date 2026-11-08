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
  if(!std::isfinite(rate)||rate<=0.||!std::isfinite(freq)||!std::isfinite(q)||q<=0.){
   b0_=1.;b1_=b2_=a1_=a2_=0.;return;
  }
  const double nyquist=rate*0.5;
  const double hi=std::nextafter(nyquist,0.0);
  const double lo=std::min(1.e-5,hi);
  const double f=std::clamp(freq,lo,hi);
  const double w=2.*3.1415926535897932384626433832795*f/rate;
  const double co=std::cos(w),si=std::sin(w),alpha=si/(2.*std::clamp(q,0.05,40.));
  const double norm=1./(1.+alpha);
  double n0=1.,n1=0.,n2=0.;
  switch(mode){
   case Mode::LowPass:n0=(1.-co)*.5;n1=1.-co;n2=n0;break;
   case Mode::HighPass:n0=(1.+co)*.5;n1=-(1.+co);n2=n0;break;
   case Mode::BandPass:n0=alpha;n1=0.;n2=-alpha;break;
   case Mode::Notch:n0=1.;n1=-2.*co;n2=1.;break;
  }
  b0_=n0*norm;b1_=n1*norm;b2_=n2*norm;a1_=-2.*co*norm;a2_=(1.-alpha)*norm;
 }
 double tick(double x) noexcept {
  if(!std::isfinite(x)){reset();return 0.;}
  const double y=b0_*x+b1_*x1_+b2_*x2_-a1_*y1_-a2_*y2_;
  x2_=x1_;x1_=x;y2_=y1_;y1_=std::isfinite(y)?y:0.;
  if(std::abs(y1_)<1.e-290)y1_=0.;
  if(std::abs(y2_)<1.e-290)y2_=0.;
  return y1_;
 }
};
class StereoFilter {
 Biquad primaryL_{},primaryR_{},secondaryL_{},secondaryR_{};
 Settings primarySettings_{},secondarySettings_{},pending_{};
 double rate_{48000.};
 double wet_{1.},wetTarget_{1.},wetStep_{0.};
 std::uint32_t wetLeft_{0},filterLeft_{0},filterTotal_{0};
 bool ready_{false},pendingValid_{false};
 static bool sameFilter(const Settings& a,const Settings& b) noexcept {
  return a.mode==b.mode&&a.frequency==b.frequency&&a.quality==b.quality;
 }
 std::uint32_t smoothFrames() const noexcept {
  return static_cast<std::uint32_t>(std::clamp(rate_*0.005,1.0,48000.0));
 }
 void startFilterChange(const Settings& s) noexcept {
  if(sameFilter(primarySettings_,s))return;
  secondaryL_=primaryL_;secondaryR_=primaryR_;
  secondaryL_.configure(s.mode,rate_,s.frequency,s.quality);
  secondaryR_.configure(s.mode,rate_,s.frequency,s.quality);
  secondarySettings_=s;
  filterLeft_=filterTotal_=smoothFrames();
 }
public:
 void reset() noexcept {
  primaryL_.reset();primaryR_.reset();secondaryL_.reset();secondaryR_.reset();
  filterLeft_=filterTotal_=wetLeft_=0;pendingValid_=false;ready_=false;
 }
 void configure(double rate,const Settings& incoming) noexcept {
  const double wantedRate=(std::isfinite(rate)&&rate>0.)?rate:rate_;
  Settings s=incoming;
  if(!std::isfinite(s.frequency)||s.frequency<=0.)s.frequency=1000.;
  if(!std::isfinite(s.quality)||s.quality<=0.)s.quality=.7071067811865476;
  s.wet=std::clamp(std::isfinite(s.wet)?s.wet:0.,0.,1.);
  if(wantedRate!=rate_){rate_=wantedRate;reset();}
  const double desiredWet=s.bypass?0.0:s.wet;
  if(!ready_){
   rate_=wantedRate;primarySettings_=s;
   primaryL_.configure(s.mode,rate_,s.frequency,s.quality);
   primaryR_.configure(s.mode,rate_,s.frequency,s.quality);
   wet_=wetTarget_=desiredWet;ready_=true;
   return;
  }
  if(desiredWet!=wetTarget_){
   wetTarget_=desiredWet;wetLeft_=smoothFrames();
   wetStep_=(wetTarget_-wet_)/static_cast<double>(wetLeft_);
  }
  if(filterLeft_){
   // Latest request wins; never restart an already running crossfade
   // from stale history when automation is denser than 5 ms.
   if(!sameFilter(s,secondarySettings_)){pending_=s;pendingValid_=true;}
   else pendingValid_=false;
  }else startFilterChange(s);
 }
 template<typename Sample>
 void process(const Sample* inL,const Sample* inR,Sample* outL,Sample* outR,std::uint32_t frames) noexcept {
  if(!inL||!inR||!outL||!outR)return;
  for(std::uint32_t i=0;i<frames;++i){
   if(!filterLeft_&&pendingValid_){
    const auto req=pending_;pendingValid_=false;startFilterChange(req);
   }
   const double xL=std::isfinite(static_cast<double>(inL[i]))?static_cast<double>(inL[i]):0.;
   const double xR=std::isfinite(static_cast<double>(inR[i]))?static_cast<double>(inR[i]):0.;
   const double pL=primaryL_.tick(xL),pR=primaryR_.tick(xR);
   double fL=pL,fR=pR;
   if(filterLeft_){
    const double sL=secondaryL_.tick(xL),sR=secondaryR_.tick(xR);
    const double phase=static_cast<double>(filterTotal_-filterLeft_+1)/static_cast<double>(filterTotal_);
    fL=pL+(sL-pL)*phase;fR=pR+(sR-pR)*phase;
    if(--filterLeft_==0){
     primaryL_=secondaryL_;primaryR_=secondaryR_;primarySettings_=secondarySettings_;
    }
   }
   if(wetLeft_){
    wet_+=wetStep_;
    if(--wetLeft_==0)wet_=wetTarget_;
   }
   outL[i]=static_cast<Sample>(wet_==0.?xL:xL+(fL-xL)*wet_);
   outR[i]=static_cast<Sample>(wet_==0.?xR:xR+(fR-xR)*wet_);
  }
 }
};
} // namespace zplane
