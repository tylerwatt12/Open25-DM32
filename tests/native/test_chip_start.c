/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "hrc_native_chip.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Observe the real chip setup's MMIO writes without adding a firmware hook. */
typedef struct { uint32_t reg,value; } write_event;
static write_event writes[160];
static unsigned write_count,irq_on_count,requests,keys;
static unsigned reset_reads,release_after=3;
static int reset_pending;
static uint8_t first[NT_BYTES];
static uint32_t register_read(hrc7000_t *m,uint32_t reg)
{
    if (reg==C7_SYS_SOFT_RSTN && reset_pending) {
        reset_reads++;
        if (release_after && reset_reads==release_after) {
            m->regs[reg/4]|=2u;
            reset_pending=0;
        }
    }
    return hrc7000_rd(m,reg);
}
static void register_write(hrc7000_t *m,uint32_t reg,uint32_t value)
{
    assert(write_count<sizeof writes/sizeof *writes);
    writes[write_count++]=(write_event){reg,value};
    assert(reg!=C7_AUDIO_BUFFER_CLR); /* raw P25 does not reset the DMR codec buffers */
    if (reg==C7_SYS_SOFT_RSTN) {
        /* No CPU, audio, ADC, codec, FM, protocol or reserved bit changes. */
        assert((value&~2u)==(hrc7000_rd(m,reg)&~2u));
        reset_pending=!(value&2u);
    }
    if (reg==C7_LAYER2_TXRX_CTRL && value==0x80) {
        assert(irq_on_count==1);
        assert(!memcmp((const void *)m->buf,first,NT_BYTES));
        assert(!memcmp((const void *)(m->buf+NT_BYTES),first,NT_BYTES));
    }
    hrc7000_wr(m,reg,value);
}
#define hrc7000_rd register_read
#define hrc7000_wr register_write
#include "../../integration/radiofoundry/native/hrc_native_chip.c"
#undef hrc7000_wr
#undef hrc7000_rd

