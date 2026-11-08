// Native Windows host probe against the exact staged .vst3 module.
// Tests real VST3 factory, 32/64-bit rendering, automation and project recall.
#include "FilterCore.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/utility/memoryibstream.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#include <array>
#include <algorithm>
#include <chrono>
#include <numeric>
#include <thread>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <vector>
#ifdef ZPLANE_WITH_X3_RESEARCH
#include "emux_filters.hpp"
#include "emux_extended.hpp"
#endif

using namespace Steinberg;
using namespace Steinberg::Vst;
namespace {
void require(bool ok,const char* message){
 if(!ok){std::cerr<<"NATIVE HOST FAIL: "<<message<<"\n";std::exit(2);}
}
struct Module {
 HMODULE dll{};
 IPluginFactory* factory{};
 explicit Module(const std::filesystem::path& path){
  dll=LoadLibraryW(path.c_str());require(dll!=nullptr,"LoadLibraryW");
  const auto init=reinterpret_cast<bool (*)()>(GetProcAddress(dll,"InitDll"));
  require(init&&init(),"InitDll");
  const auto get=reinterpret_cast<IPluginFactory* (*)()>(GetProcAddress(dll,"GetPluginFactory"));
  require(get!=nullptr,"GetPluginFactory");
  factory=get();require(factory!=nullptr,"Factory");
 }
 ~Module(){
  if(factory)factory->release();
  if(dll){
   const auto exitDll=reinterpret_cast<bool (*)()>(GetProcAddress(dll,"ExitDll"));
   if(exitDll)(void)exitDll();
   FreeLibrary(dll);
  }
 }
 TUID const* find(const char* category) const {
  // PClassInfo::cid is a TUID (array of 16 bytes) whose contents persist
  // only in the supplied result. Copying to static storage avoids dangling.
  static TUID processor{},controller{};
  for(int32 i=0;i<factory->countClasses();++i){
   PClassInfo info{};
   if(factory->getClassInfo(i,&info)!=kResultOk)continue;
   if(std::strcmp(info.category,category)==0){
    if(std::strcmp(category,kVstAudioEffectClass)==0){
     std::memcpy(processor,info.cid,sizeof(TUID));return &processor;
    }
    std::memcpy(controller,info.cid,sizeof(TUID));return &controller;
   }
  }
  return nullptr;
 }
};
struct Instance {
 IComponent* component{};
 IAudioProcessor* audio{};
 IEditController* controller{};
 HostApplication host{};
 explicit Instance(Module& module){
  const auto* pcid=module.find(kVstAudioEffectClass);
  require(pcid!=nullptr,"Processor class");
  require(module.factory->createInstance(*pcid,IComponent::iid,reinterpret_cast<void**>(&component))==kResultOk&&component,
          "Processor factory");
  require(component->queryInterface(IAudioProcessor::iid,reinterpret_cast<void**>(&audio))==kResultOk&&audio,"Audio interface");
  const auto* ccid=module.find(kVstComponentControllerClass);
  require(ccid!=nullptr,"Controller class");
  require(module.factory->createInstance(*ccid,IEditController::iid,reinterpret_cast<void**>(&controller))==kResultOk&&controller,
          "Controller factory");
  require(component->initialize(&host)==kResultOk,"Processor init");
  require(controller->initialize(&host)==kResultOk,"Controller init");
  // Verify the GUI is actually exposed as a VST3 editor (not just a
  // .uidesc resource accidentally omitted from the binary build).
  auto* view=controller->createView(ViewType::kEditor);
  require(view!=nullptr,"Native VST3 editor view must exist");
  ViewRect rect{};
  require(view->getSize(&rect)==kResultOk,"Native VSTGUI editor dimensions");
  require(rect.right-rect.left>=800&&rect.bottom-rect.top>=400,
          "Native VSTGUI expected layout size");
  view->release();
 }
 void setup(int32 sampleFormat,double sampleRate=48000.,int32 maxBlock=512){
  SpeakerArrangement stereo=SpeakerArr::kStereo;
  require(audio->setBusArrangements(&stereo,1,&stereo,1)==kResultOk,"Stereo arrangement");
  ProcessSetup setup{};setup.sampleRate=sampleRate;setup.maxSamplesPerBlock=maxBlock;
  setup.processMode=kRealtime;setup.symbolicSampleSize=sampleFormat;
  require(audio->setupProcessing(setup)==kResultOk,"setupProcessing");
  const auto tail=audio->getTailSamples();
  require(tail!=kInfiniteTail&&tail>=48000u,"Finite bounded IIR filter tail");
  require(component->setActive(true)==kResultOk,"setActive(true)");
  require(audio->setProcessing(true)==kResultOk,"setProcessing(true)");
 }
 void stop(){
  require(audio->setProcessing(false)==kResultOk,"setProcessing(false)");
  require(component->setActive(false)==kResultOk,"setActive(false)");
 }
 ~Instance(){
  if(controller){controller->terminate();controller->release();}
  if(audio)audio->release();
  if(component){component->terminate();component->release();}
 }
};
template<typename T>
void render(Instance& inst,int32 format,ParameterChanges* changes,bool verify){
 std::array<T,512> left{},right{},outL{},outR{};
 for(int i=0;i<512;++i){left[static_cast<size_t>(i)]=(i%2)?T(.25):T(-.25);right[static_cast<size_t>(i)]=0;}
 T* inputChannels[2]={left.data(),right.data()};
 T* outputChannels[2]={outL.data(),outR.data()};
 AudioBusBuffers in{},out{};in.numChannels=2;out.numChannels=2;
 if(format==kSample32){
  in.channelBuffers32=reinterpret_cast<Sample32**>(inputChannels);
  out.channelBuffers32=reinterpret_cast<Sample32**>(outputChannels);
 }else{
  in.channelBuffers64=reinterpret_cast<Sample64**>(inputChannels);
  out.channelBuffers64=reinterpret_cast<Sample64**>(outputChannels);
 }
 ProcessData process{};
 process.numSamples=512;process.numInputs=1;process.inputs=&in;
 process.numOutputs=1;process.outputs=&out;process.symbolicSampleSize=format;
 process.processMode=kRealtime;process.inputParameterChanges=changes;
 require(inst.audio->process(process)==kResultOk,"Native process");
 for(int i=0;i<512;++i){
  require(std::isfinite(static_cast<double>(outL[static_cast<size_t>(i)])),"Finite left output");
  require(outR[static_cast<size_t>(i)]==T(0),"No unexpected right crosstalk");
 }
 if(verify){
  // Bypass point at 128: first sample before it must still be filtered,
  // samples 400..511 must be exactly dry after the 240-frame fade.
  require(outL[127]!=left[127],"Filter audible before sample-offset bypass");
  for(int i=400;i<512;++i)
   require(outL[static_cast<size_t>(i)]==left[static_cast<size_t>(i)],
           "Sample-accurate bypass and five-ms ramp");
 }
}
void exerciseDenseAutomation(Module& module) {
 // Adversarial 4096 host events (the documented capacity). Use distinct offsets:
 // Steinberg's queue coalesces repeated points at one offset, which made
 // our earlier overflow fixture invalid. Exercise sample-time order independently of
 // the six standalone CTests.
 Instance host(module);
 host.setup(kSample32,48000.,4096);
 std::array<float,4096> inL{},inR{},outL{},outR{};
 for(std::size_t n=0;n<inL.size();++n)inL[n]=n%2==0?.125f:-.125f;
 Sample32* inputs[]{inL.data(),inR.data()};
 Sample32* outputs[]{outL.data(),outR.data()};
 AudioBusBuffers in{},out{};
 in.numChannels=out.numChannels=2;
 in.channelBuffers32=inputs;out.channelBuffers32=outputs;
 ProcessData data{};
 data.numSamples=4096;
 data.numInputs=data.numOutputs=1;
 data.inputs=&in;data.outputs=&out;
 data.symbolicSampleSize=kSample32;data.processMode=kRealtime;
 ParameterChanges changes;
 int32 index=0;
 auto* wet=changes.addParameterData(3,index);
 require(wet!=nullptr,"4096-event parameter queue");
 for(int i=0;i<4096;++i){
  int32 point=0;
  const auto offset=i;
  const auto normalized=(i%2)?0.99:0.01;
  require(wet->addPoint(offset,normalized,point)==kResultOk,
          "VST3 queue can hold dense events");
 }
 data.inputParameterChanges=&changes;
 // Independent automation oracle: the unautomated plugin supplies the
 // filtered PCM. A separate, analytically computed 240-sample gain ramp
 // then predicts EVERY dense host parameter event's audio consequence.
 // This fails if a processor collapses the 4096 points to one final value
 // while still returning finite PCM (the former test's blind spot).
 Instance reference(module);
 reference.setup(kSample32,48000.,4096);
 std::array<float,4096> filteredL{},filteredR{};
 Sample32* referenceOutputs[]{filteredL.data(),filteredR.data()};
 AudioBusBuffers referenceOut{};
 referenceOut.numChannels=2;
 referenceOut.channelBuffers32=referenceOutputs;
 ProcessData referenceData=data;
 referenceData.outputs=&referenceOut;
 referenceData.inputParameterChanges=nullptr;
 require(reference.audio->process(referenceData)==kResultOk,
         "Reference filtered PCM for dense automation");
 require(host.audio->process(data)==kResultOk,
         "4096 densely automated edits must not overrun event timeline");
 double wetEnvelope=1.0;
 unsigned distinguishable=0;
 for(int i=0;i<4096;++i){
  const auto n=static_cast<std::size_t>(i);
  const double requested=(i%2)?0.99:0.01;
  // 48 kHz * 5 ms = 240 frames; each new host point retargets
  // the envelope from its CURRENT value, not from a stale endpoint.
  wetEnvelope+=(requested-wetEnvelope)/240.0;
  const double expected=static_cast<double>(inL[n])+
     (static_cast<double>(filteredL[n])-static_cast<double>(inL[n]))*wetEnvelope;
  require(std::isfinite(outL[n])&&std::isfinite(outR[n])&&outR[n]==0.f,
          "Dense automation must remain finite and stereo-separated");
  require(std::fabs(static_cast<double>(outL[n])-expected)<1.e-6,
          "Every dense host sample-offset wet edit must affect actual PCM");
  if(std::fabs(static_cast<double>(outL[n])-static_cast<double>(filteredL[n]))>0.01)
   ++distinguishable;
 }
 require(distinguishable>256,
         "Dense automation oracle must distinguish ignored host parameter events");
 // 4097 points must fail explicitly rather than silently discard
 // parameter edits or run out of bounds.
 auto* queue=changes.getParameterData(0);
 int32 added=0;
 require(queue->addPoint(4096,.25,added)==kResultOk,"Overflow fixture");
 require(host.audio->process(data)!=kResultOk,"Overflow must be reported");
 // Rejecting event 4097 must not commit any partial parameter state.
 // The last accepted offset 4095 specified wet=0.99.
 Steinberg::ResizableMemoryIBStream afterOverflow;
 require(host.component->getState(&afterOverflow)==kResultOk,
         "Processor state remains saveable after automation overflow");
 afterOverflow.rewind();
 require(host.controller->setComponentState(&afterOverflow)==kResultOk,
         "Controller accepts processor state after rejected overflow");
 require(std::fabs(host.controller->getParamNormalized(3)-0.99)<1.e-12,
         "Rejected overflow must preserve last accepted wet parameter");
 reference.stop();
 host.stop();
 std::cout<<"NATIVE HOST PASS: 4096 timed edits, finite PCM, overflow rejection\\n";
}
void exerciseAttachedEditor(Module& module) {
 // The previous smoke only queried the 900x470 dimensions. That cannot
 // catch malformed child-view definitions: actually attach the VSTGUI
 // editor to an HWND and require it to create a native child window.
 Instance instance(module);
 HWND parent=CreateWindowExW(0,L"STATIC",L"ZPlane QA host",
                           WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
                           CW_USEDEFAULT,CW_USEDEFAULT,920,520,
                           nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
 require(parent!=nullptr,"Create native Win32 GUI test parent");
 auto* view=instance.controller->createView(ViewType::kEditor);
 require(view!=nullptr,"Create independent GUI instance");
 require(view->isPlatformTypeSupported(kPlatformTypeHWND)==kResultTrue,
         "Win32 host platform must be supported");
 require(view->attached(parent,kPlatformTypeHWND)==kResultOk,
         "VSTGUI must attach and parse its child views");
 // Simulate the VST3 host's documented onSize callback after it has
 // allocated the editor HWND. The prior native probe never performed this
 // host step, so the editor reported its initial 0x0 platform frame.
 ViewRect requested{0,0,900,470};
 require(view->onSize(&requested)==kResultOk,"Host initial editor onSize callback");
 ViewRect rect{};
 const auto sizeResult=view->getSize(&rect);
 std::cout<<"VSTGUI attached reported size: "<<rect.getWidth()<<" x "<<rect.getHeight()
          <<" result "<<sizeResult<<"\\n";
 require(sizeResult==kResultOk&&rect.getWidth()==900&&rect.getHeight()==470,
         "VSTGUI attached canvas must match host initial onSize");
 require(GetWindow(parent,GW_CHILD)!=nullptr,
         "Attached VSTGUI must create an actual native child window");
 require(view->removed()==kResultOk,"Clean VSTGUI detach");
 view->release();
 require(DestroyWindow(parent)!=0,"Destroy GUI test parent");
 std::cout<<"NATIVE GUI HOST PASS: real HWND editor attach, child window and detach\\n";
}
void exercise(Module& module,int32 format){
 Instance i(module);i.setup(format);
 ParameterChanges changes;
 int32 index=0;
 auto* bypass=changes.addParameterData(4,index);
 require(bypass!=nullptr,"Bypass parameter queue");
 int32 pointIndex=0;
 require(bypass->addPoint(128,1.,pointIndex)==kResultOk,"Bypass at offset 128");
 // Earlier and later points in the same queue must preserve their timestamps.
 // Final point is the persisted ON state, so the prior save/recall oracle
 // continues to detect a collapsed-queue implementation.
 int32 index2=0;
 auto* wet=changes.addParameterData(3,index2);
 require(wet!=nullptr,"Wet queue");
 int32 point2=0;
 require(wet->addPoint(32,0.6,point2)==kResultOk,"Wet at offset 32");
 require(wet->addPoint(64,1.,point2)==kResultOk,"Wet at offset 64");
 if(format==kSample32)render<float>(i,format,&changes,true);
 else render<double>(i,format,&changes,true);
 Steinberg::ResizableMemoryIBStream saved;
 require(i.component->getState(&saved)==kResultOk,"Component state save");
 require(saved.getCursor()==56,"Expected state v2 size");
 saved.rewind();
 require(i.controller->setComponentState(&saved)==kResultOk,"Controller project recall");
 require(i.controller->getParamNormalized(4)==1.,"Controller bypass matches processor");
 // Fresh instance must reproduce saved audible bypass.
 Instance restored(module);
 saved.rewind();
 require(restored.component->setState(&saved)==kResultOk,"Fresh processor state recall");
 saved.rewind();
 require(restored.controller->setComponentState(&saved)==kResultOk,"Fresh controller state recall");
 require(restored.controller->getParamNormalized(4)==1.,"Fresh controller bypass persisted");
 restored.setup(format);
 if(format==kSample32)render<float>(restored,format,nullptr,false);
 else render<double>(restored,format,nullptr,false);
 // Host must reject truncated state without mutating previously valid settings.
 Steinberg::ResizableMemoryIBStream corrupt;
 const std::uint8_t truncated[]={0x5A,0x58,0x50,0x31};
 int32 wrote=0;
 require(corrupt.write(const_cast<std::uint8_t*>(truncated),4,&wrote)==kResultOk&&wrote==4,
         "Truncated fixture setup");
 corrupt.rewind();
 require(restored.component->setState(&corrupt)!=kResultOk,"Truncated processor recall rejection");
 restored.stop();i.stop();
}
#ifdef ZPLANE_WITH_X3_RESEARCH
emux::Configuration referenceFor(unsigned id,float morph,float q,unsigned rate,
 const emux::FactoryBank& factory,const emux::ExtendedBank& extended) {
 if(id<=33)return {factory.coefficients(id-1,morph,q,rate),6};
 if(id<=50)return extended.coefficients(id-34,morph,q,rate);
 if(id<55)return extended.morph_coefficients(
  static_cast<emux::MorphKind>(id-51),
  emux::MorphParameters{32,96,64,48,112,64},morph,q,rate);
 std::array<emux::DesignerSection,6> sections{};
 sections[0].shape=emux::Shape::eq;
 sections[1].shape=emux::Shape::lowpass;
 return extended.designer_coefficients(sections,morph,q,rate);
}
template<typename Sample>
void exerciseX3Mode(Module& module,unsigned id,unsigned rate,int32 format,
 const emux::FactoryBank& factory,const emux::ExtendedBank& extended) {
 Instance host(module);
 host.setup(format,static_cast<double>(rate));
 ParameterChanges changes;
 const std::array<std::pair<ParamID,ParamValue>,3> params{{
  {5,static_cast<double>(id)/55.},
  {1,.35},
  {2,.65}
 }};
 for(const auto& [param,value]:params){
  int32 queueIndex=0;
  auto* queue=changes.addParameterData(param,queueIndex);
  require(queue!=nullptr,"X3 host parameter queue");
  int32 pointIndex=0;
  require(queue->addPoint(0,value,pointIndex)==kResultOk,"X3 sample-zero parameter");
 }
 std::array<Sample,128> il{},ir{},ol{},orr{};
 il[0]=static_cast<Sample>(.001f);
 ir[7]=static_cast<Sample>(-.001f);
 Sample* inputs[2]{il.data(),ir.data()};
 Sample* outputs[2]{ol.data(),orr.data()};
 AudioBusBuffers in{},out{};
 in.numChannels=out.numChannels=2;
 if(format==kSample32){
  in.channelBuffers32=reinterpret_cast<Sample32**>(inputs);
  out.channelBuffers32=reinterpret_cast<Sample32**>(outputs);
 }else{
  in.channelBuffers64=reinterpret_cast<Sample64**>(inputs);
  out.channelBuffers64=reinterpret_cast<Sample64**>(outputs);
 }
 ProcessData data{};
 data.numSamples=128;
 data.numInputs=data.numOutputs=1;
 data.inputs=&in;data.outputs=&out;
 data.symbolicSampleSize=format;
 data.processMode=kRealtime;
 data.inputParameterChanges=&changes;
 require(host.audio->process(data)==kResultOk,"X3 full native VST3 render");
 const auto cfg=referenceFor(id,.35f,.65f,rate,factory,extended);
 emux::Filter wantedL(cfg.coefficients,cfg.sections);
 emux::Filter wantedR(cfg.coefficients,cfg.sections);
 for(int i=0;i<128;++i){
  const auto left=static_cast<Sample>(wantedL.process_sample(static_cast<float>(il[i])));
  const auto right=static_cast<Sample>(wantedR.process_sample(static_cast<float>(ir[i])));
  if(ol[i]!=left||orr[i]!=right){
   std::cerr<<"X3 PCM mismatch id="<<id<<" rate="<<rate<<" bits="<<(format==kSample32?32:64)
            <<" sample="<<i<<" expected L="<<left<<" actual L="<<ol[i]<<"\n";
   require(false,"all 55 X3 native hosted PCM must match independent research reference");
  }
 }
 host.stop();
}
void exerciseX3TimedFilterSwitch(Module& module,
 const emux::FactoryBank& factory,const emux::ExtendedBank& extended) {
 // An actual sample-offset filter change must start at exactly frame 128.
 // This reference constructs two independent stateful E-MU filters and
 // their 240-frame crossfade without calling X3ResearchAdapter.
 constexpr unsigned kRate=48000;
 constexpr int kFrames=512,kOffset=128,kFade=240;
 Instance host(module);
 host.setup(kSample32,kRate,kFrames);
 ParameterChanges changes;
 int32 slot=0;
 auto* filter=changes.addParameterData(5,slot);
 require(filter!=nullptr,"X3 timed filter automation queue");
 int32 point=0;
 require(filter->addPoint(0,1./55.,point)==kResultOk,"Initial X3 mode 1");
 require(filter->addPoint(kOffset,2./55.,point)==kResultOk,"Timed X3 mode 2");
 for(const auto& [tag,value]:std::array<std::pair<ParamID,double>,2>{{{1,.35},{2,.65}}}){
  int32 n=0;
  auto* q=changes.addParameterData(tag,n);
  require(q!=nullptr,"X3 morph/resonance automation");
  int32 idx=0;
  require(q->addPoint(0,value,idx)==kResultOk,"X3 initial morph/Q point");
 }
 std::array<float,kFrames> il{},ir{},ol{},orr{};
 for(int n=0;n<kFrames;++n){
  il[static_cast<std::size_t>(n)]=static_cast<float>(.018*std::sin(n*.071));
  ir[static_cast<std::size_t>(n)]=static_cast<float>(.013*std::cos(n*.093));
 }
 Sample32* input[]{il.data(),ir.data()},*output[]{ol.data(),orr.data()};
 AudioBusBuffers in{},out{};
 in.numChannels=out.numChannels=2;in.channelBuffers32=input;out.channelBuffers32=output;
 ProcessData data{};
 data.numSamples=kFrames;data.numInputs=data.numOutputs=1;
 data.inputs=&in;data.outputs=&out;data.symbolicSampleSize=kSample32;
 data.processMode=kRealtime;data.inputParameterChanges=&changes;
 require(host.audio->process(data)==kResultOk,"Timed X3 native process");
 const auto cfgA=referenceFor(1,.35f,.65f,kRate,factory,extended);
 const auto cfgB=referenceFor(2,.35f,.65f,kRate,factory,extended);
 emux::Filter primaryL(cfgA.coefficients,cfgA.sections);
 emux::Filter primaryR(cfgA.coefficients,cfgA.sections);
 emux::Filter nextL(cfgA.coefficients,cfgA.sections);
 emux::Filter nextR(cfgA.coefficients,cfgA.sections);
 bool detectedChange=false;
 for(int n=0;n<kFrames;++n){
  if(n==kOffset){
   nextL=primaryL;nextR=primaryR;
   nextL.set_coefficients(cfgB.coefficients,cfgB.sections);
   nextR.set_coefficients(cfgB.coefficients,cfgB.sections);
  }
  const auto a=il[static_cast<std::size_t>(n)],b=ir[static_cast<std::size_t>(n)];
  float wantL=0.f,wantR=0.f;
  if(n<kOffset){
   wantL=primaryL.process_sample(a);wantR=primaryR.process_sample(b);
  }else if(n<kOffset+kFade){
   const auto firstL=primaryL.process_sample(a),firstR=primaryR.process_sample(b);
   const auto secondL=nextL.process_sample(a),secondR=nextR.process_sample(b);
   const auto alpha=static_cast<double>(n-kOffset+1)/kFade;
   wantL=static_cast<float>(firstL+(secondL-firstL)*alpha);
   wantR=static_cast<float>(firstR+(secondR-firstR)*alpha);
   if(std::fabs(wantL-firstL)>1.e-7f)detectedChange=true;
  }else{
   wantL=nextL.process_sample(a);wantR=nextR.process_sample(b);
  }
  if(wantL!=ol[static_cast<std::size_t>(n)]||wantR!=orr[static_cast<std::size_t>(n)]){
   std::cerr<<"X3 timed host PCM mismatch at frame "<<n<<" expected L="<<wantL
            <<" actual L="<<ol[static_cast<std::size_t>(n)]<<"\\n";
   require(false,"X3 sample-offset filter crossfade PCM exactness");
  }
 }
 require(detectedChange,"Timed X3 oracle must distinguish changed and original filter");
 host.stop();
 std::cout<<"NATIVE HOST PASS: exact sample-offset X3 filter transition at frame 128\\n";
}
void exerciseX3LegacyFamilySwitch(Module& module,
 const emux::FactoryBank& factory,const emux::ExtendedBank& extended) {
 // Independent combined DSP PCM oracle, not a finite-output smoke.
 // Exercises both directions within ONE host buffer, preserving sample offsets.
 constexpr int kFrames=768,kToX3=128,kToLegacy=512,kFade=240;
 Instance host(module);
 host.setup(kSample32,48000.,kFrames);
 ParameterChanges changes;
 int32 slot=0;
 auto* filter=changes.addParameterData(5,slot);
 require(filter!=nullptr,"Family switch filter parameter queue");
 int32 point=0;
 require(filter->addPoint(kToX3,1./55.,point)==kResultOk,"Legacy to X3 point");
 require(filter->addPoint(kToLegacy,0.,point)==kResultOk,"X3 to legacy point");
 std::array<float,kFrames> il{},ir{},ol{},orr{};
 for(int n=0;n<kFrames;++n){
  il[static_cast<std::size_t>(n)]=static_cast<float>(.018*std::sin(n*.071)+.006*std::cos(n*.031));
  ir[static_cast<std::size_t>(n)]=static_cast<float>(.013*std::cos(n*.093)-.004*std::sin(n*.045));
 }
 Sample32* input[]{il.data(),ir.data()},*output[]{ol.data(),orr.data()};
 AudioBusBuffers in{},out{};
 in.numChannels=out.numChannels=2;in.channelBuffers32=input;out.channelBuffers32=output;
 ProcessData data{};
 data.numSamples=kFrames;data.numInputs=data.numOutputs=1;
 data.inputs=&in;data.outputs=&out;data.symbolicSampleSize=kSample32;
 data.processMode=kRealtime;data.inputParameterChanges=&changes;
 require(host.audio->process(data)==kResultOk,"Native dual-family PCM switch");
 // Compute legacy from a separate documented biquad state, not the plugin
 // being verified; X3 follows independent original E-MU research reference.
 zplane::Settings oldSettings{};
 oldSettings.frequency=20.*std::pow(1000.,.5);
 oldSettings.quality=.2*std::pow(100.,.5);
 zplane::StereoFilter legacy;
 legacy.configure(48000.,oldSettings);
 const auto cfg=referenceFor(1,.5f,.5f,48000,factory,extended);
 emux::Filter x3L(cfg.coefficients,cfg.sections),x3R(cfg.coefficients,cfg.sections);
 unsigned distinguishable=0;
 for(int n=0;n<kFrames;++n){
  float legacyL=0.f,legacyR=0.f;
  legacy.process(&il[static_cast<std::size_t>(n)],&ir[static_cast<std::size_t>(n)],
                 &legacyL,&legacyR,1);
  float researchL=0.f,researchR=0.f;
  if(n>=kToX3){
   researchL=x3L.process_sample(il[static_cast<std::size_t>(n)]);
   researchR=x3R.process_sample(ir[static_cast<std::size_t>(n)]);
  }
  double blend=0.;
  if(n>=kToX3&&n<kToX3+kFade)
   blend=double(n-kToX3+1)/kFade;
  else if(n>=kToX3+kFade&&n<kToLegacy)
   blend=1.;
  else if(n>=kToLegacy&&n<kToLegacy+kFade)
   blend=1.-double(n-kToLegacy+1)/kFade;
  const float wantL=static_cast<float>(double(legacyL)+(double(researchL)-double(legacyL))*blend);
  const float wantR=static_cast<float>(double(legacyR)+(double(researchR)-double(legacyR))*blend);
  if(std::fabs(ol[static_cast<std::size_t>(n)]-wantL)>1.e-6f||
     std::fabs(orr[static_cast<std::size_t>(n)]-wantR)>1.e-6f){
   std::cerr<<"Family PCM mismatch at "<<n<<" expected "<<wantL
            <<" observed "<<ol[static_cast<std::size_t>(n)]<<"\\n";
   require(false,"Legacy/X3 bidirectional 5ms sample-accurate independent PCM oracle");
  }
  if(blend>0.&&blend<1.&&std::fabs(researchL-legacyL)>1.e-5f)
   ++distinguishable;
 }
 require(distinguishable>50,"Family-switch oracle must detect hard-switch mutation");
 // A single long process block must equal a partitioned host timeline
 // sample-for-sample. This catches the latent state-aging error where a
 // family fade finishes inside a host segment and both DSP engines keep
 // running until the *segment* ends, instead of stopping the inactive
 // engine at the exact end of the 240-sample ramp.
 Instance split(module);
 split.setup(kSample32,48000.,kFrames);
 std::array<float,kFrames> splitL{},splitR{};
 for(const auto& [base,end] :
     std::array<std::pair<int,int>,4>{{{0,256},{256,384},{384,640},{640,kFrames}}}) {
  ParameterChanges partChanges;
  int32 partSlot=0;
  auto* partFilter=partChanges.addParameterData(5,partSlot);
  require(partFilter!=nullptr,"Partitioned X3 family mode queue");
  for(const auto& [globalOffset,mode] :
      std::array<std::pair<int,double>,2>{{{kToX3,1./55.},{kToLegacy,0.}}}) {
   if(globalOffset>=base&&globalOffset<end){
    int32 partPoint=0;
    require(partFilter->addPoint(globalOffset-base,mode,partPoint)==kResultOk,
            "Partitioned host filter edit");
   }
  }
  Sample32* partInput[]{il.data()+base,ir.data()+base};
  Sample32* partOutput[]{splitL.data()+base,splitR.data()+base};
  AudioBusBuffers partIn{},partOut{};
  partIn.numChannels=partOut.numChannels=2;
  partIn.channelBuffers32=partInput;partOut.channelBuffers32=partOutput;
  ProcessData chunk{};
  chunk.numSamples=end-base;chunk.numInputs=chunk.numOutputs=1;
  chunk.inputs=&partIn;chunk.outputs=&partOut;
  chunk.symbolicSampleSize=kSample32;chunk.processMode=kRealtime;
  chunk.inputParameterChanges=&partChanges;
  require(split.audio->process(chunk)==kResultOk,"Partitioned X3 family process");
 }
 for(int n=0;n<kFrames;++n) {
  if(ol[static_cast<std::size_t>(n)]!=splitL[static_cast<std::size_t>(n)]||
     orr[static_cast<std::size_t>(n)]!=splitR[static_cast<std::size_t>(n)]){
   std::cerr<<"Family host partition mismatch at sample "<<n
            <<" whole "<<ol[static_cast<std::size_t>(n)]
            <<" split "<<splitL[static_cast<std::size_t>(n)]<<"\\n";
   require(false,"Native family crossfade must be block-partition invariant");
  }
 }
 split.stop();
 host.stop();
 std::cout<<"NATIVE HOST PASS: exact 5ms Legacy/X3/Legacy crossfades at 128/512\\n";
}
void exercisePacedX3(Module& module) {
 // Host-realistic, bounded 256-frame pacing. Measure actual process()
 // call duration separately from scheduler wakeup lag. Do not claim an
 // ASIO PASS based on this uncontrolled cloud-runner measurement.
 using Clock=std::chrono::steady_clock;
 constexpr int kFrames=256,kBlocks=180;
 constexpr double kRate=48000.0;
 constexpr double kDeadlineMs=1000.0*kFrames/kRate;
 // In the initial cloud run, 118/180 wakeups were more than one
 // callback deadline late despite short DSP execution. Request a 1 ms
 // Windows timer period, and report wakeup jitter separately.
 const auto timerPeriodStatus=timeBeginPeriod(1);
 std::array<double,1000> timerOverheads{};
 for(auto& elapsed:timerOverheads){
  const auto a=Clock::now();
  const auto b=Clock::now();
  elapsed=std::chrono::duration<double,std::milli>(b-a).count();
 }
 std::sort(timerOverheads.begin(),timerOverheads.end());
 const double timerP99=timerOverheads[989];
 Instance host(module);
 host.setup(kSample32,kRate,kFrames);
 std::array<float,kFrames> inL{},inR{},outL{},outR{};
 for(int n=0;n<kFrames;++n){
  inL[static_cast<std::size_t>(n)]=static_cast<float>(std::sin(n*.14)*.01);
  inR[static_cast<std::size_t>(n)]=static_cast<float>(std::cos(n*.19)*.01);
 }
 Sample32* inputs[]{inL.data(),inR.data()};
 Sample32* outputs[]{outL.data(),outR.data()};
 AudioBusBuffers in{},out{};
 in.numChannels=out.numChannels=2;
 in.channelBuffers32=inputs;out.channelBuffers32=outputs;
 ProcessData data{};
 data.numSamples=kFrames;data.numInputs=data.numOutputs=1;
 data.inputs=&in;data.outputs=&out;
 data.symbolicSampleSize=kSample32;data.processMode=kRealtime;
 std::vector<double> callbacks,wakeLateness;
 callbacks.reserve(kBlocks);wakeLateness.reserve(kBlocks);
 const auto t0=Clock::now()+std::chrono::milliseconds(15);
 for(int block=0;block<kBlocks;++block){
  const auto due=t0+std::chrono::duration_cast<Clock::duration>(
    std::chrono::duration<double>(double(block*kFrames)/kRate));
  std::this_thread::sleep_until(due);
  const auto wake=Clock::now();
  wakeLateness.push_back(std::chrono::duration<double,std::milli>(wake-due).count());
  ParameterChanges changes;
  const std::array<std::pair<ParamID,double>,3> edits{{
   {5,double(1+block%55)/55.},
   {1,double(block%101)/100.},
   {2,double((block*7)%101)/100.}
  }};
  for(const auto& [param,value]:edits){
   int32 index=0;
   auto* queue=changes.addParameterData(param,index);
   require(queue!=nullptr,"Paced X3 host queue");
   int32 point=0;
   require(queue->addPoint(0,value,point)==kResultOk,"Paced X3 parameter edit");
  }
  data.inputParameterChanges=&changes;
  const auto before=Clock::now();
  const auto result=host.audio->process(data);
  const auto after=Clock::now();
  require(result==kResultOk,"Paced X3 callback status");
  callbacks.push_back(std::chrono::duration<double,std::milli>(after-before).count());
  for(int n=0;n<kFrames;++n)
   require(std::isfinite(outL[static_cast<std::size_t>(n)])
         &&std::isfinite(outR[static_cast<std::size_t>(n)]),
         "Paced real X3 mode/morph/resonance output finite");
 }
 const auto sum=std::accumulate(callbacks.begin(),callbacks.end(),0.0);
 std::sort(callbacks.begin(),callbacks.end());
 const auto percentile=[&](double p){
  const auto idx=std::min(callbacks.size()-1,
   static_cast<std::size_t>(std::ceil(p*static_cast<double>(callbacks.size()))-1.));
  return callbacks[idx];
 };
 const auto over=std::count_if(callbacks.begin(),callbacks.end(),
     [&](double elapsed){return elapsed>kDeadlineMs;});
 const auto wakeMisses=std::count_if(wakeLateness.begin(),wakeLateness.end(),
     [&](double elapsed){return elapsed>kDeadlineMs;});
 std::cout<<"PACED X3 CLOUD MEASUREMENT: "<<kBlocks<<" callbacks; 48kHz/256"
          <<" deadline_ms="<<kDeadlineMs
          <<" mean_ms="<<sum/callbacks.size()
          <<" p95_ms="<<percentile(.95)
          <<" p99_ms="<<percentile(.99)
          <<" max_ms="<<callbacks.back()
          <<" callback_overruns="<<over
          <<" scheduler_late_blocks="<<wakeMisses
          <<" timer_resolution_requested="<<(timerPeriodStatus==TIMERR_NOERROR?1:0)
          <<" clock_p99_overhead_ms="<<timerP99
          <<" (NOT ASIO certification)\\n";
 if(timerPeriodStatus==TIMERR_NOERROR)timeEndPeriod(1);
 host.stop();
}
void exerciseX3(Module& module) {
 std::array<wchar_t,32768> directory{};
 const auto len=GetEnvironmentVariableW(L"ZPLANE_X3_BANK_DIR",directory.data(),
                                       static_cast<DWORD>(directory.size()));
 require(len>0&&len<directory.size(),"X3 original bank directory required");
 const auto root=std::filesystem::path(directory.data());
 const auto factory=emux::FactoryBank::from_table_file((root/"factory-bank.bin").string());
 const auto extended=emux::ExtendedBank::from_table_files(
  (root/"standard-bank.bin").string(),(root/"morph-support.bin").string());
 unsigned tested=0;
 for(unsigned rate:{44100u,48000u,96000u,192000u}){
  for(unsigned id=1;id<=55;++id){
   exerciseX3Mode<float>(module,id,rate,kSample32,factory,extended);
   exerciseX3Mode<double>(module,id,rate,kSample64,factory,extended);
   tested+=2;
  }
 }
 exerciseX3TimedFilterSwitch(module,factory,extended);
 exerciseX3LegacyFamilySwitch(module,factory,extended);
 std::cout<<"Native staged VST3 original-bank X3 modes: "<<tested
          <<" stereo 32/64-bit render cases, 55 filters * 4 rates PASS\n";
}
#endif
}
int main(int argc,char** argv){
 require(argc==2,"Expected packaged VST3 binary path");
 Module module{std::filesystem::path(argv[1])};
 exercise(module,kSample32);
 exercise(module,kSample64);
 exerciseDenseAutomation(module);
 exerciseAttachedEditor(module);
#ifdef ZPLANE_WITH_X3_RESEARCH
 exerciseX3(module);
 exercisePacedX3(module);
#endif
 std::cout<<"NATIVE HOST PASS: factory, stereo PCM, timed bypass, float/double, fresh recall, corrupt-state rejection\n";
 return 0;
}
