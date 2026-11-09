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
 p[6]=0.;p[5]=40./55.;p[4]=0.;
 (void)computeAnalytic(p,ok);
 CHECK(!ok); // must NEVER quietly display legacy curve for selected X3
 p[4]=1.;
 auto legacy=computeAnalytic(p,ok);
 CHECK(!ok); // X3 path is supplied by the dedicated impulse measurement view
 CHECK(frequency(0)==20.);
 CHECK(frequency(kPoints-1)>19999.);
 std::puts("FILTER DISPLAY PASS: complex transfer, wet/bypass, null identity, Morpheus modulation, X3 no-fake gate, log axis");
 return EXIT_SUCCESS;
}
