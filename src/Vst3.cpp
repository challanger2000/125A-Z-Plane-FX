#include "FilterCore.h"
#include "AutomationTimeline.h"
#include "MorpheusFilter.h"
#include "DualLfo.h"
#ifdef ZPLANE_WITH_X3_RESEARCH
#include "X3ResearchAdapter.h"
#include "EmbeddedX3Resources.h"
#include <filesystem>
#include <string>
#endif
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "base/source/fstreamer.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <cstdio>
#include "FilterResponseView.h"

namespace zplane {
using namespace Steinberg;
using namespace Steinberg::Vst;
static const FUID kProcessor(0x3E858CA1,0xF71C4BC2,0xAA13012B,0xD7EE4561);
static const FUID kController(0x8577FE18,0x947B4E20,0xA4CB7205,0xC3F2A019);
constexpr uint32 kMagic=0x3150585Au;
enum : ParamID { kMode=0,kCutoff=1,kQuality=2,kWet=3,kBypass=4,kX3Filter=5,
 kMorpheusOn=6,kMorpheusCube=7,kMorpheusMorph=8,kMorpheusFreq=9,kMorpheusTransform=10,
 kLfo1On=11,kLfo1Rate=12,kLfo1Depth=13,kLfo1Shape=14,kLfo1Target=15,kLfo1Sync=16,
 kLfo2On=17,kLfo2Rate=18,kLfo2Depth=19,kLfo2Shape=20,kLfo2Target=21,kLfo2Sync=22 };
constexpr DualLfo::Params defaults={0.,0.5,0.5,1.,0.,0.,0.,0.,0.5,0.5,0.5,
  0.,.5,0.,0.,.25,1., 0.,.5,0.,0.,.25,1.};
static double clampParam(double x) noexcept {return std::isfinite(x)?std::clamp(x,0.0,1.0):0.;}
static Settings makeSettings(const DualLfo::Params& p) noexcept {
 Settings s;
 s.mode=static_cast<Mode>(std::min(3,static_cast<int>(std::floor(p[0]*4.))));
 s.frequency=20.*std::pow(1000.,p[1]);
 s.quality=0.2*std::pow(100.,p[2]);
 s.wet=p[3];s.bypass=p[4]>=0.5;
 return s;
}
class Processor final : public AudioEffect {
 DualLfo::Params params_{defaults}; // processor/audio owner: host process and control methods follow SDK lifecycle
 StereoFilter dsp_;
 double effectiveCutoff_{.5},effectiveQ_{.5};
 Settings legacySettings() const noexcept {
  auto copy=params_;copy[kCutoff]=effectiveCutoff_;copy[kQuality]=effectiveQ_;
  return makeSettings(copy);
 }
#ifdef ZPLANE_WITH_X3_RESEARCH
 X3ResearchAdapter x3_{};
 bool x3Ready_{false};
 // Family mixing is independent of the inner X3 filter-to-filter fade.
 // Retarget from the currently audible blend on arbitrarily dense edits.
 double familyBlend_{0.},familyTarget_{0.},familyStep_{0.};
 std::uint32_t familyLeft_{0};
 bool familyReady_{false};
 template<typename Sample>
 void renderFamilies(const Sample* il,const Sample* ir,Sample* ol,Sample* orr,
                     std::uint32_t frames,bool x3Available) noexcept {
  if(familyLeft_==0&&familyBlend_<=0.){
   dsp_.configure(rate_,legacySettings());
   dsp_.process(il,ir,ol,orr,frames);
   return;
  }
  if(familyLeft_==0&&familyBlend_>=1.){
   if(x3Available)x3_.process(il,ir,ol,orr,frames,params_[kWet],params_[kBypass]>=0.5);
   else for(std::uint32_t n=0;n<frames;++n){
    ol[n]=std::isfinite(static_cast<double>(il[n]))?il[n]:Sample(0);
    orr[n]=std::isfinite(static_cast<double>(ir[n]))?ir[n]:Sample(0);
   }
   return;
  }
  dsp_.configure(rate_,legacySettings());
  // Bounded, fixed stack scratch. 128 frames, two stereo DSP engines;
  // no callback heap allocations or per-host-block scratch allocation.
  constexpr std::uint32_t kScratch=128;
  std::array<Sample,kScratch> legacyL{},legacyR{},x3L{},x3R{};
  for(std::uint32_t base=0;base<frames;){
   // An already completed ramp is a HARD DSP ownership boundary,
   // even inside one long VST3 process segment. Otherwise the inactive
   // filter keeps accumulating history for different lengths depending
   // on the host's unrelated block partitioning (audible on re-entry).
   if(familyLeft_==0){
    if(familyBlend_<=0.)
     dsp_.process(il+base,ir+base,ol+base,orr+base,frames-base);
    else if(x3Available)
     x3_.process(il+base,ir+base,ol+base,orr+base,frames-base,
                 params_[kWet],params_[kBypass]>=0.5);
    else for(std::uint32_t n=base;n<frames;++n){
     ol[n]=std::isfinite(static_cast<double>(il[n]))?il[n]:Sample(0);
     orr[n]=std::isfinite(static_cast<double>(ir[n]))?ir[n]:Sample(0);
    }
    return;
   }
   // The last transitioning sample is part of the dual-engine fade.
   // Never render the inactive path for surplus frames in this chunk.
   const auto count=std::min({kScratch,frames-base,familyLeft_});
   dsp_.process(il+base,ir+base,legacyL.data(),legacyR.data(),count);
   if(x3Available)
    x3_.process(il+base,ir+base,x3L.data(),x3R.data(),count,
                params_[kWet],params_[kBypass]>=0.5);
   else for(std::uint32_t n=0;n<count;++n){
    x3L[n]=std::isfinite(static_cast<double>(il[base+n]))?il[base+n]:Sample(0);
    x3R[n]=std::isfinite(static_cast<double>(ir[base+n]))?ir[base+n]:Sample(0);
   }
   for(std::uint32_t n=0;n<count;++n){
    if(familyLeft_){
     familyBlend_+=familyStep_;
     if(--familyLeft_==0)familyBlend_=familyTarget_;
    }
    const auto a=familyBlend_;
    ol[base+n]=a<=0.?legacyL[n]:a>=1.?x3L[n]:
     static_cast<Sample>(static_cast<double>(legacyL[n])+
       (static_cast<double>(x3L[n])-static_cast<double>(legacyL[n]))*a);
    orr[base+n]=a<=0.?legacyR[n]:a>=1.?x3R[n]:
     static_cast<Sample>(static_cast<double>(legacyR[n])+
       (static_cast<double>(x3R[n])-static_cast<double>(legacyR[n]))*a);
   }
   base+=count;
  }
 }
#endif
 // Dedicated, seven-stage Morpheus/UltraProteus signal path.
 MorpheusFilter morpheus_{};
 double cubeBlend_{0.},cubeTarget_{0.},cubeStep_{0.};
 std::uint32_t cubeLeft_{0};
 bool cubeReady_{false};
 std::uint64_t freeSamples_{0};
 double fallbackBeats_{0.};
 template<typename Sample>
 void renderOriginal(const Sample* il,const Sample* ir,Sample* ol,Sample* orr,
                     std::uint32_t frames,bool useX3) noexcept {
#ifdef ZPLANE_WITH_X3_RESEARCH
  renderFamilies(il,ir,ol,orr,frames,useX3);
#else
  (void)useX3;
  dsp_.configure(rate_,legacySettings());
  dsp_.process(il,ir,ol,orr,frames);
#endif
 }
 template<typename Sample>
 void renderProduct(const Sample* il,const Sample* ir,Sample* ol,Sample* orr,
                    std::uint32_t frames,bool useX3) noexcept {
  if(cubeLeft_==0&&cubeBlend_<=0.){
   renderOriginal(il,ir,ol,orr,frames,useX3);
   return;
  }
  if(cubeLeft_==0&&cubeBlend_>=1.){
   morpheus_.process(il,ir,ol,orr,frames);
   return;
  }
  constexpr std::uint32_t kScratch=128;
  std::array<Sample,kScratch> originalL{},originalR{},cubeL{},cubeR{};
  for(std::uint32_t offset=0;offset<frames;){
   if(cubeLeft_==0){
    if(cubeBlend_<=0.)renderOriginal(il+offset,ir+offset,ol+offset,orr+offset,frames-offset,useX3);
    else morpheus_.process(il+offset,ir+offset,ol+offset,orr+offset,frames-offset);
    return;
   }
   const auto count=std::min({kScratch,frames-offset,cubeLeft_});
   renderOriginal(il+offset,ir+offset,originalL.data(),originalR.data(),count,useX3);
   morpheus_.process(il+offset,ir+offset,cubeL.data(),cubeR.data(),count);
   for(std::uint32_t n=0;n<count;++n){
    cubeBlend_+=cubeStep_;
    if(--cubeLeft_==0)cubeBlend_=cubeTarget_;
    const auto mix=cubeBlend_;
    ol[offset+n]=mix<=0.?originalL[n]:mix>=1.?cubeL[n]:
      static_cast<Sample>(static_cast<double>(originalL[n])+
        (static_cast<double>(cubeL[n])-static_cast<double>(originalL[n]))*mix);
    orr[offset+n]=mix<=0.?originalR[n]:mix>=1.?cubeR[n]:
      static_cast<Sample>(static_cast<double>(originalR[n])+
        (static_cast<double>(cubeR[n])-static_cast<double>(originalR[n]))*mix);
   }
   offset+=count;
  }
 }
 AutomationTimeline timeline_{}; // preallocated outside realtime callback
 // getState() may be called from a host control thread while VST3 process()
 // changes multiple parameter values. Reading the plain audio params_
 // concurrently was a C++ data race and could save mixed bank/filter state.
 // Only the audio owner publishes; readers obtain a coherent atomic frame.
 static_assert(std::atomic<double>::is_always_lock_free);
 static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
 std::array<std::atomic<double>,11> published_{};
 std::atomic<std::uint64_t> publicationSeq_{0};
 void publishParams() noexcept {
  publicationSeq_.fetch_add(1,std::memory_order_acq_rel); // writer active
  for(std::size_t i=0;i<params_.size();++i)
   published_[i].store(params_[i],std::memory_order_relaxed);
  publicationSeq_.fetch_add(1,std::memory_order_release); // coherent
 }
 bool readPublished(DualLfo::Params& values) const noexcept {
  // Control thread only; a bounded retry returns failure rather than
  // writing a torn project state in pathological contention.
  for(unsigned retry=0;retry<512;++retry){
   const auto before=publicationSeq_.load(std::memory_order_acquire);
   if(before&1u)continue;
   for(std::size_t i=0;i<values.size();++i)
    values[i]=published_[i].load(std::memory_order_relaxed);
   const auto after=publicationSeq_.load(std::memory_order_acquire);
   if(before==after)return true;
  }
  return false;
 }
 double rate_{48000.};
public:
 Processor(){
  setControllerClass(kController);
  for(std::size_t i=0;i<defaults.size();++i)
   published_[i].store(defaults[i],std::memory_order_relaxed);
 }
 static FUnknown* createInstance(void*){return static_cast<IAudioProcessor*>(new Processor());}
 tresult PLUGIN_API initialize(FUnknown* context) override {
  const auto rc=AudioEffect::initialize(context); if(rc!=kResultOk)return rc;
  addAudioInput(STR16("Stereo In"),SpeakerArr::kStereo);
  addAudioOutput(STR16("Stereo Out"),SpeakerArr::kStereo);
#ifdef ZPLANE_WITH_X3_RESEARCH
  // Filter-bank data ship *inside this DLL*. Extract only transiently,
  // on the VST3 control thread; the current X3 research parser requires
  // file paths. All temporary files are removed after loading. A missing
  // resource is a hard initialization error, never a silently dry X3 mode.
  try {
   HMODULE ownModule=nullptr;
   if(!GetModuleHandleExW(
      GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|
      GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
      reinterpret_cast<LPCWSTR>(&Processor::createInstance),&ownModule))
    return kResultFalse;
   EmbeddedX3Resources embedded;
   if(!embedded.extract(ownModule))return kResultFalse;
   std::string reason;
   x3Ready_=x3_.load(embedded.directory(),reason);
   if(!x3Ready_)return kResultFalse;
  }catch(const std::exception&){return kResultFalse;}
#endif
  return kResultOk;
 }
 tresult PLUGIN_API setBusArrangements(SpeakerArrangement* in,int32 ni,SpeakerArrangement* out,int32 no) override {
  if(ni!=1||no!=1||!in||!out||in[0]!=SpeakerArr::kStereo||out[0]!=SpeakerArr::kStereo)return kResultFalse;
  return kResultTrue;
 }
 tresult PLUGIN_API canProcessSampleSize(int32 symbolic) override {
  return (symbolic==kSample32||symbolic==kSample64)?kResultTrue:kResultFalse;
 }
 tresult PLUGIN_API setupProcessing(ProcessSetup& setup) override {
  if(!std::isfinite(setup.sampleRate)||setup.sampleRate<=0.)return kInvalidArgument;
  const auto rc=AudioEffect::setupProcessing(setup);
  if(rc==kResultOk){
   rate_=setup.sampleRate;
   effectiveCutoff_=params_[kCutoff];effectiveQ_=params_[kQuality];
   freeSamples_=0;fallbackBeats_=0.;
   dsp_.reset();
   dsp_.configure(rate_,legacySettings());
   morpheus_.reset(rate_);
   cubeBlend_=cubeTarget_=cubeStep_=0.;cubeLeft_=0;cubeReady_=false;
#ifdef ZPLANE_WITH_X3_RESEARCH
   x3_.reset();
   familyBlend_=familyTarget_=familyStep_=0.;
   familyLeft_=0;familyReady_=false;
#endif
  }
  return rc;
 }
 tresult PLUGIN_API setProcessing(TBool) override {return kResultOk;}
 tresult PLUGIN_API setActive(TBool active) override {
  if(!active){
   freeSamples_=0;fallbackBeats_=0.;
   effectiveCutoff_=params_[kCutoff];effectiveQ_=params_[kQuality];
   dsp_.reset();
   morpheus_.reset(rate_);
   cubeBlend_=cubeTarget_=cubeStep_=0.;cubeLeft_=0;cubeReady_=false;
#ifdef ZPLANE_WITH_X3_RESEARCH
   x3_.reset();
   familyBlend_=familyTarget_=familyStep_=0.;
   familyLeft_=0;familyReady_=false;
#endif
  }
  return AudioEffect::setActive(active);
 }
 uint32 PLUGIN_API getTailSamples() override {
  // The resonant IIR has a decaying but potentially long impulse response.
  // Never report infinite duration for a non-generative audio insert.
  // Conservative 30-second cap at all supported sample rates.
  return static_cast<uint32>(std::clamp(rate_*30.,1.,static_cast<double>(UINT32_MAX-1)));
 }
 uint32 PLUGIN_API getProcessContextRequirements() override {
  return IProcessContextRequirements::kNeedTempo|
         IProcessContextRequirements::kNeedProjectTimeMusic|
         IProcessContextRequirements::kNeedTransportState;
 }
 tresult PLUGIN_API process(ProcessData& data) override {
  if(data.numSamples<0||data.numInputs<0||data.numOutputs<0)return kInvalidArgument;
  // Host automation is time-stamped. Never collapse a queue to its last
  // event before rendering the entire block.
  auto& timeline=timeline_;
  timeline.clear();
  if(data.inputParameterChanges){
    const int32 count=data.inputParameterChanges->getParameterCount();
    for(int32 i=0;i<count;++i) {
      auto* queue=data.inputParameterChanges->getParameterData(i);if(!queue)continue;
      const auto id=queue->getParameterId();if(id>kLfo2Sync)continue;
      const int32 points=queue->getPointCount();
      for(int32 p=0;p<points;++p) {
        int32 offset=0;ParamValue value=0.;
        if(queue->getPoint(p,offset,value)!=kResultOk||offset<0||offset>data.numSamples||!std::isfinite(value))continue;
        if(!timeline.add(static_cast<uint32>(offset),id,clampParam(value)))
          return kResultFalse; // no silent loss of accepted automation
      }
    }
  }
  timeline.finalize();
  if(data.numSamples==0){
    for(std::size_t e=0;e<timeline.size();++e)params_[timeline[e].parameter]=timeline[e].value;
    publishParams();
    return kResultOk;
  }
  if(data.numInputs<1||data.numOutputs<1||!data.inputs||!data.outputs)return kInvalidArgument;
  auto& in=data.inputs[0];auto& out=data.outputs[0];
  if(in.numChannels!=2||out.numChannels!=2)return kInvalidArgument;
  const auto n=static_cast<uint32>(data.numSamples);
  if(data.symbolicSampleSize!=kSample32&&data.symbolicSampleSize!=kSample64)return kInvalidArgument;
  if(data.symbolicSampleSize==kSample32){
    if(!in.channelBuffers32||!out.channelBuffers32||!in.channelBuffers32[0]||!in.channelBuffers32[1]||!out.channelBuffers32[0]||!out.channelBuffers32[1])return kInvalidArgument;
  }else{
    if(!in.channelBuffers64||!out.channelBuffers64||!in.channelBuffers64[0]||!in.channelBuffers64[1]||!out.channelBuffers64[0]||!out.channelBuffers64[1])return kInvalidArgument;
  }
  const auto* context=data.processContext;
  const bool tempoValid=context&&(context->state&ProcessContext::kTempoValid)&&
     std::isfinite(context->tempo)&&context->tempo>=10.&&context->tempo<=400.;
  const auto bpm=tempoValid?context->tempo:120.;
  const bool ppqValid=context&&(context->state&ProcessContext::kProjectTimeMusicValid)&&
     std::isfinite(context->projectTimeMusic);
  const DualLfo::Clock clock{ppqValid?context->projectTimeMusic:fallbackBeats_,bpm};
  const auto sampleBase=freeSamples_;
  const auto quantum=DualLfo::quantum(rate_);
  std::uint32_t cursor=0;
  std::size_t event=0;
  while(cursor<n){
    while(event<timeline.size()&&timeline[event].offset==cursor){
      params_[timeline[event].parameter]=timeline[event].value;++event;
    }
    const auto next=event<timeline.size()?std::min(n,timeline[event].offset):n;
    if(next<=cursor)return kResultFalse;
    auto length=next-cursor;
    const bool modActive=DualLfo::anyActive(params_);
    if(modActive){
     const auto remaining=quantum-(sampleBase+cursor)%quantum;
     length=std::min(length,static_cast<std::uint32_t>(remaining));
    }
    const auto mod=modActive?
      DualLfo::evaluate(params_,sampleBase+cursor,rate_,clock,cursor):
      DualLfo::Result{params_[kMorpheusMorph],params_[kMorpheusFreq],
                      params_[kMorpheusTransform],params_[kCutoff],params_[kQuality]};
    effectiveCutoff_=mod.x3Morph;effectiveQ_=mod.x3Q;
    const bool wantCube=params_[kMorpheusOn]>=0.5;
    if(!cubeReady_){
     cubeBlend_=cubeTarget_=wantCube?1.:0.;cubeReady_=true;
    }else if((wantCube?1.:0.)!=cubeTarget_){
     cubeTarget_=wantCube?1.:0.;
     cubeLeft_=static_cast<std::uint32_t>(std::clamp(rate_*.005,1.,48000.));
     cubeStep_=(cubeTarget_-cubeBlend_)/static_cast<double>(cubeLeft_);
    }
    // The new engine remains stateful only while audible or crossfading.
    const bool cubeAudible=wantCube||cubeBlend_>0.||cubeLeft_>0;
    if(cubeAudible){
     morpheus_.completeDeferred();
     const auto cubeIndex=static_cast<unsigned>(
       std::clamp(std::floor(params_[kMorpheusCube]*288.+0.5),0.,288.));
     morpheus_.set({cubeIndex,mod.morph,mod.frequency,
                    mod.transform},params_[kWet],params_[kBypass]>=0.5);
     length=std::min(length,morpheus_.samplesToQueuedTransition());
    }
    bool renderX3=false;
#ifdef ZPLANE_WITH_X3_RESEARCH
    const auto requested=static_cast<unsigned>(
      std::min(55.,std::floor(params_[kX3Filter]*55.+0.5)));
    const bool useX3=requested>0;
    if(!familyReady_){
     familyBlend_=familyTarget_=useX3?1.:0.;
     familyReady_=true;
    }else if((useX3?1.:0.)!=familyTarget_){
     familyTarget_=useX3?1.:0.;
     familyLeft_=static_cast<std::uint32_t>(std::clamp(rate_*.005,1.,48000.));
     familyStep_=(familyTarget_-familyBlend_)/static_cast<double>(familyLeft_);
    }
    const bool originalAudible=cubeBlend_<1.||cubeLeft_>0;
    const bool x3Audible=originalAudible&&(useX3||familyBlend_>0.||familyLeft_>0);
    if(x3Audible&&x3Ready_){
     x3_.completeDeferred();
     if(useX3)
      renderX3=x3_.set(requested,static_cast<float>(mod.x3Morph),
                       static_cast<float>(mod.x3Q),
                       static_cast<unsigned>(std::llround(rate_)));
     else renderX3=x3_.hasBanks();
    }
    if(renderX3)length=std::min(length,x3_.samplesToQueuedTransition());
#endif
    if(data.symbolicSampleSize==kSample32)
     renderProduct(in.channelBuffers32[0]+cursor,in.channelBuffers32[1]+cursor,
                   out.channelBuffers32[0]+cursor,out.channelBuffers32[1]+cursor,
                   length,renderX3);
    else
     renderProduct(in.channelBuffers64[0]+cursor,in.channelBuffers64[1]+cursor,
                   out.channelBuffers64[0]+cursor,out.channelBuffers64[1]+cursor,
                   length,renderX3);
    if(cubeAudible)morpheus_.completeDeferred();
    cursor+=length;
  }
  freeSamples_+=n;
  fallbackBeats_+=static_cast<double>(n)*bpm/(60.*rate_);
  // An event at offset n defines the state for the following callback.
  while(event<timeline.size()){
    params_[timeline[event].parameter]=timeline[event].value;++event;
  }
  publishParams();
  out.silenceFlags=0;return kResultOk;
 }
 tresult PLUGIN_API getState(IBStream* stream) override {
  if(!stream)return kInvalidArgument;
  IBStreamer st(stream,kLittleEndian);
  if(!st.writeInt32u(kMagic)||!st.writeInt32u(4))return kResultFalse;
  DualLfo::Params coherent{};
  if(!readPublished(coherent))return kResultFalse;
  for(double p:coherent)if(!st.writeDouble(p))return kResultFalse;
  return kResultOk;
 }
 tresult PLUGIN_API setState(IBStream* stream) override {
  if(!stream)return kInvalidArgument;IBStreamer st(stream,kLittleEndian);
  uint32 magic=0,version=0;if(!st.readInt32u(magic)||!st.readInt32u(version)||magic!=kMagic||(version!=1&&version!=2&&version!=3&&version!=4))return kResultFalse;
  auto candidate=params_;
  for(std::size_t i=0;i<(version==1?5u:version==2?6u:version==3?11u:23u);++i){auto& p=candidate[i];if(!st.readDouble(p)||!std::isfinite(p)||p<0.||p>1.)return kResultFalse;}
  if(version==1)candidate[kX3Filter]=0.;
  if(version<3)for(std::size_t i=6;i<11;++i)candidate[i]=defaults[i];
  if(version<4)for(std::size_t i=11;i<candidate.size();++i)candidate[i]=defaults[i];
  // Per VST3 lifecycle, setState is a host control-thread operation
  // with audio processing suspended. getState alone may be concurrent.
  params_=candidate;publishParams();
  effectiveCutoff_=params_[kCutoff];effectiveQ_=params_[kQuality];
  freeSamples_=0;fallbackBeats_=0.;
  dsp_.configure(rate_,legacySettings());
  morpheus_.reset(rate_);
  cubeBlend_=cubeTarget_=cubeStep_=0.;cubeLeft_=0;cubeReady_=false;
#ifdef ZPLANE_WITH_X3_RESEARCH
  x3_.reset();
  familyBlend_=familyTarget_=familyStep_=0.;
  familyLeft_=0;familyReady_=false;
#endif
  return kResultOk;
 }
};
class Controller final : public EditController, public VSTGUI::VST3EditorDelegate {
public:
 VSTGUI::CView* createCustomView(VSTGUI::UTF8StringPtr name,
      const VSTGUI::UIAttributes&,const VSTGUI::IUIDescription*,
      VSTGUI::VST3Editor*) override {
  if(!name||std::strcmp(name,"125AFilterResponse")!=0)return nullptr;
  return new FilterResponseView(VSTGUI::CRect(0.,0.,830.,178.),this);
 }
 IPlugView* PLUGIN_API createView(FIDString name) override {
#ifdef ZPLANE_RESPONSE_DIAGNOSTICS
  std::fprintf(stderr,"[125A DIAG] Controller createView enter name=%s\n",name?name:"(null)");
  std::fflush(stderr);
#endif
  if(!name||std::strcmp(name,ViewType::kEditor)!=0)return nullptr;
  // The SDK's Win32 resource resolver may point at the host executable.
  // Resolve the UI beside THIS plugin module, not the DAW working directory.
  static const std::string uiPath=[]() -> std::string {
   HMODULE module=nullptr;
   if(!GetModuleHandleExW(
      GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
      reinterpret_cast<LPCWSTR>(&Controller::createInstance),&module)||!module)return {};
   std::wstring dllPath(32768,L'\0');
   const auto size=GetModuleFileNameW(module,dllPath.data(),
                                      static_cast<DWORD>(dllPath.size()));
   if(size==0||size>=dllPath.size())return {};
   dllPath.resize(size);
   const auto ui=std::filesystem::path(dllPath).parent_path().parent_path()
                     /"Resources"/"ZPlane.uidesc";
   if(!std::filesystem::exists(ui))return {};
   const std::wstring path=ui.wstring();
   const auto needed=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,
                   path.c_str(),static_cast<int>(path.size()),nullptr,0,nullptr,nullptr);
   if(needed<=0)return {};
   std::string utf8(static_cast<std::size_t>(needed),'\0');
   if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,
      path.c_str(),static_cast<int>(path.size()),utf8.data(),needed,nullptr,nullptr)!=needed)return {};
   return utf8;
  }();
  if(uiPath.empty())return nullptr;
