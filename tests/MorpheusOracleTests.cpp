// Independent 289-cube comparison: build the original published Morphosis
// numerical core in a TEST-ONLY binary. Nothing is linked into the shipped VST3.
#include "MorpheusFilter.h"
#include "MorphosisDSP.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace {
bool near(double a,double b,double absolute=2.e-11,double relative=5.e-9) noexcept {
 return std::isfinite(a)&&std::isfinite(b)&&
        std::abs(a-b)<=absolute+relative*std::max(std::abs(a),std::abs(b));
}
[[noreturn]] void fail(unsigned cube,unsigned grid,double rate,unsigned stage,
                       const char* field,double expected,double observed){
 std::cerr<<"INDEPENDENT ORACLE FAIL: cube="<<cube<<" grid="<<grid<<" rate="<<rate
          <<" stage="<<stage<<" field="<<field
          <<" expected="<<expected<<" actual="<<observed<<"\n";
 std::exit(EXIT_FAILURE);
}
void compare(double expected,double observed,unsigned cube,unsigned grid,
             double rate,unsigned stage,const char* field){
 if(!near(expected,observed))fail(cube,grid,rate,stage,field,expected,observed);
}
}

int main(){
 using zplane::MorpheusFilter;
 const std::array<std::array<double,3>,5> controls{{
  {0.,0.,0.},{.125,.875,.5},{.5,.5,.5},{.9,.1,.3},{1.,1.,1.}
 }};
 const std::array<double,5> rates{{
  44100., 48000., MorpheusFilter::kNativeRate, 96000., 192000.
 }};
 const std::array<double,6> spectrumHz{{20.,110.,440.,1200.,5800.,18000.}};
 unsigned comparisons=0, responses=0;
 for(unsigned cube=1;cube<MorpheusFilter::kCubes;++cube){
  if(std::strcmp(MorpheusFilter::name(cube),
                 morphosis::cube_data::kCubes[cube].name)!=0)
   fail(cube,0,0.,0,"cube-name",1.,0.);
  for(unsigned grid=0;grid<controls.size();++grid){
   const auto [morph,freq,transform]=controls[grid];
   const MorpheusFilter::Control c{cube,morph,freq,transform};
   for(double rate:rates){
    const auto actual=MorpheusFilter::describe(c,rate);
    const auto reference=morphosis::MorphosisDSP::makeCoefficients(
       static_cast<int>(cube),-5.+freq*10.,-5.+morph*10.,-5.+transform*10.,rate);
    if(!actual.valid||actual.nullFilter||!reference.valid||
       reference.usedSafeFallback||reference.neutralBypass)
      fail(cube,grid,rate,0,"valid-no-fallback",1.,0.);
    compare(reference.gain,actual.gain,cube,grid,rate,7,"gain");
    for(unsigned k=0;k<MorpheusFilter::kStages;++k){
     const auto& a=actual.stages[k];
     const auto& r=reference.stages[k];
     compare(r.a,a.a,cube,grid,rate,k,"a");
     compare(r.b,a.b,cube,grid,rate,k,"b");
     compare(r.radius,a.radius,cube,grid,rate,k,"radius");
     compare(r.z1,a.z1,cube,grid,rate,k,"z1");
     compare(r.z2,a.z2,cube,grid,rate,k,"z2");
     compare(r.inputGain,a.scale,cube,grid,rate,k,"inputGain");
    }
    for(double hz:spectrumHz){
     const auto m=MorpheusFilter::magnitude(actual,hz,rate);
     const auto ref=morphosis::MorphosisDSP::responseMagnitude(reference,hz,rate);
     if(!near(ref,m,1.e-8,4.e-5))
      fail(cube,grid,rate,7,"frequencyResponse",ref,m);
     ++responses;
    }
    ++comparisons;
   }
  }
 }
 const auto nullCube=MorpheusFilter::describe({0,.5,.5,.5},48000.);
 if(!nullCube.nullFilter||!near(MorpheusFilter::magnitude(nullCube,440.,48000.),1.))
  fail(0,0,48000.,0,"intentionalNullIdentity",1.,0.);
 std::cout<<"INDEPENDENT MORPHEUS ORACLE PASS: "<<comparisons
          <<" real 7-stage coefficient comparisons; "<<responses
          <<" analytic frequency-response comparisons, 288 non-null cubes, "
          <<"5 3D settings, 5 rates. Null cube is deliberate exact dry identity. "
          <<"No hardware bit-exact or nonlinear accuracy claim.\n";
 return EXIT_SUCCESS;
}
