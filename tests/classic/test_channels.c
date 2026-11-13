/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Exercise the real encoder, flash journal and reboot path with the upstream fixture. */
#define main upstream_codeplug_main
#include "codeplugEditTest.c"
#undef main

int main(void)
{
    int prior=upstream_codeplug_main();
    fresh();
    channel_t p=make("P25 local", 438500000, 430900000), back;
    p.mode=RADIO_MODE_P25;p.p25_nac=0xABC;p.p25_tg=65535;p.p25_id=9999999;p.power=8;
    CHECK(codeplug_set_channel(9,&p));
    CHECK(codeplug_init()==CODEPLUG_OPENFW);
    CHECK(codeplug_channel(9,&back));
    CHECK(back.mode==RADIO_MODE_P25 && back.p25_nac==0xABC && back.p25_tg==65535);
    CHECK(back.p25_id==9999999 && back.power==8 && back.rx_hz==p.rx_hz && back.tx_hz==p.tx_hz);
    CHECK(!back.roam && !back.dmo && !back.vox);
    uint8_t r[CODEPLUG_OPENFW_CHANNEL_SIZE];memset(r,0xFF,sizeof r);
    codeplug_openfw_encode(&p,r);
    CHECK(r[0x18]==0xA5 && r[0x2C]==0xDA && r[0x2B]==0xBC);
    r[0x2C]&=15;CHECK(!codeplug_openfw_decode(r,&back));
    const channel_t valid=p;
    for(unsigned invalid=0;invalid<7;invalid++) {
        p=valid;
        switch(invalid) {
        case 0:p.p25_nac=0x1000;break;case 1:p.p25_nac=0xF7F;break;
        case 2:p.p25_nac=0xF7E;break;case 3:p.p25_id=10000000;break;
        case 4:p.p25_id=0;break;case 5:p.p25_tg=0;break;case 6:p.bw=RADIO_BW_25K;break;
        }
        CHECK(!codeplug_set_channel(9,&p));
        CHECK(codeplug_channel(9,&back) && back.p25_nac==valid.p25_nac);
    }
    p=valid;p.rx_only=true;p.p25_nac=0xF7E;p.p25_tg=0;p.p25_id=0;
    CHECK(codeplug_set_channel(10,&p));
    CHECK(codeplug_init()==CODEPLUG_OPENFW && codeplug_channel(10,&back));
    CHECK(back.rx_only && back.p25_nac==0xF7E && back.p25_tg==0 && back.p25_id==0);
    CHECK(sim_flash_violations()==0);
    puts("Classic P25 channel persistence, limits and unchanged FM/DMR encodings checked");
    return prior || failures ? 1 : 0;
}