#ifdef ZPLANE_RESPONSE_DIAGNOSTICS
  std::fprintf(stderr,"[125A DIAG] Controller createView UI path resolved\n");
  std::fflush(stderr);
#endif
  auto* editor=new VSTGUI::VST3Editor(this,"ZPlaneMain",uiPath.c_str());
#ifdef ZPLANE_RESPONSE_DIAGNOSTICS
  std::fprintf(stderr,"[125A DIAG] Controller createView exit editor=%p\n",static_cast<void*>(editor));
  std::fflush(stderr);
#endif
  return editor;
 }
 static FUnknown* createInstance(void*){return static_cast<IEditController*>(new Controller());}
 tresult PLUGIN_API initialize(FUnknown* context) override {
#ifdef ZPLANE_RESPONSE_DIAGNOSTICS
  std::fprintf(stderr,"[125A DIAG] Controller initialize enter\n");
  std::fflush(stderr);
#endif
  const auto rc=EditController::initialize(context);if(rc!=kResultOk)return rc;
  auto* mode=new StringListParameter(STR16("Filter Shape"),kMode);
  mode->appendString(STR16("Low Pass"));mode->appendString(STR16("High Pass"));
  mode->appendString(STR16("Band Pass"));mode->appendString(STR16("Notch"));
  parameters.addParameter(mode);
  parameters.addParameter(STR16("Cutoff"),nullptr,0,defaults[1],ParameterInfo::kCanAutomate,kCutoff);
  parameters.addParameter(STR16("Resonance"),nullptr,0,defaults[2],ParameterInfo::kCanAutomate,kQuality);
  parameters.addParameter(STR16("Wet"),nullptr,0,defaults[3],ParameterInfo::kCanAutomate,kWet);
  parameters.addParameter(STR16("Bypass"),nullptr,1,defaults[4],ParameterInfo::kCanAutomate|ParameterInfo::kIsBypass,kBypass);
  auto* x3List=new StringListParameter(STR16("X3 Filter"),kX3Filter);
  x3List->appendString(STR16("Legacy Standard Filter"));
  x3List->appendString(STR16("Ace of Bass"));
  x3List->appendString(STR16("MegaSweepz"));
  x3List->appendString(STR16("Early Rizer"));
  x3List->appendString(STR16("Millennium"));
  x3List->appendString(STR16("Meaty Gizmo"));
  x3List->appendString(STR16("Klub Klassik"));
  x3List->appendString(STR16("BassBox 303"));
  x3List->appendString(STR16("Fuzzi Face"));
  x3List->appendString(STR16("Dead Ringer"));
  x3List->appendString(STR16("TB or Not TB"));
  x3List->appendString(STR16("Ooh to Eee"));
  x3List->appendString(STR16("Boland Bass"));
  x3List->appendString(STR16("Multi Q Vox"));
  x3List->appendString(STR16("Talking Hedz"));
  x3List->appendString(STR16("Zoom Peaks"));
  x3List->appendString(STR16("DJ Alkaline"));
  x3List->appendString(STR16("Bass Tracer"));
  x3List->appendString(STR16("Rogue Hertz"));
  x3List->appendString(STR16("Razor Blades"));
  x3List->appendString(STR16("Radio Craze"));
  x3List->appendString(STR16("Eeh to Aah"));
  x3List->appendString(STR16("Ubu Orator"));
  x3List->appendString(STR16("Deep Bouche"));
  x3List->appendString(STR16("Freak Shifta"));
  x3List->appendString(STR16("Cruz Pusher"));
  x3List->appendString(STR16("Angelz Hairz"));
  x3List->appendString(STR16("Dream Weava"));
  x3List->appendString(STR16("Acid Ravage"));
  x3List->appendString(STR16("Bass-O-Matic"));
  x3List->appendString(STR16("Lucifer's Q"));
  x3List->appendString(STR16("Tooth Comb"));
  x3List->appendString(STR16("Ear Bender"));
  x3List->appendString(STR16("Klang Kling"));
  x3List->appendString(STR16("2 Pole Lowpass"));
  x3List->appendString(STR16("4 Pole Lowpass"));
  x3List->appendString(STR16("6 Pole Lowpass"));
  x3List->appendString(STR16("2 Pole Highpass"));
  x3List->appendString(STR16("4 Pole Highpass"));
  x3List->appendString(STR16("2 Pole Bandpass"));
  x3List->appendString(STR16("4 Pole Bandpass"));
  x3List->appendString(STR16("Contrary Bandpass"));
  x3List->appendString(STR16("Swept EQ 1 Octave"));
  x3List->appendString(STR16("Swept EQ 2/1 Octave"));
  x3List->appendString(STR16("Swept EQ 3/1 Octave"));
  x3List->appendString(STR16("Phaser 1"));
  x3List->appendString(STR16("Phaser 2"));
  x3List->appendString(STR16("Bat Phaser"));
  x3List->appendString(STR16("Flanger Lite"));
  x3List->appendString(STR16("Vocal Ah-Ay-Ee"));
  x3List->appendString(STR16("Vocal Oo-Ah"));
  x3List->appendString(STR16("Dual EQ Morph"));
  x3List->appendString(STR16("Dual EQ + LP Morph"));
  x3List->appendString(STR16("Dual EQ Morph/Expression"));
  x3List->appendString(STR16("Peak/Shelf Morph"));
  x3List->appendString(STR16("Morph Designer"));
  parameters.addParameter(x3List);
  parameters.addParameter(STR16("Morpheus Active"),nullptr,1,defaults[6],ParameterInfo::kCanAutomate,kMorpheusOn);
  auto* cubeList=new StringListParameter(STR16("Morpheus Cube"),kMorpheusCube);
  cubeList->appendString(STR16("Null Cube"));
  cubeList->appendString(STR16("LPFlange.4"));
  cubeList->appendString(STR16("LPFlngBk.4"));
  cubeList->appendString(STR16("Flange2.4"));
  cubeList->appendString(STR16("CubeFlanger"));
  cubeList->appendString(STR16("Flange3.4"));
  cubeList->appendString(STR16("Flange4.4"));
  cubeList->appendString(STR16("Flange5"));
  cubeList->appendString(STR16("Flange6.4"));
  cubeList->appendString(STR16("Flange6R.4"));
  cubeList->appendString(STR16("Flange7.4"));
  cubeList->appendString(STR16("BriteFlng.4"));
  cubeList->appendString(STR16("Flng>Flng1"));
  cubeList->appendString(STR16("LP>Flng2"));
  cubeList->appendString(STR16("Flng>Flng3"));
  cubeList->appendString(STR16("Flng>Flng4"));
  cubeList->appendString(STR16("Flng>Flng5"));
  cubeList->appendString(STR16("Flng>Flng6"));
  cubeList->appendString(STR16("Flng>FlngT"));
  cubeList->appendString(STR16("Flng>FlngC"));
  cubeList->appendString(STR16("CO>FlngT"));
  cubeList->appendString(STR16("AEParLPVow"));
  cubeList->appendString(STR16("AEParaVowel"));
  cubeList->appendString(STR16("AOLpParaVow"));
  cubeList->appendString(STR16("AOParaVowel"));
  cubeList->appendString(STR16("AUParaVow.4"));
  cubeList->appendString(STR16("UOParaVow.4"));
  cubeList->appendString(STR16("SftEOVowl.4"));
  cubeList->appendString(STR16("SoftEOAE"));
  cubeList->appendString(STR16("Vocal Cube"));
  cubeList->appendString(STR16("C1-6Harms.4"));
  cubeList->appendString(STR16("Voce.4"));
  cubeList->appendString(STR16("ChoralComb.4"));
  cubeList->appendString(STR16("Bassutoi.4"));
  cubeList->appendString(STR16("Be-Ye.4"));
  cubeList->appendString(STR16("Ee-Yi.4"));
  cubeList->appendString(STR16("Ii-Yi.4"));
  cubeList->appendString(STR16("Uhrrrah.4"));
  cubeList->appendString(STR16("YeahYeah.4"));
  cubeList->appendString(STR16("Vow>Vow1"));
  cubeList->appendString(STR16("Vow>Vow2"));
  cubeList->appendString(STR16("YahYahs.4"));
  cubeList->appendString(STR16("YoYo.4"));
  cubeList->appendString(STR16("VowelSpace"));
  cubeList->appendString(STR16("BrickWaLP.4"));
  cubeList->appendString(STR16("BrickWaLP2"));
  cubeList->appendString(STR16("MdQ 2PoleLP"));
  cubeList->appendString(STR16("HiQ 2PoleLP"));
  cubeList->appendString(STR16("MdQ 4PoleLP"));
  cubeList->appendString(STR16("HiQ 4PoleLP"));
  cubeList->appendString(STR16("2 PoleLoQ.4"));
  cubeList->appendString(STR16("4 PoleLoQ.4"));
  cubeList->appendString(STR16("4 PoleMidQ.4"));
  cubeList->appendString(STR16("2pole>4pole"));
  cubeList->appendString(STR16("LowPassPlus"));
  cubeList->appendString(STR16("Low Past.4"));
  cubeList->appendString(STR16("APass.4"));
  cubeList->appendString(STR16("HPSweep.4"));
  cubeList->appendString(STR16("HiSwept1.4"));
  cubeList->appendString(STR16("HghsSwpt2.4"));
  cubeList->appendString(STR16("Hi Accent.4"));
  cubeList->appendString(STR16("HiPassSwp.4"));
  cubeList->appendString(STR16("DeepCombs.4"));
  cubeList->appendString(STR16("One Peak"));
  cubeList->appendString(STR16("More Peaks"));
  cubeList->appendString(STR16("Rev Peaks"));
  cubeList->appendString(STR16("Notcher2.4"));
  cubeList->appendString(STR16("Ntches2Oct.4"));
  cubeList->appendString(STR16("Odd>+"));
  cubeList->appendString(STR16("VarSlope.4"));
  cubeList->appendString(STR16("BassEQ 1.4"));
  cubeList->appendString(STR16("B BOOST.4"));
  cubeList->appendString(STR16("BssBOOST2.4"));
  cubeList->appendString(STR16("BassDrumEQ"));
  cubeList->appendString(STR16("Snare LPEQ2"));
  cubeList->appendString(STR16("HiHatLPEQ"));
  cubeList->appendString(STR16("HP LP PZ"));
  cubeList->appendString(STR16("PZ Notch"));
  cubeList->appendString(STR16("Band-aid"));
  cubeList->appendString(STR16("4>2 LowQHiQ"));
  cubeList->appendString(STR16("Wah4Vib.4"));
  cubeList->appendString(STR16("Wa Wa.4"));
  cubeList->appendString(STR16("BrassRez.4"));
  cubeList->appendString(STR16("Clr>Oboe"));
  cubeList->appendString(STR16("0>Muter"));
  cubeList->appendString(STR16("PZ Syn Horn"));
  cubeList->appendString(STR16("HP Brass"));
  cubeList->appendString(STR16("BrassyBlast"));
  cubeList->appendString(STR16("Brass Swell"));
  cubeList->appendString(STR16("BrsSwell2.4"));
  cubeList->appendString(STR16("LoVelTrum"));
  cubeList->appendString(STR16("Chiffin.4"));
  cubeList->appendString(STR16("ShakuFilter"));
  cubeList->appendString(STR16("Cym"));
  cubeList->appendString(STR16("VelMarim"));
  cubeList->appendString(STR16("EZ Vibez.4"));
  cubeList->appendString(STR16("Piano 1"));
  cubeList->appendString(STR16("Piano 2.4"));
  cubeList->appendString(STR16("EZ Rhodez.4"));
  cubeList->appendString(STR16("MoogVocodr.4"));
  cubeList->appendString(STR16("MoogVocSwp"));
  cubeList->appendString(STR16("StrngThng.4"));
  cubeList->appendString(STR16("StrSweep.4"));
  cubeList->appendString(STR16("BassXpress"));
  cubeList->appendString(STR16("Qbase.4"));
  cubeList->appendString(STR16("AcGtrRs.4"));
  cubeList->appendString(STR16("TubeSust.4"));
  cubeList->appendString(STR16("GtrSkwk"));
  cubeList->appendString(STR16("GuitXpress"));
  cubeList->appendString(STR16("G MajTrans.4"));
  cubeList->appendString(STR16("Tam"));
  cubeList->appendString(STR16("HOTwell.4"));
  cubeList->appendString(STR16("Bell Wah A"));
  cubeList->appendString(STR16("Bell Wah B"));
  cubeList->appendString(STR16("0>Bell-"));
  cubeList->appendString(STR16("0>Bell+"));
  cubeList->appendString(STR16("Bel>Crs>Bel"));
  cubeList->appendString(STR16("Chrs>Flng1"));
  cubeList->appendString(STR16("Chrs>Flng2"));
  cubeList->appendString(STR16("Ev/OdNtch.4"));
  cubeList->appendString(STR16("Odd/EvnNtch"));
  cubeList->appendString(STR16("NotchPeaks.4"));
  cubeList->appendString(STR16("LoudBoost.4"));
  cubeList->appendString(STR16("Swingshift"));
  cubeList->appendString(STR16("500up.4"));
  cubeList->appendString(STR16("C1Harmonic"));
  cubeList->appendString(STR16("Odd>Even"));
  cubeList->appendString(STR16("0>Shp1"));
  cubeList->appendString(STR16("0>Shp2"));
  cubeList->appendString(STR16("0>Shp3"));
  cubeList->appendString(STR16("Shp>Shp1"));
  cubeList->appendString(STR16("Lpeq Vel"));
  cubeList->appendString(STR16("PeakSweep.4"));
  cubeList->appendString(STR16("PowerSweeps"));
  cubeList->appendString(STR16("TSweep.4"));
  cubeList->appendString(STR16("SweepHiQ1.4"));
  cubeList->appendString(STR16("V>FcQuad.4"));
  cubeList->appendString(STR16("Nexus.4"));
  cubeList->appendString(STR16("Krators.4"));
  cubeList->appendString(STR16("Harmonix.4"));
  cubeList->appendString(STR16("GreenWorld.4"));
  cubeList->appendString(STR16("Comb/Swap.4"));
  cubeList->appendString(STR16("Comb/HP.4"));
  cubeList->appendString(STR16("Swirly"));
  cubeList->appendString(STR16("Cavatate.4"));
  cubeList->appendString(STR16("GentleRez.4"));
  cubeList->appendString(STR16("Bendup/Swap"));
  cubeList->appendString(STR16("Bendup"));
  cubeList->appendString(STR16("Skweezit"));
  cubeList->appendString(STR16("Lo/High.4"));
  cubeList->appendString(STR16("SubtleMove.4"));
  cubeList->appendString(STR16("BuzzyPad.4"));
  cubeList->appendString(STR16("Bw5kHz+6.4"));
  cubeList->appendString(STR16("Bw65Hz/2k.4"));
  cubeList->appendString(STR16("Bb80Hzbw1.4"));
  cubeList->appendString(STR16("HighsTwist.4"));
  cubeList->appendString(STR16("Cubix"));
  cubeList->appendString(STR16("HarmSweep.4"));
  cubeList->appendString(STR16("EvenCuts.4"));
  cubeList->appendString(STR16("OddCuts.4"));
  cubeList->appendString(STR16("PWMTrans.4"));
  cubeList->appendString(STR16("HiEndQ.4"));
  cubeList->appendString(STR16("BroadRes.4"));
  cubeList->appendString(STR16("ResoHose.4"));
  cubeList->appendString(STR16("HeeghCube"));
  cubeList->appendString(STR16("NoizCube"));
  cubeList->appendString(STR16("VeloctyTilt"));
  cubeList->appendString(STR16("Syn Wow.4"));
  cubeList->appendString(STR16("CombSweep.4"));
  cubeList->appendString(STR16("Diffuser.4"));
  cubeList->appendString(STR16("MdlySweep.4"));
  cubeList->appendString(STR16("StrongShimr"));
  cubeList->appendString(STR16("Acc.Vel-1"));
  cubeList->appendString(STR16("Acc.Vel-2"));
  cubeList->appendString(STR16("Vel2-Wind"));
  cubeList->appendString(STR16("Harmo"));
  cubeList->appendString(STR16("Start>EndA"));
  cubeList->appendString(STR16("Start>EndB"));
  cubeList->appendString(STR16("MovingPick1"));
  cubeList->appendString(STR16("MovingPick2"));
  cubeList->appendString(STR16("Mph+Trns1"));
  cubeList->appendString(STR16("0>Odds"));
  cubeList->appendString(STR16("Comb Voices"));
  cubeList->appendString(STR16("Odd-Ev Hrm"));
  cubeList->appendString(STR16("OddHrm+Rez"));
  cubeList->appendString(STR16("EvnHrm+Rez"));
  cubeList->appendString(STR16("MellowPeaks"));
  cubeList->appendString(STR16("AHmBnd.4"));
  cubeList->appendString(STR16("Vintage"));
  cubeList->appendString(STR16("MildQPole"));
  cubeList->appendString(STR16("Bonk>CO"));
  cubeList->appendString(STR16("Speaker"));
  cubeList->appendString(STR16("Expander"));
  cubeList->appendString(STR16("Separator"));
  cubeList->appendString(STR16("MildPolSwap"));
  cubeList->appendString(STR16("PoleCross.4"));
  cubeList->appendString(STR16("ApDistB6.4"));
  cubeList->appendString(STR16("Dave'sRave"));
  cubeList->appendString(STR16("Poles 1-7"));
  cubeList->appendString(STR16("2 Poles"));
  cubeList->appendString(STR16("4 Poles A"));
  cubeList->appendString(STR16("4 Poles B"));
  cubeList->appendString(STR16("4 Poles C"));
  cubeList->appendString(STR16("6 Poles"));
  cubeList->appendString(STR16("MultiPole"));
  cubeList->appendString(STR16("Tracker"));
  cubeList->appendString(STR16("Tracker 2"));
  cubeList->appendString(STR16("Tracker 3"));
  cubeList->appendString(STR16("Tracker5ths"));
  cubeList->appendString(STR16("InFifths.4"));
  cubeList->appendString(STR16("HarmTracker"));
  cubeList->appendString(STR16("1BndPrmtrcA"));
  cubeList->appendString(STR16("1BndPrmtrcB"));
  cubeList->appendString(STR16("1BndPrmtrcC"));
  cubeList->appendString(STR16("1BndPrmtrcD"));
  cubeList->appendString(STR16("MltiMetricA"));
  cubeList->appendString(STR16("MltiMetricB"));
  cubeList->appendString(STR16("MltiMetricC"));
  cubeList->appendString(STR16("Omni Metric"));
  cubeList->appendString(STR16("HarmncPeaks"));
  cubeList->appendString(STR16("HrmncPeaks2"));
  cubeList->appendString(STR16("InHarMetric"));
  cubeList->appendString(STR16("HarmShifter"));
  cubeList->appendString(STR16("HarmShiftr2"));
  cubeList->appendString(STR16("HarmShiftr3"));
  cubeList->appendString(STR16("Phaser"));
  cubeList->appendString(STR16("HrmncPhaser"));
  cubeList->appendString(STR16("HrmncPhsr2"));
  cubeList->appendString(STR16("Wide Bands"));
  cubeList->appendString(STR16("New Boost"));
  cubeList->appendString(STR16("VowelSpace2"));
  cubeList->appendString(STR16("VowelSpace3"));
  cubeList->appendString(STR16("Oh Shaper"));
  cubeList->appendString(STR16("Ah Shaper"));
  cubeList->appendString(STR16("Oo Shaper"));
  cubeList->appendString(STR16("FrmntShaper"));
  cubeList->appendString(STR16("Piano LP.4"));
  cubeList->appendString(STR16("PianoSndBrd"));
  cubeList->appendString(STR16("Strike Cube"));
  cubeList->appendString(STR16("Clav Curves"));
  cubeList->appendString(STR16("Symphony"));
  cubeList->appendString(STR16("String Cube"));
  cubeList->appendString(STR16("Swell Cube"));
  cubeList->appendString(STR16("Quartet.4"));
  cubeList->appendString(STR16("Mellotron.4"));
  cubeList->appendString(STR16("Cello"));
  cubeList->appendString(STR16("Key Squeak"));
  cubeList->appendString(STR16("Pluck.4"));
  cubeList->appendString(STR16("Pick It"));
  cubeList->appendString(STR16("El Pick It"));
  cubeList->appendString(STR16("ElGuit Cube"));
  cubeList->appendString(STR16("ElGuit Pick"));
  cubeList->appendString(STR16("Breather 1"));
  cubeList->appendString(STR16("Breather 2"));
  cubeList->appendString(STR16("WindNoise 1"));
  cubeList->appendString(STR16("WindNoise 2"));
  cubeList->appendString(STR16("Blow Cube"));
  cubeList->appendString(STR16("Overblow"));
  cubeList->appendString(STR16("Wind Filter"));
  cubeList->appendString(STR16("FlutBreth.4"));
  cubeList->appendString(STR16("VClarinet.4"));
  cubeList->appendString(STR16("New Mute.4"));
  cubeList->appendString(STR16("Trempeto.4"));
  cubeList->appendString(STR16("SfBrzando.4"));
  cubeList->appendString(STR16("UduFilter.4"));
  cubeList->appendString(STR16("Hip Kick"));
  cubeList->appendString(STR16("Cymbal Cube"));
  cubeList->appendString(STR16("ChimeFlange"));
  cubeList->appendString(STR16("CableRing.4"));
  cubeList->appendString(STR16("TensionWire"));
  cubeList->appendString(STR16("Auto Clang"));
  cubeList->appendString(STR16("Dragon Claw"));
  cubeList->appendString(STR16("Spectra"));
  cubeList->appendString(STR16("RippleSheet"));
  cubeList->appendString(STR16("Clear Water"));
  cubeList->appendString(STR16("Invisible"));
  cubeList->appendString(STR16("Wine Glass"));
  cubeList->appendString(STR16("SnakeCros.4"));
  cubeList->appendString(STR16("Analog.4"));
  cubeList->appendString(STR16("Skrtch Cube"));
  cubeList->appendString(STR16("KitchenSink"));
  cubeList->appendString(STR16("Head Pan 1"));
  cubeList->appendString(STR16("Head Pan 2"));
  cubeList->appendString(STR16("Arpeggio"));
  cubeList->appendString(STR16("HeavyFilt.4"));
  cubeList->appendString(STR16("Noiz Cube 2"));
  cubeList->appendString(STR16("Feedback"));
  cubeList->appendString(STR16("AllPoleDist"));
  cubeList->appendString(STR16("AllPoleDst2"));
  parameters.addParameter(cubeList);
  parameters.addParameter(STR16("Morpheus Morph"),nullptr,0,defaults[8],ParameterInfo::kCanAutomate,kMorpheusMorph);
  parameters.addParameter(STR16("Morpheus Frequency"),nullptr,0,defaults[9],ParameterInfo::kCanAutomate,kMorpheusFreq);
  parameters.addParameter(STR16("Morpheus Transform"),nullptr,0,defaults[10],ParameterInfo::kCanAutomate,kMorpheusTransform);
#ifdef ZPLANE_RESPONSE_DIAGNOSTICS
  std::fprintf(stderr,"[125A DIAG] Controller 11 legacy parameters registered\n");
  std::fflush(stderr);
#endif
  for(unsigned n=0;n<2;++n){
   const ParamID b=kLfo1On+n*6;
   const bool first=n==0;
   parameters.addParameter(first?STR16("LFO 1 On"):STR16("LFO 2 On"),nullptr,1,defaults[b],ParameterInfo::kCanAutomate,b);
   parameters.addParameter(first?STR16("LFO 1 Rate / Division"):STR16("LFO 2 Rate / Division"),nullptr,0,defaults[b+1],ParameterInfo::kCanAutomate,b+1);
   parameters.addParameter(first?STR16("LFO 1 Depth"):STR16("LFO 2 Depth"),nullptr,0,defaults[b+2],ParameterInfo::kCanAutomate,b+2);
   auto* shape=new StringListParameter(first?STR16("LFO 1 Shape"):STR16("LFO 2 Shape"),b+3);
   shape->appendString(STR16("Sine"));shape->appendString(STR16("Triangle"));shape->appendString(STR16("Saw"));
   shape->appendString(STR16("Square"));shape->appendString(STR16("Sample Hold"));
   parameters.addParameter(shape);
   auto* target=new StringListParameter(first?STR16("LFO 1 Target"):STR16("LFO 2 Target"),b+4);
   target->appendString(STR16("Off"));target->appendString(STR16("Morph / X3 Morph"));
   target->appendString(STR16("Morpheus Frequency"));target->appendString(STR16("Morpheus Transform"));
   target->appendString(STR16("Q / X3 Resonance"));
   parameters.addParameter(target);
   parameters.addParameter(first?STR16("LFO 1 Sync"):STR16("LFO 2 Sync"),nullptr,1,defaults[b+5],ParameterInfo::kCanAutomate,b+5);
#ifdef ZPLANE_RESPONSE_DIAGNOSTICS
   std::fprintf(stderr,"[125A DIAG] Controller LFO %u registered count=%u\n",n+1,
     static_cast<unsigned>(parameters.getParameterCount()));
   std::fflush(stderr);
#endif
  }
#ifdef ZPLANE_RESPONSE_DIAGNOSTICS
  std::fprintf(stderr,"[125A DIAG] Controller initialize exit count=%u\n",
     static_cast<unsigned>(parameters.getParameterCount()));
  std::fflush(stderr);
#endif
  return kResultOk;
 }
 tresult PLUGIN_API setComponentState(IBStream* stream) override {
  if(!stream)return kInvalidArgument;IBStreamer st(stream,kLittleEndian);
  uint32 magic=0,version=0;if(!st.readInt32u(magic)||!st.readInt32u(version)||magic!=kMagic||(version!=1&&version!=2&&version!=3&&version!=4))return kResultFalse;
  auto values=defaults;
  for(std::size_t i=0;i<(version==1?5u:version==2?6u:version==3?11u:23u);++i){auto& p=values[i];if(!st.readDouble(p)||!std::isfinite(p)||p<0.||p>1.)return kResultFalse;}
  for(ParamID id=0;id<values.size();++id)setParamNormalized(id,values[id]);
  return kResultOk;
 }
};
} // namespace zplane

