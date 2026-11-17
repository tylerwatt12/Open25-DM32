/* SPDX-License-Identifier: MIT */
/* Real generated driver and prompt writer, with RAM-backed hardware registers. */
#define PLATFORM_HRC7000_H
#define HRC_DMR_GLUE_H
#define pdFALSE 0
#define portYIELD_FROM_ISR(x) ((void)(x))
#define IRQ_PCM_WR 28
#define IRQ_TIMER_PCM 5
#define TIMER_PCM 5
#include <setjmp.h>
#include <assert.h>
#include <stdio.h>
#include "modem/hrc7000/hrc7000.h"
#include "FreeRTOS.h"
static uint32_t now_us;
static void (*timer_isr)(void);
static unsigned timer_period, timer_acks;
static bool run_one;
static unsigned task_waits;
static jmp_buf task_done;
static void hrc_timer_ack(uint8_t n) { assert(n==TIMER_PCM);timer_acks++; }
static void hrc_timer_periodic(uint8_t n,uint32_t us) { assert(n==TIMER_PCM);timer_period=us; }
static void hrc_timer_stop(uint8_t n) { assert(n==TIMER_PCM);timer_period=0; }
static uint32_t hrc_micros(void) { return now_us; }
static uint32_t hrc_irq_save(void) { return 0; }
static void hrc_irq_restore(uint32_t s) { (void)s; }
static void hrc_irq_enable(unsigned irq,bool on) { (void)irq;(void)on; }
static void hrc_irq_register(unsigned irq,void (*fn)(void)) { if(irq==IRQ_TIMER_PCM)timer_isr=fn; }
static void hrc_barrier(void) { }
static bool hrc_dmr_tx_active(void) { return false; }
static bool tone_active;
bool beep_busy(void) {return tone_active;}
void hrc7000_set_crit(hrc7000_t *d,uint32_t (*a)(void),void (*b)(uint32_t)) { d->crit_enter=a;d->crit_exit=b; }
void hrc7000_audio_bits(hrc7000_t *d,uint32_t set,uint32_t clear) { d->audio_ctl=(d->audio_ctl&~clear)|set; }
void vTaskNotifyGiveFromISR(TaskHandle_t t,BaseType_t *w) { (void)t;(void)w; }
BaseType_t xTaskCreate(void (*fn)(void *),const char *n,unsigned s,void *c,unsigned p,TaskHandle_t *h)
{ (void)fn;(void)n;(void)s;(void)c;(void)p;*h=(void *)1;return pdPASS; }
uint32_t ulTaskNotifyTake(BaseType_t c,TickType_t t)
{ (void)c;(void)t;if(run_one && task_waits++)longjmp(task_done,1);return 0; }
void xTaskNotifyGive(TaskHandle_t t) { (void)t; }
#include "../../build/radiofoundry-p25/platform/hrc7000/audio_pcm.c"
/* Actual board ownership logic; keep modem/source hardware local to the test. */
#define AMP_RADIO 1u
#define AMP_PCM 2u
#define PIN_AMP_EN 10u
static bool pcm_on, amp_enabled;
static unsigned amp_writes, source_changes;
static hrc7000_t *board_modem;
static void hrc_pin_write(unsigned pin,bool on)
{ assert(pin==PIN_AMP_EN);amp_enabled=on;amp_writes++; }
static void speaker_source(void) { source_changes++; }
static void ring_play(void) { hrc_audio_pcm_ring(board_modem,pcm_on); }
#define modem (*board_modem)
#include "../../build/audio-host/board_speaker.inc"
#undef modem
static void enqueue(unsigned serial)
{
    uint8_t next=(uint8_t)((spk_head+1u)%SPK_BLOCKS);assert(next!=spk_tail);
    for(unsigned i=0;i<160;i++)spk_q[spk_head][i]=(int16_t)(serial*160+i);
    spk_loud[spk_head]=true;spk_head=next;
}
/* Run the real producer while the timer consumes queued PCM inside a slow callback. */
static bool change_generation;
static void slow_block(const int16_t *mic,int16_t *spk,void *ctx)
{
    (void)mic;(void)ctx;
    assert(spk==spk_q[spk_head]); /* reserved head, even when full at entry */
    for(unsigned i=0;i<160;i++)spk[i]=(int16_t)(99*160+i);
    for(unsigned i=0;i<12;i++){now_us+=2000;timer_isr();}
    if(change_generation)spk_drop();
}
static void one_block(void)
{
    mic_ready=0;task_waits=0;run_one=true;
    if(!setjmp(task_done))audio_task(NULL);
    run_one=false;
}
int main(void)
{
    uint32_t regs[1024]={0},diag[16];uint8_t buf[4096]={0};
    hrc7000_t chip={.regs=regs,.buf=buf};board_modem=&chip;hrc_audio_pcm_attach(&chip,dm32_speaker_pcm);
    speaker_pcm(true);playback_pace(true);
    for(now_us=0;now_us<1000000;now_us+=2000) {
        if(now_us%20000==0)enqueue(now_us/20000);
        fifo_feed();fifo_feed(); /* before/after callback cannot burst */
    }
    playback_stats(diag);
    assert(diag[2]==250 && diag[5]==4000 && diag[6]==4000 && !diag[7] && !diag[10]);
    assert(!play_clock.late);
    enqueue(50);regs[C7_AUDIO_CONTROL/4]=C7_AUDIO_RING_FULL;
    unsigned pos=spk_pos;fifo_feed();assert(spk_pos==pos && play_diag[3]==1);
    regs[C7_AUDIO_CONTROL/4]=0;fifo_feed();assert(spk_pos==32);
    /* Exact little-endian samples, with no skipped chunk on a full refusal. */
    assert(buf[C7_PROMPT_FIFO]==(8000&255) && buf[C7_PROMPT_FIFO+1]==(8000>>8));
    fifo_held=true;now_us+=4000;fifo_feed();assert(spk_pos==32);fifo_held=false;
    tx_gated=true;fifo_feed();assert(spk_pos==32);tx_gated=false;
    fifo_feed();assert(spk_pos==64);
    now_us+=40000;fifo_feed();assert(play_clock.late==1);
    unsigned n=play_diag[2];fifo_feed();assert(play_diag[2]==n);
    spk_drop();playback_pace(false);enqueue(0);enqueue(1);fifo_feed();
    playback_stats(diag);assert(diag[2]==10 && diag[7]==9 && !diag[10]);
    /* Automatic muting leaves routing/queues intact and waits for the actual
     * final loud chunk's bounded FIFO tail. Idle polls cause no GPIO writes. */
    spk_drop();loud_recent=false;playback_pace(true);speaker_pcm(true);
    assert(pcm_on && spk_routed && !amp_enabled && !hrc_audio_pcm_amp_wanted());
    unsigned writes=amp_writes, sources=source_changes, generation=spk_gen;
    for(unsigned i=0;i<100;i++){now_us+=2000;fifo_feed();}
    assert(amp_writes==writes && source_changes==sources && spk_gen==generation);
#if CONFIG_BEEP
    tone_active=true;fifo_feed();assert(amp_enabled&&!hrc_audio_pcm_spk_pending());
    writes=amp_writes;for(unsigned i=0;i<100;i++){now_us+=2000;fifo_feed();}
    assert(amp_enabled&&amp_writes==writes); /* tone lead/gap does not click the amplifier */
    tone_active=false;fifo_feed();assert(!amp_enabled);
#endif
    enqueue(2);speaker_pcm(true);
    assert(amp_enabled && spk_gen==generation && spk_head!=spk_tail);
    for(unsigned i=0;i<5;i++){now_us+=4000;fifo_feed();assert(amp_enabled);}
    assert(spk_head==spk_tail);
    now_us=loud_pushed+31999;fifo_feed();assert(amp_enabled);
    now_us++;fifo_feed();assert(!amp_enabled && pcm_on && spk_routed && spk_gen==generation);
    writes=amp_writes;
    /* Silence still clocks through the FIFO but cannot wake the amplifier. */
    memset(spk_q[spk_head],0,sizeof spk_q[spk_head]);spk_loud[spk_head]=false;
    spk_head=(uint8_t)((spk_head+1u)%SPK_BLOCKS);
    for(unsigned i=0;i<5;i++){now_us+=4000;fifo_feed();}
    assert(!amp_enabled && amp_writes==writes && spk_head==spk_tail);
    enqueue(3);now_us+=4000;fifo_feed();assert(amp_enabled);
    fifo_held=true;unsigned before=spk_pos;fifo_feed();assert(!amp_enabled && spk_pos==before);
    fifo_held=false;now_us+=4000;fifo_feed();assert(amp_enabled);
    tx_gated=true;before=spk_pos;fifo_feed();assert(!amp_enabled && spk_pos==before);
    tx_gated=false;now_us+=4000;fifo_feed();assert(amp_enabled);
    /* FM owns the same physical amplifier independently of paced PCM. */
    amp(AMP_RADIO,true);chip.audio_ctl|=C7_AUDIO_FM_PLAY;
    spk_drop();loud_recent=false;generation=spk_gen;sources=source_changes;
    fifo_feed();assert(amp_enabled && !hrc_audio_pcm_amp_wanted());
    assert((chip.audio_ctl&C7_AUDIO_FM_PLAY) && pcm_on && spk_routed);
    assert(spk_gen==generation && source_changes==sources);
    amp(AMP_RADIO,false);assert(!amp_enabled);
    /* FIFO-tail subtraction remains correct across the 32-bit clock wrap. */
    playback_pace(true);now_us=UINT32_MAX-10000u;enqueue(4);fifo_feed();assert(amp_enabled);
    spk_drop();now_us=loud_pushed+31999u;fifo_feed();assert(amp_enabled);
    now_us++;fifo_feed();assert(!amp_enabled);
    playback_pace(false);assert(amp_enabled); /* legacy beeps keep their route */
    speaker_pcm(false);assert(!amp_enabled && !pcm_on && !spk_routed);
    /* Ownership gates must silence the amp synchronously, before a task tick. */
    spk_drop();loud_recent=false;playback_pace(true);speaker_pcm(true);enqueue(5);fifo_feed();assert(amp_enabled);
    hrc_audio_pcm_hold(true);assert(!amp_enabled);
    hrc_audio_pcm_hold(false);assert(amp_enabled);
    hrc_audio_pcm_tx_gate();assert(!amp_enabled && tx_gated && spk_head==spk_tail);
    now_us+=GATE_MIN_US;gate_check();fifo_feed();assert(!tx_gated && !amp_enabled);
    /* Timer lifecycle and refill continue through a blocked callback. A queue
     * full at entry can publish once the timer makes room, without a discard. */
    tx_gated=false;fifo_held=false;loud_recent=false;
    assert(start(slow_block,NULL) && timer_isr && timer_period==2000);
    playback_pace(true);speaker_pcm(true);
    for(unsigned i=0;i<SPK_BLOCKS-1;i++)enqueue(i);
    assert((spk_head+1u)%SPK_BLOCKS==spk_tail);
    unsigned callbacks=blocks;
    one_block();
    assert(blocks==callbacks+1 && timer_acks==12);
    assert(play_diag[2]==6 && play_diag[6]==4000 && !play_diag[8] && !play_diag[9]);
    assert(spk_head==0 && spk_tail==1 && spk_pos==32);
    assert(spk_q[SPK_BLOCKS-1][0]==99*160 && spk_q[1][0]==160);
    /* Gate/route generation changes during cb must reject its stale samples. */
    change_generation=true;one_block();
    assert(spk_head==spk_tail && play_diag[9]==1);
    stop();assert(!running && !timer_period);
    puts("Real PCM driver: continuous 8 kHz, full retry/sample order, hold/TX gates, missed deadline, legacy A/B PASS");
}
