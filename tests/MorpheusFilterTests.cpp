#include "MorpheusFilter.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string_view>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"MORPHEUS FAIL %s at line %d\n",#x,__LINE__);return EXIT_FAILURE;}}while(false)
int main(){
 using zplane::MorpheusFilter;
 static_assert(MorpheusFilter::kCubes==289);
 CHECK(std::string_view(MorpheusFilter::name(0))=="Null Cube");
 CHECK(std::string_view(MorpheusFilter::name(43))=="VowelSpace");
 CHECK(std::string_view(MorpheusFilter::name(288))=="AllPoleDst2");
 for(unsigned index=0;index<289;++index) {
  for(const auto& xyz:std::array<std::array<double,3>,3>{{{{0.,0.,0.}},{{.5,.5,.5}},{{1.,1.,1.}}}}){
   const MorpheusFilter::Control c{index,xyz[0],xyz[1],xyz[2]};
   for(const double rate:{44100.,48000.,96000.}){
    const auto d=MorpheusFilter::describe(c,rate);
    CHECK(d.valid);
    const auto h=MorpheusFilter::magnitude(d,1000.,rate);
    CHECK(std::isfinite(h)&&h>=0.);
   }
  }
 }
 const auto neutral=MorpheusFilter::describe({0,.5,.5,.5},48000.);
 CHECK(neutral.nullFilter&&MorpheusFilter::magnitude(neutral,1000.,48000.)==1.);
 const auto voice=MorpheusFilter::describe({43,.3,.7,.4},48000.);
 CHECK(voice.valid&&!voice.nullFilter);
 const auto freq1=MorpheusFilter::magnitude(voice,300.,48000.);
 const auto freq2=MorpheusFilter::magnitude(voice,3000.,48000.);
 CHECK(std::isfinite(freq1)&&std::isfinite(freq2)&&std::abs(freq1-freq2)>1.e-5);
 for(const double rate:{44100.,48000.,96000.,192000.}){
  MorpheusFilter whole,split;
  whole.reset(rate);split.reset(rate);
  const MorpheusFilter::Control c{43,.23,.62,.7};
  whole.set(c,1.,false);split.set(c,1.,false);
  std::array<float,2048> l{},r{},a{},b{},c1{},c2{};
  for(std::size_t i=0;i<l.size();++i){
   l[i]=static_cast<float>(.1*std::sin(i*.1)+.01*std::cos(i*.03));
   r[i]=static_cast<float>(.08*std::cos(i*.11)-.01*std::sin(i*.05));
  }
  whole.process(l.data(),r.data(),a.data(),b.data(),2048);
  split.process(l.data(),r.data(),c1.data(),c2.data(),17);
  split.process(l.data()+17,r.data()+17,c1.data()+17,c2.data()+17,113);
  split.process(l.data()+130,r.data()+130,c1.data()+130,c2.data()+130,1918);
  CHECK(a==c1&&b==c2);
  double diff=0.;for(std::size_t i=0;i<l.size();++i){
   CHECK(std::isfinite(a[i])&&std::isfinite(b[i]));
   diff+=std::abs(static_cast<double>(a[i])-l[i]);
  }
  CHECK(diff>.001);
  whole.set({44,.8,.2,.4},1.,false);
  split.set({44,.8,.2,.4},1.,false);
  whole.process(l.data(),r.data(),a.data(),b.data(),2048);
  split.process(l.data(),r.data(),c1.data(),c2.data(),121);
  split.process(l.data()+121,r.data()+121,c1.data()+121,c2.data()+121,1927);
  CHECK(a==c1&&b==c2);
  whole.set({288,.4,.7,.9},1.,false);
  split.set({288,.4,.7,.9},1.,false);
  whole.set({17,.3,.2,.7},1.,false);
  split.set({17,.3,.2,.7},1.,false);
  whole.process(l.data(),r.data(),a.data(),b.data(),2048);
  split.process(l.data(),r.data(),c1.data(),c2.data(),2048);
  CHECK(a==c1&&b==c2);
  whole.completeDeferred();split.completeDeferred();
  whole.process(l.data(),r.data(),a.data(),b.data(),2048);
  split.process(l.data(),r.data(),c1.data(),c2.data(),2048);
  CHECK(a==c1&&b==c2);
  whole.set({0,.5,.5,.5},0.,false);
  whole.process(l.data(),r.data(),a.data(),b.data(),2048);
  whole.process(l.data(),r.data(),a.data(),b.data(),2048);
  CHECK(a==l&&b==r);
 }
 std::puts("MORPHEUS PASS: 289 cube datasets, 3D axes, sample rates, non-dry, fade/partition, dry identity");
 return EXIT_SUCCESS;
}
