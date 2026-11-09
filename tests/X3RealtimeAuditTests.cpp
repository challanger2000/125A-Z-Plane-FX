#include "X3ResearchAdapter.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <malloc.h>
#include <new>
#include <limits>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// Instrument C++ new/new[] in this translation unit and all inline
// research adapter code compiled into it. Direct malloc/HeapAlloc in DLLs
// is OUTSIDE this probe and must never be claimed as verified.
namespace {
thread_local bool gAudioThreadGuard = false;
std::atomic<unsigned long long> gAudioNewCalls{0};
struct AudioGuard final {
 AudioGuard() noexcept {gAudioThreadGuard=true;}
 ~AudioGuard() noexcept {gAudioThreadGuard=false;}
 AudioGuard(const AudioGuard&)=delete;
 AudioGuard& operator=(const AudioGuard&)=delete;
};
void countAllocation() noexcept {
 if(gAudioThreadGuard)gAudioNewCalls.fetch_add(1,std::memory_order_relaxed);
}
}
void* operator new(std::size_t bytes) {
 countAllocation();
 if(void* p=std::malloc(bytes?bytes:1))return p;
 throw std::bad_alloc();
}
void* operator new[](std::size_t bytes) {return ::operator new(bytes);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
void* operator new(std::size_t bytes,std::align_val_t a) {
 countAllocation();
 if(void* p=_aligned_malloc(bytes?bytes:1,static_cast<std::size_t>(a)))return p;
 throw std::bad_alloc();
}
void* operator new[](std::size_t bytes,std::align_val_t a) {
 return ::operator new(bytes,a);
}
void operator delete(void* p,std::align_val_t) noexcept {_aligned_free(p);}
void operator delete[](void* p,std::align_val_t) noexcept {_aligned_free(p);}
void operator delete(void* p,std::size_t,std::align_val_t) noexcept {_aligned_free(p);}
void operator delete[](void* p,std::size_t,std::align_val_t) noexcept {_aligned_free(p);}

int main() {
 std::array<wchar_t,32768> env{};
 const auto got=GetEnvironmentVariableW(L"ZPLANE_X3_BANK_DIR",env.data(),static_cast<DWORD>(env.size()));
 if(got==0||got>=env.size()) {
  std::fputs("X3 RT FAIL: real research bank fixtures missing\n",stderr);
  return 1;
 }
 zplane::X3ResearchAdapter x3;
 std::string reason;
 if(!x3.load(std::filesystem::path(env.data()),reason)||!x3.hasBanks()) {
  std::fprintf(stderr,"X3 RT FAIL: off-thread real bank load: %s\n",reason.c_str());
  return 1;
 }
 constexpr unsigned frames=1024;
 std::array<float,frames> inputL{},inputR{},resultL{},resultR{};
 std::array<double,frames> input64L{},input64R{},result64L{},result64R{};
 for(std::size_t i=0;i<inputL.size();++i){
  const auto left=(i%101==0?0.001:0.)+0.0001*std::sin(static_cast<double>(i)*0.019);
  const auto right=(i%73==0?-0.0008:0.)+0.0001*std::cos(static_cast<double>(i)*0.023);
  inputL[i]=static_cast<float>(left);inputR[i]=static_cast<float>(right);
  input64L[i]=left;input64R[i]=right;
 }
 constexpr std::array<std::array<float,2>,6> controls{{
  {0.f,0.f},{0.f,1.f},{1.f,0.f},{1.f,1.f},{.5f,.5f},{.35f,.65f}
 }};
 unsigned cases=0,failedCase=0,failedRate=0;
 unsigned long long firstAlloc=0;
 bool returnedFalse=false,nonFinite=false,raised=false;
 try {
  AudioGuard guard;
  for(unsigned rate:{44100u,48000u,96000u,192000u}) {
   for(unsigned id=1;id<=55;++id) {
    for(const auto& pair:controls) {
     x3.reset();
     const unsigned alternate=id==55?1:id+1;
     const bool valid=x3.set(1,.5f,.5f,rate)&&
       x3.set(id,pair[0],pair[1],rate);
     if(!valid){returnedFalse=true;failedCase=id;failedRate=rate;break;}
     x3.process(inputL.data(),inputR.data(),resultL.data(),resultR.data(),frames,1.,false);
     if(!x3.set(alternate,pair[0],pair[1],rate)){
      returnedFalse=true;failedCase=id;failedRate=rate;break;
     }
     x3.process(input64L.data(),input64R.data(),result64L.data(),result64R.data(),17,1.,false);
     if(!x3.set(id,pair[0],pair[1],rate)){
      returnedFalse=true;failedCase=id;failedRate=rate;break;
     }
     x3.process(inputL.data()+17,inputR.data()+17,resultL.data()+17,resultR.data()+17,frames-17,0.,true);
     x3.completeDeferred();
     x3.process(input64L.data(),input64R.data(),result64L.data(),result64R.data(),frames,1.,false);
     for(unsigned n:{0u,17u,239u,511u,1023u}) {
      if(!std::isfinite(resultL[n])||!std::isfinite(resultR[n])||
         !std::isfinite(result64L[n])||!std::isfinite(result64R[n])){
       nonFinite=true;failedCase=id;failedRate=rate;break;
      }
     }
     ++cases;
     const auto seen=gAudioNewCalls.load(std::memory_order_relaxed);
     if(seen||nonFinite){firstAlloc=seen;failedCase=id;failedRate=rate;break;}
    }
    if(returnedFalse||nonFinite||firstAlloc)break;
   }
   if(returnedFalse||nonFinite||firstAlloc)break;
  }
 }catch(const std::exception&) {raised=true;}
 catch(...) {raised=true;}
 const auto newCalls=gAudioNewCalls.load(std::memory_order_relaxed);
 if(raised||returnedFalse||nonFinite||newCalls||cases!=55u*4u*6u) {
  std::fprintf(stderr,
   "X3 RT FAIL: cases=%u/1320 filter=%u rate=%u exceptions=%d refused=%d nonfinite=%d C++ new in guarded DSP=%llu\n",
   cases,failedCase,failedRate,raised,returnedFalse,nonFinite,newCalls);
  return 1;
 }
 // Adversarial, FINITE extreme upstream PCM. The insert must never
 // emit Inf/NaN or keep an IIR poisoned across a subsequent normal note.
 for(unsigned rate:{44100u,48000u,96000u,192000u}) {
  for(unsigned id=1;id<=55;++id) {
   x3.reset();
   if(!x3.set(id,.5f,.5f,rate))return 1;
   inputL.fill(std::numeric_limits<float>::max());
   inputR.fill(-std::numeric_limits<float>::max());
   x3.process(inputL.data(),inputR.data(),resultL.data(),resultR.data(),frames,1.,false);
   for(unsigned n=0;n<frames;++n) {
    if(!std::isfinite(resultL[n])||!std::isfinite(resultR[n])) {
     std::fprintf(stderr,"X3 RT FAIL: nonfinite hostile input result mode=%u rate=%u sample=%u L=%g R=%g\\n",
                  id,rate,n,resultL[n],resultR[n]);
     return 1;
    }
   }
   inputL.fill(0.f);inputR.fill(0.f);
   inputL[0]=.001f;inputR[7]=-.001f;
   x3.process(inputL.data(),inputR.data(),resultL.data(),resultR.data(),frames,1.,false);
   for(unsigned n=0;n<frames;++n) {
    if(!std::isfinite(resultL[n])||!std::isfinite(resultR[n])) {
     std::fprintf(stderr,"X3 RT FAIL: poisoned IIR state persisted mode=%u rate=%u sample=%u\\n",id,rate,n);
     return 1;
    }
   }
  }
 }
 std::printf("X3 RT PASS: 1320 real-bank mode/rate/Morph-Q cases, 32/64-bit streaming, queued fades; C++ heap new calls inside guarded adapter=0; no C++ exceptions or nonfinite samples. Direct malloc/HeapAlloc in linked code NOT COVERED.\n");
 return 0;
}
