/* SPDX-License-Identifier: MIT */
/* Exercise the real encoder, flash journal and reboot path with the upstream fixture. */
#define main upstream_codeplug_main
#include "codeplugEditTest.c"
#undef main
#include "codeplug/codeplug_kv.h"
#include "codeplug/codeplug_tag.h"

/* Synthesize the released D/E layout, never write it from production code. */
static void legacy_fixture(uint16_t number,const channel_t *ch)
{
    channel_t ordinary=*ch;ordinary.mode=RADIO_MODE_FM;
    CHECK(codeplug_set_channel(number,&ordinary));
    uint8_t r[CODEPLUG_OPENFW_CHANNEL_SIZE];memset(r,0xFF,sizeof r);
    codeplug_openfw_encode(ch,r);
    r[0x18]=0xA5;r[0x2B]=(uint8_t)ch->p25_nac;
    r[0x2C]=(uint8_t)((ch->p25_tx_nac_override?0xE0:0xD0)|(ch->p25_nac>>8));
    r[0x2E]=(uint8_t)ch->p25_tg;r[0x2F]=(uint8_t)(ch->p25_tg>>8);
    if(!ch->rx_only)r[0x33]&=(uint8_t)~4u;
    if(ch->p25_tx_nac_override){unsigned tx=codeplug_p25_tx_nac(ch);r[0x22]=tx;r[0x23]=tx>>8;}
    CHECK(storage_write(EE+0x3790+(number-1)*56,r,sizeof r));
}
static void test_legacy_refusal(uint16_t slot)
{
    for(unsigned tag=13;tag<=14;tag++) {
        fresh();channel_t old=make("Legacy P25",446050000,446050000),back;
        old.mode=RADIO_MODE_P25;old.p25_nac=0x293;old.p25_tg=60000;old.p25_id=7654321;
        old.p25_tx_nac_override=tag==14?1u:0u;
        legacy_fixture(slot,&old);
        uint8_t *snapshot=malloc(FLASH_SIZE);CHECK(snapshot!=NULL);if(!snapshot)return;
        memcpy(snapshot,sim_flash_data,FLASH_SIZE);
        uint32_t begin=sim_flash_units();
        /* A previously open old client can still send raw bytes. The existing
         * reload path must refuse those records immediately, as boot does. */
        CHECK(codeplug_reload()==CODEPLUG_BUILTIN && codeplug_foreign());
        CHECK(codeplug_init()==CODEPLUG_BUILTIN && codeplug_foreign());
        CHECK(!codeplug_openfw_decode(sim_flash_data+0x3790+(slot-1)*56,&back));
        CHECK(sim_flash_units()==begin && memcmp(snapshot,sim_flash_data,FLASH_SIZE)==0);
        codeplug_accept(true);CHECK(codeplug_foreign()); /* Use anyway cannot enable the retired format. */
        CHECK(!codeplug_set_channel(slot,&old));
        CHECK(memcmp(snapshot,sim_flash_data,FLASH_SIZE)==0);
        CHECK(codeplug_start_new());
        CHECK(!codeplug_foreign() && codeplug_init()==CODEPLUG_OPENFW);
        /* Explicit initialization preserves calibration and device-local regions. */
        CHECK(memcmp(snapshot+0x10000,sim_flash_data+0x10000,0x1000)==0);
        free(snapshot);
    }
}

