#include "MorpheusFilter.h"
#include "MorpheusReferenceFixtures.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <string_view>
namespace {
bool closeReference(double actual,double expected,double absTol,double relTol) {
 return std::isfinite(actual)&&std::isfinite(expected)&&
        std::abs(actual-expected)<=absTol+relTol*std::abs(expected);
}
bool verifyIndependentReferences() {
 using zplane::MorpheusFilter;
 for(const auto& ref:zplane_test::kReferenceCases){
  const MorpheusFilter::Control control{ref.preset,ref.morph,ref.frequency,ref.transform};
  const auto d=MorpheusFilter::describe(control,ref.rate);
  if(!d.valid||d.nullFilter||!closeReference(d.gain,ref.gain,1.e-12,1.e-11)){
   std::cerr<<"REFERENCE FAIL: gain / validity "<<ref.name<<"\n";
   return false;
  }
  for(std::size_t i=0;i<d.stages.size();++i){
   const auto& s=d.stages[i];
   const auto& golden=ref.stages[i];
   const std::array<double,6> actual{s.a,s.b,s.radius,s.z1,s.z2,s.scale};
   for(std::size_t k=0;k<actual.size();++k){
    if(!closeReference(actual[k],golden[k],5.e-11,5.e-9)){
     std::cerr<<"REFERENCE FAIL: coefficient "<<ref.name
              <<" stage="<<i<<" field="<<k
              <<" expected="<<golden[k]<<" actual="<<actual[k]<<"\n";
     return false;
    }
   }
  }
  for(std::size_t i=0;i<zplane_test::kResponseFrequencies.size();++i){
   const auto actual=MorpheusFilter::magnitude(
      d,zplane_test::kResponseFrequencies[i],ref.rate);
   const auto expected=ref.spectrum[i];
   if(!closeReference(actual,expected,2.e-8,2.e-6)){
    std::cerr<<"REFERENCE FAIL: spectrum "<<ref.name
             <<" frequency="<<zplane_test::kResponseFrequencies[i]
             <<" expected="<<expected<<" actual="<<actual<<"\n";
    return false;
   }
  }
  std::array<double,32> inputL{},inputR{},outputL{},outputR{};
  inputL[0]=0.1; inputL[1]=-0.05; inputL[2]=0.025; inputL[3]=-0.0125;
  inputR[0]=0.037; inputR[1]=-0.0185; inputR[2]=0.00925; inputR[3]=-0.004625;
  MorpheusFilter filter;
  filter.reset(ref.rate);
  filter.set(control,1.,false);
  filter.process(inputL.data(),inputR.data(),outputL.data(),outputR.data(),32);
  for(std::size_t i=0;i<32;++i){
   if(!closeReference(outputL[i],ref.left[i],2.e-8,2.e-5)||
      !closeReference(outputR[i],ref.right[i],2.e-8,2.e-5)){
    std::cerr<<"REFERENCE FAIL: stereo PCM "<<ref.name<<" sample="<<i
             <<" L="<<outputL[i]<<" / "<<ref.left[i]
             <<" R="<<outputR[i]<<" / "<<ref.right[i]<<"\n";
    return false;
   }
  }
  auto mutation=d;
  mutation.stages[0].a*=0.85;
  const auto referenceMagnitude=MorpheusFilter::magnitude(d,1000.,ref.rate);
  const auto mutantMagnitude=MorpheusFilter::magnitude(mutation,1000.,ref.rate);
  if(closeReference(referenceMagnitude,mutantMagnitude,1.e-12,1.e-5)){
   std::cerr<<"REFERENCE FAIL: spectral negative control insensitive "<<ref.name<<"\n";
   return false;
  }
 }
 return true;
}
}

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string_view>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"MORPHEUS FAIL %s at line %d\n",#x,__LINE__);return EXIT_FAILURE;}}while(false)
int main(){
 using zplane::MorpheusFilter;
 CHECK(verifyIndependentReferences());
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
 std::puts("MORPHEUS PASS: independent frozen coefficient/spectrum/stereo PCM references, 289 cubes, 3D axes, multirate, click-free transitions");
 return EXIT_SUCCESS;
}
