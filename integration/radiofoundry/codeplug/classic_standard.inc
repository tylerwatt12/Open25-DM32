/* SPDX-License-Identifier: MIT
 * Classic's P25 projection of the upstream channel-mode records.
 * Persisted bytes belong to codeplug_kv/tag, not the runtime projection. */
#include "p25/p25_codeplug.h"
#include "codeplug/codeplug_records.h"
#include "codeplug/codeplug_kv.h"
#if CONFIG_P25_CHANNELS
#define CLASSIC_P25_PARAMS 9u
#define CLASSIC_P25_BUSY_LOCKOUT 0x20u

uint32_t codeplug_p25_unit_id(uint32_t own)
{
    if(own) return own;
    uint8_t v[8];
    int n=codeplug_mode_settings(CODEPLUG_KIND_P25,v,sizeof v);
    return p25_unit_id_decode(v,n,codeplug_dmr_id());
}

void codeplug_p25_params_encode(const channel_t *ch, uint8_t v[CLASSIC_P25_PARAMS])
{
    memset(v,0,CLASSIC_P25_PARAMS);
    p25_conv_t conv={.squelch=ch->p25_squelch,.rx_nac=ch->p25_nac,.tx_nac=codeplug_p25_tx_nac(ch),
        .tx_inherit=!ch->p25_tx_nac_override};
    p25_conv_encode(&conv,v);
    /* A saved contact remains the target owner until the user edits the TG. */
    if(!ch->contact && !ch->listen_list) {v[5]=1u;put16(v+6,ch->p25_tg);}
    if (ch->p25_busy_lockout) v[5] |= CLASSIC_P25_BUSY_LOCKOUT;
}

bool codeplug_p25_params_decode(const uint8_t *v,uint16_t len,channel_t *ch)
{
    p25_conv_t conv;p25_conv_decode(v,len,&conv);
    ch->mode=RADIO_MODE_P25;
    ch->p25_nac=conv.rx_nac;
    ch->p25_squelch=conv.squelch;
    ch->p25_busy_lockout = len > 5 && (v[5] & CLASSIC_P25_BUSY_LOCKOUT);
    ch->p25_tx_nac_override=conv.tx_inherit?0u:(uint16_t)(conv.tx_nac+1u);
    ch->p25_id=codeplug_p25_unit_id(ch->dmr_id);
    ch->p25_tg=1u;
    if(len>=8 && (v[5]&1u)) {ch->p25_tg=le16(v+6);ch->contact=ch->listen_list=0;}
    else {
        uint16_t idx=ch->contact;
#if CONFIG_DMR
        idx=codeplug_channel_talk_contact(ch);
#endif
        contact_t contact;
        if(idx && codeplug_contact(idx,&contact)) {
            if(contact.type==CONTACT_PRIVATE || (contact.type==CONTACT_GROUP && contact.id>65535u)) return false;
            ch->p25_tg=contact.type==CONTACT_ALL_CALL?65535u:
                contact.type==CONTACT_GROUP&&contact.id<=65535u?(uint16_t)contact.id:1u;
        }
    }
    ch->rx_tone=ch->tx_tone=(subtone_t){0};
    ch->roam=ch->vox=false;
    ch->bw=RADIO_BW_12K5;
    return codeplug_p25_valid(ch);
}

static bool classic_standard_load(uint16_t number,channel_t *ch)
{
    codeplug_tag_apply(number,ch);
    if(!ch->kind) return true;
    if(ch->kind!=CODEPLUG_KIND_P25) {
        /* A mode this edition does not run remains parked, never FM TX. */
        ch->rx_only=true;
        return false; /* The existing failed-channel path excludes it from RX/TX and scans. */
    }
    uint8_t v[64+CODEPLUG_TAG_HEAD_MAX];
    int n=codeplug_tag_params(number,ch,v,64);
    /* Classic has no mixed-mode or secure-voice engine: never turn a channel
     * requesting either into an unintended clear transmission. */
    if(n>5 && (v[5]&0x1Eu)) return false;
    if(n>8 && v[8]) return false;
    if(ch->dmo && ch->rx_hz!=ch->tx_hz) return false; /* Native P25 TX cannot apply split-channel talkaround. */
    return n>=0 && codeplug_p25_params_decode(v,(uint16_t)(n<64?n:64),ch);
}

static bool classic_standard_store(uint16_t number,const channel_t *ch,bool preserve)
{
    if(!codeplug_mode_p25(ch->mode)) return codeplug_tag_remove(number);
    uint8_t v[CODEPLUG_TAG_HEAD_MAX+CLASSIC_P25_PARAMS];
    uint8_t old[CODEPLUG_TAG_HEAD_MAX+CLASSIC_P25_PARAMS];
    codeplug_tag_t t={.kind=CODEPLUG_KIND_P25,.system=0xFF};
    int previous=codeplug_kv_get((uint16_t)(CODEPLUG_KEY_CH_TAG+number-1u),old,sizeof old);
    bool follow=preserve && previous>=(int)CODEPLUG_TAG_HDR && codeplug_tag_read(number,&t) && t.kind==CODEPLUG_KIND_P25;
    if(!follow) t=(codeplug_tag_t){.kind=CODEPLUG_KIND_P25,.system=0xFF};
    t.flags=(uint8_t)((t.flags&~CODEPLUG_TAG_TX)|(ch->rx_only?0u:CODEPLUG_TAG_TX));
    v[0]=t.kind;v[1]=t.system;put16(v+2,codeplug_name_check(ch->name));v[4]=t.flags;
    unsigned at=CODEPLUG_TAG_HDR;
    if(t.flags&CODEPLUG_TAG_FALLBACK){put16(v+5,t.fb_chan);put16(v+7,t.fb_check);at=CODEPLUG_TAG_HEAD_MAX;}
    codeplug_p25_params_encode(ch,v+at);
    if(follow && previous>(int)at) v[at]=old[at]; /* preserve the module's squelch */
    if(follow && previous>(int)at+5) v[at+5]|=(uint8_t)(old[at+5]&~(1u | CLASSIC_P25_BUSY_LOCKOUT));
    if(follow && previous>(int)at+8) v[at+8]=old[at+8];
    codeplug_kv_op_t op={(uint16_t)(CODEPLUG_KEY_CH_TAG+number-1u),
        (uint16_t)((at+CLASSIC_P25_PARAMS)|(follow?CODEPLUG_KV_HEAD:0u)),v};
    return codeplug_kv_write(&op,1);
}

#endif