static channel_t standard_fixture(uint16_t number)
{
    channel_t p=make("Standard",446050000,446050000);
    p.mode=RADIO_MODE_P25;p.p25_nac=0x293;p.p25_tg=123;p.p25_id=101;
    CHECK(codeplug_set_channel(number,&p));CHECK(codeplug_channel(number,&p));return p;
}
static void test_standard_ownership(void)
{
    fresh();channel_t p=standard_fixture(9),back;
    uint8_t params[20];memset(params,0xCC,sizeof params);codeplug_p25_params_encode(&p,params);
    params[0]=1;params[5]|=0x80;params[8]=0;
    codeplug_tag_t t={.kind=CODEPLUG_KIND_P25,.system=7,.flags=CODEPLUG_TAG_TX|CODEPLUG_TAG_FALLBACK|0x80,
        .fb_chan=1,.fb_check=codeplug_name_check("Simplex 145.500")};
    CHECK(codeplug_tag_write(9,p.name,&t,params,sizeof params));CHECK(codeplug_channel(9,&back));
    strcpy(back.name,"Renamed");back.p25_nac=0x321;CHECK(codeplug_set_channel(9,&back));
    uint8_t raw[64];int n=codeplug_kv_get(CODEPLUG_KEY_CH_TAG+8,raw,sizeof raw);
    CHECK(n==29 && raw[1]==7 && raw[4]==t.flags && (raw[5]|(raw[6]<<8))==1 && (raw[7]|(raw[8]<<8))==t.fb_check);
    CHECK(raw[9]==1 && raw[14]==0x81 && raw[17]==0);
    CHECK(memcmp(raw+18,params+9,11)==0);CHECK(codeplug_channel(9,&back)&&back.p25_nac==0x321);
    /* Unsupported secure/mixed requests must never execute as clear P25. */
    for(unsigned flag=2;flag<=16;flag<<=1){
        uint8_t requested[9];codeplug_p25_params_encode(&back,requested);requested[5]|=flag;
        codeplug_tag_t unsupported={.kind=CODEPLUG_KIND_P25,.system=0xFF,.flags=CODEPLUG_TAG_TX};
        CHECK(codeplug_tag_write(9,back.name,&unsupported,requested,sizeof requested));
        CHECK(!codeplug_channel(9,&back));
    }
    /* A new P25 record never inherits a stale tag's system, flags or future tail. */
    p=make("Plain",446050000,446050000);CHECK(codeplug_set_channel(10,&p));
    CHECK(codeplug_tag_write(10,"Unrelated",&t,params,sizeof params));
    p.mode=RADIO_MODE_P25;p.kind=0;p.p25_nac=0x293;p.p25_tg=234;p.p25_id=101;
    CHECK(codeplug_set_channel(10,&p));CHECK(codeplug_kv_get(CODEPLUG_KEY_CH_TAG+9,raw,sizeof raw)==14);
    CHECK(raw[1]==0xFF && raw[4]==CODEPLUG_TAG_TX);
    /* A foreign kind is excluded from the existing failed-channel RX/TX path. */
    p=make("Unknown",446050000,446050000);p.mode=RADIO_MODE_DMR;p.rx_only=true;CHECK(codeplug_set_channel(11,&p));
    t=(codeplug_tag_t){.kind=99,.system=0xFF,.flags=0};CHECK(codeplug_tag_write(11,p.name,&t,params,sizeof params));
    CHECK(!codeplug_channel(11,&back));
    /* Explicitly making it a plain channel removes the tag through upstream's write lifecycle. */
    p.kind=0;p.mode=RADIO_MODE_FM;p.rx_only=false;CHECK(codeplug_set_channel(11,&p));
    CHECK(!codeplug_tag_read(11,&t));CHECK(codeplug_channel(11,&back)&&back.mode==RADIO_MODE_FM);
}
static void test_inherited_unit_id(void)
{
    fresh();CHECK(codeplug_set_dmr_id(12345));
    uint8_t own[]={0x32,0x09,1,0};CHECK(codeplug_kv_set(CODEPLUG_KEY_MODE_SET+CODEPLUG_KIND_P25,own,sizeof own));
    channel_t p=standard_fixture(9),back;p.kind=CODEPLUG_KIND_P25;p.dmr_id=0;p.p25_id=67890;
    CHECK(codeplug_set_channel(9,&p));CHECK(codeplug_channel(9,&back));
    CHECK(back.dmr_id==0&&back.p25_id==67890);
    strcpy(back.name,"Default unit");CHECK(codeplug_set_channel(9,&back));
    CHECK(codeplug_channel(9,&back)&&back.dmr_id==0&&back.p25_id==67890);
    CHECK(codeplug_kv_remove(CODEPLUG_KEY_MODE_SET+CODEPLUG_KIND_P25));
    CHECK(codeplug_channel(9,&back)&&back.dmr_id==0&&back.p25_id==12345);
}
static void test_contact_target(void)
{
    fresh();contact_t target={.name="Group",.type=CONTACT_GROUP,.id=123};
    CHECK(codeplug_set_contact(1,&target));
    channel_t p=standard_fixture(9),back;p.contact=1;
    CHECK(codeplug_set_channel(9,&p));
    CHECK(codeplug_channel(9,&back)&&back.contact==1&&back.p25_tg==123);
    strcpy(back.name,"Contact rename");CHECK(codeplug_set_channel(9,&back));
    target.id=456;CHECK(codeplug_set_contact(1,&target));
    CHECK(codeplug_channel(9,&back)&&back.contact==1&&back.p25_tg==456);
    /* Explicit TG entry chooses a fixed target, even if its value is unchanged. */
    back.contact=0;CHECK(codeplug_set_channel(9,&back));
    target.id=789;CHECK(codeplug_set_contact(1,&target));
    CHECK(codeplug_channel(9,&back)&&back.contact==0&&back.p25_tg==456);
    codeplug_init();CHECK(codeplug_channel(9,&back)&&back.contact==0&&back.p25_tg==456);
    listen_list_t list={.name="Groups",.count=1,.contacts={1}};
    CHECK(codeplug_set_listen_list(1,&list));back.contact=0;back.listen_list=1;
    CHECK(codeplug_set_channel(9,&back));CHECK(codeplug_channel(9,&back)&&back.listen_list==1&&back.p25_tg==789);
    strcpy(back.name,"List rename");CHECK(codeplug_set_channel(9,&back));
    target.id=321;CHECK(codeplug_set_contact(1,&target));
    CHECK(codeplug_channel(9,&back)&&back.listen_list==1&&back.p25_tg==321);
    back.contact=1;back.listen_list=0;CHECK(codeplug_set_channel(9,&back));
    target.type=CONTACT_PRIVATE;CHECK(codeplug_set_contact(1,&target));
    CHECK(!codeplug_channel(9,&back)); /* Never change a unit call into a group call. */
    target.type=CONTACT_GROUP;target.id=65536;CHECK(codeplug_set_contact(1,&target));
    CHECK(!codeplug_channel(9,&back));
    target.id=123;CHECK(codeplug_set_contact(1,&target));CHECK(codeplug_channel(9,&back));
    back.dmo=true;CHECK(codeplug_set_channel(9,&back));CHECK(codeplug_channel(9,&back)&&back.dmo);
    back.tx_hz+=12500;CHECK(codeplug_set_channel(9,&back));CHECK(!codeplug_channel(9,&back));
}
static void test_delete_tag_boundary(void)
{
    fresh();(void)standard_fixture(9);
    uint8_t *snapshot=malloc(FLASH_SIZE);CHECK(snapshot!=NULL);if(!snapshot)return;
    memcpy(snapshot,sim_flash_data,FLASH_SIZE);
    uint32_t start=sim_flash_units();CHECK(codeplug_delete_channel(9));uint32_t units=sim_flash_units()-start;
    for(uint32_t cut=1;cut<=units;cut+=cut<40?1:997){
        memcpy(sim_flash_data,snapshot,FLASH_SIZE);sim_flash_power_on();CHECK(storage_init());codeplug_init();
        sim_flash_cut_after(cut);(void)codeplug_delete_channel(9);sim_flash_power_on();CHECK(storage_init());codeplug_init();
        channel_t back;if(codeplug_channel(9,&back))CHECK(back.mode==RADIO_MODE_P25&&back.p25_tg==123);
    }
    free(snapshot);
}


