/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Actual native prepare and board set_mode; only hardware callees are doubled. */
#include "config.h"
#include "hal_radio.h"
#include "hrc_native_port.h"
#include "rf/fd6818/fd6818.h"
#include <assert.h>
#include <stdio.h>
static uint32_t timers[7],rx_hz,calibration_hz,modulation_bias;
static unsigned rx_calls,tx_calls,safe_calls,depth;
static bool transmitting,dmr_keyed,rx_am_on,have_rx,cal_valid=true;
static radio_mode_t mode=RADIO_MODE_FM;
static hrc7000_t modem;
static fd6818_t fd;
static const struct { unsigned power_count; } board={9};
#define TMR_CTRL(n) timers[n]
#define TIMER_PA 3
#define DMR_PA_DELAY_US 28000
#define MIC_GAIN_DB 11
enum { PIN_SPK_SOURCE, PIN_MIC_PATH };
static bool board_in_band(uint32_t hz,bool tx)
{ (void)tx;return (hz>=136000000 && hz<=174000000) || (hz>=400000000 && hz<=480000000); }
static bool bf_cal_valid(void) { return cal_valid; }
static uint8_t bf_cal_tx_center(void) { return 1; }
static uint16_t bf_cal_modcal(uint32_t hz)
{ calibration_hz=hz;return hz<280000000 ? 0x4141 : 0x5252; }
static void wake(void) { }
static void hrc_dac_b_power(bool on) { assert(on); }
void fd6818_set_mode(fd6818_t *d,fd6818_mode_t m) { assert(d==&fd);(void)m; }
void hrc7000_dmr_enter(hrc7000_t *d,uint32_t bias,uint8_t gain)
{ assert(d==&modem && gain==MIC_GAIN_DB);modulation_bias=bias; }
void hrc7000_fm_idle(hrc7000_t *d) { assert(d==&modem); }
static void hrc_dmr_enter(void) { }
static void hrc_dmr_leave(void) { }
static void pa_off(void) { }
/* The runner extracts this exact function from the generated board source. */
#include "board_set_mode.inc"
static void hrc_audio_pcm_hold(bool on) { (void)on; }
static void audio(bool on) { assert(!on); }
static void hrc_pin_write(unsigned pin,bool on) { assert(pin<=PIN_MIC_PATH && !on); }
static void speaker_source(void) { }
static bool rx(uint32_t hz,radio_bw_t bw,const subtone_t *tone)
{
    assert(mode==RADIO_MODE_DMR && bw==RADIO_BW_12K5 && !tone);
    assert(calibration_hz==hz && rx_hz==hz);
    assert((modulation_bias&0xffffu)==(hz<280000000 ? 0x4141u : 0x5252u));
    rx_hz=hz;rx_calls++;return true;
}
static bool tx(uint32_t hz,radio_bw_t bw,uint8_t power,const subtone_t *tone)
{ assert(hz==rx_hz && bw==RADIO_BW_12K5 && power<board.power_count && !tone);tx_calls++;return true; }
static void board_rf_safe(void) { safe_calls++; }
static void dmr_rf_switch(bool on) { assert(on); }
static void hrc_timer_oneshot(unsigned n,uint32_t us) { assert(n==3 && us==28000); }
static void dmr_pa_on(void) { }
static uint32_t hrc_irq_save(void) { return depth++; }
static void hrc_irq_restore(uint32_t n) { assert(depth==n+1);depth=n; }
void hn_init(hn_chip *chip,hrc7000_t *m,const hn_hooks *hooks,nt_ops *ops)
{ chip->modem=m;chip->hooks=hooks;(void)ops; }
int hn_port_on(hn_chip *chip,nt_test *t) { (void)chip;(void)t;return 1; }
void hn_port_quiet(void) { }
void hn_port_restore(void) { }
#include "dm32_native.inc"
int main(void)
{
    native_init();
    nt_config c={.frequency_hz=145500000,.mode=NT_TRANSMIT,.p25_sync=1,.audio=1};
    const uint32_t frequencies[]={145500000,435000000,150000000,440000000};
    rx_hz=435000000;
    for(unsigned i=0;i<4;i++) {
        c.frequency_hz=frequencies[i];c.mode=i&1 ? NT_RECEIVE : NT_TRANSMIT;
        assert(mode==RADIO_MODE_FM && native_prepare(NULL,&c));
        assert(calibration_hz==frequencies[i] && rx_calls==i+1);
        native_off(NULL);assert(native_restore(NULL) && mode==RADIO_MODE_FM);
    }
    assert(tx_calls==2 && safe_calls==4);
    /* Rejected setup cannot update the board cache or reach calibration/RF. */
    uint32_t before=rx_hz;c.mode=NT_TRANSMIT;c.frequency_hz=145500000;
    cal_valid=false;assert(!native_prepare(NULL,&c) && rx_hz==before);cal_valid=true;
    timers[6]=1;assert(!native_prepare(NULL,&c) && rx_hz==before);timers[6]=0;
    c.frequency_hz=100000000;assert(!native_prepare(NULL,&c) && rx_hz==before);
    assert(rx_calls==4 && tx_calls==2 && !depth);
    puts("PASS actual native board prepare/set_mode: current-session VHF/UHF calibration, unchanged rejection and RX/TX routing");
    return 0;
}
