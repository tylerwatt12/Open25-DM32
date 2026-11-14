/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Actual P25 microphone/playback callbacks and shared local feedback ownership. */
#include <assert.h>
#include <stdio.h>
#include "p25_lab.c"

static unsigned critical_depth;
static audio_block_cb_t installed;
static unsigned starts,stops;
static bool fail_start;
static bool stream_start(audio_block_cb_t cb,void *ctx)
{ (void)ctx;installed=cb;starts++;return !fail_start; }
static void stream_stop(void) { installed=NULL;stops++; }
static void speaker_route(bool on) { (void)on; }
static const hal_audio_t audio={.start=stream_start,.stop=stream_stop,.speaker_pcm=speaker_route};
void audio_test_enter(void) { critical_depth++; }
void audio_test_exit(void) { assert(critical_depth);critical_depth--; }
uint32_t hal_micros(void) { return 20000; }
const board_t board={.audio=&audio};

static unsigned count_nonzero(const int16_t *pcm)
{
    unsigned n=0;
    for(unsigned i=0;i<160;i++)n+=pcm[i]!=0;
    return n;
}

int main(void)
{
    int16_t mic[160],original[160],spk[160],first[160],next[160];
    for(unsigned i=0;i<160;i++)mic[i]=(int16_t)(i*23-1500);
    memcpy(original,mic,sizeof mic);
    p25_audio_fifo_reset(&fifo);live_capturing=1;live_released=0;
    mic_block(mic,spk,NULL);
    assert(p25_audio_fifo_take(&fifo,first)&&!memcmp(first,mic,sizeof mic));
    assert(!count_nonzero(spk)&&live_mic_frames==1&&!critical_depth);

    unsigned frames=live_mic_frames;
    live_released=1;mic_block(mic,spk,NULL);
    live_released=0;live_capturing=0;mic_block(mic,spk,NULL);
    assert(!p25_audio_fifo_take(&fifo,next)&&live_mic_frames==frames);
    live_capturing=1;
    mic_block(mic,spk,NULL);
    assert(p25_audio_fifo_take(&fifo,next)&&!memcmp(next,mic,sizeof mic));
    assert(!memcmp(original,mic,sizeof mic));

    /* Lean playback preserves the queued PCM without per-sample diagnostics. */
    p25_audio_fifo_reset(&fifo);pcm_blocks=nonzero=played=played_nonzero=0;
    pcm_crc=0x12345678;audio_on=1;
    for(unsigned n=0;n<P25_AUDIO_PREFILL;n++)sink(NULL,first);
    assert(pcm_blocks==P25_AUDIO_PREFILL&&!nonzero&&pcm_crc==0x12345678);
    audio_block(mic,spk,NULL);
    assert(played==1&&!played_nonzero&&count_nonzero(spk)>0&&!memcmp(spk,first,sizeof first));
    assert(!critical_depth&&!stop_requested);

    /* A real queued warning cannot replace or stop the P25 microphone owner. */
    beep_init(&audio);state=1;audio_on=0;live_capturing=1;live_released=0;
    p25_audio_fifo_reset(&fifo);stream_start(mic_block,NULL);
    unsigned prior_starts=starts,prior_stops=stops;
    beep_play(BEEP_TOT_WARN,50,p25_lab_busy());
    for(unsigned i=0;i<12;i++)beep_service(p25_lab_busy());
    assert(installed==mic_block&&starts==prior_starts&&stops==prior_stops&&beep_busy());
    installed(mic,spk,NULL);
    assert(p25_audio_fifo_take(&fifo,next)&&!memcmp(next,mic,sizeof mic));
    assert(!count_nonzero(spk)); /* local warnings never enter encoded audio */

    /* Direct P25 -> FM handoff resumes the pending beep under its own owner. */
    stream_stop();state=0;beep_service(p25_lab_busy());
    assert(installed==beep_audio_block&&starts==prior_starts+1);
    unsigned beep_samples=0;
    for(unsigned i=0;i<32&&installed;i++) {
        installed(mic,spk,NULL);beep_samples+=count_nonzero(spk);beep_service(false);
    }
    assert(beep_samples>0&&!beep_busy()&&!installed);

    /* RX mixes local feedback without replacing its PCM callback. */
    state=1;audio_on=1;p25_audio_fifo_reset(&fifo);
    pcm_blocks=nonzero=played=played_nonzero=0;memset(first,0,sizeof first);
    for(unsigned i=0;i<P25_AUDIO_PREFILL;i++)sink(NULL,first);
    stream_start(audio_block,NULL);prior_starts=starts;prior_stops=stops;
    beep_play(BEEP_KEY,50,p25_lab_busy());installed(mic,spk,NULL);
    assert(count_nonzero(spk)>0&&!nonzero&&!played_nonzero);
    assert(installed==audio_block&&starts==prior_starts&&stops==prior_stops);
    installed(mic,spk,NULL);beep_service(true);
    assert(!beep_busy()&&!critical_depth);
    stream_stop();state=0;fail_start=true;
    beep_stats_t before_failure,after_failure;beep_get_stats(&before_failure);
    beep_play(BEEP_ERROR,50,false);beep_get_stats(&after_failure);
    assert(!installed&&!beep_busy()&&after_failure.played==before_failure.played);
    assert(after_failure.no_stream==before_failure.no_stream+1);
    puts("PASS shared P25 PCM: normal mic, capture/release, queued warning, RX beep mix and direct FM handoff");
    return 0;
}
