// Generic v5 own-engine oracle: drives a carved demo binary's GENUINE V2 synth
// with the source-ported _viruz2.cpp player (verbatim from c2_oracle.c).
// Per-binary config comes in via -D flags (see build commands below).
//
// Build: gcc -m32 -no-pie -O0 v5_oracle.c -I<toolkit> -o oracle \
//   -DIMG_SIZE=0x800000 -DVA_INIT=0x.. -DVA_GLOB=0x.. -DVA_MIDI=0x.. -DVA_REND=0x.. \
//   -DRDTSC0=0x.. -DRDTSC1=0x.. [-DIAT_ALLOC=.. -DIAT_ALLOC2=.. -DIAT_FREE=..]
// Run:   ./oracle <unpacked.bin> <song.v2m> <out.f32> <seconds>

#include <stdint.h>
typedef uint8_t u8; typedef uint32_t u32; typedef int32_t s32; typedef uint64_t u64;

#define IMG_BASE 0x400000u
#ifndef IMG_SIZE
#define IMG_SIZE 0x800000u
#endif

#define ORACLE_IMG_SIZE IMG_SIZE
#include "oracle.h"

static const u32 RDTSC_SITES[] = { RDTSC0, RDTSC1 };

static void* __attribute__((stdcall)) stub_alloc(int one,int size){(void)one;return calloc(1,size?size:1);}
static void* __attribute__((stdcall)) stub_ident(void*p){return p;}
static void  __attribute__((stdcall)) stub_free(void*p){free(p);}

// binary synth entry points via stdcall function pointers (callee cleans stack)
static void (__attribute__((stdcall)) *p_init)(void*,int)        = (void*)VA_INIT;   // (patchmap, samplerate) ret8
static void (__attribute__((stdcall)) *p_glob)(void*)            = (void*)VA_GLOB;   // (globals) ret4
static void (__attribute__((stdcall)) *p_midi)(void*)            = (void*)VA_MIDI;   // (midibuf) ret4
static void (__attribute__((stdcall)) *p_rend)(void*,int,int,int)= (void*)VA_REND;   // (buf,count,a,b) ret16
#ifdef VA_LYRICS
static void (__attribute__((stdcall)) *p_lyrics)(const char**)   = (void*)VA_LYRICS; // (speechptrs) ret4
#endif

static void synthInit(void*pm){ p_init(pm,44100); }
static void synthSetGlobals(void*g){ p_glob(g); }
static void synthProcessMIDI(void*p){ p_midi(p); }
static void synthRender(void*buf,int smp){ p_rend(buf,smp,0,0); }

// ---- player ported verbatim from genthree/_viruz2.cpp (== c2_oracle.c) ----
typedef struct { u32 ccnum; u8* ccptr; } bcctl;
typedef struct { u32 notenum; u8* noteptr; u32 pcnum; u8* pcptr; u32 pbnum; u8* pbptr; bcctl ctl[7]; } basech;
static struct { u8* patchmap; u8* globals; u32 timediv,timediv2,maxtime; u8* gptr; u32 gdnum; basech chan[16];
    const char* speechdata; const char* speechptrs[256]; } base;
static struct {
    int running, silent; u32 time, nexttime; u8* gptr; u32 gnt,gnr,usecs,num,den,tpq,bar,beat,tick;
    struct { u8* noteptr; u32 notenr,notent; u8 lastnte,lastvel; u8* pcptr; u32 pcnr,pcnt; u8 lastpc;
             u8* pbptr; u32 pbnr,pbnt; u8 lastpb0,lastpb1;
             struct { u8* ccptr; u32 ccnt,ccnr; u8 lastcc; } ctl[7]; } chan[16];
    u32 cursmpl, smpldelta, smplrem, tdif;
} state;
static u8 midibuf[4096];
static u32 g_spsize;  // stashed for reporting only

#define GETDELTA(p,w) ((u32)((p)[0]+((p)[w]<<8)+((p)[2*(w)]<<16)))
#define UPDATENT(n,v,p,w)  do{ if((n)<(w)){ (v)=state.time+GETDELTA((p),(w)); if((v)<state.nexttime) state.nexttime=(v);} }while(0)
#define UPDATENT2(n,v,p,w) do{ if((n)<(w) && GETDELTA((p),(w))){ (v)=state.time+GETDELTA((p),(w)); } }while(0)
#define UPDATENT3(n,v,p,w) do{ if((n)<(w) && (v)<state.nexttime) state.nexttime=(v); }while(0)
#define PUTSTAT(s) do{ u8 bla=(s); if(laststat!=bla){ laststat=bla; *mptr++=(u8)laststat; } }while(0)

