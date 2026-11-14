/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "radio/beep.h"
/* Real Classic admission/state machine/native controller; worker and RF are fixtures. */
static nt_test *classic_live;
static bool classic_busy, classic_stop, classic_receiving;
static uint32_t classic_sid;
static unsigned classic_starts, classic_restores;
void mock_enter(void) {}
void mock_exit(void) {}
uint32_t hal_micros(void) { return now*1000u; }
static bool classic_on(void) { return true; }
static uint16_t classic_battery(void) { return 8000; }
int p25_lab_busy(void) { return classic_busy; }
int p25_lab_set_channel(uint16_t n,uint16_t t,uint32_t i)
{ CHECK(n==0x293 && t==42 && i==3101234);return !classic_busy; }
void p25_lab_channel_stop(void) { classic_stop=true; }
int p25_lab_channel_receiving(void) { return classic_receiving; }
int p25_lab_channel_rx(uint32_t hz)
{ classic_sid=native_lab_voice_start(hz,0);classic_busy=classic_sid!=0;classic_stop=false;return classic_busy; }
int p25_lab_live_begin(uint32_t sid) { classic_sid=sid;classic_busy=true;classic_stop=false;return true; }
void p25_lab_live_up(void) { if(classic_live && classic_live->config.mode==NT_TRANSMIT)classic_stop=true; }
static int classic_start(void *ctx,nt_test *t)
{ (void)ctx;classic_live=t;classic_starts++;return true; }
static int classic_write(void *ctx,const uint8_t b[36]) { (void)ctx;(void)b;return true; }
static void classic_noop(void *ctx) { (void)ctx; }
static int classic_restore(void *ctx) { (void)ctx;classic_restores++;return true; }
const nt_ops classic_ops={.start=classic_start,.write=classic_write,.cancel_send=classic_noop,
 .request_key=classic_noop,.pa_on=classic_noop,.quiet=classic_noop,.restore=classic_restore};

