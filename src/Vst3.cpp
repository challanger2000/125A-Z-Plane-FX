#include "FilterCore.h"
#include "AutomationTimeline.h"
#ifdef ZPLANE_WITH_X3_RESEARCH
#include "X3ResearchAdapter.h"
#include <filesystem>
#include <string>
#include <cstdlib>
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
#include "base/source/fstreamer.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace zplane {
using namespace Steinberg;
using namespace Steinberg::Vst;
static const FUID kProcessor(0x3E858CA1,0xF71C4BC2,0xAA13012B,0xD7EE4561);
static const FUID kController(0x8577FE18,0x947B4E20,0xA4CB7205,0xC3F2A019);
constexpr uint32 kMagic=0x3150585Au;
enum : ParamID { kMode=0,kCutoff=1,kQuality=2,kWet=3,kBypass=4,kX3Filter=5 };
constexpr std::array<double,6> defaults={0.,0.5,0.5,1.,0.,0.};
static double clampParam(double x) noexcept {return std::isfinite(x)?std::clamp(x,0.0,1.0):0.;}
static Settings makeSettings(const std::array<double,6>& p) noexcept {
 Settings s;
 s.mode=static_cast<Mode>(std::min(3,static_cast<int>(std::floor(p[0]*4.))));
 s.frequency=20.*std::pow(1000.,p[1]);
 s.quality=0.2*std::pow(100.,p[2]);
 s.wet=p[3];s.bypass=p[4]>=0.5;
 return s;
}
class Processor final : public AudioEffect {
 std::array<double,6> params_{defaults}; // processor/audio owner: host process and control methods follow SDK lifecycle
 StereoFilter dsp_;
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
   dsp_.configure(rate_,makeSettings(params_));
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
  dsp_.configure(rate_,makeSettings(params_));
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
 AutomationTimeline timeline_{}; // preallocated outside realtime callback
 double rate_{48000.};
public:
 Processor(){setControllerClass(kController);}
 static FUnknown* createInstance(void*){return static_cast<IAudioProcessor*>(new Processor());}
 tresult PLUGIN_API initialize(FUnknown* context) override {
  const auto rc=AudioEffect::initialize(context); if(rc!=kResultOk)return rc;
  addAudioInput(STR16("Stereo In"),SpeakerArr::kStereo);
  addAudioOutput(STR16("Stereo Out"),SpeakerArr::kStereo);
#ifdef ZPLANE_WITH_X3_RESEARCH
  // Bank assets never ship with this prototype. Load once on the host
  // control thread, from the owner's own locally exported X3 banks.
  const char* explicitPath=std::getenv("ZPLANE_X3_BANK_DIR");
  const char* localData=std::getenv("LOCALAPPDATA");
  std::filesystem::path bankDir;
  if(explicitPath&&*explicitPath)bankDir=std::filesystem::u8path(explicitPath);
  else if(localData&&*localData)bankDir=std::filesystem::u8path(localData)/"125A"/"ZPlaneFX"/"Banks";
  if(!bankDir.empty()){
   std::string reason;
   x3Ready_=x3_.load(bankDir,reason);
  }
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
   dsp_.reset();
   dsp_.configure(rate_,makeSettings(params_));
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
   dsp_.reset();
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
      const auto id=queue->getParameterId();if(id>kX3Filter)continue;
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
  std::uint32_t cursor=0;
  std::size_t event=0;
  while(cursor<n){
    while(event<timeline.size()&&timeline[event].offset==cursor){
      params_[timeline[event].parameter]=timeline[event].value;++event;
    }
    const auto next=event<timeline.size()?std::min(n,timeline[event].offset):n;
    if(next<=cursor)return kResultFalse;
    const auto length=next-cursor;
#ifdef ZPLANE_WITH_X3_RESEARCH
    const auto requested=static_cast<unsigned>(std::min(55.,std::floor(params_[kX3Filter]*55.+0.5)));
    const bool useX3=requested>0;
    if(!familyReady_){
     familyBlend_=familyTarget_=useX3?1.:0.;
     familyReady_=true;
    }else if((useX3?1.:0.)!=familyTarget_){
     familyTarget_=useX3?1.:0.;
     familyLeft_=static_cast<std::uint32_t>(std::clamp(rate_*.005,1.,48000.));
     familyStep_=(familyTarget_-familyBlend_)/static_cast<double>(familyLeft_);
    }
    bool renderX3=false;
    const bool x3Audible=useX3||familyBlend_>0.||familyLeft_>0;
    if(x3Audible&&x3Ready_){
     x3_.completeDeferred();
     if(useX3){
      // Morph and Q, not legacy cutoff/resonance units.
      renderX3=x3_.set(requested,static_cast<float>(params_[kCutoff]),
                        static_cast<float>(params_[kQuality]),
                        static_cast<unsigned>(std::llround(rate_)));
     }else{
      // Continue the outgoing X3 filter until the family fade ends.
      renderX3=x3_.hasBanks();
     }
    }
    if(data.symbolicSampleSize==kSample32)
     renderFamilies(in.channelBuffers32[0]+cursor,in.channelBuffers32[1]+cursor,
                    out.channelBuffers32[0]+cursor,out.channelBuffers32[1]+cursor,
                    length,renderX3);
    else
     renderFamilies(in.channelBuffers64[0]+cursor,in.channelBuffers64[1]+cursor,
                    out.channelBuffers64[0]+cursor,out.channelBuffers64[1]+cursor,
                    length,renderX3);
#else
    dsp_.configure(rate_,makeSettings(params_));
    if(data.symbolicSampleSize==kSample32){
     dsp_.process(in.channelBuffers32[0]+cursor,in.channelBuffers32[1]+cursor,
                  out.channelBuffers32[0]+cursor,out.channelBuffers32[1]+cursor,length);
    }else{
     dsp_.process(in.channelBuffers64[0]+cursor,in.channelBuffers64[1]+cursor,
                  out.channelBuffers64[0]+cursor,out.channelBuffers64[1]+cursor,length);
    }
#endif
    cursor=next;
  }
  // An event at offset n defines the state for the following callback.
  while(event<timeline.size()){
    params_[timeline[event].parameter]=timeline[event].value;++event;
  }
  out.silenceFlags=0;return kResultOk;
 }
 tresult PLUGIN_API getState(IBStream* stream) override {
  if(!stream)return kInvalidArgument;
  IBStreamer st(stream,kLittleEndian);
  if(!st.writeInt32u(kMagic)||!st.writeInt32u(2))return kResultFalse;
  for(double p:params_)if(!st.writeDouble(p))return kResultFalse;
  return kResultOk;
 }
 tresult PLUGIN_API setState(IBStream* stream) override {
  if(!stream)return kInvalidArgument;IBStreamer st(stream,kLittleEndian);
  uint32 magic=0,version=0;if(!st.readInt32u(magic)||!st.readInt32u(version)||magic!=kMagic||(version!=1&&version!=2))return kResultFalse;
  auto candidate=params_;
  for(std::size_t i=0;i<(version==1?5u:6u);++i){auto& p=candidate[i];if(!st.readDouble(p)||!std::isfinite(p)||p<0.||p>1.)return kResultFalse;}
  if(version==1)candidate[kX3Filter]=0.;
  params_=candidate;dsp_.configure(rate_,makeSettings(params_));
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
 IPlugView* PLUGIN_API createView(FIDString name) override {
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
  return new VSTGUI::VST3Editor(this,"ZPlaneMain",uiPath.c_str());
 }
 static FUnknown* createInstance(void*){return static_cast<IEditController*>(new Controller());}
 tresult PLUGIN_API initialize(FUnknown* context) override {
  const auto rc=EditController::initialize(context);if(rc!=kResultOk)return rc;
  auto* mode=new StringListParameter(STR16("Filter Shape"),kMode);
  mode->appendString(STR16("Low Pass"));mode->appendString(STR16("High Pass"));
  mode->appendString(STR16("Band Pass"));mode->appendString(STR16("Notch"));
  parameters.addParameter(mode);
  parameters.addParameter(STR16("Cutoff"),nullptr,0,defaults[1],ParameterInfo::kCanAutomate,kCutoff);
  parameters.addParameter(STR16("Resonance"),nullptr,0,defaults[2],ParameterInfo::kCanAutomate,kQuality);
  parameters.addParameter(STR16("Wet"),nullptr,0,defaults[3],ParameterInfo::kCanAutomate,kWet);
  parameters.addParameter(STR16("Bypass"),nullptr,1,defaults[4],ParameterInfo::kCanAutomate|ParameterInfo::kIsBypass,kBypass);
  auto* x3List=new StringListParameter(STR16("X3 Filter (banks required)"),kX3Filter);
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
  return kResultOk;
 }
 tresult PLUGIN_API setComponentState(IBStream* stream) override {
  if(!stream)return kInvalidArgument;IBStreamer st(stream,kLittleEndian);
  uint32 magic=0,version=0;if(!st.readInt32u(magic)||!st.readInt32u(version)||magic!=kMagic||(version!=1&&version!=2))return kResultFalse;
  auto values=defaults;
  for(std::size_t i=0;i<(version==1?5u:6u);++i){auto& p=values[i];if(!st.readDouble(p)||!std::isfinite(p)||p<0.||p>1.)return kResultFalse;}
  for(ParamID id=0;id<values.size();++id)setParamNormalized(id,values[id]);
  return kResultOk;
 }
};
} // namespace zplane

BEGIN_FACTORY_DEF("125A Systems","","")
DEF_CLASS2(INLINE_UID_FROM_FUID(zplane::kProcessor),Steinberg::PClassInfo::kManyInstances,
  kVstAudioEffectClass,"125A Z-Plane FX",Steinberg::Vst::kDistributable,
  Steinberg::Vst::PlugType::kFxFilter,"0.1.0",kVstVersionString,zplane::Processor::createInstance)
DEF_CLASS2(INLINE_UID_FROM_FUID(zplane::kController),Steinberg::PClassInfo::kManyInstances,
  kVstComponentControllerClass,"125A Z-Plane FX Controller",0,"","0.1.0",
  kVstVersionString,zplane::Controller::createInstance)
END_FACTORY
