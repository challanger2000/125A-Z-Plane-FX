// Native Windows host probe against the exact staged .vst3 module.
// Tests real VST3 factory, 32/64-bit rendering, automation and project recall.
#include "FilterCore.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "base/source/fobject.h"
#include "base/source/fstreamer.h"
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
#include <atomic>
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
void exerciseConcurrentStateSnapshots(Module& module) {
 // Actual staged VST3 with a live audio thread repeatedly changing ALL
 // eleven parameters while an independent control thread saves project state.
 // Each save must be one COMPLETE vector (defaults/A/B), never a mixture.
 Instance host(module);
 host.setup(kSample32,48000.,64);
 constexpr std::array<double,11> a{{0.,.15,.25,.80,0.,0.,0.,0.,.5,.5,.5}};
 constexpr std::array<double,11> b{{1.,.85,.75,.20,1.,1./55.,1.,43./288.,.8,.3,.6}};
 constexpr std::array<double,11> defaultsLocal{{0.,.5,.5,1.,0.,0.,0.,0.,.5,.5,.5}};
 std::array<float,64> inL{},inR{},outL{},outR{};
 for(int i=0;i<64;++i){inL[static_cast<std::size_t>(i)]=i%7==0?.08f:.03f;}
 Sample32* input[]{inL.data(),inR.data()},*output[]{outL.data(),outR.data()};
 AudioBusBuffers in{},out{};in.numChannels=out.numChannels=2;
 in.channelBuffers32=input;out.channelBuffers32=output;
 ProcessData data{};
 data.numSamples=64;data.numInputs=data.numOutputs=1;
 data.inputs=&in;data.outputs=&out;
 data.symbolicSampleSize=kSample32;data.processMode=kRealtime;
 std::atomic<bool> ended{false};
 std::atomic<unsigned> renderErrors{0};
 std::thread worker([&] {
  for(unsigned block=0;block<5000;++block){
   ParameterChanges edits;
   const auto& values=(block%2)?a:b;
   for(ParamID id=0;id<11;++id){
    int32 slot=0,point=0;
    auto* q=edits.addParameterData(id,slot);
    if(!q||q->addPoint(0,values[id],point)!=kResultOk) {
     renderErrors.fetch_add(1);break;
    }
   }
   data.inputParameterChanges=&edits;
   if(host.audio->process(data)!=kResultOk){
    renderErrors.fetch_add(1);break;
   }
   if((block&7u)==0)std::this_thread::yield();
  }
  ended.store(true,std::memory_order_release);
 });
 unsigned reads=0,mixed=0,rejected=0;
 for(unsigned trial=0;trial<8000;++trial) {
  Steinberg::ResizableMemoryIBStream stream;
  if(host.component->getState(&stream)!=kResultOk){++rejected;continue;}
  stream.rewind();
  Steinberg::IBStreamer reader(&stream,kLittleEndian);
  Steinberg::uint32 magic=0,version=0;
  std::array<double,11> sample{};
  bool valid=reader.readInt32u(magic)&&reader.readInt32u(version)&&
    magic==0x3150585Au&&version==3;
  for(auto& value:sample)
   if(valid)valid=reader.readDouble(value);
  if(!valid||!(sample==a||sample==b||sample==defaultsLocal)){
   ++mixed;
   if(mixed<=2)std::cerr<<"Concurrent saved state was not atomic at read "<<trial<<"\\n";
  }
  ++reads;
  if(ended.load(std::memory_order_acquire)&&reads>=512)break;
 }
 worker.join();
 require(renderErrors.load()==0,"Concurrent audio process must not fail");
 require(rejected==0,"Concurrent getState must find a coherent state");
 require(reads>=512&&mixed==0,
         "Concurrent host save must never contain mixed parameter generations");
 host.stop();
 std::cout<<"NATIVE HOST PASS: concurrent 5000 real callback state edits and "
          <<reads<<" coherent eleven-parameter project saves\\n";
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
// Native GUI edit bridge: receives actual VSTGUI mouse gestures and
// forwards each edit into the packaged processor's real VST3 parameter
// queue (zero-sample control calls, no audio thread involved).
class NativeGUIEditBridge final : public FObject,public IComponentHandler {
 IAudioProcessor* processor_{};
public:
 std::array<unsigned,11> starts{},updates{},ends{};
 bool failed{false};
 explicit NativeGUIEditBridge(IAudioProcessor* p):processor_(p){}
 tresult PLUGIN_API beginEdit(ParamID id) override {
  if(id<starts.size())++starts[id];
  return kResultOk;
 }
 tresult PLUGIN_API performEdit(ParamID id,ParamValue value) override {
  if(id<updates.size())++updates[id];
  ParameterChanges changes;
  int32 slot=0,point=0;
  auto* q=changes.addParameterData(id,slot);
  if(!q||q->addPoint(0,value,point)!=kResultOk){
   failed=true;return kResultFalse;
  }
  ProcessData data{};
  data.numSamples=0;data.inputParameterChanges=&changes;
  data.symbolicSampleSize=kSample32;data.processMode=kRealtime;
  const auto result=processor_->process(data);
  if(result!=kResultOk)failed=true;
  return result;
 }
 tresult PLUGIN_API endEdit(ParamID id) override {
  if(id<ends.size())++ends[id];
  return kResultOk;
 }
 tresult PLUGIN_API restartComponent(int32) override {return kResultOk;}
 OBJ_METHODS(NativeGUIEditBridge,FObject)
 DEFINE_INTERFACES DEF_INTERFACE(IComponentHandler) END_DEFINE_INTERFACES(FObject)
 REFCOUNT_METHODS(FObject)
};
void nativeGuiPump() {
 MSG msg{};
 for(int pass=0;pass<4;++pass){
  while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){
   TranslateMessage(&msg);DispatchMessageW(&msg);
  }
  Sleep(5);
 }
}
HWND nativeGuiTarget(HWND host,int& x,int& y) {
 POINT local{x,y};
 HWND target=ChildWindowFromPointEx(host,local,CWP_SKIPINVISIBLE|CWP_SKIPDISABLED);
 if(target&&target!=host){
  ClientToScreen(host,&local);
  ScreenToClient(target,&local);
  x=local.x;y=local.y;
  return target;
 }
 return host;
}
void nativeGuiClick(HWND host,int x,int y,WPARAM modifier=0) {
 int hx=x,hy=y;
 HWND target=nativeGuiTarget(host,hx,hy);
 SetFocus(target);
 SendMessageW(target,WM_LBUTTONDOWN,MK_LBUTTON|modifier,MAKELPARAM(hx,hy));
 nativeGuiPump();
 SendMessageW(target,WM_LBUTTONUP,modifier,MAKELPARAM(hx,hy));
 nativeGuiPump();
}
void nativeGuiDrag(HWND host,int startX,int startY,int finalX,int finalY) {
 int hx=startX,hy=startY;
 HWND target=nativeGuiTarget(host,hx,hy);
 SetForegroundWindow(host);SetFocus(target);
 POINT start{startX,startY};
 ClientToScreen(host,&start);
 SetCursorPos(start.x,start.y);
 nativeGuiPump();
 SendMessageW(target,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(hx,hy));
 nativeGuiPump();
 POINT end{finalX,finalY};
 ClientToScreen(host,&end);
 SetCursorPos(end.x,end.y);
 nativeGuiPump();
 ScreenToClient(target,&end);
 SendMessageW(target,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(end.x,end.y));
 nativeGuiPump();
 SendMessageW(target,WM_LBUTTONUP,0,MAKELPARAM(end.x,end.y));
 nativeGuiPump();
}
void exerciseNativeGUIEdits(Module& module) {
 // Three sliders plus Bypass. Option-menu selection requires a separate
 // modal/menu harness and is intentionally NOT claimed as GUI PASS here.
 Instance inst(module);
 inst.setup(kSample32,48000.,512);
 auto* bridge=new NativeGUIEditBridge(inst.audio);
 require(inst.controller->setComponentHandler(bridge)==kResultOk,
         "Native GUI host component handler");
 HWND parent=CreateWindowExW(0,L"STATIC",L"Z-Plane GUI control QA",
        WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
        40,40,940,540,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
 require(parent!=nullptr,"Native GUI control window");
 ShowWindow(parent,SW_SHOWNA);UpdateWindow(parent);
 auto* view=inst.controller->createView(ViewType::kEditor);
 require(view!=nullptr,"Create real GUI control editor");
 require(view->attached(parent,kPlatformTypeHWND)==kResultOk,
         "GUI control editor attach");
 ViewRect rect{0,0,900,835};
 require(view->onSize(&rect)==kResultOk,"GUI control host onSize");
 nativeGuiPump();
 struct SliderPoint {ParamID id;int x;int y;int width;};
 const std::array<SliderPoint,3> sliders{{
  {1,34,278,390},{2,473,278,391},{3,34,369,390}
 }};
 for(const auto& slider:sliders){
  require(inst.controller->setParamNormalized(slider.id,.14)==kResultTrue,
          "Initialize native VSTGUI slider");
  nativeGuiPump();
  const auto before=inst.controller->getParamNormalized(slider.id);
  const auto count=bridge->updates[slider.id];
  nativeGuiDrag(parent,slider.x+slider.width/5,slider.y,
                      slider.x+slider.width*4/5,slider.y);
  const auto after=inst.controller->getParamNormalized(slider.id);
  if(!(std::isfinite(after)&&after>=0.&&after<=1.&&
       std::fabs(after-before)>.05&&bridge->updates[slider.id]>count)){
   std::cerr<<"Native GUI gesture FAIL id="<<slider.id
            <<" before="<<before<<" after="<<after
            <<" hostUpdates="<<bridge->updates[slider.id]-count<<"\\n";
   require(false,"Native GUI slider must forward actual pointer drag");
  }
  require(bridge->starts[slider.id]>0&&
          bridge->starts[slider.id]==bridge->ends[slider.id],
          "Native GUI drag beginEdit/endEdit balanced");
  Steinberg::ResizableMemoryIBStream state;
  require(inst.component->getState(&state)==kResultOk,
          "Native pointer edit must update processor save state");
  state.rewind();
  require(inst.controller->setComponentState(&state)==kResultOk,
          "Native GUI pointer edit must survive state roundtrip");
  require(std::fabs(inst.controller->getParamNormalized(slider.id)-after)<1.e-6,
          "Native GUI slider processor-state roundtrip");
  nativeGuiClick(parent,slider.x+slider.width/2,slider.y,MK_CONTROL);
  const auto reset=inst.controller->getParamNormalized(slider.id);
  require(std::fabs(reset-(slider.id==3?1.:.5))<1.e-6,
          "Native GUI Ctrl+left must reset actual parameter default");
 }
 require(inst.controller->setParamNormalized(4,0.)==kResultTrue,
         "Initialize native bypass checkbox OFF");
 nativeGuiPump();
 const auto before=bridge->updates[4];
 nativeGuiClick(parent,555,370);
 require(inst.controller->getParamNormalized(4)>.5&&bridge->updates[4]>before,
         "Native bypass checkbox must click ON and automate");
 nativeGuiClick(parent,555,370);
 require(inst.controller->getParamNormalized(4)<.5,
         "Native bypass checkbox must click OFF and automate");
 require(bridge->starts[4]>0&&bridge->starts[4]==bridge->ends[4],
         "Native bypass host gestures balanced");
 require(!bridge->failed,"Native GUI host forwarding must not reject edit");
 require(view->removed()==kResultOk,"GUI control editor detach");
 view->release();
 require(DestroyWindow(parent)!=0,"Destroy native GUI control parent");
 require(inst.controller->setComponentHandler(nullptr)==kResultOk,
         "Unbind native GUI host component handler");
 bridge->release();
 inst.stop();
 std::cout<<"NATIVE GUI HOST PASS: Morph/Q/Wet drags, Ctrl-default reset, "
          <<"Bypass on/off, host edit lifecycle and processor project recall\\n";
}

void nativeGuiKey(HWND canvas,WPARAM key) {
 // The platform's HWND keyboard event path, not a direct parameter write.
 SendMessageW(canvas,WM_KEYDOWN,key,1);
 SendMessageW(canvas,WM_KEYUP,key,0xC0000001L);
 nativeGuiPump();
}
void exerciseNativeGUIOptionMenus(Module& module) {
 // Real HWND keyboard focus -> VSTGUI COptionMenu -> Controller edit ->
 // IComponentHandler -> the loaded VST3 processor -> persisted state.
 // Select "Down" on each focusable control in native Tab traversal.
 // Unlike directly calling setParamNormalized(), this fails if the menu
 // is not reachable, has the wrong parameter binding, or sends no host edit.
 Instance inst(module);
 inst.setup(kSample32,48000.,512);
 auto* bridge=new NativeGUIEditBridge(inst.audio);
 require(inst.controller->setComponentHandler(bridge)==kResultOk,
         "Option-menu native host component handler");
 HWND parent=CreateWindowExW(0,L"STATIC",L"Z-Plane native option menu QA",
        WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
        20,20,940,540,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
 require(parent!=nullptr,"Option-menu native parent HWND");
 ShowWindow(parent,SW_SHOWNA);UpdateWindow(parent);
 auto* view=inst.controller->createView(ViewType::kEditor);
 require(view!=nullptr,"Option-menu native editor creation");
 require(view->attached(parent,kPlatformTypeHWND)==kResultOk,
         "Option-menu native editor attachment");
 ViewRect rect{0,0,900,835};
 require(view->onSize(&rect)==kResultOk,
         "Option-menu native editor 900x835 onSize");
 nativeGuiPump();
 HWND canvas=GetWindow(parent,GW_CHILD);
 require(canvas!=nullptr,"VSTGUI child HWND for option-menu key events");
 require(inst.controller->setParamNormalized(0,0.)==kResultTrue,
         "Seed Legacy filter selection at index 0");
 require(inst.controller->setParamNormalized(5,0.)==kResultTrue,
         "Seed X3 filter selection at Legacy index 0");
 nativeGuiPump();
 SetFocus(canvas);
 bool legacySelected=false,x3Selected=false;
 for(int tab=0;tab<20&&!(legacySelected&&x3Selected);++tab) {
  nativeGuiKey(canvas,VK_TAB);
  const auto legacyEdits=bridge->updates[0];
  const auto x3Edits=bridge->updates[5];
  nativeGuiKey(canvas,VK_DOWN);
  if(bridge->updates[0]>legacyEdits) {
   const auto actual=inst.controller->getParamNormalized(0);
   require(std::isfinite(actual)&&std::fabs(actual-1./3.)<1.e-6,
           "Native Legacy menu must select exact index 1 / 3");
   for(int index=2;index<=3;++index){
    const auto priorEdits=bridge->updates[0];
    nativeGuiKey(canvas,VK_DOWN);
    require(bridge->updates[0]>priorEdits,
            "Every Legacy menu index must generate a real host edit");
    require(std::fabs(inst.controller->getParamNormalized(0)-index/3.)<1.e-6,
            "Legacy option menu must expose all four valid selections");
   }
   nativeGuiKey(canvas,VK_DOWN);
   require(std::fabs(inst.controller->getParamNormalized(0)-1.)<1.e-6,
           "Legacy selector must not step past last of four modes");
   legacySelected=true;
  }
  if(bridge->updates[5]>x3Edits) {
   const auto actual=inst.controller->getParamNormalized(5);
   require(std::isfinite(actual)&&std::fabs(actual-1./55.)<1.e-6,
           "Native X3 menu must select exact X3 filter index 1 / 55");
   for(int index=2;index<=55;++index){
    const auto priorEdits=bridge->updates[5];
    nativeGuiKey(canvas,VK_DOWN);
    require(bridge->updates[5]>priorEdits,
            "Every X3 menu index must generate a real host edit");
    require(std::fabs(inst.controller->getParamNormalized(5)-index/55.)<1.e-6,
            "X3 COptionMenu must expose all 55 real filter modes");
   }
   nativeGuiKey(canvas,VK_DOWN);
   require(std::fabs(inst.controller->getParamNormalized(5)-1.)<1.e-6,
           "X3 menu must not advance beyond filter 55");
   x3Selected=true;
  }
 }
 if(!legacySelected||!x3Selected)
  std::cerr<<"Native option-menu keyboard FAIL: Legacy="<<legacySelected
           <<" X3="<<x3Selected
           <<" legacy edits="<<bridge->updates[0]
           <<" X3 edits="<<bridge->updates[5]<<"\\n";
 require(legacySelected&&x3Selected,
         "Tab focus + Down must operate both real VSTGUI COptionMenus");
 require(bridge->starts[0]==bridge->ends[0]&&bridge->starts[0]>0,
         "Legacy COptionMenu host begin/end edit balanced");
 require(bridge->starts[5]==bridge->ends[5]&&bridge->starts[5]>0,
         "X3 COptionMenu host begin/end edit balanced");
 Steinberg::ResizableMemoryIBStream state;
 require(inst.component->getState(&state)==kResultOk,
         "Both COptionMenu GUI edits must persist in processor state");
 const auto wantLegacy=inst.controller->getParamNormalized(0);
 const auto wantX3=inst.controller->getParamNormalized(5);
 state.rewind();
 require(inst.controller->setComponentState(&state)==kResultOk,
         "Both option menu edits must survive Controller recall");
 require(std::fabs(inst.controller->getParamNormalized(0)-wantLegacy)<1.e-10
      &&std::fabs(inst.controller->getParamNormalized(5)-wantX3)<1.e-10,
         "Both real native option menus must match processor saved state");
 require(!bridge->failed,"Option menu host bridge must not fail");
 require(view->removed()==kResultOk,"Remove native option-menu editor");
 view->release();
 require(DestroyWindow(parent)!=0,"Destroy native option-menu parent");
 require(inst.controller->setComponentHandler(nullptr)==kResultOk,
         "Unbind option-menu native host handler");
 bridge->release();
 inst.stop();
 std::cout<<"NATIVE GUI HOST PASS: both COptionMenus via HWND Tab/Down, "
          <<"host edits and persisted processor recall\\n";
}

void nativeGuiInjectKey(WORD code) {
 INPUT pair[2]{};
 pair[0].type=INPUT_KEYBOARD;pair[0].ki.wVk=code;
 pair[1].type=INPUT_KEYBOARD;pair[1].ki.wVk=code;
 pair[1].ki.dwFlags=KEYEVENTF_KEYUP;
 (void)SendInput(2,pair,sizeof(INPUT));
}
void nativeGuiMouseChooseNext(HWND parent,HWND canvas,int x,int y,
                              std::atomic<bool>& observedMenu) {
 // A real OS TrackPopupMenu is modal and blocks the plugin UI thread.
 // A bounded helper supplies native keyboard input from another thread;
 // no direct Controller setter or fabricated WM_COMMAND may be used.
 int localX=x,localY=y;
 HWND target=nativeGuiTarget(parent,localX,localY);
 SetForegroundWindow(parent);SetFocus(target);
 POINT desktop{x,y};ClientToScreen(parent,&desktop);
 SetCursorPos(desktop.x,desktop.y);
 nativeGuiPump();
 std::atomic<bool> done{false};
 std::thread keyDriver([&](){
  const DWORD threadID=GetWindowThreadProcessId(canvas,nullptr);
  for(int n=0;n<24&&!done.load();++n){
   GUITHREADINFO info{};info.cbSize=sizeof(info);
   if(GetGUIThreadInfo(threadID,&info)&&(info.flags&GUI_INMENUMODE)){
    observedMenu.store(true);break;
   }
   Sleep(25);
  }
  if(!done.load()){
   // The native popup initially has no selected row; its first
   // Down can focus entry zero even after Home. A second Down
   // moves to the intended first non-default option.
   Sleep(130);
   nativeGuiInjectKey(VK_HOME);
   Sleep(95);
   nativeGuiInjectKey(VK_DOWN);
   Sleep(95);
   nativeGuiInjectKey(VK_DOWN);
   Sleep(95);
   nativeGuiInjectKey(VK_RETURN);
   Sleep(300);
   if(!done.load()){
    // Never leave TrackPopupMenu blocking indefinitely on a CI desktop
    // which rejects SendInput. Abort safely and mark the test FAIL.
    nativeGuiInjectKey(VK_ESCAPE);
    PostMessageW(canvas,WM_CANCELMODE,0,0);
   }
  }
 });
 SendMessageW(target,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(localX,localY));
 SendMessageW(target,WM_LBUTTONUP,0,MAKELPARAM(localX,localY));
 nativeGuiPump();
 done.store(true);
 keyDriver.join();
}
void exerciseNativeGUIMousePopups(Module& module) {
 // Both real Win32 popup menus must allow a user-triggered choice that
 // reaches the live processor. The existing keyboard-only option test
 // intentionally cannot stand in for this.
 Instance inst(module);
 inst.setup(kSample32,48000.,512);
 auto* bridge=new NativeGUIEditBridge(inst.audio);
 require(inst.controller->setComponentHandler(bridge)==kResultOk,
         "Native menu popup host edit bridge");
 HWND parent=CreateWindowExW(0,L"STATIC",L"ZPlane popup QA",
        WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
        20,20,940,540,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
 require(parent!=nullptr,"Native popup host window");
 ShowWindow(parent,SW_SHOWNORMAL);UpdateWindow(parent);
 auto* view=inst.controller->createView(ViewType::kEditor);
 require(view!=nullptr,"Native popup editor creation");
 require(view->attached(parent,kPlatformTypeHWND)==kResultOk,
         "Native popup editor attach");
 ViewRect rect{0,0,900,835};
 require(view->onSize(&rect)==kResultOk,
         "Native popup editor host sizing");
 nativeGuiPump();
 HWND canvas=GetWindow(parent,GW_CHILD);
 require(canvas!=nullptr,"Native popup editor child HWND");
 struct Menu {ParamID id;int x;int y;double expected;};
 const std::array<Menu,2> menus{{{0,285,425,1./3.},
                                 {5,440,176,1./55.}}};
 for(const auto& item:menus){
  require(inst.controller->setParamNormalized(item.id,0.)==kResultTrue,
          "Initialize native popup menu to first selection");
  nativeGuiPump();
  std::atomic<bool> opened{false};
  const auto before=bridge->updates[item.id];
  nativeGuiMouseChooseNext(parent,canvas,item.x,item.y,opened);
  const auto selected=inst.controller->getParamNormalized(item.id);
  if(!opened.load()||bridge->updates[item.id]<=before||
     std::fabs(selected-item.expected)>1.e-6)
   std::cerr<<"Native popup FAIL id="<<item.id
            <<" modal menu observed="<<opened.load()
            <<" edits="<<bridge->updates[item.id]-before
            <<" selected="<<selected
            <<" expected="<<item.expected<<"\\n";
  require(opened.load(),"Native popup menu must open as a real OS menu");
  require(bridge->updates[item.id]>before &&
          std::fabs(selected-item.expected)<1.e-6,
          "Native mouse popup choice must reach exact VST3 parameter index");
  require(bridge->starts[item.id]==bridge->ends[item.id]&&
          bridge->starts[item.id]>0,
          "Native popup host Begin/End edits balanced");
 }
 Steinberg::ResizableMemoryIBStream saved;
 require(inst.component->getState(&saved)==kResultOk,
         "Native popup choices persist in processor state");
 saved.rewind();
 require(inst.controller->setComponentState(&saved)==kResultOk,
         "Native popup choices survive controller recall");
 require(std::fabs(inst.controller->getParamNormalized(0)-1./3.)<1.e-6&&
         std::fabs(inst.controller->getParamNormalized(5)-1./55.)<1.e-6,
         "Both native Win32 popup choices persist in real project state");
 require(!bridge->failed,"Native mouse popup host bridge must not fail");
 require(view->removed()==kResultOk,"Native popup view clean detach");
 view->release();
 require(DestroyWindow(parent)!=0,"Destroy native popup window");
 require(inst.controller->setComponentHandler(nullptr)==kResultOk,
         "Clear native popup bridge");
 bridge->release();inst.stop();
 std::cout<<"NATIVE GUI HOST PASS: real OS mouse popups, exact Legacy/X3 entries, "
          <<"live VST3 host edits and state recall\\n";
}
void exerciseFilterGraphNative(Module& module) {
 // Direct2D surfaces cannot be validated by GDI BitBlt on Windows Actions
 // (the capture returns unchanged white/black pixels even while VSTGUI
 // draw() updates the graph). Validate the actual custom-view creation,
 // draw callback, and exact curve samples that drawLine consumes instead.
 using QaFn=std::uint64_t (__cdecl*)(unsigned);
 auto* get= reinterpret_cast<QaFn>(GetProcAddress(module.dll,"ZPlaneResponseQaGet"));
 require(get!=nullptr,"Development-only response view paint QA API");
 Instance host(module);
 host.setup(kSample32,48000.,512);
 HWND parent=CreateWindowExW(0,L"STATIC",L"125A graphical DSP response QA",
         WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
         35,-330,950,930,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
 require(parent!=nullptr,"Create graph HWND");
 ShowWindow(parent,SW_SHOWNA);UpdateWindow(parent);
 const auto previousCreated=get(0);
 auto* view=host.controller->createView(ViewType::kEditor);
 require(view!=nullptr,"Create graph VST3 editor");
 require(view->attached(parent,kPlatformTypeHWND)==kResultOk,"Graph editor attach");
 ViewRect size{0,0,900,835};
 require(view->onSize(&size)==kResultOk,"Graph editor exact host size");
 HWND canvas=GetWindow(parent,GW_CHILD);
 require(canvas!=nullptr,"Graph real child canvas");
 const auto repaint=[canvas]{
  for(int pass=0;pass<8;++pass){
   nativeGuiPump();
   InvalidateRect(canvas,nullptr,FALSE);
   UpdateWindow(canvas);
  }
 };
 repaint();
 require(get(0)>previousCreated&&get(1)>0,"VSTGUI must construct and paint custom curve view");
 const auto oldDraw=get(1),legacy=get(3);
 require(host.controller->setParamNormalized(6,1.)==kResultTrue&&
         host.controller->setParamNormalized(7,46./288.)==kResultTrue,
         "Activate actual Morpheus response display");
 repaint();
 const auto morpheus=get(3);
 require(get(1)>oldDraw&&legacy!=morpheus,
         "VSTGUI draw() must receive different legacy and Morpheus curve data");
 require(host.controller->setParamNormalized(8,.08)==kResultTrue,
         "Automate Morpheus morph in graphical view");
 repaint();
 const auto morphed=get(3);
 require(morpheus!=morphed,"Morph must change actual VSTGUI plotted points");
 require(host.controller->setParamNormalized(4,1.)==kResultTrue,
         "Bypass for flat displayed frequency response");
 repaint();
 const auto bypass=get(3);
 require(morphed!=bypass,"Bypass must redraw the flat transfer curve");
#ifdef ZPLANE_WITH_X3_RESEARCH
 require(host.controller->setParamNormalized(4,0.)==kResultTrue&&
         host.controller->setParamNormalized(6,0.)==kResultTrue&&
         host.controller->setParamNormalized(5,1./55.)==kResultTrue,
         "Activate real X3 impulse measurement");
 repaint();
 const auto x3A=get(3);
 require(host.controller->setParamNormalized(5,24./55.)==kResultTrue,
         "Change X3 filter for its separately measured response");
 repaint();
 const auto x3B=get(3);
 require(x3A!=x3B,
         "Two X3 filters must produce different actual VSTGUI curve points");
#endif
 require(get(2)>=4,"VSTGUI view must read controller parameter changes");
 require(view->removed()==kResultOk,"Custom graph view detached without stale timer");
 view->release();
 require(DestroyWindow(parent)!=0,"Custom graph parent cleanup");
 host.stop();
 std::cout<<"FILTER RESPONSE NATIVE VSTGUI PASS: custom CView constructed and drawn, "
          <<"legacy/Morpheus/morph/bypass/X3 curve data changes in draw() callback\n";
}
void exerciseAttachedEditor(Module& module) {
 // The previous smoke only queried the 900x835 dimensions. That cannot
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
 ViewRect requested{0,0,900,835};
 require(view->onSize(&requested)==kResultOk,"Host initial editor onSize callback");
 ViewRect rect{};
 const auto sizeResult=view->getSize(&rect);
 std::cout<<"VSTGUI attached reported size: "<<rect.getWidth()<<" x "<<rect.getHeight()
          <<" result "<<sizeResult<<"\\n";
 require(sizeResult==kResultOk&&rect.getWidth()==900&&rect.getHeight()==835,
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
 require(saved.getCursor()==96,"Expected 8-byte header plus eleven v3 doubles");
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
 const emux::FactoryBank& factory,const emux::ExtendedBank& extended,
 float morph,float resonance) {
 Instance host(module);
 host.setup(format,static_cast<double>(rate));
 ParameterChanges changes;
 const std::array<std::pair<ParamID,ParamValue>,3> params{{
  {5,static_cast<double>(id)/55.},
  {1,static_cast<double>(morph)},
  {2,static_cast<double>(resonance)}
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
 const auto cfg=referenceFor(id,morph,resonance,rate,factory,extended);
 emux::Filter wantedL(cfg.coefficients,cfg.sections);
 emux::Filter wantedR(cfg.coefficients,cfg.sections);
 for(int i=0;i<128;++i){
  const auto left=static_cast<Sample>(wantedL.process_sample(static_cast<float>(il[i])));
  const auto right=static_cast<Sample>(wantedR.process_sample(static_cast<float>(ir[i])));
  if(ol[i]!=left||orr[i]!=right){
   std::cerr<<"X3 PCM mismatch id="<<id<<" rate="<<rate<<" bits="<<(format==kSample32?32:64)
            <<" morph="<<morph<<" Q="<<resonance
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
  // Once the 240-sample family fade has completed at frame 367,
  // X3 owns the DSP output exclusively. The dormant Legacy filter
  // MUST NOT accumulate hidden state until its re-entry at frame 512.
  if(n<kToX3+kFade||n>=kToLegacy)
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
void exerciseX3RapidFamilyRetarget(Module& module,
 const emux::FactoryBank& factory,const emux::ExtendedBank& extended) {
 // Rapid reversals of a 240-frame family fade, with independent per-sample
 // DSP/envelope oracle and variable host block partition boundaries.
 constexpr int kFrames=1024,kFade=240;
 const std::array<std::pair<int,double>,7> edits{{
  {128,1./55.},{190,0.},{224,1./55.},{260,0.},
  {315,1./55.},{600,0.},{700,1./55.}
 }};
 std::array<float,kFrames> inL{},inR{},wholeL{},wholeR{},partL{},partR{};
 for(int n=0;n<kFrames;++n){
  inL[static_cast<std::size_t>(n)]=static_cast<float>(.02*std::sin(n*.061)+.007*std::cos(n*.017));
  inR[static_cast<std::size_t>(n)]=static_cast<float>(.014*std::cos(n*.083)-.005*std::sin(n*.029));
 }
 auto render=[&](Instance& host,int base,int end,
                 std::array<float,kFrames>& outL,std::array<float,kFrames>& outR){
  ParameterChanges automation;
  int32 slot=0;
  auto* queue=automation.addParameterData(5,slot);
  require(queue!=nullptr,"Rapid family filter queue");
  for(const auto& [sample,value]:edits)
   if(sample>=base&&sample<end){
    int32 idx=0;
    require(queue->addPoint(sample-base,value,idx)==kResultOk,
            "Rapid family sample-offset host automation");
   }
  Sample32* in[]{inL.data()+base,inR.data()+base};
  Sample32* out[]{outL.data()+base,outR.data()+base};
  AudioBusBuffers input{},output{};
  input.numChannels=output.numChannels=2;
  input.channelBuffers32=in;output.channelBuffers32=out;
  ProcessData data{};
  data.numSamples=end-base;data.numInputs=data.numOutputs=1;
  data.inputs=&input;data.outputs=&output;
  data.symbolicSampleSize=kSample32;data.processMode=kRealtime;
  data.inputParameterChanges=&automation;
  require(host.audio->process(data)==kResultOk,"Rapid family process callback");
 };
 Instance whole(module),partitioned(module);
 whole.setup(kSample32,48000.,kFrames);
 partitioned.setup(kSample32,48000.,kFrames);
 render(whole,0,kFrames,wholeL,wholeR);
 constexpr std::array<int,10> boundaries{{0,137,196,237,319,499,600,711,833,1024}};
 for(std::size_t b=1;b<boundaries.size();++b)
  render(partitioned,boundaries[b-1],boundaries[b],partL,partR);
 zplane::Settings settings{};
 settings.frequency=20.*std::pow(1000.,.5);
 settings.quality=.2*std::pow(100.,.5);
 zplane::StereoFilter legacy;
 legacy.configure(48000.,settings);
 const auto cfg=referenceFor(1,.5f,.5f,48000,factory,extended);
 emux::Filter researchL(cfg.coefficients,cfg.sections),
              researchR(cfg.coefficients,cfg.sections);
 double blend=0.,target=0.,increment=0.;
 unsigned fadeLeft=0,significant=0;
 bool researchInitialized=false;
 std::size_t nextEdit=0;
 for(int n=0;n<kFrames;++n){
  if(nextEdit<edits.size()&&edits[nextEdit].first==n){
   target=edits[nextEdit].second>0.?1.:0.;
   if(target==1.)researchInitialized=true;
   fadeLeft=kFade;
   increment=(target-blend)/double(kFade);
   ++nextEdit;
  }
  const auto index=static_cast<std::size_t>(n);
  float a=0.f,b=0.f,c=0.f,d=0.f;
  const bool activeTransition=fadeLeft>0;
  // Exactly one owner engine advances at an endpoint. Both advance
  // exclusively while an in-flight transition samples them.
  if(activeTransition||blend<=0.)
   legacy.process(&inL[index],&inR[index],&a,&b,1);
  if(researchInitialized&&(activeTransition||blend>0.)){
   c=researchL.process_sample(inL[index]);
   d=researchR.process_sample(inR[index]);
  }
  if(fadeLeft){
   blend+=increment;
   if(--fadeLeft==0)blend=target;
  }
  const float predictedL=static_cast<float>(double(a)+(double(c)-double(a))*blend);
  const float predictedR=static_cast<float>(double(b)+(double(d)-double(b))*blend);
  if(std::fabs(wholeL[index]-predictedL)>1.e-6f||
     std::fabs(wholeR[index]-predictedR)>1.e-6f){
   std::cerr<<"Rapid family independent PCM mismatch at sample "<<n
            <<" actual "<<wholeL[index]<<" expected "<<predictedL<<"\\n";
   require(false,"Rapid retarget state must match independent DSP/envelope oracle");
  }
  if(wholeL[index]!=partL[index]||wholeR[index]!=partR[index]){
   std::cerr<<"Rapid family host partition mismatch at sample "<<n
            <<" whole "<<wholeL[index]<<" split "<<partL[index]<<"\\n";
   require(false,"Rapid family retarget is host block-partition invariant");
  }
  if(activeTransition&&std::fabs(a-c)>1.e-5f)++significant;
 }
 require(significant>100,"Rapid family oracle must distinguish real DSP paths");
 partitioned.stop();whole.stop();
 std::cout<<"NATIVE HOST PASS: seven rapid family edits, independent PCM and block partition\\n";
}
template<typename Sample>
void exerciseX3FamilyWetBypass(Module& module,int32 format,unsigned rate) {
 // A metamorphic native-EXE oracle for 32-/64-bit cross-family PCM under
 // simultaneous X3↔Legacy, Wet and Bypass automation at four host rates.
 constexpr int kFrames=5120;
 struct Edit {ParamID id;int offset;double value;};
 constexpr std::array<Edit,7> edits{{
  {3,0,0.},{5,512,1./55.},{3,1280,1.},{5,2500,0.},
  {3,3000,0.},{4,3150,1.},{5,4000,1./55.}
 }};
 std::array<Sample,kFrames> inL{},inR{},wholeL{},wholeR{},splitL{},splitR{};
 for(int n=0;n<kFrames;++n){
  const auto i=static_cast<std::size_t>(n);
  inL[i]=static_cast<Sample>(.15*std::sin(n*.057)+.029*std::cos(n*.017)+1.e-12);
  inR[i]=static_cast<Sample>(.11*std::cos(n*.073)-.018*std::sin(n*.031)-2.e-12);
 }
 auto render=[&](Instance& host,int begin,int end,
                 std::array<Sample,kFrames>& left,
                 std::array<Sample,kFrames>& right){
  ParameterChanges changes;
  for(const auto id:std::array<ParamID,3>{{3,4,5}}){
   int32 slot=0;
   auto* q=changes.addParameterData(id,slot);
   require(q!=nullptr,"Cross-family multi-rate parameter queue");
   for(const auto& edit:edits) {
    if(edit.id==id&&edit.offset>=begin&&edit.offset<end){
     int32 point=0;
     require(q->addPoint(edit.offset-begin,edit.value,point)==kResultOk,
             "Cross-family multi-rate valid sample-offset edit");
    }
   }
  }
  Sample* source[]{inL.data()+begin,inR.data()+begin};
  Sample* target[]{left.data()+begin,right.data()+begin};
  AudioBusBuffers input{},output{};
  input.numChannels=output.numChannels=2;
  if(format==kSample32){
   input.channelBuffers32=reinterpret_cast<Sample32**>(source);
   output.channelBuffers32=reinterpret_cast<Sample32**>(target);
  }else{
   input.channelBuffers64=reinterpret_cast<Sample64**>(source);
   output.channelBuffers64=reinterpret_cast<Sample64**>(target);
  }
  ProcessData data{};
  data.numSamples=end-begin;data.numInputs=data.numOutputs=1;
  data.inputs=&input;data.outputs=&output;data.symbolicSampleSize=format;
  data.processMode=kRealtime;data.inputParameterChanges=&changes;
  require(host.audio->process(data)==kResultOk,
          "Cross-family multi-rate hosted process");
 };
 Instance whole(module),partitioned(module);
 whole.setup(format,rate,kFrames);partitioned.setup(format,rate,kFrames);
 render(whole,0,kFrames,wholeL,wholeR);
 constexpr std::array<int,16> boundaries{{
  0,1,117,501,513,801,1279,1500,2297,2501,
  2999,3151,3479,4001,4711,kFrames
 }};
 for(std::size_t b=1;b<boundaries.size();++b)
  render(partitioned,boundaries[b-1],boundaries[b],splitL,splitR);
 unsigned activeSamples=0;
 for(int n=0;n<kFrames;++n){
  const auto i=static_cast<std::size_t>(n);
  if(!std::isfinite(static_cast<double>(wholeL[i]))||
     !std::isfinite(static_cast<double>(wholeR[i])))
   require(false,"Cross-family Wet/Bypass PCM must remain finite");
  if(wholeL[i]!=splitL[i]||wholeR[i]!=splitR[i]){
   std::cerr<<"Multirate family split mismatch sample="<<n
            <<" rate="<<rate<<" bits="<<(format==kSample32?32:64)
            <<" left whole="<<wholeL[i]<<" split="<<splitL[i]<<"\\n";
   require(false,"All filter/Wet/Bypass events are block-partition invariant");
  }
  // After the initial 5-ms Wet=0 ramp at the highest supported rate,
  // and before Wet rises at sample 1280, both active engines must be dry.
  // From sample 5000 all family and bypass ramps have ended.
  if((n>=1100&&n<1280)||n>=5000)
   require(wholeL[i]==inL[i]&&wholeR[i]==inR[i],
           "Cross-family Dry/Wet/Bypass must preserve exact original PCM");
  if(n>=2050&&n<2300&&
      (std::fabs(static_cast<double>(wholeL[i])-static_cast<double>(inL[i]))>1.e-5||
       std::fabs(static_cast<double>(wholeR[i])-static_cast<double>(inR[i]))>1.e-5))
   ++activeSamples;
 }
 require(activeSamples>100,
         "Positive control: cross-family host audio must react audibly to Wet=1");
 partitioned.stop();whole.stop();
 std::cout<<"NATIVE HOST PASS: family Wet/Bypass interleaving "
          <<(format==kSample32?32:64)<<"-bit "<<rate<<" Hz, "
          <<"exact dry PCM and irregular host block partitions\\n";
}
void exerciseX3TimedMorphResonance(Module& module) {
 // In-host automation must not depend on the host's arbitrary callback
 // partitioning. A third, unautomated real instance is the negative
 // signal control: changing Morph/Q must produce distinguishable PCM.
 constexpr int kFrames=4096;
 struct Edit {ParamID param;int at;double value;};
 constexpr std::array<Edit,14> edits{{
  {5,0,1./55.},{1,0,.35},{2,0,.65},
  {1,127,.95},{2,210,.05},{1,257,.15},{2,411,1.},
  {1,505,0.},{2,777,.5},{1,1023,1.},{2,1609,0.},
  {1,1951,.41},{2,2507,.91},{1,3501,.6}
 }};
 std::array<float,kFrames> inL{},inR{},wholeL{},wholeR{},splitL{},splitR{},fixedL{},fixedR{};
 for(int n=0;n<kFrames;++n){
  const auto i=static_cast<std::size_t>(n);
  inL[i]=static_cast<float>(.031*std::sin(n*.11)+.011*std::cos(n*.032)+(n%257==0?.1:0.));
  inR[i]=static_cast<float>(.027*std::cos(n*.091)-.014*std::sin(n*.041)-(n%193==0?.075:0.));
 }
 auto render=[&](Instance& instance,int begin,int end,
                 std::array<float,kFrames>& outL,
                 std::array<float,kFrames>& outR,bool automated){
  ParameterChanges changes;
  for(ParamID id:std::array<ParamID,3>{{5,1,2}}){
   int32 slot=0;
   auto* q=changes.addParameterData(id,slot);
   require(q!=nullptr,"X3 Morph/Q host parameter queue");
   for(const auto& edit:edits) {
    if(edit.param!=id||edit.at<begin||edit.at>=end||
       (!automated&&edit.at!=0))continue;
    int32 point=0;
    require(q->addPoint(edit.at-begin,edit.value,point)==kResultOk,
            "X3 Morph/Q sample-offset automation point");
   }
  }
  Sample32* input[]{inL.data()+begin,inR.data()+begin};
  Sample32* output[]{outL.data()+begin,outR.data()+begin};
  AudioBusBuffers in{},out{};
  in.numChannels=out.numChannels=2;
  in.channelBuffers32=input;out.channelBuffers32=output;
  ProcessData data{};
  data.numSamples=end-begin;data.numInputs=data.numOutputs=1;
  data.inputs=&in;data.outputs=&out;data.symbolicSampleSize=kSample32;
  data.processMode=kRealtime;data.inputParameterChanges=&changes;
  require(instance.audio->process(data)==kResultOk,
          "X3 Morph/Q native hosted processing");
 };
 Instance whole(module),partitioned(module),fixed(module);
 whole.setup(kSample32,48000.,kFrames);
 partitioned.setup(kSample32,48000.,kFrames);
 fixed.setup(kSample32,48000.,kFrames);
 render(whole,0,kFrames,wholeL,wholeR,true);
 constexpr std::array<int,17> bounds{{
  0,1,64,129,211,258,410,501,508,777,1026,1608,1953,2505,3503,3987,4096
 }};
 for(std::size_t b=1;b<bounds.size();++b)
  render(partitioned,bounds[b-1],bounds[b],splitL,splitR,true);
 render(fixed,0,kFrames,fixedL,fixedR,false);
 unsigned affected=0;
 for(int n=0;n<kFrames;++n){
  const auto i=static_cast<std::size_t>(n);
  require(std::isfinite(wholeL[i])&&std::isfinite(wholeR[i]),
          "X3 Morph/Q output must remain finite");
  if(wholeL[i]!=splitL[i]||wholeR[i]!=splitR[i]){
   std::cerr<<"X3 Morph/Q host-block mismatch sample "<<n
            <<" whole="<<wholeL[i]<<" split="<<splitL[i]<<"\\n";
   require(false,"Morph/Q audio must not depend on host block partitions");
  }
  if(n>400&&(std::fabs(wholeL[i]-fixedL[i])>1.e-5f||
             std::fabs(wholeR[i]-fixedR[i])>1.e-5f))++affected;
 }
 require(affected>100,
         "Negative control: ignored X3 Morph/Q automation must differ in actual PCM");
 Steinberg::ResizableMemoryIBStream state;
 require(whole.component->getState(&state)==kResultOk,
         "X3 Morph/Q post-automation state must save");
 state.rewind();
 require(whole.controller->setComponentState(&state)==kResultOk,
         "X3 Morph/Q state must recall");
 require(std::fabs(whole.controller->getParamNormalized(1)-.6)<1.e-12&&
         std::fabs(whole.controller->getParamNormalized(2)-.91)<1.e-12&&
         std::fabs(whole.controller->getParamNormalized(5)-1./55.)<1.e-12,
         "X3 Morph/Q last host events must survive state recall");
 fixed.stop();partitioned.stop();whole.stop();
 std::cout<<"NATIVE HOST PASS: dynamic X3 Morph/Q automation, irregular blocks, "
          <<"finite stereo PCM and frozen-parameter negative control\\n";
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
template<typename Sample>
void testEmbeddedModes(Module& module,unsigned rate,int32 precision) {
 // This test deliberately DOES NOT consult the original research files.
 // The exact staged VST3 must load its own bank resources from its DLL,
 // otherwise processor initialization fails, or wet audio stays dry.
 Instance host(module);
 host.setup(precision,static_cast<double>(rate),2048);
 constexpr unsigned frames=2048;
 std::array<Sample,frames> il{},ir{},ol{},orr{};
 for(unsigned i=0;i<frames;++i){
  const double phase=static_cast<double>(i)/static_cast<double>(rate);
  il[i]=static_cast<Sample>(0.08*std::sin(2.*3.141592653589793*317.*phase)+
                            (i%101==0?0.05:0.0));
  ir[i]=static_cast<Sample>(0.07*std::cos(2.*3.141592653589793*727.*phase)-
                            (i%79==0?0.04:0.0));
 }
 Sample* input[]{il.data(),ir.data()};
 Sample* output[]{ol.data(),orr.data()};
 AudioBusBuffers in{},out{};
 in.numChannels=out.numChannels=2;
 if(precision==kSample32){
  in.channelBuffers32=reinterpret_cast<Sample32**>(input);
  out.channelBuffers32=reinterpret_cast<Sample32**>(output);
 }else{
  in.channelBuffers64=reinterpret_cast<Sample64**>(input);
  out.channelBuffers64=reinterpret_cast<Sample64**>(output);
 }
 ProcessData data{};
 data.numSamples=frames;data.numInputs=data.numOutputs=1;
 data.inputs=&in;data.outputs=&out;
 data.symbolicSampleSize=precision;data.processMode=kRealtime;
 for(unsigned id=1;id<=55;++id){
  ParameterChanges changes;
  int32 queue=0,index=0;
  auto* choice=changes.addParameterData(5,queue);
  require(choice&&choice->addPoint(0,static_cast<double>(id)/55.,index)==kResultOk,
          "Embedded X3 selector automation");
  data.inputParameterChanges=&changes;
  require(host.audio->process(data)==kResultOk,
          "Embedded VST3 X3 mode render without external banks");
  double changed=0.,maxAbs=0.;
  for(unsigned i=frames/2;i<frames;++i){
   const auto vl=static_cast<double>(ol[i]),vr=static_cast<double>(orr[i]);
   require(std::isfinite(vl)&&std::isfinite(vr),
           "Embedded X3 output finite");
   changed+=std::abs(vl-static_cast<double>(il[i]))+
            std::abs(vr-static_cast<double>(ir[i]));
   maxAbs=std::max({maxAbs,std::abs(vl),std::abs(vr)});
  }
  if(changed<=0.00001||maxAbs<=0.0000001){
   std::cerr<<"Embedded standalone X3 mode not audible: filter="<<id
            <<" rate="<<rate<<" bits="<<(precision==kSample32?32:64)
            <<" delta="<<changed<<" max="<<maxAbs<<"\n";
   require(false,"Embedded 55 X3 filters must not silently return dry/silent");
  }
 }
 host.stop();
}
void exerciseSelfContainedX3(Module& module) {
 for(unsigned rate:{44100u,48000u,96000u,192000u}){
  testEmbeddedModes<float>(module,rate,kSample32);
  testEmbeddedModes<double>(module,rate,kSample64);
 }
 std::cout<<"EMBEDDED X3 STANDALONE PASS: 55 modes * 4 rates * 2 sample precisions = "
          <<"440 wet non-dry real VST3 renders with no user bank files\n";
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
 // Each X3 filter is exercised at both Morph/Q endpoints, their
 // crossed extremes, midpoint, and the historical .35/.65 reference.
 // A fixed single-point oracle had left these parameter boundaries open.
 constexpr std::array<std::pair<float,float>,6> kControlGrid{{
  {0.f,0.f},{0.f,1.f},{1.f,0.f},{1.f,1.f},
  {.5f,.5f},{.35f,.65f}
 }};
 unsigned tested=0;
 for(unsigned rate:{44100u,48000u,96000u,192000u}){
  for(unsigned id=1;id<=55;++id) {
   for(const auto& [morph,resonance]:kControlGrid){
    exerciseX3Mode<float>(module,id,rate,kSample32,factory,extended,morph,resonance);
    exerciseX3Mode<double>(module,id,rate,kSample64,factory,extended,morph,resonance);
    tested+=2;
   }
  }
 }
 exerciseX3TimedFilterSwitch(module,factory,extended);
 exerciseX3LegacyFamilySwitch(module,factory,extended);
 exerciseX3RapidFamilyRetarget(module,factory,extended);
 exerciseX3TimedMorphResonance(module);
 for(unsigned rate:{44100u,48000u,96000u,192000u}){
  exerciseX3FamilyWetBypass<float>(module,kSample32,rate);
  exerciseX3FamilyWetBypass<double>(module,kSample64,rate);
 }
 std::cout<<"Native staged VST3 original-bank X3 modes: "<<tested
          <<" stereo 32/64-bit PCM renders, 55 filters * 4 rates * 6 Morph/Q pairs PASS\n";
}
#endif
}
void exerciseMorpheusCubes(Module& module) {
 for(int precision:{kSample32,kSample64}){
  Instance host(module);
  host.setup(precision,48000.,1024);
  std::array<float,1024> fl{},fr{},fol{},forr{};
  std::array<double,1024> dl{},dr{},dol{},dorr{};
  for(int i=0;i<1024;++i){
   const auto l=.07*std::sin(i*.053)+.023*std::cos(i*.17);
   const auto r=.06*std::cos(i*.041)-.017*std::sin(i*.11);
   fl[i]=static_cast<float>(l);fr[i]=static_cast<float>(r);dl[i]=l;dr[i]=r;
  }
  Sample32* fIn[]{fl.data(),fr.data()},*fOut[]{fol.data(),forr.data()};
  Sample64* dIn[]{dl.data(),dr.data()},*dOut[]{dol.data(),dorr.data()};
  AudioBusBuffers input{},output{};
  input.numChannels=output.numChannels=2;
  if(precision==kSample32){input.channelBuffers32=fIn;output.channelBuffers32=fOut;}
  else{input.channelBuffers64=dIn;output.channelBuffers64=dOut;}
  ProcessData data{};
  data.numSamples=1024;data.numInputs=data.numOutputs=1;
  data.inputs=&input;data.outputs=&output;
  data.symbolicSampleSize=precision;data.processMode=kRealtime;
  unsigned finite=0,audible=0;
  for(int cube=0;cube<289;++cube){
   ParameterChanges edits;
   const std::array<ParamID,5> ids{{6,7,8,9,10}};
   const std::array<double,5> values{{1.,cube/288.,.43,.61,.28}};
   for(std::size_t n=0;n<ids.size();++n){
    int32 index=0,point=0;
    auto* q=edits.addParameterData(ids[n],index);
    require(q&&q->addPoint(0,values[n],point)==kResultOk,
            "Morpheus cube parameter queue");
   }
   data.inputParameterChanges=&edits;
   require(host.audio->process(data)==kResultOk,
           "Host must render every embedded Morpheus cube");
   double difference=0.;
   for(int n=512;n<1024;++n){
    const auto l=precision==kSample32?static_cast<double>(fol[n]):dol[n];
    const auto r=precision==kSample32?static_cast<double>(forr[n]):dorr[n];
    require(std::isfinite(l)&&std::isfinite(r),
            "All Morpheus cube renders finite");
    const auto sourceL=precision==kSample32?static_cast<double>(fl[n]):dl[n];
    const auto sourceR=precision==kSample32?static_cast<double>(fr[n]):dr[n];
    difference+=std::abs(l-sourceL)+std::abs(r-sourceR);
   }
   ++finite;if(cube>0&&difference>0.0001)++audible;
  }
  require(finite==289&&audible>200,
          "At least 200 distinct non-null cube modes must produce audible wet DSP");
  host.stop();
 }
 std::cout<<"MORPHEUS NATIVE HOST PASS: all 289 cubes rendered in float/double, "
          <<"over 200 non-null wet modes audibly different from dry\n";
}
int main(int argc,char** argv){
 require(argc==2||(argc==3&&std::strcmp(argv[2],"--embedded-smoke")==0),
         "Expected packaged VST3 binary path with optional --embedded-smoke");
 Module module{std::filesystem::path(argv[1])};
#ifdef ZPLANE_WITH_X3_RESEARCH
 if(argc==3){
  exerciseSelfContainedX3(module);
  return 0;
 }
#else
 require(argc==2,"Embedded X3 test requires research engine");
#endif
 exercise(module,kSample32);
 exercise(module,kSample64);
 exerciseDenseAutomation(module);
 exerciseConcurrentStateSnapshots(module);
 exerciseMorpheusCubes(module);
 exerciseAttachedEditor(module);
 exerciseFilterGraphNative(module);
 exerciseNativeGUIEdits(module);
 exerciseNativeGUIOptionMenus(module);
 exerciseNativeGUIMousePopups(module);
#ifdef ZPLANE_WITH_X3_RESEARCH
 exerciseX3(module);
 exercisePacedX3(module);
#endif
 std::cout<<"NATIVE HOST PASS: factory, stereo PCM, timed bypass, float/double, fresh recall, corrupt-state rejection\n";
 return 0;
}
