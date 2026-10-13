/* SPDX-License-Identifier: BSD-2-Clause */
/* GGE DAC player, Build 67 reference rev 1.0 pp96-100.
 * One PCM16-LE stereo STREAM, voices 0/1; GGE video is never enabled.
 * NetBSD requires >=3 hardware blocks. Use four blocks and sample V_POS
 * each tick for quarter-ring completion; half-ring IRQs also request service.
 */
#include "opt_a2560me.h"
#define _M68K_BUS_DMA_PRIVATE
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/systm.h>
#include <sys/callout.h>
#include <sys/sysctl.h>
#include <sys/evcnt.h>
#include <uvm/uvm_extern.h>
#include <dev/audio/audio_if.h>
#include <machine/board.h>
#include <machine/bus.h>
#include <machine/intr.h>
#include <machine/pmap.h>

#define GGE_BASE 0xfd004000U
#define CTRL 0x00
#define STATUS 0x04
#define MASTER 0x08
#define EVENT 0x0c
#define IRQEN 0x10
#define PLAYING 0x14
#define VCTRL 0x400
#define ADDR 0x404
#define LEN 0x408
#define LOOP 0x40c
#define STEP 0x410
#define VOL 0x414
#define POS 0x418
#define STREAM16 0x94U
#define RINGSIZE 32768
#define BLOCKSIZE 8192
#define FRAMEBYTES 4
#define IRQBIT 0x0020