static void ssInitBase(u8* d){
    base.timediv=*(u32*)d; base.timediv2=10000*base.timediv; base.maxtime=*(u32*)(d+4);
    base.gdnum=*(u32*)(d+8); d+=12; base.gptr=d; d+=10*base.gdnum;
    for(int ch=0;ch<16;ch++){ basech*c=&base.chan[ch]; c->notenum=*(u32*)d; d+=4;
        if(c->notenum){ c->noteptr=d; d+=5*c->notenum; c->pcnum=*(u32*)d; d+=4; c->pcptr=d; d+=4*c->pcnum;
            c->pbnum=*(u32*)d; d+=4; c->pbptr=d; d+=5*c->pbnum;
            for(int cn=0;cn<7;cn++){ c->ctl[cn].ccnum=*(u32*)d; d+=4; c->ctl[cn].ccptr=d; d+=4*c->ctl[cn].ccnum; } } }
    u32 size=*(u32*)d; d+=4; base.globals=d; d+=size;
    size=*(u32*)d; d+=4; base.patchmap=d; d+=size;
    u32 spsize=*(u32*)d; d+=4;
    if(!spsize){ for(int i=0;i<256;i++) base.speechptrs[i]=" "; }
    else { base.speechdata=(const char*)d; d+=spsize; u32*p32=(u32*)base.speechdata; u32 n=*p32++;
        for(u32 i=0;i<n;i++) base.speechptrs[i]=base.speechdata+*p32++; }
    g_spsize=spsize;
}
static void ssReset(){
    state.time=0; state.nexttime=(u32)-1; state.gptr=base.gptr; state.gnr=0;
    UPDATENT(state.gnr,state.gnt,state.gptr,base.gdnum);
    for(int ch=0;ch<16;ch++){ basech*bc=&base.chan[ch]; if(!bc->notenum) continue;
        state.chan[ch].noteptr=bc->noteptr; state.chan[ch].notenr=state.chan[ch].lastnte=state.chan[ch].lastvel=0;
        UPDATENT(state.chan[ch].notenr,state.chan[ch].notent,state.chan[ch].noteptr,bc->notenum);
        state.chan[ch].pcptr=bc->pcptr; state.chan[ch].pcnr=state.chan[ch].lastpc=0;
        UPDATENT(state.chan[ch].pcnr,state.chan[ch].pcnt,state.chan[ch].pcptr,bc->pcnum);
        state.chan[ch].pbptr=bc->pbptr; state.chan[ch].pbnr=state.chan[ch].lastpb0=state.chan[ch].lastpb1=0;
        UPDATENT(state.chan[ch].pbnr,state.chan[ch].pbnt,state.chan[ch].pbptr,bc->pcnum);
        for(int cn=0;cn<7;cn++){ state.chan[ch].ctl[cn].ccptr=bc->ctl[cn].ccptr; state.chan[ch].ctl[cn].ccnr=state.chan[ch].ctl[cn].lastcc=0;
            UPDATENT(state.chan[ch].ctl[cn].ccnr,state.chan[ch].ctl[cn].ccnt,state.chan[ch].ctl[cn].ccptr,bc->ctl[cn].ccnum); } }
    state.usecs=500000*441; state.num=4; state.den=4; state.tpq=8; state.bar=0; state.beat=0; state.tick=0; state.smplrem=0;
    synthInit(base.patchmap); synthSetGlobals(base.globals);
#ifdef VA_LYRICS
    // ronan speech: mirror the genuine player's ssReset tail
    // (_viruz2.cpp: synthInit -> synthSetGlobals -> synthSetLyrics(speechptrs)).
    // Feeds the v2m's 256 phoneme-string pointers into ronan so ch15 vocodes
    // real speech instead of collapsing to silence. ronan was init'd by synthInit.
    p_lyrics(base.speechptrs);
#endif
}
static void ssTick(){
    if(!state.running) return;
    state.tick+=state.nexttime-state.time;
    while(state.tick>=base.timediv){ state.tick-=base.timediv; state.beat++; }
    u32 qpb=(state.num*4/state.den);
    while(state.beat>=qpb){ state.beat-=qpb; state.bar++; }
    state.time=state.nexttime; state.nexttime=(u32)-1;
    u8*mptr=midibuf; u32 laststat=(u32)-1;
    if(state.gnr<base.gdnum && state.time==state.gnt){
        state.usecs=(*(u32*)(state.gptr+3*base.gdnum+4*state.gnr))*441;
        state.num=state.gptr[7*base.gdnum+state.gnr]; state.den=state.gptr[8*base.gdnum+state.gnr];
        state.tpq=state.gptr[9*base.gdnum+state.gnr]; state.gnr++;
        UPDATENT2(state.gnr,state.gnt,state.gptr+state.gnr,base.gdnum); }
    UPDATENT3(state.gnr,state.gnt,state.gptr+state.gnr,base.gdnum);
    for(int ch=0;ch<16;ch++){ basech*bc=&base.chan[ch]; if(!bc->notenum) continue;
        if(state.chan[ch].pcnr<bc->pcnum && state.time==state.chan[ch].pcnt){
            PUTSTAT(0xc0|ch); *mptr++=(state.chan[ch].lastpc+=state.chan[ch].pcptr[3*bc->pcnum]);
            state.chan[ch].pcnr++; state.chan[ch].pcptr++;
            UPDATENT2(state.chan[ch].pcnr,state.chan[ch].pcnt,state.chan[ch].pcptr,bc->pcnum); }
        UPDATENT3(state.chan[ch].pcnr,state.chan[ch].pcnt,state.chan[ch].pcptr,bc->pcnum);
        for(int cn=0;cn<7;cn++){ bcctl*bcc=&bc->ctl[cn];
            if(state.chan[ch].ctl[cn].ccnr<bcc->ccnum && state.time==state.chan[ch].ctl[cn].ccnt){
                PUTSTAT(0xb0|ch); *mptr++=cn+1; *mptr++=(state.chan[ch].ctl[cn].lastcc+=state.chan[ch].ctl[cn].ccptr[3*bcc->ccnum]);
                state.chan[ch].ctl[cn].ccnr++; state.chan[ch].ctl[cn].ccptr++;
                UPDATENT2(state.chan[ch].ctl[cn].ccnr,state.chan[ch].ctl[cn].ccnt,state.chan[ch].ctl[cn].ccptr,bcc->ccnum); }
            UPDATENT3(state.chan[ch].ctl[cn].ccnr,state.chan[ch].ctl[cn].ccnt,state.chan[ch].ctl[cn].ccptr,bcc->ccnum); }
        if(state.chan[ch].pbnr<bc->pbnum && state.time==state.chan[ch].pbnt){
            PUTSTAT(0xe0|ch); *mptr++=(state.chan[ch].lastpb0+=state.chan[ch].pbptr[3*bc->pcnum]);
            *mptr++=(state.chan[ch].lastpb1+=state.chan[ch].pbptr[4*bc->pcnum]);
            state.chan[ch].pbnr++; state.chan[ch].pbptr++;
            UPDATENT2(state.chan[ch].pbnr,state.chan[ch].pbnt,state.chan[ch].pbptr,bc->pbnum); }
        UPDATENT3(state.chan[ch].pbnr,state.chan[ch].pbnt,state.chan[ch].pbptr,bc->pbnum);
        while(state.chan[ch].notenr<bc->notenum && state.time==state.chan[ch].notent){
            PUTSTAT(0x90|ch); *mptr++=(state.chan[ch].lastnte+=state.chan[ch].noteptr[3*bc->notenum]);
            *mptr++=(state.chan[ch].lastvel+=state.chan[ch].noteptr[4*bc->notenum]);
            state.chan[ch].notenr++; state.chan[ch].noteptr++;
            UPDATENT2(state.chan[ch].notenr,state.chan[ch].notent,state.chan[ch].noteptr,bc->notenum); }
        UPDATENT3(state.chan[ch].notenr,state.chan[ch].notent,state.chan[ch].noteptr,bc->notenum); }
    *mptr++=0xfd;
#ifdef KK_VOICEBASE
    if(getenv("KK_MIDITRACE")){ u8*q=midibuf; u32 st=0;
        fprintf(stderr,"[midi] t=%u :",state.time);
        while(*q!=0xfd){ if(*q&0x80){ st=*q; fprintf(stderr," |%02x",*q); q++; }
            else fprintf(stderr," %02x",*q), q++; }
        fprintf(stderr,"\n"); }
#endif
    synthProcessMIDI(midibuf);
    if(state.nexttime==(u32)-1) state.running=0;
}
static void ssRender(float* outbuf, u32 len){
    if(state.running && !state.silent){
        while(len){ u32 torender=(len>state.smpldelta)?state.smpldelta:len;
            if(torender) synthRender(outbuf,torender);
            outbuf+=2*torender; len-=torender; state.smpldelta-=torender; state.cursmpl+=torender;
            if(!state.smpldelta){ ssTick();
                if(state.running){ u64 prod=(u64)(state.nexttime-state.time)*(u64)state.usecs;
                    u32 q=(u32)(prod/base.timediv2), r=(u32)(prod%base.timediv2);
                    u32 nr=state.smplrem+r, carry=(nr<state.smplrem)?1:0; state.smplrem=nr; state.smpldelta=q+carry; }
                else state.smpldelta=(u32)-1; } }
    } else { memset(outbuf,0,len*2*sizeof(float)); state.cursmpl+=len; }
}

