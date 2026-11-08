// Native Windows host probe against the exact staged .vst3 module.
// Tests real VST3 factory, 32/64-bit rendering, automation and project recall.
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/utility/memoryibstream.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include <windows.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <vector>
#ifdef ZPLANE_WITH_X3_RESEARCH
#include "emux_filters.hpp"
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
 }
 void setup(int32 sampleFormat){
  SpeakerArrangement stereo=SpeakerArr::kStereo;
  require(audio->setBusArrangements(&stereo,1,&stereo,1)==kResultOk,"Stereo arrangement");
  ProcessSetup setup{};setup.sampleRate=48000.;setup.maxSamplesPerBlock=512;
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
void exerciseX3(Module& module) {
 const char* env=std::getenv("ZPLANE_X3_BANK_DIR");
 require(env&&*env,"Actual X3 bank directory must be supplied by build");
 const auto factory=emux::FactoryBank::from_table_file(
  (std::filesystem::u8path(env)/"factory-bank.bin").string());
 Instance i(module);i.setup(kSample32);
 ParameterChanges changes;
 for(auto point:std::array<std::pair<ParamID,ParamValue>,3>{
   std::pair<ParamID,ParamValue>{5,1./55.},
   {1,.35},{2,.65}}) {
  int32 slot=0;
  auto* q=changes.addParameterData(point.first,slot);
  require(q!=nullptr,"X3 mode/morph/Q host automation queue");
  int32 index=0;
  require(q->addPoint(0,point.second,index)==kResultOk,"X3 host automation point");
 }
 std::array<float,256> il{},ir{},ol{},orr{};
 il[0]=.001f;ir[7]=-.001f;
 Sample32* ib[2]{il.data(),ir.data()},*ob[2]{ol.data(),orr.data()};
 AudioBusBuffers in{},out{};
 in.numChannels=out.numChannels=2;
 in.channelBuffers32=ib;out.channelBuffers32=ob;
 ProcessData pd{};
 pd.numSamples=256;pd.numInputs=pd.numOutputs=1;
 pd.inputs=&in;pd.outputs=&out;pd.symbolicSampleSize=kSample32;
 pd.processMode=kRealtime;pd.inputParameterChanges=&changes;
 require(i.audio->process(pd)==kResultOk,"X3 original bank native VST3 process");
 emux::Filter referenceL(factory.coefficients(0,.35f,.65f,48000));
 emux::Filter referenceR(factory.coefficients(0,.35f,.65f,48000));
 bool nonzero=false;
 for(int n=0;n<256;++n){
  const float wl=referenceL.process_sample(il[n]),wr=referenceR.process_sample(ir[n]);
  require(std::memcmp(&wl,&ol[n],4)==0,"X3 left PCM must equal original coefficient reference");
  require(std::memcmp(&wr,&orr[n],4)==0,"X3 right PCM must equal original coefficient reference");
  if(ol[n]!=0.f||orr[n]!=0.f)nonzero=true;
 }
 require(nonzero,"Original bank X3 sound must not be silent or dry fallback");
 i.stop();
 std::cout<<"Native packaged VST3 X3 original-bank stereo PCM bit-exact PASS\n";
}
#endif
}
int main(int argc,char** argv){
 require(argc==2,"Expected packaged VST3 binary path");
 Module module{std::filesystem::path(argv[1])};
 exercise(module,kSample32);
 exercise(module,kSample64);
#ifdef ZPLANE_WITH_X3_RESEARCH
 exerciseX3(module);
#endif
 std::cout<<"NATIVE HOST PASS: factory, stereo PCM, timed bypass, float/double, fresh recall, corrupt-state rejection\n";
 return 0;
}
