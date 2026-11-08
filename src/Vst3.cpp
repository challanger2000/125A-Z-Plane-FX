#include "FilterCore.h"
#include "AutomationTimeline.h"
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "base/source/fstreamer.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace zplane {
using namespace Steinberg;
using namespace Steinberg::Vst;
static const FUID kProcessor(0x3E858CA1,0xF71C4BC2,0xAA13012B,0xD7EE4561);
static const FUID kController(0x8577FE18,0x947B4E20,0xA4CB7205,0xC3F2A019);
constexpr uint32 kMagic=0x3150585Au;
enum : ParamID { kMode=0,kCutoff=1,kQuality=2,kWet=3,kBypass=4 };
constexpr std::array<double,5> defaults={0.,0.5,0.5,1.,0.};
static double clampParam(double x) noexcept {return std::isfinite(x)?std::clamp(x,0.0,1.0):0.;}
static Settings makeSettings(const std::array<double,5>& p) noexcept {
 Settings s;
 s.mode=static_cast<Mode>(std::min(3,static_cast<int>(std::floor(p[0]*4.))));
 s.frequency=20.*std::pow(1000.,p[1]);
 s.quality=0.2*std::pow(100.,p[2]);
 s.wet=p[3];s.bypass=p[4]>=0.5;
 return s;
}
class Processor final : public AudioEffect {
 std::array<double,5> params_{defaults}; // processor/audio owner: host process and control methods follow SDK lifecycle
 StereoFilter dsp_;
 AutomationTimeline timeline_{}; // preallocated outside realtime callback
 double rate_{48000.};
public:
 Processor(){setControllerClass(kController);}
 static FUnknown* createInstance(void*){return static_cast<IAudioProcessor*>(new Processor());}
 tresult PLUGIN_API initialize(FUnknown* context) override {
  const auto rc=AudioEffect::initialize(context); if(rc!=kResultOk)return rc;
  addAudioInput(STR16("Stereo In"),SpeakerArr::kStereo);
  addAudioOutput(STR16("Stereo Out"),SpeakerArr::kStereo);
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
  if(rc==kResultOk){rate_=setup.sampleRate;dsp_.reset();dsp_.configure(rate_,makeSettings(params_));}
  return rc;
 }
 tresult PLUGIN_API setProcessing(TBool) override {return kResultOk;}
 tresult PLUGIN_API setActive(TBool active) override {
  if(!active)dsp_.reset();return AudioEffect::setActive(active);
 }
 uint32 PLUGIN_API getTailSamples() override {return kInfiniteTail;}
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
      const auto id=queue->getParameterId();if(id>kBypass)continue;
      const int32 points=queue->getPointCount();
      for(int32 p=0;p<points;++p) {
        int32 offset=0;ParamValue value=0.;
        if(queue->getPoint(p,offset,value)!=kResultOk||offset<0||offset>data.numSamples||!std::isfinite(value))continue;
        if(!timeline.add(static_cast<uint32>(offset),id,clampParam(value)))
          return kResultFalse; // no silent loss of accepted automation
      }
    }
  }
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
    dsp_.configure(rate_,makeSettings(params_));
    const auto length=next-cursor;
    if(data.symbolicSampleSize==kSample32){
      dsp_.process(in.channelBuffers32[0]+cursor,in.channelBuffers32[1]+cursor,
                   out.channelBuffers32[0]+cursor,out.channelBuffers32[1]+cursor,length);
    }else{
      dsp_.process(in.channelBuffers64[0]+cursor,in.channelBuffers64[1]+cursor,
                   out.channelBuffers64[0]+cursor,out.channelBuffers64[1]+cursor,length);
    }
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
  if(!st.writeInt32u(kMagic)||!st.writeInt32u(1))return kResultFalse;
  for(double p:params_)if(!st.writeDouble(p))return kResultFalse;
  return kResultOk;
 }
 tresult PLUGIN_API setState(IBStream* stream) override {
  if(!stream)return kInvalidArgument;IBStreamer st(stream,kLittleEndian);
  uint32 magic=0,version=0;if(!st.readInt32u(magic)||!st.readInt32u(version)||magic!=kMagic||version!=1)return kResultFalse;
  auto candidate=params_;
  for(double& p:candidate){if(!st.readDouble(p)||!std::isfinite(p)||p<0.||p>1.)return kResultFalse;}
  params_=candidate;dsp_.configure(rate_,makeSettings(params_));return kResultOk;
 }
};
class Controller final : public EditController {
public:
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
  return kResultOk;
 }
 tresult PLUGIN_API setComponentState(IBStream* stream) override {
  if(!stream)return kInvalidArgument;IBStreamer st(stream,kLittleEndian);
  uint32 magic=0,version=0;if(!st.readInt32u(magic)||!st.readInt32u(version)||magic!=kMagic||version!=1)return kResultFalse;
  auto values=defaults;
  for(double& p:values)if(!st.readDouble(p)||!std::isfinite(p)||p<0.||p>1.)return kResultFalse;
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
