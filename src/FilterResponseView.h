#pragma once
// 125A VSTGUI custom response plot. UI thread only; no audio callback work.
// X3: FFT of an impulse passed through the SAME research DSP adapter that
// renders audio. The impulse response is finite-window approximated (8192).
#include "FilterResponseMath.h"
#include "ResponseTelemetry.h"
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/uidescription/uiattributes.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include <algorithm>
#include <atomic>
#include <bit>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace zplane {
#ifdef ZPLANE_RESPONSE_DIAGNOSTICS
namespace response_qa {
inline std::atomic<std::uint64_t> created{0},drawn{0},updated{0},curveSignature{0},baseSignature{0},live{0};
inline std::atomic<std::uint64_t> baseComputations{0},x3Computations{0},graphComputeMicroseconds{0},graphComputeMaxMicroseconds{0};
}
#endif
class FilterResponseView final : public VSTGUI::CView {
 Steinberg::Vst::EditController* controller_{};
 const std::atomic<double>* hostRate_{};
 const std::atomic<bool>* hostRateKnown_{};
 const ResponseTelemetryMailbox* telemetry_{};
 double sampleRate_{response::kRate};
 bool actualRate_{false};
 response::Params parameters_{},effective_{};
 response::Curve db_{},baseDb_{};
 bool haveParameters_{false},valid_{false},baseValid_{false},modulating_{false};
 VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer_{};
#ifdef ZPLANE_WITH_X3_RESEARCH
 std::unique_ptr<X3ResearchAdapter> x3_;
 bool x3Available_{false};
 static constexpr std::size_t kFftSize=8192;
 std::vector<std::complex<double>> spectrum_{};
 std::vector<float> input_{},output_{};
 static bool loadX3(X3ResearchAdapter& adapter) {
  HMODULE module=nullptr;
  if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|
       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
       reinterpret_cast<LPCWSTR>(&FilterResponseView::loadX3),&module)||!module)
    return false;
  EmbeddedX3Resources embedded;
  if(!embedded.extract(module))return false;
  std::string reason;
  return adapter.load(embedded.directory(),reason);
 }
 static void fft(std::vector<std::complex<double>>& values) noexcept {
  const auto n=values.size();
  for(std::size_t i=1,j=0;i<n;++i){
   auto bit=n>>1;
   for(;j&bit;bit>>=1)j^=bit;
   j^=bit;
   if(i<j)std::swap(values[i],values[j]);
  }
  for(std::size_t len=2;len<=n;len<<=1){
   const auto step=-2.*3.14159265358979323846/len;
   const std::complex<double> root{std::cos(step),std::sin(step)};
   for(std::size_t i=0;i<n;i+=len){
    std::complex<double> w{1.,0.};
    for(std::size_t j=0;j<len/2;++j){
     const auto a=values[i+j],b=values[i+j+len/2]*w;
     values[i+j]=a+b;
     values[i+j+len/2]=a-b;
     w*=root;
    }
   }
  }
 }
 bool refreshX3(const response::Params& source,response::Curve& destination) {
  if(!x3Available_||!x3_||
     sampleRate_>static_cast<double>(std::numeric_limits<unsigned>::max()))return false;
  const auto index=static_cast<unsigned>(
    std::clamp(std::floor(response::clamp01(source[5])*55.+.5),0.,55.));
  if(index==0)return false;
#ifdef ZPLANE_RESPONSE_DIAGNOSTICS
  response_qa::x3Computations.fetch_add(1,std::memory_order_relaxed);
#endif
  x3_->reset();
  const auto rounded=static_cast<unsigned>(std::llround(sampleRate_));
  if(std::abs(sampleRate_-rounded)>.01)return false;
  if(!x3_->set(index,static_cast<float>(response::clamp01(source[1])),
                       static_cast<float>(response::clamp01(source[2])),rounded))
    return false;
  std::fill(input_.begin(),input_.end(),0.f);
  std::fill(output_.begin(),output_.end(),0.f);
  input_[0]=.1f;
  x3_->process(input_.data(),input_.data(),output_.data(),output_.data(),
               static_cast<std::uint32_t>(kFftSize),1.,false);
  for(std::size_t i=0;i<kFftSize;++i){
   if(!std::isfinite(output_[i]))return false;
   spectrum_[i]={static_cast<double>(output_[i])*10.,0.};
  }
  fft(spectrum_);
  for(std::size_t i=0;i<destination.size();++i){
   const auto hz=response::frequency(i);
   if(hz>=sampleRate_*.5){
    destination[i]=std::numeric_limits<float>::quiet_NaN();
    continue;
   }
   const auto bin=hz*kFftSize/sampleRate_;
   const auto lo=std::min(static_cast<std::size_t>(bin),kFftSize/2-1);
   const auto alpha=std::clamp(bin-lo,0.,1.);
   const auto h=spectrum_[lo]*(1.-alpha)+spectrum_[lo+1]*alpha;
   destination[i]=response::levelDb(response::wetTransfer(h,source));
  }
  return true;
 }
