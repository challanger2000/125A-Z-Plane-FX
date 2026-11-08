#include "X3ResearchAdapter.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#define REQUIRE(x) do{if(!(x)){std::fprintf(stderr,"REAL X3 ADAPTER FAIL %s at line %d\n",#x,__LINE__);return EXIT_FAILURE;}}while(false)
int main() {
 const char* dir=std::getenv("ZPLANE_X3_BANK_DIR");
 REQUIRE(dir&&*dir);
 const std::filesystem::path root=std::filesystem::u8path(dir);
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
 REQUIRE(nonzero>0);
 std::printf("55 original X3 modes * 4 sample rates: %u plugin PCM samples bit-identical to reference implementation\n",checked);
 return EXIT_SUCCESS;
}
