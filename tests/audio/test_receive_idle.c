/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Actual channel RX worker, CAI/IMBE decoder and paced audio callback. */
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include "p25_lab.c"

static jmp_buf finished;
static void (*worker_fn)(void *);
static audio_block_cb_t callback;
static void *callback_ctx;
static uint32_t clock_ms,audio_at,first_raw_at,stream_at,next_raw_at,pending_until;
static uint32_t maximum_ms,stop_at,last_frame_at,seen_frames,first_frame_at;
static unsigned depth,turn,starts,stops,owned,sequence,partial_after_deadline,output_nonzero;
static uint8_t air[4096],raw[128][36];
static unsigned air_symbols,raw_count;
static uint16_t receive_nac=0x293,receive_group=42;

void audio_test_enter(void) { depth++; }
void audio_test_exit(void) { assert(depth);depth--; }
uint32_t hal_millis(void) { return clock_ms; }
uint32_t hal_micros(void) { return clock_ms*1000u; }
BaseType_t xTaskCreate(void (*fn)(void *),const char *name,unsigned words,void *ctx,unsigned priority,TaskHandle_t *h)
{ (void)name;(void)ctx;assert(words==2048 && priority==3);worker_fn=fn;*h=(void *)1;return pdPASS; }
void xTaskNotifyGive(TaskHandle_t h) { assert(h && !depth); }
uint32_t ulTaskNotifyTake(BaseType_t clear,TickType_t wait)
{ assert(clear && wait==portMAX_DELAY);if(turn++)longjmp(finished,1);return 1; }
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t h) { (void)h;return 512; }
void vTaskDelay(TickType_t ms)
{
    clock_ms+=ms;assert(clock_ms<=maximum_ms);
    if(voice.rx_frames!=seen_frames) {
        if(!seen_frames)first_frame_at=clock_ms;
        seen_frames=voice.rx_frames;last_frame_at=clock_ms;
    }
    if(clock_ms>=500 && !seen_frames && voice.framer.symbols)partial_after_deadline++;
    while(callback && clock_ms-audio_at>=20) {
        int16_t mic[160]={0},spk[160];audio_at+=20;
        callback(mic,spk,callback_ctx);
        for(unsigned i=0;i<160;i++)if(spk[i]) { output_nonzero++;pending_until=clock_ms+32; }
    }
}
static bool audio_start(audio_block_cb_t cb,void *ctx)
{ callback=cb;callback_ctx=ctx;audio_at=clock_ms;return true; }
static void audio_stop(void)
{
    assert(!fifo.count && clock_ms>=pending_until);
    stop_at=clock_ms;callback=NULL;stops++;
}
static void route(bool on) { (void)on; }
static bool pending(void) { return clock_ms<pending_until; }
static const hal_audio_t audio={.start=audio_start,.stop=audio_stop,.speaker_pcm=route,.spk_pending=pending};
const board_t board={.audio=&audio};
uint32_t native_lab_voice_start(uint32_t hz,uint32_t ms)
{ assert(hz==145500000 && !ms && !owned);owned=1;starts++;next_raw_at=first_raw_at;return starts; }
int native_lab_voice_poll(uint32_t sid,uint8_t *s,uint8_t *why)
{ assert(sid==starts && owned);*s=NT_RX;*why=NT_OK;return 1; }
int native_lab_voice_read(uint32_t sid,nt_record *r)
{
    assert(sid==starts && owned);
    if(clock_ms<next_raw_at)return 0;
    memset(r,0,sizeof *r);r->sequence=sequence++;r->time_us=next_raw_at*1000u;
    if(next_raw_at>=stream_at) {
        unsigned index=(next_raw_at-stream_at)/30u;
        if(index<raw_count)memcpy(r->raw,raw[index],36);
    }
    next_raw_at+=30;return 1;
}
void native_lab_voice_stop(uint32_t sid) { assert(sid==starts && owned && !callback); }
int native_lab_voice_release(uint32_t sid) { assert(sid==starts && owned);owned=0;return 1; }
bool input_ptt_down(void) { return false; }
int native_lab_live_poll(uint32_t sid,uint8_t *s,uint8_t *r,uint8_t *q)
{ (void)sid;(void)s;(void)r;(void)q;assert(0);return 0; }
int native_lab_live_block(uint32_t sid,const uint8_t block[36],int last)
{ (void)sid;(void)block;(void)last;assert(0);return 0; }
int native_lab_live_go(uint32_t sid) { (void)sid;assert(0);return 0; }

