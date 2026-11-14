/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Synthetic independent IRQ phases through the real controller. */
#include "native_test.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static nt_test test;
static unsigned writes, requests, keys, quiets, unlocks, stop_in_start, stop_after_stage, fail_write;
static uint8_t sent[16][NT_BYTES];
static int start(void *ctx,nt_test *t)
{
    (void)ctx;
    if(stop_in_start)nt_stop(t,t->session,NT_USER_STOP);
    return 1;
}
static int write_block(void *ctx,const uint8_t raw[NT_BYTES])
{
    (void)ctx;assert(writes<16);memcpy(sent[writes++],raw,NT_BYTES);return !fail_write;
}
static void request(void *ctx){(void)ctx;requests++;}
static void pa(void *ctx){(void)ctx;keys++;}
static void quiet(void *ctx){(void)ctx;quiets++;}
static int restore(void *ctx){(void)ctx;return 1;}
static uint32_t lock(void *ctx){(void)ctx;return 0;}
static void unlock(void *ctx,uint32_t prior)
{
    (void)ctx;(void)prior;
    /* STOP can run as soon as the staging critical section releases. */
    if(++unlocks==2 && stop_after_stage)nt_stop(&test,test.session,NT_USER_STOP);
}
static const nt_ops ops={.start=start,.write=write_block,.cancel_send=quiet,.request_key=request,
    .pa_on=pa,.quiet=quiet,.restore=restore,.lock=lock,.unlock=unlock};