static void classic_pump(void)
{
    native_lab_service(&sm,now);
    if(classic_stop && classic_busy) {
        native_lab_voice_stop(classic_sid);native_lab_service(&sm,now);
        CHECK(native_lab_voice_release(classic_sid));classic_busy=classic_stop=false;
    }
}
static void classic_select(bool receive)
{
    setup_codeplug();start();native_lab_init();
    fake_power.switch_on=classic_on;fake_power.battery_mv=classic_battery;
    classic_busy=classic_stop=classic_receiving=false;classic_starts=classic_restores=0;
    sm.view.ch.mode=RADIO_MODE_P25;sm.view.ch.bw=RADIO_BW_12K5;
    sm.view.ch.p25_nac=0x293;sm.view.ch.p25_tg=42;sm.view.ch.p25_id=3101234;
    sm.view.power=2;
    req(RREQ_PTT,1);CHECK(hw.tx_calls==0 && !sm.ptt);
    if(receive) {
        classic_pump();classic_pump();
        CHECK(classic_live && classic_live->state==NT_RX && native_lab_owns());
    }
}
static void classic_press(void)
{
    sm.tx_start_ms=now-197000u;sm.view.tx_elapsed_ms=197000;
    sm.view.tot_s=15;sm.set.tot_warn_s=10;
    sm.tot_warned=true;sm.view.tot_warning=true;
    beep_stats_t prior_beeps,after_beeps;beep_get_stats(&prior_beeps);
    int prior_starts=aud.starts;
    radio_req_t p={.type=RREQ_PTT,.arg=1};CHECK(native_lab_request(&sm,&p)==1);
    CHECK(sm.tx_start_ms==now&&!sm.view.tx_elapsed_ms&&!sm.tot_warned&&!sm.view.tot_warning);
    classic_pump();classic_pump();
    radio_sm_native_maintenance(&sm,now+100);
    beep_get_stats(&after_beeps);
    CHECK(!sm.view.tot_warning&&after_beeps.played==prior_beeps.played&&aud.starts==prior_starts);
    uint8_t raw[36]={0};
    CHECK(native_lab_live_block(classic_sid,raw,0));CHECK(native_lab_live_block(classic_sid,raw,0));
    CHECK(native_lab_live_go(classic_sid));classic_pump();
    CHECK(classic_live->state==NT_TX && classic_live->config.power==2);
}
static void test_classic_ownership(void)
{
    classic_select(true);
    for(unsigned gate=0;gate<6;gate++) {
        channel_t saved=sm.view.ch;
        sm.set.tx_inhibit=gate==0;sm.view.ch.rx_only=gate==1;
        if(gate==2)sm.view.ch.tx_hz=100000000;
        if(gate==3)sm.batt.level=BATT_CRITICAL;
        if(gate==4)sm.view.ch.p25_id=0;
        if(gate==5){sm.set.bcl=RADIO_BCL_CARRIER;sm.view.state=RS_RX;}
        radio_req_t p={.type=RREQ_PTT,.arg=1};
        CHECK(native_lab_request(&sm,&p)==1 && sm.view.state==RS_TX_DENIED);
        CHECK(classic_live->state==NT_RX && classic_starts==1);
        p.arg=0;CHECK(native_lab_request(&sm,&p)==1 && !sm.ptt);
        sm.view.ch=saved;sm.set.tx_inhibit=false;sm.batt.level=BATT_OK;sm.set.bcl=RADIO_BCL_OFF;
    }
    classic_press();
    beep_stats_t prior_beeps,after_beeps;beep_get_stats(&prior_beeps);
    int prior_starts=aud.starts;
    now+=5000;radio_sm_native_maintenance(&sm,now);beep_get_stats(&after_beeps);
    CHECK(sm.view.tot_warning&&sm.tot_warned&&after_beeps.played==prior_beeps.played+1);
    CHECK(aud.starts==prior_starts&&beep_busy()); /* warning cannot replace P25 PCM */
    int16_t local_beep[160]={0};
    for(unsigned i=0;i<30;i++){memset(local_beep,0,sizeof local_beep);beep_mix(local_beep);}
    CHECK(!beep_busy());
    sm.view.tot_s=1;now+=1000;classic_pump();
    CHECK(!classic_live->owned && !classic_live->keyed && sm.view.tx_deny==TXDENY_TIMEOUT);
    radio_req_t up={.type=RREQ_PTT,.arg=0};CHECK(native_lab_request(&sm,&up)==1);
    classic_pump();classic_pump();
    CHECK(classic_live->state==NT_RX);
    radio_req_t change={.type=RREQ_ZONE,.arg=1};
    CHECK(native_lab_request(&sm,&change)==NATIVE_REQUEST_WAIT);
    CHECK(native_lab_owns()); /* normal core stays excluded until the worker releases */
    classic_pump();CHECK(native_lab_request(&sm,&change)==0);
    radio_sm_request(&sm,&change,now);CHECK(!native_lab_owns());
    CHECK(sm.view.ch.mode==RADIO_MODE_FM);
    req(RREQ_PTT,1);CHECK(sm.view.state==RS_TX && hw.tx_calls==1);
    req(RREQ_PTT,0);CHECK(!sm.ptt && sm.view.state==RS_IDLE);
    /* PTT arriving in the same queue batch as selection must never key FM. */
    classic_select(false);classic_press();CHECK(hw.tx_calls==0);
    radio_req_t off={.type=RREQ_POWER_OFF};
    CHECK(native_lab_request(&sm,&off)==NATIVE_REQUEST_WAIT);classic_pump();
    CHECK(native_lab_request(&sm,&off)==0 && !native_lab_owns() && !sm.ptt);
    uint8_t raw_arm[17]={1},reply[512];
    CHECK(native_lab_command(raw_arm,sizeof raw_arm,reply,sizeof reply)==1 && reply[0]!=0);
}
static void test_classic_idle_rearm(void)
{
    classic_select(true);
    uint32_t old=classic_sid;unsigned starts_before=classic_starts;
    /* The RX worker's normal idle exit uses this existing stop/release path. */
    native_lab_voice_stop(old);classic_pump();
    CHECK(!classic_live->owned && classic_live->state==NT_STOPPED && !classic_live->stats.faults);
    CHECK(native_lab_voice_release(old));classic_busy=false;
    classic_pump();classic_pump();
    CHECK(classic_live->state==NT_RX && classic_sid!=old && classic_starts==starts_before+1);
    uint8_t s,r;CHECK(!native_lab_voice_poll(old,&s,&r) && !native_lab_voice_release(old));
    CHECK(classic_live->state==NT_RX && !classic_live->keyed && !hw.tx_calls);

    classic_select(true);old=classic_sid;
    native_lab_voice_stop(old);
    radio_req_t down={.type=RREQ_PTT,.arg=1};
    CHECK(native_lab_request(&sm,&down)==1);
    classic_pump();classic_pump();
    CHECK(classic_sid!=old && classic_live->state==NT_ARMED && classic_live->config.mode==NT_TRANSMIT);
    CHECK(!native_lab_voice_release(old)); /* late RX cleanup cannot release the pending TX */
    uint8_t block[36]={0};
    CHECK(native_lab_live_block(classic_sid,block,0) && native_lab_live_block(classic_sid,block,0));
    CHECK(native_lab_live_go(classic_sid));classic_pump();
    CHECK(classic_live->state==NT_TX && !classic_live->keyed && !hw.tx_calls);
    down.arg=0;CHECK(native_lab_request(&sm,&down)==1);classic_pump();classic_pump();classic_pump();
    CHECK(classic_live->state==NT_RX && !sm.ptt && !classic_live->keyed && !hw.tx_calls);
}
int main(void)
{
    int prior=upstream_radio_main();test_classic_ownership();test_classic_idle_rearm();
    printf("Classic real FM/DMR/P25 admission and ownership: %d failures\n",failures);
    return prior || failures ? 1 : 0;
}