struct ggedac_softc {
 device_t dev;
 volatile uint8_t *regs;
 kmutex_t lock, intrlock;
 callout_t poll;
 void *softint;
 bool irq, running;
 unsigned lastblock;
 int tone, tone_end;
 uint16_t volume;
 void (*intr)(void *);
 void *intrarg;
 void *buffer;
 bus_dma_segment_t seg;
 int nsegs;
 struct sysctllog *sysctllog;
 struct evcnt irqs, completions, clips, starves, late;
};
static inline uint32_t rd(struct ggedac_softc *s,unsigned r) {
 return *(volatile uint32_t *)(s->regs+r);
}
static inline void wr(struct ggedac_softc *s,unsigned r,uint32_t v) {
 *(volatile uint32_t *)(s->regs+r)=v;
}
static const struct audio_format formats[] = {{
 .mode=AUMODE_PLAY, .encoding=AUDIO_ENCODING_SLINEAR_LE,
 .validbits=16, .precision=16, .channels=2,
 .channel_mask=AUFMT_STEREO, .frequency_type=1, .frequency={48000}
}};
static bool valid(const audio_params_t *p) {
 return p->sample_rate==48000 && p->channels==2 && p->precision==16 &&
     p->encoding==AUDIO_ENCODING_SLINEAR_LE;
}
static int query(void *arg,audio_format_query_t *q) {
 return audio_query_format(formats,__arraycount(formats),q);
}
static int setformat(void *arg,int mode,const audio_params_t *p,
 const audio_params_t *r,audio_filter_reg_t *pf,audio_filter_reg_t *rf) {
 if(mode!=AUMODE_PLAY || !valid(p)) return EINVAL;
 /* MI audio swaps native big-endian samples into the advertised LE format. */
 pf->codec=NULL; pf->context=NULL; return 0;
}
static int blocksize(void *arg,int size,int mode,const audio_params_t *p) {
 return BLOCKSIZE;
}
static size_t buffersize(void *arg,int mode,size_t size) { return RINGSIZE; }
static void *allocm(void *arg,int direction,size_t size) {
 struct ggedac_softc *s=arg;
 if(direction!=AUMODE_PLAY || size!=RINGSIZE || s->buffer) return NULL;
 /* A single physical DDR segment; never use SRAM or a hard-coded scratch PA. */
 if(_bus_dmamem_alloc_common(NULL,0x00800000,0x40000000,size,PAGE_SIZE,
     0,&s->seg,1,&s->nsegs,BUS_DMA_WAITOK)) return NULL;
 if(_bus_dmamem_map(NULL,&s->seg,s->nsegs,size,&s->buffer,
     BUS_DMA_WAITOK|BUS_DMA_COHERENT)) {
  _bus_dmamem_free(NULL,&s->seg,s->nsegs); return NULL;
 }
 memset(s->buffer,0,size);
 return s->buffer;
}
static void freem(void *arg,void *ptr,size_t size) {
 struct ggedac_softc *s=arg;
 KASSERT(!s->running && ptr==s->buffer && size==RINGSIZE);
 _bus_dmamem_unmap(NULL,ptr,size);
 _bus_dmamem_free(NULL,&s->seg,s->nsegs); s->buffer=NULL;
}
static void service(void *arg) {
 struct ggedac_softc *s=arg;
 mutex_enter(&s->intrlock);
 if(s->tone && (int)(getticks()-s->tone_end)>=0) {
  wr(s,CTRL,0); s->tone=0;
 }
 if(s->running) {
  uint32_t flags=rd(s,STATUS);
  if(flags&1) s->clips.ev_count++;
  if(flags&2) s->starves.ev_count++;
  wr(s,STATUS,flags&3);
  wr(s,EVENT,1); /* Peripheral deasserts before clearing controller pending. */
  unsigned pos=rd(s,POS);
  if(pos<RINGSIZE/FRAMEBYTES) {
   unsigned block=pos/(BLOCKSIZE/FRAMEBYTES);
   unsigned count=(block+4-s->lastblock)%4;
   if(count>1) s->late.ev_count++;
   /* At most three callbacks; a callback is for a consumed block, never
    * merely for submission or for a duplicate half-ring interrupt. */
   while(count-- && s->running) {
    s->lastblock=(s->lastblock+1)%4;
    s->completions.ev_count++;
    s->intr(s->intrarg);
   }
  }
 }
 REG16(0xfec00102)=IRQBIT;
 if(s->running && s->irq) REG16(0xfec0011a)&=~IRQBIT;
 mutex_exit(&s->intrlock);
}
static int interrupt(void *arg) {
 struct ggedac_softc *s=arg;
 REG16(0xfec0011a)|=IRQBIT;
 s->irqs.ev_count++;
 softint_schedule(s->softint);
 return 1;
}
static void poll(void *arg) {
 struct ggedac_softc *s=arg;
 service(s);
 mutex_enter(&s->intrlock);
 if(s->running || s->tone) callout_schedule(&s->poll,1);
 mutex_exit(&s->intrlock);
}
static int halt(void *arg) {
 struct ggedac_softc *s=arg;
 KASSERT(mutex_owned(&s->intrlock));
 s->running=false;
 wr(s,IRQEN,0); REG16(0xfec0011a)|=IRQBIT;
 callout_halt(&s->poll,&s->intrlock);
 /* Let the documented <=255-sample STOP ramp finish, bounded to 10ms. */
 wr(s,VCTRL,STREAM16|2);
 for(unsigned n=0;n<1000 && (rd(s,PLAYING)&1);n++) delay(10);
 wr(s,CTRL,0); delay(10);
 wr(s,EVENT,0xffffffff); REG16(0xfec00102)=IRQBIT;
 s->tone=0; s->intr=NULL; s->intrarg=NULL;
 return 0;
}
static int trigger(void *arg,void *start,void *end,int size,
 void (*intr)(void *),void *intrarg,const audio_params_t *p) {
 struct ggedac_softc *s=arg;
 KASSERT(mutex_owned(&s->intrlock));
 if(!valid(p) || start!=s->buffer || (uint8_t *)end-(uint8_t *)start!=RINGSIZE ||
     size!=BLOCKSIZE || !intr) return EINVAL;
 if(s->running || s->tone) return EBUSY;
 wr(s,CTRL,0); wr(s,IRQEN,0); wr(s,EVENT,0xffffffff); wr(s,STATUS,3);
 wr(s,ADDR,s->seg.ds_addr); wr(s,LEN,RINGSIZE/FRAMEBYTES);
 wr(s,LOOP,0); wr(s,STEP,0x10000); wr(s,VOL,0x8080);
 s->intr=intr; s->intrarg=intrarg; s->lastblock=0; s->running=true;
 /* buffer mapping is cache-inhibited; no dirty CPU cache lines exist. */
 __asm volatile("" ::: "memory");
 wr(s,VCTRL,STREAM16|1); wr(s,CTRL,1);
 if(s->irq) {
  REG16(0xfec00102)=IRQBIT; wr(s,IRQEN,1);
  REG16(0xfec0011a)&=~IRQBIT;
 }
 callout_schedule(&s->poll,1);
 return 0;
}
static int getdev(void *arg,struct audio_device *d) {
 static const struct audio_device id={"GGE DAC player","67","ggedac"};
 *d=id; return 0;
}
/* Logical mixer scale 0..255 maps to 0..unity, never the hardware's 2x gain. */
static unsigned hwvol(unsigned v) { return (v*128+127)/255; }
static int setport(void *arg,mixer_ctrl_t *m) {
 struct ggedac_softc *s=arg;
 if(m->dev!=1 || m->type!=AUDIO_MIXER_VALUE ||
     (m->un.value.num_channels!=1 && m->un.value.num_channels!=2)) return EINVAL;
 unsigned l=m->un.value.level[0];
 unsigned r=m->un.value.num_channels==2?m->un.value.level[1]:l;
 mutex_enter(&s->intrlock);
 s->volume=l|(r<<8); wr(s,MASTER,hwvol(l)|(hwvol(r)<<8));
 mutex_exit(&s->intrlock); return 0;
}
static int getport(void *arg,mixer_ctrl_t *m) {
 struct ggedac_softc *s=arg;
 if(m->dev!=1 || m->type!=AUDIO_MIXER_VALUE ||
     (m->un.value.num_channels!=1 && m->un.value.num_channels!=2)) return EINVAL;
 m->un.value.level[0]=s->volume&255;
 if(m->un.value.num_channels==2) m->un.value.level[1]=s->volume>>8;
 return 0;
}
static int devinfo(void *arg,mixer_devinfo_t *d) {
 d->prev=d->next=AUDIO_MIXER_LAST; d->mixer_class=0;
 if(d->index==0) {
  d->type=AUDIO_MIXER_CLASS; strcpy(d->label.name,AudioCoutputs);
 } else if(d->index==1) {
  d->type=AUDIO_MIXER_VALUE; strcpy(d->label.name,AudioNmaster);
  strcpy(d->un.v.units.name,AudioNvolume); d->un.v.num_channels=2; d->un.v.delta=2;
 } else return ENXIO;
 return 0;
}
static int props(void *arg) { return AUDIO_PROP_PLAYBACK; }
static void locks(void *arg,kmutex_t **intr,kmutex_t **thread) {
 struct ggedac_softc *s=arg; *intr=&s->intrlock; *thread=&s->lock;
}
static const struct audio_hw_if hw={
 .query_format=query,.set_format=setformat,.round_blocksize=blocksize,
 .halt_output=halt,.getdev=getdev,.set_port=setport,.get_port=getport,
 .query_devinfo=devinfo,.allocm=allocm,.freem=freem,.round_buffersize=buffersize,
 .get_props=props,.trigger_output=trigger,.get_locks=locks
};
static int codec_write(uint16_t word) {
 for(unsigned n=0;n<1000;n++) {
  if(!(REG16(0xfec20e00)&0x8000)) {
   REG16(0xfec20e00)=word; delay(10); return 0;
  }
  delay(10);
 }
 return ETIMEDOUT;
}
static int tone_sysctl(SYSCTLFN_ARGS) {
 struct ggedac_softc *s=rnode->sysctl_data;
 struct sysctlnode node=*rnode;
 int value,error;
 mutex_enter(&s->intrlock); value=s->tone; mutex_exit(&s->intrlock);
 node.sysctl_data=&value;
 error=sysctl_lookup(SYSCTLFN_CALL(&node));
 if(error || newp==NULL) return error;
 if(value<0 || value>1) return EINVAL;
 mutex_enter(&s->lock); mutex_enter(&s->intrlock);
 if(s->running) error=EBUSY;
 else {
  s->tone=value; wr(s,CTRL,value?2:0);
  if(value) { s->tone_end=getticks()+2*hz; callout_schedule(&s->poll,1); }
 }
 mutex_exit(&s->intrlock); mutex_exit(&s->lock);
 return error;
}
static int match(device_t p,cfdata_t cf,void *aux) {
 /* Avoid bus errors on old FPGA builds: inspect GAVIN before mapping GGE. */
 return REG32(0xfec0000c)==0x9b06c && REG32(0xfec0003c)==67 &&
     REG32(0xfec00010)==0x73380011 && REG32(0xfec00014)==0x00170826;
}
#ifdef A2560ME_AUDIO_JITTER
#include "ggedac_jitter.h"
#endif
static void attach(device_t parent,device_t self,void *aux) {
 struct ggedac_softc *s=device_private(self);
 s->dev=self;
 vaddr_t va=uvm_km_alloc(kernel_map,PAGE_SIZE,0,UVM_KMF_VAONLY|UVM_KMF_WAITVA);
 if(!va) { aprint_error(": cannot map registers\n");return; }
 pmap_kenter_pa(va,GGE_BASE,VM_PROT_READ|VM_PROT_WRITE,PMAP_NOCACHE);
 pmap_update(pmap_kernel()); s->regs=(void *)va;
 mutex_init(&s->lock,MUTEX_DEFAULT,IPL_NONE);
 mutex_init(&s->intrlock,MUTEX_DEFAULT,IPL_VM);
 callout_init(&s->poll,CALLOUT_MPSAFE); callout_setfunc(&s->poll,poll,s);
 wr(s,IRQEN,0); wr(s,CTRL,0);
 /* Voice RAM survives reset: stop every voice before enabling this engine. */
 for(unsigned i=0;i<32;i++) wr(s,VCTRL+i*32,2);
 wr(s,CTRL,1); delay(6000); wr(s,CTRL,0);
 if(rd(s,PLAYING)!=0) { aprint_error(": cannot stop stale voices\n");return; }
 wr(s,EVENT,0xffffffff); wr(s,STATUS,3);
 s->volume=0x8080; wr(s,MASTER,0x4040);
 /* WM8776: reset; quiet HP; 16-bit I2S; 512fs DAC clock; DAC-only mux;
  * digital unity with UPDATE; no ADC bypass or gain from unrelated inputs. */
 static const uint16_t words[]={0x2e00,0x0567,0x1402,0x1845,0x2c01,0x0bff,0x1a00};
 for(unsigned i=0;i<__arraycount(words);i++) {
  if(codec_write(words[i])) { aprint_error(": codec timeout\n");return; }
 }
 aprint_normal(": 48000 Hz PCM16-LE stereo, DDR streaming\n");
#ifdef A2560ME_AUDIO_JITTER
 ggedac_jitter(s); /* Does not return; only the opt-in RAM experiment. */
#endif
 evcnt_attach_dynamic(&s->irqs,EVCNT_TYPE_INTR,NULL,device_xname(self),"interrupts");
 evcnt_attach_dynamic(&s->completions,EVCNT_TYPE_MISC,NULL,device_xname(self),"blocks");
 evcnt_attach_dynamic(&s->clips,EVCNT_TYPE_MISC,NULL,device_xname(self),"clip");
 evcnt_attach_dynamic(&s->starves,EVCNT_TYPE_MISC,NULL,device_xname(self),"starve");
 evcnt_attach_dynamic(&s->late,EVCNT_TYPE_MISC,NULL,device_xname(self),"late service");
 s->softint=softint_establish(SOFTINT_SERIAL|SOFTINT_MPSAFE,service,s);
 if(s->softint) {
  int ps=splhigh();
  REG16(0xfec0011a)|=IRQBIT;
  REG16(0xfec0010a)&=~IRQBIT; /* active low */
  REG16(0xfec00112)&=~IRQBIT; /* level: no lost edge on deferred service */
  s->irq=a2560me_intr_establish(0x15,interrupt,s)==0;
  REG16(0xfec0011a)|=IRQBIT; splx(ps);
 }
 aprint_normal_dev(self,"%s; position service each tick\n",s->irq?"IRQ 0x15":"polling fallback");
 const struct sysctlnode *root;
 if(sysctl_createv(&s->sysctllog,0,NULL,&root,0,CTLTYPE_NODE,
     device_xname(self),NULL,NULL,0,NULL,0,CTL_HW,CTL_CREATE,CTL_EOL)==0)
  sysctl_createv(&s->sysctllog,0,&root,NULL,CTLFLAG_READWRITE,CTLTYPE_INT,
      "test_tone",SYSCTL_DESCR("Write 1 for a two-second 1kHz output test"),
      tone_sysctl,0,(void *)s,0,CTL_CREATE,CTL_EOL);
 audio_attach_mi(&hw,s,self);
}
CFATTACH_DECL_NEW(ggedac,sizeof(struct ggedac_softc),match,attach,NULL,NULL);