int main(void)
{
    CHECK(codeplug_kind_built(CODEPLUG_KIND_P25));
    CHECK(!codeplug_kind_built(CODEPLUG_KIND_P25_TRUNK));
    int prior=upstream_codeplug_main();
    test_legacy_refusal(9);
    test_legacy_refusal(39);
    test_standard_ownership();
    test_inherited_unit_id();
    test_contact_target();
    test_delete_tag_boundary();
    fresh();
    channel_t p=make("P25 local", 438500000, 430900000), back;
    p.mode=RADIO_MODE_P25;p.p25_squelch=1;p.p25_nac=0xABC;p.p25_tg=65535;p.p25_id=9999999;p.power=8;
    CHECK(codeplug_set_channel(9,&p));
    CHECK(codeplug_init()==CODEPLUG_OPENFW);
    CHECK(codeplug_channel(9,&back));
    CHECK(back.mode==RADIO_MODE_P25 && back.p25_squelch==1 && back.p25_nac==0xABC && back.p25_tg==65535);
    CHECK(back.p25_id==9999999 && back.power==8 && back.rx_hz==p.rx_hz && back.tx_hz==p.tx_hz);
    CHECK(!back.roam && !back.dmo && !back.vox);
    CHECK(!back.p25_busy_lockout);
    p.p25_busy_lockout = true;
    CHECK(codeplug_set_channel(9, &p));
    CHECK(codeplug_init() == CODEPLUG_OPENFW);
    CHECK(codeplug_channel(9, &back) && back.p25_busy_lockout);
    p.p25_busy_lockout = false;
    CHECK(codeplug_set_channel(9, &p));
    CHECK(codeplug_channel(9, &back) && !back.p25_busy_lockout);
    uint8_t r[CODEPLUG_OPENFW_CHANNEL_SIZE];memset(r,0xFF,sizeof r);
    codeplug_openfw_encode(&p,r);
    CHECK(r[0x18]==1 && (r[0x33]&4)); /* harmless upstream base record */
    CHECK(codeplug_openfw_decode(r,&back) && back.mode==RADIO_MODE_DMR && back.rx_only);
    uint8_t params[9];codeplug_p25_params_encode(&p,params);
    CHECK(codeplug_p25_params_decode(params,sizeof params,&back) && back.p25_nac==0xABC);
    /* The standard P25 record carries the full usable 24-bit unit-ID range. */
    p.p25_id=0xFFFFFEu;p.dmr_id=0xFFFFFEu;
    CHECK(codeplug_set_channel(9,&p));CHECK(codeplug_channel(9,&back));
    CHECK(back.p25_id==0xFFFFFEu);
    const channel_t valid=p;
    for(unsigned invalid=0;invalid<7;invalid++) {
        p=valid;
        switch(invalid) {
        case 0:p.p25_nac=0x1000;break;case 1:p.p25_nac=0xF7F;break;
        case 2:p.p25_nac=0xFFFF;break;case 3:p.p25_id=0xFFFFFF;break;
        case 4:p.p25_id=0;break;case 5:p.p25_tg=0;break;case 6:p.bw=RADIO_BW_25K;break;
        }
        CHECK(!codeplug_set_channel(9,&p));
        CHECK(codeplug_channel(9,&back) && back.p25_nac==valid.p25_nac);
    }
    p=valid;p.rx_only=true;p.p25_nac=0xF7E;p.p25_tg=0;p.p25_id=0;
    CHECK(codeplug_set_channel(10,&p));
    CHECK(codeplug_init()==CODEPLUG_OPENFW && codeplug_channel(10,&back));
    CHECK(back.rx_only && back.p25_nac==0xF7E && back.p25_tg==0 && back.p25_id==0);
    /* Distinct RX/TX, including TX zero and wildcard RX, use RFKV mode parameters. */
    for(unsigned tx=0;tx<0x1000;tx++) {
        if(tx==0xF7E || tx==0xF7F)continue;
        p=valid;p.p25_nac=0xF7E;p.p25_tx_nac_override=(uint16_t)(tx+1);
        codeplug_openfw_encode(&p,r);
        CHECK(r[0x18]==1 && (r[0x33]&4));
        CHECK(codeplug_openfw_decode(r,&back));
        codeplug_p25_params_encode(&p,params);
        CHECK(params[3]==(uint8_t)tx && params[4]==(0x80u|(tx>>8)));
        CHECK(codeplug_p25_params_decode(params,sizeof params,&back) && back.p25_nac==0xF7E);
        CHECK(codeplug_p25_tx_nac(&back)==tx && !back.tx_tone.type);
    }
    p=valid;p.p25_tx_nac_override=0x457;
    CHECK(codeplug_set_channel(11,&p));
    CHECK(codeplug_init()==CODEPLUG_OPENFW && codeplug_channel(11,&back));
    CHECK(codeplug_p25_tx_nac(&back)==0x456 && back.p25_nac==valid.p25_nac);
    for(unsigned tx=0xF7E;tx<=0x1000;tx+=(tx==0xF7E?1:0x81)) {
        p.p25_tx_nac_override=(uint16_t)(tx+1);
        CHECK(!codeplug_p25_valid(&p) && !codeplug_set_channel(11,&p));
    }
    p=valid;p.p25_tx_nac_override=0x294;p.p25_nac=0x2A0;
    CHECK(codeplug_p25_tx_nac(&p)==0x293);
    p.p25_tx_nac_override=0;CHECK(codeplug_p25_tx_nac(&p)==0x2A0);
    CHECK(sim_flash_violations()==0);
    puts("Classic P25 channel persistence, limits and unchanged FM/DMR encodings checked");
    return prior || failures ? 1 : 0;
}