static void drain(p25_voice *v)
{
    uint8_t dibits[144];
    while(v->started && v->count) {
        unsigned available=0;
        for(unsigned i=0;i<v->count;i++)available+=v->queue[(v->tail+i)%P25_VOICE_QUEUE].bytes*4u;
        available-=v->packet_symbol;
        size_t n=p25_voice_tx_read(v,dibits,available<sizeof dibits?available:sizeof dibits);
        assert(n);
        for(unsigned i=0;i<n;i++) {
            assert(air_symbols<sizeof air*4);
            air[air_symbols/4]|=(uint8_t)(dibits[i]<<(6-2*(air_symbols%4)));air_symbols++;
        }
    }
}
static void fixture(unsigned ldus,bool without_hdu)
{
    static p25_voice v;p25_voice_init(&v,0x293,42,NULL,NULL);
    const p25_call call={0x293,42,123456,0,0,0};
    memset(air,0,sizeof air);memset(raw,0,sizeof raw);air_symbols=0;
    assert(p25_voice_tx_begin(&v,&call));drain(&v);
    for(unsigned f=0;f<ldus*9;f++) {
        int16_t pcm[160];
        for(unsigned i=0;i<160;i++){unsigned phase=(f*160+i)%40;pcm[i]=(int16_t)(phase<20?(int)phase*900-9000:27000-(int)phase*900);}
        assert(p25_voice_tx_pcm(&v,pcm));drain(&v);
    }
    assert(p25_voice_tx_end(&v));drain(&v);
    unsigned offset=without_hdu?P25_HDU_BYTES:0;
    raw_count=(air_symbols-offset*4+143)/144;assert(raw_count<=128);
    for(unsigned i=0;i<raw_count;i++)for(unsigned b=0;b<288;b++) {
        unsigned from=b<24?b:b<72?b+108:b<180?b-48:b;
        unsigned at=offset*8+i*288+from;
        if(at<air_symbols*2)raw[i][b/8]|=(uint8_t)(((air[at/8]>>(7-at%8))&1u)<<(7-b%8));
    }
}
static void execute(uint32_t first,uint32_t stream,uint32_t limit)
{
    clock_ms=audio_at=pending_until=stop_at=last_frame_at=seen_frames=first_frame_at=0;
    depth=turn=starts=stops=owned=sequence=partial_after_deadline=output_nonzero=0;
    first_raw_at=first;stream_at=stream;maximum_ms=limit;
    assert(p25_lab_set_channel(receive_nac,receive_group,123456) && p25_lab_channel_rx(145500000));
    if(!setjmp(finished))worker_fn(NULL);
    assert(state==2 && starts==1 && stops==1 && !owned && !callback && !depth);
    assert(!fifo.overflows && !unsupported && !fifo.count);
}
int main(void)
{
    p25_lab_start();beep_init(&audio);
    raw_count=0;
    execute(0,UINT32_MAX,800);
    assert(records>=17 && !voice.rx_frames && stop_at>=500 && stop_at<=510);
    execute(5000,UINT32_MAX,5800);
    assert(records>=17 && !voice.rx_frames && stop_at>=5500 && stop_at<=5510);
    fixture(12,false);execute(0,0,4000);
    assert(voice.rx_frames==14 && pcm_blocks==108 && played==pcm_blocks);
    assert(output_nonzero>0 && !voice.rx_muted && !voice.rx_erasures);
    assert(stop_at>=last_frame_at+499 && stop_at<=last_frame_at+510);
    assert(last_frame_at>2000); /* ongoing 180 ms LDUs never restart the worker */
    receive_group=43;execute(0,0,4000);
    assert(voice.rx_frames==14 && voice.rx_muted==108 && !output_nonzero && last_frame_at>2000);
    receive_group=42;receive_nac=0x123;execute(0,0,1000);
    assert(!voice.rx_frames && voice.framer.bad_nids>=2 && !pcm_blocks && stop_at>=500 && stop_at<=510);
    receive_nac=0x293;
    fixture(2,false);execute(0,480,1800);
    assert(partial_after_deadline && first_frame_at>500 && voice.rx_frames==4);
    assert(played==pcm_blocks && stop_at>=last_frame_at+499);
    fixture(2,true);execute(0,480,1800);
    assert(partial_after_deadline && first_frame_at>=630 && voice.rx_frames==3);
    assert(played==pcm_blocks && stop_at>=last_frame_at+499);
    puts("PASS actual P25 channel worker: noise cannot renew idle, first-raw grace after quiet, continuous valid LDUs, partial HDU/LDU guard, TDU tail drained before normal release");
    return 0;
}