#endif
 void calculate(const response::Params& p,response::Curve& curve,bool& valid){
#ifdef ZPLANE_WITH_X3_RESEARCH
  if(p[6]<.5&&p[5]>=1./110.){
   if(p[4]>=.5||p[3]<=0.){
    curve.fill(0.f);valid=true;
   }else valid=refreshX3(p,curve);
   return;
  }
#endif
  curve=response::computeAnalytic(p,valid,sampleRate_);
 }
 void updateFromController(){
  if(!controller_)return;
  response::Params base{};
  for(std::size_t i=0;i<base.size();++i)
   base[i]=response::clamp01(controller_->getParamNormalized(
       static_cast<Steinberg::Vst::ParamID>(i)));
  const bool known=hostRateKnown_&&hostRateKnown_->load(std::memory_order_acquire);
  const auto reported=known&&hostRate_?hostRate_->load(std::memory_order_relaxed):response::kRate;
  const auto newRate=std::isfinite(reported)&&reported>0.?reported:response::kRate;
  auto target=base;
  bool live=false;
  ResponseTelemetry packet{};
  // Live curve may only use values that the audio engine actually used.
  // Avoid showing previous-block telemetry after a fresh GUI/host edit.
  if(telemetry_&&telemetry_->read(packet)&&packet.modulationActive!=0&&
     std::abs(packet.sampleRate-newRate)<1.e-6){
   bool sameBase=true;
   for(std::size_t i=0;i<base.size();++i)
    if(std::abs(base[i]-packet.base[i])>1.e-7){sameBase=false;break;}
   if(sameBase){
    target[8]=packet.effective.morph;
    target[9]=packet.effective.frequency;
    target[10]=packet.effective.transform;
    target[1]=packet.effective.x3Morph;
    target[2]=packet.effective.x3Q;
    live=true;
   }
  }
  const bool baseChanged=!haveParameters_||base!=parameters_||
    sampleRate_!=newRate||actualRate_!=known;
  if(haveParameters_&&base==parameters_&&target==effective_&&
     sampleRate_==newRate&&actualRate_==known&&modulating_==live)return;
  haveParameters_=true;
  parameters_=base;effective_=target;sampleRate_=newRate;
  actualRate_=known;modulating_=live;
#ifdef ZPLANE_RESPONSE_DIAGNOSTICS
  response_qa::updated.fetch_add(1,std::memory_order_relaxed);
#endif
#ifdef ZPLANE_RESPONSE_DIAGNOSTICS
  const auto graphStarted=std::chrono::steady_clock::now();
#endif
  calculate(effective_,db_,valid_);
  // A stationary base transfer is invariant under LFO phase. Each X3
  // recomputation would otherwise perform another 8192-frame impulse + FFT.
  if(modulating_){
   if(baseChanged){
    calculate(parameters_,baseDb_,baseValid_);
#ifdef ZPLANE_RESPONSE_DIAGNOSTICS
    response_qa::baseComputations.fetch_add(1,std::memory_order_relaxed);
#endif
   }
  }else {baseDb_=db_;baseValid_=valid_;}
#ifdef ZPLANE_RESPONSE_DIAGNOSTICS
  const auto elapsed=std::chrono::duration_cast<std::chrono::microseconds>(
    std::chrono::steady_clock::now()-graphStarted).count();
  const auto micros=static_cast<std::uint64_t>(std::max<std::int64_t>(0,elapsed));
  response_qa::graphComputeMicroseconds.fetch_add(micros,std::memory_order_relaxed);
  auto maxUs=response_qa::graphComputeMaxMicroseconds.load(std::memory_order_relaxed);
  while(maxUs<micros&&!response_qa::graphComputeMaxMicroseconds.compare_exchange_weak(
    maxUs,micros,std::memory_order_relaxed,std::memory_order_relaxed)){}
#endif
  invalid();
 }
