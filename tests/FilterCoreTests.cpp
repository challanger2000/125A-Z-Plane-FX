#include "FilterCore.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#define REQUIRE(x) do{if(!(x)){std::fprintf(stderr,"FAIL %s at %d\n",#x,__LINE__);return EXIT_FAILURE;}}while(false)
int main(){
 using namespace zplane;
 std::array<float,256> l{},r{},oL{},oR{};
 l[0]=1.;r[3]=-.5f;Settings settings{};
 for(int mode=0;mode<4;++mode){
  StereoFilter f;settings.mode=static_cast<Mode>(mode);settings.wet=1.;settings.bypass=false;
  f.configure(48000.,settings);f.process(l.data(),r.data(),oL.data(),oR.data(),256);
  REQUIRE(std::abs(oL[0])>1.e-6f);REQUIRE(oR[0]==0.);
  for(auto v:oL)REQUIRE(std::isfinite(v));
  for(auto v:oR)REQUIRE(std::isfinite(v));
 }
 StereoFilter dry;settings.bypass=true;dry.configure(44100.,settings);
 dry.process(l.data(),r.data(),oL.data(),oR.data(),256);REQUIRE(oL==l&&oR==r);
 settings.bypass=false;settings.wet=0.;dry.configure(44100.,settings);
 dry.process(l.data(),r.data(),oL.data(),oR.data(),256);REQUIRE(oL==l&&oR==r);
 std::array<float,256> aL{},aR{},bL{},bR{};
 settings.wet=1.;settings.mode=Mode::LowPass;
 StereoFilter a,b;a.configure(96000.,settings);b.configure(96000.,settings);
 a.process(l.data(),r.data(),aL.data(),aR.data(),256);
 b.process(l.data(),r.data(),bL.data(),bR.data(),128);
 b.process(l.data()+128,r.data()+128,bL.data()+128,bR.data()+128,128);
 REQUIRE(aL==bL&&aR==bR);
 StereoFilter bad;bad.configure(48000.,settings);
 l[0]=std::numeric_limits<float>::quiet_NaN();
 bad.process(l.data(),r.data(),oL.data(),oR.data(),256);
 REQUIRE(oL[0]==0.&&std::isfinite(oL[1]));
 // A clean steady DC response must not discontinuously jump
 // from the low-pass steady-state output to high-pass zero on change.
 std::array<double,2048> dc{},zero{},out1{},out2{},parts{};
 dc.fill(.5);
 Settings s; s.frequency=1000.;s.quality=.7071067811865476;
 StereoFilter whole,partitioned;whole.configure(48000.,s);partitioned.configure(48000.,s);
 whole.process(dc.data(),zero.data(),out1.data(),out2.data(),2048);
 partitioned.process(dc.data(),zero.data(),parts.data(),out2.data(),2048);
 REQUIRE(out1[2047]>.49&&out1[2047]<.51);
 s.mode=Mode::HighPass;
 whole.configure(48000.,s);partitioned.configure(48000.,s);
 whole.process(dc.data(),zero.data(),out1.data(),out2.data(),2048);
 partitioned.process(dc.data(),zero.data(),parts.data(),out2.data(),32);
 partitioned.process(dc.data()+32,zero.data()+32,parts.data()+32,out2.data()+32,2016);
 REQUIRE(out1==parts);
 REQUIRE(out1[0]>.45);REQUIRE(std::abs(out1[2047])<.0001);
 // Continuous rapid filter requests must remain finite, then converge
 // to the most recent requested shape without restarting a ramp forever.
 StereoFilter rapid;Settings changes;rapid.configure(48000.,changes);
 for(int i=0;i<64;++i){
  changes.mode=(i%2)?Mode::LowPass:Mode::HighPass;
  rapid.configure(48000.,changes);
  rapid.process(dc.data(),zero.data(),out1.data(),out2.data(),1);
  REQUIRE(std::isfinite(out1[0])&&std::isfinite(out2[0]));
 }
 changes.mode=Mode::HighPass;
 rapid.configure(48000.,changes);
 rapid.process(dc.data(),zero.data(),out1.data(),out2.data(),2048);
 REQUIRE(std::abs(out1[2047])<.0001);
 // Bypass crossfade finishes in 240 samples at 48 kHz.
 StereoFilter smooth;Settings hp;hp.mode=Mode::HighPass;hp.frequency=1000.;
 smooth.configure(48000.,hp);
 std::array<double,1024> ones{},zeros{},wL{},wR{};ones.fill(.75);
 smooth.process(ones.data(),zeros.data(),wL.data(),wR.data(),1024);
 hp.bypass=true;smooth.configure(48000.,hp);
 smooth.process(ones.data(),zeros.data(),wL.data(),wR.data(),1024);
 REQUIRE(wL[0]<.75&&wL[0]>-.1);
 REQUIRE(wL[511]==.75&&wL[1023]==.75&&wR[1023]==0.);
 std::puts("FilterCore: PCM, block partition, crossfade, ramp, rapid automation PASS");
 return EXIT_SUCCESS;
}
