#include "FilterCore.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#define REQUIRE(c) do {if(!(c)){std::fprintf(stderr,"FAIL %s line %d\n",#c,__LINE__);return EXIT_FAILURE;}} while(false)
int main(){
 using namespace zplane;
 std::array<float,256> inL{},inR{},outL{},outR{};
 inL[0]=1.0f;inR[3]=-0.5f;
 Settings s{};
 for(int m=0;m<4;++m){
   StereoFilter filter; s.mode=static_cast<Mode>(m);s.wet=1.;s.bypass=false;
   filter.configure(48000.,s); filter.process(inL.data(),inR.data(),outL.data(),outR.data(),256);
   REQUIRE(std::abs(outL[0])>0.000001f);
   REQUIRE(outR[0]==0.f);
   for(float v:outL)REQUIRE(std::isfinite(v));
   for(float v:outR)REQUIRE(std::isfinite(v));
 }
 StereoFilter bypass; s.wet=1.;s.bypass=true;bypass.configure(44100.,s);
 bypass.process(inL.data(),inR.data(),outL.data(),outR.data(),256);
 REQUIRE(outL==inL);REQUIRE(outR==inR);
 s.bypass=false;s.wet=0.;bypass.configure(44100.,s);
 bypass.process(inL.data(),inR.data(),outL.data(),outR.data(),256);
 REQUIRE(outL==inL);REQUIRE(outR==inR);
 // Same block partition must generate exactly the same filter-state output.
 std::array<float,256> aL{},aR{},bL{},bR{};
 s.wet=1.;s.mode=Mode::LowPass;
 StereoFilter a,b;a.configure(96000.,s);b.configure(96000.,s);
 a.process(inL.data(),inR.data(),aL.data(),aR.data(),256);
 b.process(inL.data(),inR.data(),bL.data(),bR.data(),128);
 b.process(inL.data()+128,inR.data()+128,bL.data()+128,bR.data()+128,128);
 REQUIRE(aL==bL);REQUIRE(aR==bR);
 StereoFilter bad; s.wet=1.; bad.configure(48000.,s);
 inL[0]=std::numeric_limits<float>::quiet_NaN();
 bad.process(inL.data(),inR.data(),outL.data(),outR.data(),256);
 REQUIRE(outL[0]==0.f);REQUIRE(std::isfinite(outL[1]));
 // Smooth bypass transition at 48k: 5 ms, with 0 and 100% exact endpoints.
 StereoFilter smooth;
 Settings hp{}; hp.mode=Mode::HighPass;hp.frequency=1000.;hp.wet=1.;
 smooth.configure(48000.,hp);
 std::array<double,1024> ones{},zeros{},wetL{},wetR{};
 ones.fill(0.75);
 smooth.process(ones.data(),zeros.data(),wetL.data(),wetR.data(),1024);
 hp.bypass=true;smooth.configure(48000.,hp);
 smooth.process(ones.data(),zeros.data(),wetL.data(),wetR.data(),1024);
 REQUIRE(wetL[0]<0.75&&wetL[0]>-0.1);
 REQUIRE(wetL[511]==0.75);
 REQUIRE(wetL[1023]==0.75);
 REQUIRE(wetR[1023]==0.);
 std::puts("FilterCore: active PCM, bad-input and wet-ramp tests PASS");return EXIT_SUCCESS;
}