public:
 explicit FilterResponseView(const VSTGUI::CRect& rect,
                             Steinberg::Vst::EditController* controller,
                             const std::atomic<double>* hostRate,
                             const std::atomic<bool>* hostRateKnown,
                             const ResponseTelemetryMailbox* telemetry)
   : VSTGUI::CView(rect),controller_(controller),hostRate_(hostRate),
     hostRateKnown_(hostRateKnown),telemetry_(telemetry) {
  setMouseEnabled(false);
#ifdef ZPLANE_WITH_X3_RESEARCH
  x3_=std::make_unique<X3ResearchAdapter>();
  x3Available_=loadX3(*x3_);
  spectrum_.resize(kFftSize);
  input_.resize(kFftSize);
  output_.resize(kFftSize);
#endif
#ifdef ZPLANE_RESPONSE_DIAGNOSTICS
  response_qa::created.fetch_add(1,std::memory_order_relaxed);
#endif
  updateFromController();
  timer_=VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>(
    [this](VSTGUI::CVSTGUITimer*){updateFromController();},100);
 }
 ~FilterResponseView() noexcept override {
  if(timer_)timer_->stop();
  timer_=nullptr;
 }
 void draw(VSTGUI::CDrawContext* context) override {
  if(!context)return;
  const auto bounds=getViewSize();

  context->setFillColor(VSTGUI::CColor{17,22,28});
  context->drawRect(bounds,VSTGUI::kDrawFilled);
  const auto left=bounds.left+44.,right=bounds.right-14.;
  const auto top=bounds.top+26.,bottom=bounds.bottom-24.;
  const auto width=right-left,height=bottom-top;
  if(width<=20.||height<=20.)return;
  context->setDrawMode(VSTGUI::kAntiAliasing);
  context->setLineWidth(1.);
  context->setFrameColor(VSTGUI::CColor{45,54,65});
  for(int level=-48;level<=24;level+=24){
   const auto y=bottom-(level+48.)/72.*height;
   context->drawLine(VSTGUI::CPoint(left,y),VSTGUI::CPoint(right,y));
  }
  for(const double hz:{100.,1000.,10000.}){
   const auto x=left+width*std::log(hz/response::kMinHz)/
      std::log(response::kMaxHz/response::kMinHz);
   context->drawLine(VSTGUI::CPoint(x,top),VSTGUI::CPoint(x,bottom));
  }
  context->setFontColor(VSTGUI::CColor{160,178,192});
  char rateLabel[96]{};
  std::snprintf(rateLabel,sizeof(rateLabel),
      actualRate_?"FILTER RESPONSE  |  %.2f kHz host":
                  "FILTER RESPONSE  |  %.2f kHz reference",sampleRate_/1000.);
  context->drawString(rateLabel,VSTGUI::CPoint(left,top-9.));
  context->drawString("+24",VSTGUI::CPoint(bounds.left+5.,top+5.));
  context->drawString("0",VSTGUI::CPoint(bounds.left+17.,bottom-height*2./3.+5.));
  context->drawString("-48",VSTGUI::CPoint(bounds.left+5.,bottom));
  context->drawString("20 Hz",VSTGUI::CPoint(left,bottom+17.));
  context->drawString("1 kHz",VSTGUI::CPoint(left+width*.566,bottom+17.));
  context->drawString("20 kHz",VSTGUI::CPoint(right-42.,bottom+17.));
  if(!valid_){
   context->setFontColor(VSTGUI::CColor{218,141,94});
   context->drawString("FILTER RESPONSE NOT AVAILABLE",VSTGUI::CPoint(left+width*.24,top+height*.5));
   setDirty(false);
   return;
  }
  const auto drawCurve=[&](const response::Curve& curve,
                          const VSTGUI::CColor& color,double widthPx) {
   context->setFrameColor(color);
   context->setLineWidth(widthPx);
   for(std::size_t i=1;i<curve.size();++i){
    if(!std::isfinite(curve[i-1])||!std::isfinite(curve[i]))continue;
    const auto previous=std::clamp(static_cast<double>(curve[i-1]),-48.,24.);
    const auto current=std::clamp(static_cast<double>(curve[i]),-48.,24.);
    const auto x1=left+width*static_cast<double>(i-1)/(curve.size()-1);
    const auto x2=left+width*static_cast<double>(i)/(curve.size()-1);
    const auto y1=bottom-(previous+48.)/72.*height;
    const auto y2=bottom-(current+48.)/72.*height;
    context->drawLine(VSTGUI::CPoint(x1,y1),VSTGUI::CPoint(x2,y2));
   }
  };
  if(modulating_&&baseValid_){
   drawCurve(baseDb_,VSTGUI::CColor{78,109,120},1.);
   context->setFontColor(VSTGUI::CColor{160,178,192});
   context->drawString("BASE / DSP LFO TARGET",
                       VSTGUI::CPoint(right-190.,top-9.));
  }
  drawCurve(db_,VSTGUI::CColor{112,220,172},2.);
#ifdef ZPLANE_RESPONSE_DIAGNOSTICS
  // Deterministically fingerprint exactly the curve samples supplied to
  // drawLine(), not the unreliable GDI BitBlt of a Direct2D surface.
  const auto signature=[](const response::Curve& curve,bool valid) {
   std::uint64_t result=14695981039346656037ull;
   for(const auto db:curve){
    const auto scaled=std::isfinite(db)?
       std::clamp(std::lround(db*1000.f),-48000l,24000l):std::numeric_limits<std::int32_t>::min();
    result^=static_cast<std::uint32_t>(scaled);
    result*=1099511628211ull;
   }
   result^=static_cast<std::uint64_t>(valid?1u:0u);
   return result*1099511628211ull;
  };
  response_qa::curveSignature.store(signature(db_,valid_),std::memory_order_release);
  response_qa::baseSignature.store(signature(baseDb_,baseValid_),std::memory_order_release);
  response_qa::live.store(modulating_?1u:0u,std::memory_order_release);
  response_qa::drawn.fetch_add(1,std::memory_order_release);
#endif
  setDirty(false);
 }
};
} // namespace zplane