#ifdef ZPLANE_RESPONSE_DIAGNOSTICS
// Development-only ABI for independent native host view-paint regression.
// This symbol is absent from normal BUILD_TESTING=OFF production builds.
extern "C" __declspec(dllexport) std::uint64_t __cdecl
ZPlaneResponseQaGet(unsigned field) noexcept {
 using namespace zplane::response_qa;
 switch(field){
  case 0:return created.load(std::memory_order_acquire);
  case 1:return drawn.load(std::memory_order_acquire);
  case 2:return updated.load(std::memory_order_acquire);
  case 3:return curveSignature.load(std::memory_order_acquire);
  default:return 0;
 }
}
#endif

BEGIN_FACTORY_DEF("125A Systems","","")
DEF_CLASS2(INLINE_UID_FROM_FUID(zplane::kProcessor),Steinberg::PClassInfo::kManyInstances,
  kVstAudioEffectClass,"125A Z-Plane FX",Steinberg::Vst::kDistributable,
  Steinberg::Vst::PlugType::kFxFilter,"0.1.0",kVstVersionString,zplane::Processor::createInstance)
DEF_CLASS2(INLINE_UID_FROM_FUID(zplane::kController),Steinberg::PClassInfo::kManyInstances,
  kVstComponentControllerClass,"125A Z-Plane FX Controller",0,"","0.1.0",
  kVstVersionString,zplane::Controller::createInstance)
END_FACTORY
