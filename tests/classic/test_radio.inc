/* SPDX-License-Identifier: GPL-3.0-or-later */
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
    radio_req_t p={.type=RREQ_PTT,.arg=1};CHECK(native_lab_request(&sm,&p)==1);
    classic_pump();classic_pump();
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
int main(void)
{
    int prior=upstream_radio_main();test_classic_ownership();
    printf("Classic real FM/DMR/P25 admission and ownership: %d failures\n",failures);
    return prior || failures ? 1 : 0;
}