static void load(void)
{
    uint8_t raw[NT_BYTES];memset(raw,(int)(0x77+test.stats.tx_loaded),sizeof raw);
    assert(nt_load(&test,test.session,test.stats.tx_loaded,raw));
}
static void fresh(void)
{
    writes=requests=keys=quiets=unlocks=stop_in_start=stop_after_stage=fail_write=0;
    nt_init(&test,&ops);assert(nt_arm_live(&test,145500000));
    for(unsigned i=0;i<3;i++)load();
}
static void phase(int phy_first)
{
    fresh();assert(nt_begin(&test,test.session,0));
    assert(test.early_armed && writes==1 && !requests && !keys);
    assert(test.tx_tail==0 && test.tx_count==3 && !test.stats.tx_sent);
    nt_pa_ready(&test,500);assert(!keys); /* A preload alone cannot key PA. */
    if(phy_first){
        nt_boundary(&test,NT_IRQ_PHY,20000);
        assert(test.tx_tail==0 && test.tx_count==3 && !test.stats.tx_sent && writes==1);
        nt_rf_tx(&test,21000); /* A real RF-start before the first slot is now admissible. */
        assert(requests==1 && !keys);
        nt_boundary(&test,NT_IRQ_TX_SLOT,24000);
    }else{
        nt_boundary(&test,NT_IRQ_TX_SLOT,7000);
        assert(!requests && !keys && !test.stats.rf_irqs);
        nt_boundary(&test,NT_IRQ_PHY,33000);
        nt_rf_tx(&test,36000);
        assert(requests==1 && !keys);
    }
    assert(test.stats.tx_sent==1 && test.tx_tail==1 && test.tx_count==2 && writes==2);
    assert(!memcmp(sent[0],sent[1],NT_BYTES)); /* The preloaded lead is consumed once. */
    nt_pa_ready(&test,phy_first?49000:64000);assert(keys==1);
    nt_stop(&test,test.session,NT_USER_STOP);
    assert(!test.early_armed && !test.key_requested && !test.keyed && !test.tx_count);
    nt_rf_tx(&test,66000);nt_pa_ready(&test,94000);assert(requests==1 && keys==1);
    assert(nt_cleanup(&test));
}
static void real_rf_and_lead_timing(void)
{
    fresh();
    for(unsigned i=0;i<3;i++)memset(test.tx[i].raw,0x77,NT_BYTES);
    uint8_t opening[NT_BYTES];memset(opening,0xa5,sizeof opening);
    assert(nt_load(&test,test.session,3,opening));
    assert(nt_begin(&test,test.session,0));
    nt_boundary(&test,NT_IRQ_PHY,1000);nt_boundary(&test,NT_IRQ_RX_SLOT,2000);
    nt_pa_ready(&test,3000);assert(!requests && !keys && writes==1);
    nt_rf_tx(&test,6000);assert(requests==1 && !test.stats.tx_sent);
    nt_boundary(&test,NT_IRQ_TX_SLOT,10000);
    assert(requests==1 && !keys && test.stats.tx_sent==1 && test.stats.rf_irqs==1);
    nt_pa_ready(&test,34000);assert(keys==1 && test.stats.pa_keys==1);
    nt_boundary(&test,NT_IRQ_TX_SLOT,40000);
    nt_boundary(&test,NT_IRQ_TX_SLOT,70000);
    assert(writes==4 && requests==1 && test.stats.rf_irqs==1);
    for(unsigned i=0;i<4;i++)for(unsigned j=0;j<NT_BYTES;j++)assert(sent[i][j]==0x77);
    nt_boundary(&test,NT_IRQ_TX_SLOT,100000);
    assert(writes==5 && !memcmp(sent[4],opening,NT_BYTES));
    /* The normal 28 ms PA delay ends 66 ms before the opening CAI is written. */
    assert(100000-34000>=66000 && requests==1 && keys==1);
    nt_rf_tx(&test,101000);nt_pa_ready(&test,129000);
    assert(test.stats.rf_irqs==2 && requests==1 && keys==1); /* actual IRQ, no second timer */
    nt_stop(&test,test.session,NT_USER_STOP);assert(nt_cleanup(&test));
}
static void missing_rf(void)
{
    fresh();assert(nt_begin(&test,test.session,0));
    for(unsigned i=0;i<6;i++) {
        uint32_t now=7000+i*30000;
        nt_boundary(&test,NT_IRQ_RX_SLOT,now-200);
        nt_boundary(&test,NT_IRQ_PHY,now-100);
        nt_boundary(&test,NT_IRQ_TX_SLOT,now);
        nt_pa_ready(&test,now+100);
        assert(test.stats.tx_sent==i+1 && !test.key_requested && !requests && !keys && !test.stats.rf_irqs);
        load();
    }
    nt_tick(&test,217001);nt_pa_ready(&test,217002);
    assert(test.state==NT_FAULT && test.reason==NT_STALLED && !keys && nt_cleanup(&test));
}
static void interruption(void)
{
    fresh();stop_in_start=1;assert(!nt_begin(&test,test.session,0));
    assert(test.state==NT_STOPPED && !writes && !test.early_armed && !keys && quiets>=1);
    assert(nt_cleanup(&test));
    fresh();stop_after_stage=1;assert(nt_begin(&test,test.session,0));
    assert(writes==1 && test.state==NT_STOPPED && !test.early_armed && !test.tx_count);
    nt_boundary(&test,NT_IRQ_TX_SLOT,1000);
    nt_rf_tx(&test,2000);nt_pa_ready(&test,30000);assert(!requests && !keys);assert(nt_cleanup(&test));
    fresh();fail_write=1;assert(!nt_begin(&test,test.session,0));
    assert(test.state==NT_FAULT && test.reason==NT_IO && !test.early_armed && quiets);
    nt_rf_tx(&test,1000);nt_pa_ready(&test,29000);assert(!requests && !keys);assert(nt_cleanup(&test));
    fresh();assert(nt_begin(&test,test.session,0));fail_write=1;
    nt_boundary(&test,NT_IRQ_TX_SLOT,7000);
    assert(test.state==NT_FAULT && test.reason==NT_IO && !test.stats.tx_sent && !requests);
    nt_pa_ready(&test,35000);assert(!keys && nt_cleanup(&test));
    fresh();assert(nt_begin(&test,test.session,0));test.tx[test.tx_tail].index=1;
    nt_boundary(&test,NT_IRQ_TX_SLOT,7000);
    assert(test.reason==NT_IO && writes==1 && !requests && nt_cleanup(&test));
    fresh();assert(nt_begin(&test,test.session,0));
    nt_boundary(&test,NT_IRQ_TX_SLOT,7000);assert(!requests);
    nt_rf_tx(&test,7400);assert(requests==1);
    nt_stop(&test,test.session,NT_USER_STOP);nt_pa_ready(&test,35000);
    assert(!keys && !test.key_requested && nt_cleanup(&test));
    fresh();assert(nt_begin(&test,test.session,0));
    nt_boundary(&test,NT_IRQ_TX_SLOT,250001);nt_pa_ready(&test,278001);
    assert(test.reason==NT_LEASE && writes==1 && !requests && !keys && nt_cleanup(&test));
    fresh();assert(nt_begin(&test,test.session,0));
    nt_boundary(&test,NT_IRQ_TX_SLOT,7000);assert(!requests);
    nt_rf_tx(&test,7400);assert(requests==1);
    nt_abnormal(&test);nt_pa_ready(&test,35000);
    assert(test.state==NT_FAULT && test.reason==NT_ABNORMAL && !keys && nt_cleanup(&test));
}
static void lease_and_other_modes(void)
{
    fresh();assert(nt_begin(&test,test.session,0));
    for(unsigned i=0;i<7;i++){nt_boundary(&test,NT_IRQ_TX_SLOT,10000+i*30000);load();}
    nt_rf_tx(&test,200000);assert(requests==1 && !keys);
    nt_pa_ready(&test,250001);assert(test.state==NT_FAULT && test.reason==NT_LEASE && !keys && !test.early_armed);
    assert(nt_cleanup(&test));
    assert(nt_reset(&test));
    nt_config raw={145500000,3,2000,250,NT_TRANSMIT,1,0,0};
    assert(nt_arm(&test,&raw,1));load();load();unsigned count=writes;
    assert(nt_begin(&test,test.session,300000));assert(writes==count && !test.early_armed);
    nt_rf_tx(&test,300500);nt_pa_ready(&test,328500);assert(requests==1 && !keys);
    nt_boundary(&test,NT_IRQ_PHY,330000);nt_rf_tx(&test,331000);assert(requests==2);
    nt_stop(&test,test.session,NT_USER_STOP);assert(nt_cleanup(&test));
    raw.mode=NT_RECEIVE;assert(nt_arm(&test,&raw,0));count=writes;
    assert(nt_begin(&test,test.session,400000));assert(writes==count && !test.early_armed);
    nt_rf_tx(&test,401000);assert(test.reason==NT_UNEXPECTED_RF && requests==2 && !keys);
}
int main(void)
{
    phase(1);phase(0);real_rf_and_lead_timing();missing_rf();interruption();lease_and_other_modes();
    puts("PASS live P25 startup: genuine RF required, early priming and three-lead timing, no key from slots alone, write/STOP/lease-safe PA");
    return 0;
}
