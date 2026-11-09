#include "FilterResponseMath.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#define CHECK(c) do {if(!(c)){std::fprintf(stderr,"FILTER DISPLAY FAIL %s at line %d\n",#c,__LINE__);return EXIT_FAILURE;}}while(false)
int main(){
 using namespace zplane::response;
 Params p{0.,.5,.5,1.,0.,0.,0.,0.,.5,.5,.5};
 bool ok=false;
 auto a=computeAnalytic(p,ok);
 CHECK(ok&&a[0]>a[255]+20.f); // legacy lowpass rolls down
 p[0]=.25;
 a=computeAnalytic(p,ok);
 CHECK(ok&&a[255]>a[0]+20.f); // highpass
 p[4]=1.;a=computeAnalytic(p,ok);
 CHECK(ok);for(const auto db:a)CHECK(db==0.f); // bypass identity
 p[4]=0.;p[6]=1.;p[7]=0.;p[8]=.5;p[9]=.5;p[10]=.5;
 a=computeAnalytic(p,ok);
 CHECK(ok);for(const auto db:a)CHECK(db==0.f); // Null Cube identity
 p[7]=46./288.;
 a=computeAnalytic(p,ok);
 CHECK(ok);
 const auto expected=zplane::MorpheusFilter::describe(cubeControl(p),kRate);
 for(const auto index:{0u,14u,80u,180u,255u}){
  const auto expectedDb=levelDb(morpheusTransfer(expected,frequency(index),kRate));
  CHECK(std::abs(a[index]-expectedDb)<2.e-5f);
 }
 auto original=a;
 p[8]=.1;const auto varied=computeAnalytic(p,ok);
 CHECK(ok);
 double delta=0.;for(std::size_t i=0;i<kPoints;++i)delta+=std::abs(varied[i]-original[i]);
 CHECK(delta>1.);
 // Host-rate oracle: use independently computed coefficient descriptions.
 p[4]=0.;p[6]=1.;p[7]=46./288.;p[8]=.29;
 const auto at48=computeAnalytic(p,ok,48000.);CHECK(ok);
 const auto at96=computeAnalytic(p,ok,96000.);CHECK(ok);
 const auto d48=zplane::MorpheusFilter::describe(cubeControl(p),48000.);
 const auto d96=zplane::MorpheusFilter::describe(cubeControl(p),96000.);
 CHECK(d48.valid&&d96.valid);
 CHECK(std::abs(at48[180]-levelDb(morpheusTransfer(d48,frequency(180),48000.)))<2.e-5f);
 CHECK(std::abs(at96[180]-levelDb(morpheusTransfer(d96,frequency(180),96000.)))<2.e-5f);
 double rateDifference=0.;
 for(std::size_t i=0;i<kPoints;++i)rateDifference+=std::abs(at48[i]-at96[i]);
 CHECK(rateDifference>.01);
 const auto low=computeAnalytic(p,ok,32000.);
 CHECK(ok&&std::isfinite(low[0])&&std::isnan(low[255]));
 p[6]=0.;p[5]=40./55.;p[4]=0.;
 (void)computeAnalytic(p,ok);
 CHECK(!ok); // must NEVER quietly display legacy curve for selected X3
 p[4]=1.;
 auto legacy=computeAnalytic(p,ok);
 CHECK(!ok); // X3 path is supplied by the dedicated impulse measurement view
 CHECK(frequency(0)==20.);
 CHECK(frequency(kPoints-1)>19999.);
 std::puts("FILTER DISPLAY PASS: host 48/96k transfer and Nyquist mask, wet/bypass, null identity, Morpheus modulation, X3 no-fake gate, log axis");
 return EXIT_SUCCESS;
}
