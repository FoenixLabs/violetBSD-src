/* SPDX-License-Identifier: BSD-2-Clause */
/* Included only by the explicit diskless RAM_USB_AUDIO_JITTER experiment.
 * All data are public diagnostics, NEVER credited as entropy. No disk I/O.
 * Bracket each observed position transition with the pre-read timestamp of
 * the last unchanged sample and the post-read timestamp of the changed one.
 * These are bounds on register visibility, not direct oscillator edges.
 */
#include <machine/clockrate.h>
#define AJ_N 2048
static struct { uint32_t pos, lo, hi; } aj_rows[AJ_N];

static void __dead
ggedac_jitter(struct ggedac_softc *s)
{
 unsigned clock_hz = a2560me_clock_rate(REG32(0xfec0000c));
 printf("AJ BEGIN v1 build=%u hz=%u n=%u credit=0\n",
     REG32(0xfec0003c), clock_hz, AJ_N);
 if (!clock_hz || !allocm(s, AUMODE_PLAY, RINGSIZE)) {
  printf("AJ FAIL allocation/clock\n");
  goto done;
 }
 /* Silent stream: zero PCM and zero digital volume, no codec tone. */
 wr(s,MASTER,0); wr(s,IRQEN,0);
 for (unsigned run=0; run<6; run++) {
  bool control = run==0 || run==5;
  unsigned pad = run==2 || run==4 ? 16 : 0;
  unsigned step = run==3 || run==4 ? 0x8000 : 0x10000;
  wr(s,CTRL,0); wr(s,EVENT,0xffffffff); wr(s,STATUS,3);
  wr(s,ADDR,s->seg.ds_addr); wr(s,LEN,RINGSIZE/FRAMEBYTES);
  wr(s,LOOP,0); wr(s,STEP,step); wr(s,VOL,0);
  wr(s,VCTRL,STREAM16|1); wr(s,CTRL,1);
  delay(1000);
  unsigned n=0, polls=0;
  int ps=splhigh();
  uint32_t start=REG32(0xfec00210);
  uint32_t before=REG32(0xfec00210)-start;
  uint32_t last=control ? before/1389 : rd(s,POS);
  uint32_t lower=before;
  while (n<AJ_N && polls++<10000000) {
   before=REG32(0xfec00210)-start;
   uint32_t pos=control ? before/1389 : rd(s,POS);
   uint32_t after=REG32(0xfec00210)-start;
   if (after>clock_hz/2) break;
   if (pos!=last) {
    aj_rows[n].pos=pos; aj_rows[n].lo=lower; aj_rows[n].hi=after;
    n++; last=pos;
   }
   lower=before;
   for (unsigned k=0;k<pad;k++) __asm volatile("nop");
  }
  splx(ps);
  unsigned status=rd(s,STATUS), playing=rd(s,PLAYING);
  wr(s,VCTRL,STREAM16|2); delay(6000); wr(s,CTRL,0);
  printf("AJ RUN %u control=%u step=%u pad=%u count=%u polls=%u status=%u playing=%u\n",
      run,control,step,pad,n,polls,status,playing);
  for (unsigned i=0;i<n;i++)
   printf("AJ D %u %u %u %u\n",run,aj_rows[i].pos,aj_rows[i].lo,aj_rows[i].hi);
  printf("AJ END %u\n",run);
 }
 wr(s,CTRL,0); delay(10); wr(s,IRQEN,0); wr(s,EVENT,0xffffffff);
 freem(s,s->buffer,RINGSIZE);
done:
 wr(s,CTRL,0); wr(s,IRQEN,0);
 printf("AJ DONE credit=0; diskless experiment halted; reset to flash kernel\n");
 (void)splhigh();
 for (;;) __asm volatile("stop #0x2700");
}
