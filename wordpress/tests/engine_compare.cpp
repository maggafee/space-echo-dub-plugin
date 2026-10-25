#include "dsp/TapeEchoEngine.h"
#include <cstdio>
#include <vector>
#include <cmath>
using namespace dub;
int main(){
  const int sr=48000, n=sr*12, block=128;
  std::vector<float> L(n),R(n),oL(n),oR(n);
  uint32_t r=7; auto nz=[&]{r^=r<<13;r^=r>>17;r^=r<<5;return (float)((int32_t)r)/2147483648.0f;};
  for(int i=0;i<n;i++){ double t=(double)i/sr; double ph=fmod(t,0.8);
    float env= ph<0.15? (float)exp(-ph*25):0; float s=(float)(0.4*env*sin(2*M_PI*330*t)+0.15*env*nz());
    if(t>6) s=0; L[i]=s; R[i]=s*0.7f+0.05f*(float)sin(2*M_PI*110*t)*(t<6);}
  TapeEchoEngine e; EngineParams p; e.setParams(p); e.prepare(sr,block);
  for(int s=0;s<n;s+=block){ double t=(double)s/sr; EngineParams q=p;
    q.delaySec = t<3?0.3f:(t<5?0.12f:0.45f); q.feedback=0.6f; q.mode = t<4?5:(t<7?11:8);
    q.stereoMode = t<2?1:(t<4?3:(t<8?2:0)); q.wowFlutter=0.5f; q.tapeAge=0.6f; q.kick = (t>7.0&&t<7.05);
    q.power = !(t>9&&t<10.5); q.duck = t>8?0.4f:0.f; q.throwOn = (t>10.6&&t<11); q.bassDb=3; q.trebleDb=-2; q.lowCutHz=90;
    e.setParams(q); e.process(L.data()+s,R.data()+s,oL.data()+s,oR.data()+s,std::min(block,n-s)); }
  FILE*f=fopen("in.f32","wb"); for(int i=0;i<n;i++){fwrite(&L[i],4,1,f);fwrite(&R[i],4,1,f);} fclose(f);
  f=fopen("cpp.f32","wb"); for(int i=0;i<n;i++){fwrite(&oL[i],4,1,f);fwrite(&oR[i],4,1,f);} fclose(f);
}
