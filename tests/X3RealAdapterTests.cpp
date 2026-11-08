#include "X3ResearchAdapter.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <windows.h>
#define REQUIRE(x) do{if(!(x)){std::fprintf(stderr,"REAL X3 ADAPTER FAIL %s at line %d\n",#x,__LINE__);return EXIT_FAILURE;}}while(false)
int main() {
 std::array<wchar_t,32768> buffer{};
 const auto len=GetEnvironmentVariableW(L"ZPLANE_X3_BANK_DIR",buffer.data(),
                  static_cast<DWORD>(buffer.size()));
 REQUIRE(len>0&&len<buffer.size());
 const std::filesystem::path root(buffer.data());
 zplane::X3ResearchAdapter plugin;
 std::string error;
 REQUIRE(plugin.load(root,error));
 REQUIRE(plugin.hasBanks());
 const auto factory=emux::FactoryBank::from_table_file((root/"factory-bank.bin").string());
 const auto extended=emux::ExtendedBank::from_table_files((root/"standard-bank.bin").string(),(root/"morph-support.bin").string());
 unsigned checked=0,nonzero=0;
 for(unsigned rate:{44100u,48000u,96000u,192000u}){
  for(unsigned id=1;id<=55;++id){
   plugin.reset();REQUIRE(plugin.set(id,0.35f,0.65f,rate));
   emux::Configuration reference{};
   if(id<=33)reference={factory.coefficients(id-1,0.35f,0.65f,rate),6};
   else if(id<=50)reference=extended.coefficients(id-34,0.35f,0.65f,rate);
   else if(id<=54)reference=extended.morph_coefficients(
       static_cast<emux::MorphKind>(id-51),emux::MorphParameters{32,96,64,48,112,64},0.35f,0.65f,rate);
   else{
    std::array<emux::DesignerSection,6> sections{};
    sections[0].shape=emux::Shape::eq;
    sections[1].shape=emux::Shape::lowpass;
    reference=extended.designer_coefficients(sections,0.35f,0.65f,rate);
   }
   emux::Filter expectedL(reference.coefficients,reference.sections);
   emux::Filter expectedR(reference.coefficients,reference.sections);
   std::array<float,64> inL{},inR{},outL{},outR{};
   inL[0]=.001f;inR[3]=-.001f;
   plugin.process(inL.data(),inR.data(),outL.data(),outR.data(),64,1.0,false);
   for(int n=0;n<64;++n){
    const float wantedL=expectedL.process_sample(inL[n]);
    const float wantedR=expectedR.process_sample(inR[n]);
    REQUIRE(std::memcmp(&wantedL,&outL[n],sizeof(float))==0);
    REQUIRE(std::memcmp(&wantedR,&outR[n],sizeof(float))==0);
    REQUIRE(std::isfinite(outL[n])&&std::isfinite(outR[n]));
    if(outL[n]!=0.0f)++nonzero;
    checked+=2;
   }
  }
 }
 // Regression: during a 5 ms crossfade, another filter selection must
 // survive until the next safe host block boundary. The old process()
 // implementation silently cleared the pending request when a fade
 // finished mid-block, leaving the wrong filter selected indefinitely.
 zplane::X3ResearchAdapter queued,reference3;
 REQUIRE(queued.load(root,error));
 REQUIRE(reference3.load(root,error));
 REQUIRE(queued.set(1,.35f,.65f,48000));
 REQUIRE(queued.set(2,.35f,.65f,48000));
 REQUIRE(queued.set(3,.35f,.65f,48000)); // latest must win
 REQUIRE(reference3.set(3,.35f,.65f,48000));
 std::array<float,512> silent{},silentOutputL{},silentOutputR{};
 queued.process(silent.data(),silent.data(),silentOutputL.data(),silentOutputR.data(),512,1.,false);
 queued.completeDeferred();
 queued.process(silent.data(),silent.data(),silentOutputL.data(),silentOutputR.data(),512,1.,false);
 queued.completeDeferred();
 // At this point the queued transition must have finished. A fresh
 // impulse must match the final requested mode rather than mode 2.
 std::array<float,64> probe{},selectedL{},selectedR{},expectedL{},expectedR{};
 probe[0]=.001f;
 queued.process(probe.data(),silent.data(),selectedL.data(),selectedR.data(),64,1.,false);
 reference3.process(probe.data(),silent.data(),expectedL.data(),expectedR.data(),64,1.,false);
 REQUIRE(std::memcmp(selectedL.data(),expectedL.data(),64*sizeof(float))==0);
 REQUIRE(std::memcmp(selectedR.data(),expectedR.data(),64*sizeof(float))==0);
 // X3 wet/bypass automation must not jump abruptly at the event
 // boundary, and must be independent of host block partitioning.
 zplane::X3ResearchAdapter smooth,partitioned;
 REQUIRE(smooth.load(root,error)&&partitioned.load(root,error));
 REQUIRE(smooth.set(36,.35f,.65f,48000)&&partitioned.set(36,.35f,.65f,48000));
 std::array<double,512> signal{},none{},initialL{},initialR{},fadedL{},fadedR{},splitL{},splitR{};
 signal.fill(.25);
 smooth.process(signal.data(),none.data(),initialL.data(),initialR.data(),512,1.,false);
 partitioned.process(signal.data(),none.data(),splitL.data(),splitR.data(),512,1.,false);
 smooth.process(signal.data(),none.data(),fadedL.data(),fadedR.data(),512,1.,true);
 partitioned.process(signal.data(),none.data(),splitL.data(),splitR.data(),17,1.,true);
 partitioned.process(signal.data()+17,none.data()+17,splitL.data()+17,splitR.data()+17,495,1.,true);
 REQUIRE(fadedL==splitL&&fadedR==splitR);
 REQUIRE(fadedL[0]!=signal[0]);  // Fade must not jump to dry immediately
 REQUIRE(fadedL[239]==signal[239]); // 5 ms at 48 kHz
 REQUIRE(fadedL[511]==signal[511]);
 // At bypass, 64-bit PCM must be exactly dry, not quantized to float32.
 std::array<double,64> dryPrecise{},dryRight{},exactL{},exactR{};
 for(std::size_t i=0;i<64;++i){
  dryPrecise[i]=.123456789012345+static_cast<double>(i)*.000000000000001;
  dryRight[i]=-.234567890123456-static_cast<double>(i)*.000000000000001;
 }
 smooth.process(dryPrecise.data(),dryRight.data(),exactL.data(),exactR.data(),64,1.,true);
 REQUIRE(exactL==dryPrecise&&exactR==dryRight);
 REQUIRE(nonzero>0);
 std::printf("55 original X3 modes * 4 sample rates: %u plugin PCM samples bit-identical to reference implementation\n",checked);
 return EXIT_SUCCESS;
}
