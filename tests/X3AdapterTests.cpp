#include "X3ResearchAdapter.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"X3 FAIL: %s at %d\n",#x,__LINE__);return EXIT_FAILURE;}} while(false)

int main() {
 namespace fs=std::filesystem;
 const auto dir=fs::temp_directory_path()/"125a_x3_adapter_fixture";
 fs::create_directories(dir);
 auto writeZero=[](const fs::path& p,size_t bytes){
  std::ofstream f(p,std::ios::binary|std::ios::trunc);
  std::array<char,1024> data{};
  while(bytes){const auto n=std::min(bytes,data.size());f.write(data.data(),static_cast<std::streamsize>(n));bytes-=n;}
 };
 // Synthetic zeros ONLY: prove all 55 dispatches compile and execute
 // without redistributing any E-MU packed coefficient data.
 writeZero(dir/"factory-bank.bin",33u*4u*240u);
 writeZero(dir/"standard-bank.bin",31u*4u*4u*5u*2u);
 writeZero(dir/"morph-support.bin",402);
 zplane::X3ResearchAdapter x3;
 std::string reason;
 CHECK(x3.load(dir,reason));CHECK(x3.hasBanks());
 std::array<float,64> left{},right{},outL{},outR{};
 left[0]=0.0001f;right[3]=-0.0001f;
 for(unsigned i=1;i<=55;++i) {
  x3.reset();
  CHECK(x3.set(i,0.5f,0.5f,48000));
  x3.process(left.data(),right.data(),outL.data(),outR.data(),64,1.,false);
  for(float s:outL)CHECK(std::isfinite(s));
  for(float s:outR)CHECK(std::isfinite(s));
  CHECK(!x3.set(i,0.5f,0.5f,12345));
 }
 x3.reset();CHECK(!x3.set(0,0.5f,0.5f,48000));
 CHECK(!x3.set(56,0.5f,0.5f,48000));
 // Negative fault injection: a new bank-load error must invalidate
 // previously active filter state, not keep rendering stale old-bank PCM.
 // This catches a subtle failed-reload bug even if hasBanks() is false.
 CHECK(x3.set(1,0.5f,0.5f,48000));
 fs::remove(dir/"factory-bank.bin");
 CHECK(!x3.load(dir,reason));
 CHECK(!reason.empty()&&!x3.hasBanks());
 CHECK(!x3.set(1,0.5f,0.5f,48000));
 outL.fill(17.5f);outR.fill(-17.5f);
 x3.process(left.data(),right.data(),outL.data(),outR.data(),64,1.,false);
 for(float s:outL)CHECK(s==17.5f);
 for(float s:outR)CHECK(s==-17.5f);
 writeZero(dir/"factory-bank.bin",33u*4u*240u);
 CHECK(x3.load(dir,reason)&&reason.empty()&&x3.hasBanks());
 CHECK(x3.set(2,0.5f,0.5f,48000));
 fs::resize_file(dir/"standard-bank.bin",31u*4u*4u*5u*2u-1u);
 CHECK(!x3.load(dir,reason));
 CHECK(!reason.empty()&&!x3.hasBanks());
 outL.fill(11.f);outR.fill(-11.f);
 x3.process(left.data(),right.data(),outL.data(),outR.data(),64,1.,false);
 for(float s:outL)CHECK(s==11.f);
 for(float s:outR)CHECK(s==-11.f);
 writeZero(dir/"standard-bank.bin",31u*4u*4u*5u*2u);
 CHECK(x3.load(dir,reason)&&reason.empty()&&x3.hasBanks());
 CHECK(x3.set(55,0.5f,0.5f,48000));
 fs::remove_all(dir);
 puts("X3 synthetic 55 modes, bad-bank load rejection, stale-DSP invalidation and recovery PASS");
 return 0;
}
