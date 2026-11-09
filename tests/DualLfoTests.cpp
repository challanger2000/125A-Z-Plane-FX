#include "DualLfo.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#define CHECK(c) do {if(!(c)){std::fprintf(stderr,"DUAL LFO FAIL: %s at %d\n",#c,__LINE__);return EXIT_FAILURE;}}while(false)
int main(){
 using L=zplane::DualLfo;
 L::Params p{};
 p[1]=p[2]=p[8]=p[9]=p[10]=.5;
 p[11]=0.;p[13]=1.;p[15]=.25;
 auto v=L::evaluate(p,3000,48000.,{},0);
 CHECK(v.morph==.5&&v.frequency==.5&&!L::anyActive(p));
 p[11]=1.;p[13]=0.;
 CHECK(!L::anyActive(p));
 p[13]=1.;p[12]=.5;p[14]=.75;p[16]=0.;
 CHECK(L::anyActive(p));
 auto plus=L::evaluate(p,0,48000.,{},0);
 CHECK(plus.morph==1.&&plus.x3Morph==1.);
 CHECK(L::shape(3,.2)==1.&&L::shape(3,.7)==-1.);
 CHECK(L::shape(4,-3.1)==L::shape(4,-3.9));
 CHECK(L::shape(4,12.1)!=L::shape(4,13.1));
 p[14]=.25;p[16]=1.; // sync, triangle, quarter-note cycle
 auto quarter=L::evaluate(p,0,48000.,{0.,120.},12000);
 auto split=L::evaluate(p,999999,48000.,{.25,120.},6000);
 CHECK(std::abs(quarter.morph-1.)<1.e-12&&split.morph==quarter.morph);
 auto seek=L::evaluate(p,987654321,48000.,{.5,120.},0);
 CHECK(std::abs(seek.morph-1.)<1.e-12);
 p[17]=1.;p[19]=1.;p[20]=.75;p[21]=1.;p[22]=1.;
 auto both=L::evaluate(p,0,48000.,{0.,120.},0);
 CHECK(both.morph==1.&&both.x3Q==1.);
 p[11]=0.;p[17]=0.;
 v=L::evaluate(p,15000,48000.,{},0);
 CHECK(v.morph==.5&&v.x3Q==.5);
 for(int i=0;i<9;++i){
  p[11]=1.;p[12]=i/8.;p[18]=i/8.;
  v=L::evaluate(p,100000000,192000.,{100000.,220.},311);
  CHECK(std::isfinite(v.morph)&&v.morph>=0.&&v.morph<=1.);
 }
 CHECK(L::quantum(48000.)==240&&L::quantum(192000.)==960);
 std::puts("DUAL LFO PASS: exact neutral path, sync/tempo/seek, 5 shapes, deterministic sample hold, saturation");
 return EXIT_SUCCESS;
}