void hrc7000_audio_bits(hrc7000_t *m,uint32_t set,uint32_t clear)
{ (void)m;(void)set;(void)clear; }
static int prepare(void *p,const nt_config *c) { (void)p;(void)c;return 1; }
static void noop(void *p) { (void)p; }
static void request(void *p) { (void)p;requests++; }
static void key_pa(void *p) { (void)p;keys++; }
static int irqs(void *p,nt_test *t)
{
    (void)p;(void)t;
    assert(write_count<sizeof writes/sizeof *writes);
    writes[write_count++]=(write_event){UINT32_MAX,0};
    irq_on_count++;
    return 1;
}
static int restore_hook(void *p) { (void)p;return 1; }
static unsigned occurrences(unsigned reg,uint32_t value)
{
    unsigned n=0;
    for(unsigned i=0;i<write_count;i++)n+=writes[i].reg==reg && writes[i].value==value;
    return n;
}
static unsigned position(unsigned reg,uint32_t value)
{
    for(unsigned i=0;i<write_count;i++)
        if(writes[i].reg==reg && writes[i].value==value)return i;
    assert(0);return 0;
}
static void setup_order(unsigned mode,unsigned p25,int audio,int channel)
{
    int live=mode==NT_TRANSMIT && audio;
    assert(!occurrences(C7_AUDIO_BUFFER_CLR,3));
    assert(position(C7_WORK_MODE,2)<position(UINT32_MAX,0));
    if (p25 && audio && (mode==NT_TRANSMIT || channel)) {
        assert(occurrences(C7_SYS_SOFT_RSTN,0x5a5a01fdu)==1 && reset_reads==release_after);
        unsigned at=position(C7_SYS_SOFT_RSTN,0x5a5a01fdu);
        assert(position(C7_LAYER2_TXRX_CTRL,0)<at && position(C7_LAYER2_CONTROL,0)<at);
        assert(position(C7_WORK_MODE,C7_WORK_MODE_FM)<at && at<position(C7_WORK_MODE,2));
    } else {
        for(unsigned i=0;i<write_count;i++)assert(writes[i].reg!=C7_SYS_SOFT_RSTN);
        assert(!reset_reads);
    }
    assert(!occurrences(C7_LAYER2_TXRX_CTRL,(1u<<23)|(1u<<22)));
    if (p25) {
        uint32_t pulse=1u<<23;
        assert(occurrences(C7_LAYER2_TXRX_CTRL,pulse)==1);
        unsigned at=position(C7_LAYER2_TXRX_CTRL,pulse);
        assert(at+1<write_count && writes[at+1].reg==C7_LAYER2_TXRX_CTRL && !writes[at+1].value);
        assert(at<position(UINT32_MAX,0));
    } else {
        assert(!occurrences(C7_LAYER2_TXRX_CTRL,1u<<23));
        assert(!occurrences(C7_LAYER2_TXRX_CTRL,(1u<<23)|(1u<<22)));
    }
    assert(occurrences(C7_LAYER2_TXRX_CTRL,0x80)==(unsigned)live);
    if(live)assert(position(UINT32_MAX,0)<position(C7_LAYER2_TXRX_CTRL,0x80));
    assert(!requests && !keys); /* reset/prime cannot start the PA sequence */
}
int main(void)
{
    uint32_t regs[0x600/4]={0};uint8_t buf[0x1000];
    regs[C7_SYS_SOFT_RSTN/4]=0x5a5a01ffu;
    hrc7000_t modem={.regs=regs,.buf=buf};hn_chip chip;nt_test test;nt_ops ops;
    hn_hooks hooks={NULL,prepare,noop,request,key_pa,irqs,noop,noop,restore_hook,NULL,NULL};
    hn_init(&chip,&modem,&hooks,&ops);nt_init(&test,&ops);
    /* The second live call starts after receive and a previous on-air payload. */
    for(unsigned call=0;call<2;call++) {
        memset(first,0x77+call,sizeof first);memset(buf,0xcc,sizeof buf);
        write_count=irq_on_count=requests=keys=reset_reads=0;
        assert(nt_arm_live(&test,145500000));
        assert(nt_load(&test,test.session,0,first) && nt_load(&test,test.session,1,first));
        assert(nt_begin(&test,test.session,call*100000));
        setup_order(NT_TRANSMIT,1,1,1);
        assert(regs[C7_SYS_SOFT_RSTN/4]==0x5a5a01ffu && !reset_pending);
        assert(!test.stats.tx_sent && test.tx_count==2 && test.early_armed);
        nt_pa_ready(&test,call*100000+100);assert(!keys);
        hn_rftx(&chip,call*100000+1000);assert(requests==1 && !keys);
        nt_pa_ready(&test,call*100000+29000);assert(keys==1 && test.stats.pa_keys==1);
        assert(nt_stop(&test,test.session,NT_USER_STOP) && nt_cleanup(&test));
        write_count=irq_on_count=requests=keys=reset_reads=0;
        assert(nt_arm_live_rx(&test,145500000) && nt_begin(&test,test.session,call*100000+40000));
        setup_order(NT_RECEIVE,1,1,1);
        assert(regs[C7_LAYER2_TXRX_CTRL/4]==0x41);
        nt_pa_ready(&test,call*100000+40100);
        for(unsigned record=0;record<2;record++) {
            regs[C7_SYS_INTERP_LIST/4]=(1u<<11)|(1u<<12);
            hn_sys(&chip,call*100000+41000+record*30000);
            hn_slot(&chip,NT_IRQ_RX_SLOT,call*100000+41000+record*30000);
            nt_record received;assert(nt_pop(&test,test.session,&received));
        }
        assert(test.state==NT_RX && test.stats.rx_seen==2 && !requests && !keys && !test.keyed);
        assert(nt_stop(&test,test.session,NT_USER_STOP) && nt_cleanup(&test));
    }
    /* Raw TX waits for its slot; no codec-buffer reset is introduced.
     * Raw non-P25 does not acquire the P25 sync-release setup. */
    for(unsigned p25=0;p25<2;p25++) {
        nt_config c={145500000,2,2000,250,NT_TRANSMIT,p25,0,0};
        write_count=irq_on_count=requests=keys=reset_reads=0;memset(first,0x55,sizeof first);
        assert(nt_arm(&test,&c,1));
        assert(nt_load(&test,test.session,0,first) && nt_load(&test,test.session,1,first));
        assert(nt_begin(&test,test.session,400000));setup_order(NT_TRANSMIT,p25,0,0);
        assert(!test.stats.tx_sent && !test.early_armed && !regs[C7_LAYER2_TXRX_CTRL/4]);
        hn_slot(&chip,NT_IRQ_TX_SLOT,401000);assert(test.stats.tx_sent==1 && !keys && !requests);
        assert(!occurrences(C7_AUDIO_BUFFER_CLR,3)); /* never clear after staging */
        assert(nt_stop(&test,test.session,NT_USER_STOP) && nt_cleanup(&test));
    }
    /* Live audio without P25 sync also retains the old startup path. */
    write_count=irq_on_count=requests=keys=reset_reads=0;
    assert(nt_arm_live(&test,145500000));test.config.p25_sync=0;
    assert(nt_load(&test,test.session,0,first) && nt_load(&test,test.session,1,first));
    assert(nt_begin(&test,test.session,500000));setup_order(NT_TRANSMIT,0,1,1);
    assert(nt_stop(&test,test.session,NT_USER_STOP) && nt_cleanup(&test));
    /* Raw RX, non-P25 channel audio and timed voice RX keep their startup. */
    for(unsigned path=0;path<3;path++) {
        write_count=irq_on_count=requests=keys=reset_reads=0;
        if(path==0) {
            nt_config c={145500000,2,2000,250,NT_RECEIVE,1,0,0};
            assert(nt_arm(&test,&c,0));
        } else if(path==1) {
            assert(nt_arm_live_rx(&test,145500000));test.config.p25_sync=0;
        } else assert(nt_arm_voice(&test,145500000,1000));
        assert(nt_begin(&test,test.session,550000));
        setup_order(NT_RECEIVE,path!=1,path!=0,0);
        assert(!requests && !keys && !test.keyed);
        assert(nt_stop(&test,test.session,NT_USER_STOP) && nt_cleanup(&test));
    }
    /* A stuck reset is bounded, restores the complete original word and
     * cannot install IRQ ownership, prime data or admit a delayed PA. */
    for(unsigned rx=0;rx<2;rx++) {
        write_count=irq_on_count=requests=keys=reset_reads=0;release_after=0;
        if(test.state==NT_FAULT)assert(nt_reset(&test));
        if(rx)assert(nt_arm_live_rx(&test,145500000));
        else {
            assert(nt_arm_live(&test,145500000));
            assert(nt_load(&test,test.session,0,first) && nt_load(&test,test.session,1,first));
        }
        assert(!nt_begin(&test,test.session,600000));
        assert(reset_reads==1024 && regs[C7_SYS_SOFT_RSTN/4]==0x5a5a01ffu && !reset_pending);
        assert(occurrences(C7_SYS_SOFT_RSTN,0x5a5a01fdu)==1 && occurrences(C7_SYS_SOFT_RSTN,0x5a5a01ffu)==1);
        assert(!irq_on_count && !occurrences(C7_WORK_MODE,2) && !occurrences(C7_LAYER2_TXRX_CTRL,0x80));
        assert(test.state==NT_FAULT && test.reason==NT_IO && !requests && !keys);
        nt_rf_tx(&test,601000);nt_pa_ready(&test,629000);
        assert(!requests && !keys && nt_cleanup(&test) && !test.owned);
        assert(regs[C7_SYS_SOFT_RSTN/4]==0x5a5a01ffu);
    }
    puts("PASS native chip startup: P25 TX/channel RX PHY reset, preserved modules, bounded timeout/restore, timed/raw RX unchanged, RX cannot key PA");
    return 0;
}