int main(int argc,char**argv){
    const char*img=argc>1?argv[1]:"unpacked.bin";
    const char*v2m=argc>2?argv[2]:"song.v2m";
    const char*outp=argc>3?argv[3]:"out.f32";
    u32 secs=argc>4?atoi(argv[4]):45;
    oracle_install_faults();
    { struct sigaction sa; memset(&sa,0,sizeof sa); sa.sa_sigaction=oracle_segv;
      sa.sa_flags=SA_SIGINFO; sigaction(SIGFPE,&sa,NULL); }
    oracle_map_image(img, IMG_SIZE);
    oracle_pin_rdtsc(RDTSC_SITES, sizeof(RDTSC_SITES)/sizeof(RDTSC_SITES[0]));
#ifdef KK_RONAN_JNE
    // KK_NORONAN: convert the `jne .noronan` guarding the ch15 syRonanProcess
    // call into an unconditional jmp (0x75->0xeb), so ch15 renders the RAW
    // oscillator carrier instead of the (lyric-less => silent) vocoded speech.
    if(getenv("KK_NORONAN")){ u8*p=(u8*)(uintptr_t)KK_RONAN_JNE;
        if(*p==0x75){ *p=0xeb; fprintf(stderr,"[orc] ronan ch15 NOP'd (raw carrier)\n"); }
        else fprintf(stderr,"[orc] WARN no jne @%#x (%02x)\n",KK_RONAN_JNE,*p); }
#endif
#ifdef IAT_ALLOC
    *(u32*)(uintptr_t)IAT_ALLOC=(u32)(uintptr_t)&stub_alloc;
    *(u32*)(uintptr_t)IAT_ALLOC2=(u32)(uintptr_t)&stub_ident;
    *(u32*)(uintptr_t)IAT_FREE=(u32)(uintptr_t)&stub_free;
#endif

    FILE*vf=fopen(v2m,"rb"); if(!vf){fprintf(stderr,"no %s\n",v2m);return 1;}
    fseek(vf,0,SEEK_END); long vn=ftell(vf); fseek(vf,0,SEEK_SET);
    u8*vbuf=malloc(vn); fread(vbuf,1,vn,vf); fclose(vf);
    fprintf(stderr,"[orc] v2m %ld bytes\n",vn);

    ssInitBase(vbuf);
    fprintf(stderr,"[orc] timediv=%u maxtime=%u gdnum=%u patchmap=%p globals=%p spsize=%u\n",
        base.timediv,base.maxtime,base.gdnum,(void*)base.patchmap,(void*)base.globals,g_spsize);
    int active=0; for(int ch=0;ch<16;ch++) if(base.chan[ch].notenum) active++;
    fprintf(stderr,"[orc] active channels=%d\n",active);
    ssReset();
    state.cursmpl=state.smpldelta=0; state.running=1; state.silent=0;

    u32 total=secs*44100, done=0; double peak=0;
    u32 CHUNK=getenv("ORC_CHUNK")?atoi(getenv("ORC_CHUNK")):2048;
    float*buf=malloc(8192*2*sizeof(float));
    FILE*out=fopen(outp,"wb");
#ifdef KK_VOICEBASE
    int vtrace=getenv("KK_VTRACE")!=NULL; u32 nextv=0;
#endif
    while(done<total){ u32 n=(total-done<CHUNK)?(total-done):CHUNK;
        ssRender(buf,n);
        for(u32 i=0;i<n*2;i++){double a=buf[i];if(a<0)a=-a;if(a>peak)peak=a;}
        fwrite(buf,2*sizeof(float),n,out); done+=n;
#ifdef KK_VOICEBASE
        if(vtrace && done>=nextv){ nextv+=44100; // once/sec
            double cpk=0; for(u32 i=0;i<n*2;i++){double a=buf[i];if(a<0)a=-a;if(a>cpk)cpk=a;}
            fprintf(stderr,"[v] %.0fs mixpk=%.3e\n",(double)done/44100,cpk);
            for(int v=0;v<32;v++){ u32 vb=KK_VOICEBASE+v*0x228;
                s32 ch=*(s32*)(uintptr_t)(KK_CHANMAP+4*v);
                if(ch<0) continue;
                u32 gate=*(u32*)(uintptr_t)(vb+0x8);
                float curvol=*(float*)(uintptr_t)(vb+0xc);
                u32 o1=vb+0x30; u32 mode=*(u32*)(uintptr_t)(o1);
                float g=*(float*)(uintptr_t)(o1+0x20); u32 freq=*(u32*)(uintptr_t)(o1+0xc);
                fprintf(stderr,"    v%02d ch%d gate=%u curvol=%.4f osc1[mode=%u gain=%.5e freq=%u]\n",
                    v,ch,gate,curvol,mode,g,freq); } }
#endif
        if(!state.running){ fprintf(stderr,"[orc] song ended at %.1fs\n",(double)done/44100); break; } }
    fclose(out);
    fprintf(stderr,"[orc] rendered %.1fs peak=%.4f\n",(double)done/44100,peak);
    return 0;
}
