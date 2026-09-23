#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
typedef struct AEffect AEffect;
typedef intptr_t (*cb)(AEffect*,int32_t,int32_t,intptr_t,void*,float);
struct AEffect { int32_t magic; intptr_t (*d)(AEffect*,int32_t,int32_t,intptr_t,void*,float);
 void*p; void (*setP)(AEffect*,int32_t,float); float (*getP)(AEffect*,int32_t);
 int32_t np,npar,ni,no,flags; intptr_t r1,r2; int32_t a,b,c; float io; void*obj,*user; int32_t uid,ver;
 void (*pr)(AEffect*,float**,float**,int32_t); void*pdr; char f[56]; };
typedef struct { int32_t type,byteSize,deltaFrames,flags,noteLength,noteOffset; unsigned char m[4]; char x[4]; } ME;
typedef struct { int32_t n; intptr_t r; void* ev[2]; } EV;
extern AEffect* VSTPluginMain(cb);
static intptr_t host(AEffect*e,int32_t op,int32_t i,intptr_t v,void*p,float o){ static double ti[16]; if(op==7){ti[4]=128.0; ((int32_t*)&ti[8])[5]=1<<10; return (intptr_t)ti;} return 0; }
int main(){
 AEffect *a=VSTPluginMain(host), *b=VSTPluginMain(host);
 printf("magic=%x params=%d flags=%x uid=%x twoInstances=%d\n",a->magic,a->npar,a->flags,a->uid,a!=b);
 char s[256]; for(int i=0;i<a->npar;i+=20){ a->d(a,8,i,0,s,0); char d[64]={0}; a->d(a,7,i,0,d,0); printf("  p%d %s = %s (norm %.3f)\n",i,s,d,a->getP(a,i)); }
 a->setP(a,14,0.25f); char d[64]; a->d(a,7,14,0,d,0); a->d(a,8,14,0,s,0); printf("set %s .25 -> %s, get %.3f\n",s,d,a->getP(a,14));
 a->setP(a,10,1.0f); a->d(a,7,10,0,d,0); printf("route 1.0 -> %s\n",d);
 ME m={1,sizeof(ME),0,0,0,0,{0x90,48,100,0}}; EV ev={1,0,{&m,0}}; a->d(a,25,0,0,&ev,0);
 float L[512],R[512],*out[2]={L,R}; double e=0; for(int k=0;k<40;k++){ a->pr(a,0,out,100); for(int i=0;i<100;i++) e+=L[i]*L[i]; }
 printf("rms after note = %.4f\n", sqrt(e/4000));
 void* ch=0; intptr_t n=a->d(a,23,0,0,&ch,0); printf("chunk %ld bytes: %.100s...\n",(long)n,(char*)ch);
 b->d(b,24,0,n,ch,0); printf("b after setChunk: cutoff norm %.3f (a %.3f)\n", b->getP(b,14), a->getP(a,14));
 a->d(a,1,0,0,0,0); b->d(b,1,0,0,0,0); return 0; }
