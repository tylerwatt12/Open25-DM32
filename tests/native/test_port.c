/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "hrc_native_port.h"
#include "platform.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
/* Array MMIO models the documented self-release; production chip code stays
 * unchanged and the actual interrupt port/timer handlers run below. */
static uint32_t register_read(hrc7000_t *m,uint32_t reg)
{
    if (reg==C7_SYS_SOFT_RSTN && !(m->regs[reg/4]&2u))m->regs[reg/4]|=2u;
    return hrc7000_rd(m,reg);
}
#define hrc7000_rd register_read
#include "../../integration/radiofoundry/native/hrc_native_chip.c"
#undef hrc7000_rd
hrc_irq_fn hrc_irq_table[64];
uint32_t mock_pic[2],mock_tmr[7];
uint32_t mock_pic_history;
static uint32_t clock_us,depth,period,pa_due;
static unsigned acks[7],offs,keys,requests;
uint32_t hrc_irq_save(void) { return depth++; }
void hrc_irq_restore(uint32_t prior) { assert(depth==prior+1);depth=prior; }
void hrc_irq_enable(uint8_t n,bool on) { uint32_t bit=1u<<(n&31); if(on)mock_pic[n/32]&=~bit;else mock_pic[n/32]|=bit; }
void hrc_irq_register(uint8_t n,hrc_irq_fn fn) { hrc_irq_enable(n,false);hrc_irq_table[n]=fn; }
void hrc_timer_ack(unsigned n) { acks[n]++; }
void hrc_timer_stop(unsigned n) { mock_tmr[n]=0; }
void hrc_timer_periodic(unsigned n,uint32_t us) { assert(n==6);period=us;mock_tmr[n]=3; }
uint32_t hrc_micros(void) { return clock_us; }
void hrc7000_audio_bits(hrc7000_t *m,uint32_t set,uint32_t clear) { (void)m;(void)set;(void)clear; }
static void prior(void) { assert(0); }
static void fast(void *p) { (void)p;offs++; }
/* Same bounded one-shot requested by the board's native_key hook. */
static void request(void *p) { (void)p;requests++;pa_due=clock_us+28000;mock_tmr[TIMER_PA]=1; }
static void pa_hook(void *p) { (void)p;keys++; }
static int prepare(void *p,const nt_config *c) { (void)p;(void)c;return 1; }
static int restore_hook(void *p) { (void)p;return 1; }
static int on(void *p,nt_test *t) { return hn_port_on(p,t); }
static void quiet_hook(void *p) { (void)p;hn_port_quiet(); }
static void reset_irq(void *p) { (void)p;hn_port_restore(); }
static uint32_t lock_hook(void *p) { (void)p;return hrc_irq_save(); }
static void unlock_hook(void *p,uint32_t irq) { (void)p;hrc_irq_restore(irq); }
int main(void)
{
    uint32_t regs[0x600/4]={0};uint8_t buf[0x1000]={0};
    regs[C7_SYS_SOFT_RSTN/4]=0x1ff;
    hrc7000_t modem={.regs=regs,.buf=buf};hn_chip chip;nt_test test;nt_ops ops;
    hn_hooks hooks={&chip,prepare,fast,request,pa_hook,on,quiet_hook,reset_irq,restore_hook,lock_hook,unlock_hook};
    hn_init(&chip,&modem,&hooks,&ops);nt_init(&test,&ops);
    for(unsigned i=0;i<64;i++)hrc_irq_table[i]=prior;
    mock_pic[0]=0xa1234567;mock_pic[1]=0x13579bde;
    uint32_t masks[2]={mock_pic[0],mock_pic[1]};
    mock_tmr[3]=1;assert(!hn_port_on(&chip,&test));mock_tmr[3]=0;
    mock_tmr[6]=1;assert(!hn_port_on(&chip,&test));mock_tmr[6]=0;
    assert(mock_pic[0]==masks[0] && mock_pic[1]==masks[1] && hrc_irq_table[29]==prior);
    nt_config c={145500000,3,2000,250,NT_TRANSMIT,1,0,0};uint8_t raw[36]={0};
    assert(nt_arm(&test,&c,1));assert(nt_load(&test,test.session,0,raw));assert(nt_load(&test,test.session,1,raw));
    assert(nt_begin(&test,test.session,0));assert(period==1000 && mock_tmr[6]==3 && !depth);
    assert(!hn_port_on(&chip,&test));
    clock_us=1000;hrc_irq_table[IRQ_MODEM_TS_TX]();assert(test.stats.tx_sent==1);
    hrc_irq_table[IRQ_MODEM_RF_TX]();assert(test.key_requested);
    hrc_irq_fn delayed_pa=hrc_irq_table[IRQ_TIMER3];
    /* No cable command or radio-task service: the real timer ISR expires it. */
    clock_us=250000;hrc_irq_table[IRQ_TIMER6]();
    assert(test.state==NT_FAULT && test.reason==NT_LEASE && offs==1 && acks[6]==1);
    assert((mock_pic[0]&(1u<<IRQ_TIMER3)) && !(mock_pic[0]&(1u<<IRQ_TIMER6)));
    delayed_pa();assert(!keys && !mock_tmr[3] && acks[3]==1);
    assert(nt_cleanup(&test) && !test.owned && !mock_tmr[6]);
    assert(mock_pic[0]==masks[0] && mock_pic[1]==masks[1]);
    for(unsigned i=0;i<64;i++)assert(hrc_irq_table[i]==prior);
    hn_port_restore();delayed_pa();assert(!keys && !depth); /* stale callback after restored context */

    uint8_t next[36];memset(next,0xa5,sizeof next);
    clock_us=300000;
    assert(nt_reset(&test));
    assert(nt_arm_live(&test,145500000));
    assert(nt_load(&test,test.session,0,raw) && nt_load(&test,test.session,1,next));
    assert(nt_load(&test,test.session,2,raw) && nt_begin(&test,test.session,clock_us));
    assert(test.early_armed && !test.stats.tx_sent && !memcmp(buf,raw,36) && !memcmp(buf+36,raw,36));
    assert(regs[C7_AUDIO_BUFFER_CLR/4]==0 && !keys && !test.key_requested);
    regs[C7_SYS_INTERP_LIST/4]=1u<<12;
    clock_us=329000;hrc_irq_table[IRQ_MODEM_SYS]();
    assert(!test.stats.tx_sent && regs[C7_LAYER2_TXRX_CTRL/4]==0x80);
    assert(!test.key_requested && requests==1 && !keys && !mock_tmr[TIMER_PA]);
    clock_us=330000;hrc_irq_table[IRQ_MODEM_RF_TX]();
    assert(test.key_requested && !test.stats.tx_sent && requests==2 && pa_due==358000);
    clock_us=333000;hrc_irq_table[IRQ_MODEM_TS_TX]();
    assert(test.stats.tx_sent==1 && regs[C7_LAYER2_TXRX_CTRL/4]==0x80 && test.key_requested);
    assert(requests==2 && !keys && test.stats.rf_irqs==1 && mock_tmr[TIMER_PA]);
    regs[C7_LAYER2_TXRX_CTRL/4]=0; /* fake the hardware consuming the send strobe */
    clock_us=333500;hrc_irq_table[IRQ_MODEM_SYS]();
    assert(!memcmp(buf,raw,36)); /* same-slot event must not replace active payload */
    clock_us=pa_due;hrc_irq_table[IRQ_TIMER3]();
    assert(keys==1 && test.stats.pa_keys==1 && test.stats.rf_irqs==1 && !mock_tmr[TIMER_PA]);
    clock_us=358500;hrc_irq_table[IRQ_MODEM_RF_TX]();
    assert(test.stats.rf_irqs==2 && requests==2 && !mock_tmr[TIMER_PA]);
    clock_us=359000;hrc_irq_table[IRQ_MODEM_SYS]();
    assert(test.stats.tx_sent==1 && regs[C7_LAYER2_TXRX_CTRL/4]==0);
    assert(!memcmp(buf,next,4) && !memcmp(buf+36,next,4));
    assert(!memcmp(buf+4,raw+4,32) && !memcmp(buf+40,raw+4,32));
    clock_us=363000;hrc_irq_table[IRQ_MODEM_TS_TX]();
    assert(test.stats.tx_sent==2 && regs[C7_LAYER2_TXRX_CTRL/4]==0x80);
    assert(!memcmp(buf,next,36) && !memcmp(buf+36,next,36));
    clock_us=393000;hrc_irq_table[IRQ_MODEM_TS_TX](); /* no PHY advance */
    assert(test.state==NT_FAULT && test.reason==NT_IO && test.stats.tx_sent==2);
    assert(regs[C7_LAYER2_TXRX_CTRL/4]==0 && nt_cleanup(&test));
    uint32_t trace[NT_TRACE_WORDS];nt_trace_summary(trace);
    assert(trace[0]==2 && trace[24]==359000 && trace[25]==1);
    /* Accepted slots alone do not key. A genuine RF IRQ arms the delay;
     * STOP cancels it, including dispatched or restored stale callbacks. */
    assert(nt_reset(&test));clock_us=500000;
    assert(nt_arm_live(&test,145500000));
    assert(nt_load(&test,test.session,0,raw) && nt_load(&test,test.session,1,next));
    assert(nt_begin(&test,test.session,clock_us));
    clock_us=507000;hrc_irq_table[IRQ_MODEM_TS_TX]();
    assert(requests==2 && !test.key_requested && !mock_tmr[TIMER_PA]);
    clock_us=508000;hrc_irq_table[IRQ_MODEM_RF_TX]();assert(requests==3 && mock_tmr[TIMER_PA]);
    delayed_pa=hrc_irq_table[IRQ_TIMER3];
    clock_us=510000;nt_stop(&test,test.session,NT_USER_STOP);assert(!mock_tmr[TIMER_PA]);
    clock_us=536000;delayed_pa();assert(keys==1 && !test.stats.pa_keys);
    assert(nt_cleanup(&test));delayed_pa();assert(keys==1 && !depth);
    /* A freshly reset channel RX keeps the same real IRQ/lease protections.
     * Neither receive slots nor a delayed Timer3 callback can request PA. */
    unsigned prior_requests=requests,prior_keys=keys;
    for(unsigned call=0;call<2;call++) {
        clock_us=1000000+call*1000000;
        assert(nt_arm_live_rx(&test,145500000) && nt_begin(&test,test.session,clock_us));
        assert(test.state==NT_RX && regs[C7_LAYER2_TXRX_CTRL/4]==0x41 && !mock_tmr[TIMER_PA]);
        assert(regs[C7_SYS_SOFT_RSTN/4]==0x1ff);
        delayed_pa=hrc_irq_table[IRQ_TIMER3];
        regs[C7_SYS_INTERP_LIST/4]=(1u<<11)|(1u<<12)|(1u<<16);
        clock_us+=1000;hrc_irq_table[IRQ_MODEM_SYS]();hrc_irq_table[IRQ_MODEM_TS_RX]();delayed_pa();
        nt_record received;assert(nt_pop(&test,test.session,&received));
        assert(test.state==NT_RX && test.stats.rx_seen==1 && test.stats.abnormal_irqs==1);
        assert(!test.key_requested && !test.keyed && requests==prior_requests && keys==prior_keys);
        if(call) {
            clock_us+=499000;hrc_irq_table[IRQ_TIMER6]();
            assert(test.state==NT_FAULT && test.reason==NT_LEASE);
        } else assert(nt_stop(&test,test.session,NT_USER_STOP));
        delayed_pa();assert(keys==prior_keys && !mock_tmr[TIMER_PA]);
        assert(nt_cleanup(&test) && !test.owned && !mock_tmr[6]);
        delayed_pa();assert(keys==prior_keys && !depth);
        assert(mock_pic[0]==masks[0] && mock_pic[1]==masks[1]);
        for(unsigned i=0;i<64;i++)assert(hrc_irq_table[i]==prior);
    }
    puts("PASS real native interrupt port: timer conflicts, independent lease shutdown, delayed PA, exact handler/mask restoration");
    return 0;
}
