#include "AutomationTimeline.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"FAIL %s at %d\n",#x,__LINE__);return EXIT_FAILURE;}} while(false)
int main() {
 zplane::AutomationTimeline t;
 CHECK(t.add(64,2,0.2));CHECK(t.add(0,3,0.4));CHECK(t.add(64,2,0.8));CHECK(t.add(32,1,0.5));
 t.finalize();
 CHECK(t.size()==4);
 CHECK(t[0].offset==0&&t[0].parameter==3);
 CHECK(t[1].offset==32&&t[1].parameter==1);
 CHECK(t[2].offset==64&&t[2].value==0.2);
 CHECK(t[3].offset==64&&t[3].value==0.8);
 CHECK(!t.add(1,6,0.1));
 for(std::size_t i=t.size();i<zplane::AutomationTimeline::kCapacity;++i)CHECK(t.add(123,3,0.1));
 t.finalize();
 CHECK(t.size()==zplane::AutomationTimeline::kCapacity);
 CHECK(!t.add(123,3,0.2));CHECK(t.overflow());
 // Adversarial descending timestamps previously caused ~8m moves during
 // callback; the new collector appends O(1) and sorts only once.
 t.clear();
 for(std::size_t i=0;i<zplane::AutomationTimeline::kCapacity;++i)
   CHECK(t.add(static_cast<std::uint32_t>(zplane::AutomationTimeline::kCapacity-i),4,0.3));
 t.finalize();
 for(std::size_t i=1;i<t.size();++i)
   CHECK(t[i-1].offset<t[i].offset);
 CHECK(t[0].offset==1);

 t.clear();CHECK(!t.overflow()&&t.size()==0);
 puts("Automation event timeline regression PASS");return EXIT_SUCCESS;
}
