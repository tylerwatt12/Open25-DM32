/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Real UI storage job, settings owner, codeplug and journal on the DM32 flash map. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui/ui_svc.h"
#include "storage/storage.h"
#include "settings/settings.h"
#include "radio/radio.h"
#include "sim/sim_flash.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); failures++; } } while (0)
#define SIZE 0x1000000u
static const hal_flash_map_t map = {
    .settings_addr=0x711000, .settings_size=0x8000, .journal_addr=0x719000,
    .openfw_eeprom=0x700000, .openfw_flash=0x720000, .openfw_end=0xF00000,
    .native_addr=0xF00000, .native_size=0x100000
};
static const band_t bands[] = {{136000000,174000000,true},{400000000,480000000,true}};
const board_t board = {.name="DM32 test", .flash=&sim_flash, .bands=bands, .band_count=2, .power_count=9};
void storage_lock(void) {}
void storage_unlock(void) {}
void storage_quick_lock(void) {}
void storage_quick_unlock(void) {}
uint32_t hal_millis(void) { return 10000; }
void radio_get_view(radio_view_t *out) { memset(out,0,sizeof(*out)); }
void radio_req_clear_temporary(void) {}
void radio_req_dmr_target_clear(void) {}
static ui_event_t events[8];
static unsigned count;
bool ui_notify(const ui_event_t *ev) { CHECK(count<8); if(count<8) events[count++]=*ev; return true; }

static uint8_t *original;
static void boot(void)
{
    sim_flash_create(SIZE,&map);
    memcpy(sim_flash_data,original,SIZE);
    CHECK(!storage_init() && storage_region()==STORAGE_REGION_FOREIGN);
    CHECK(settings_init()==SETTINGS_FOREIGN);
    codeplug_accept(false);
    CHECK(codeplug_init()==CODEPLUG_BUILTIN && codeplug_foreign());
    count=0;
}
static void ask(void)
{
    CHECK(ui_svc_name_set(UI_NAME_RADIO,"NEWTEST"));
    CHECK(count==1 && events[0].kind==UI_EV_CODEPLUG_FOREIGN);
    CHECK(memcmp(original,sim_flash_data,SIZE)==0);
}
static void unchanged_private_regions(void)
{
    CHECK(memcmp(original,sim_flash_data,0x700000)==0);
    CHECK(memcmp(original+0x710000,sim_flash_data+0x710000,0x1000)==0);
    CHECK(memcmp(original+0xF00000,sim_flash_data+0xF00000,0x100000)==0);
    CHECK(sim_flash_violations()==0);
}
int main(int argc,char **argv)
{
    original=malloc(SIZE); CHECK(original!=NULL); if(!original) return 1;
    if(argc==2) {
        FILE *f=fopen(argv[1],"rb"); CHECK(f!=NULL); if(!f) return 1;
        CHECK(fread(original,1,SIZE,f)==SIZE && fgetc(f)==EOF); fclose(f);
    } else {
        memset(original,0xFF,SIZE);
        /* Another firmware owns both settings and codeplug bytes. No identifying data. */
        memset(original+0x700000,0xAA,0x1B000);
        memset(original+0x720000,0xAA,0xB0000);
        original[0x100]=0x42; original[0x710000]=0x43; original[0xF00000]=0x44;
    }
    /* Some OEM backups already have a usable open-firmware area: keep normal editing. */
    sim_flash_create(SIZE,&map); memcpy(sim_flash_data,original,SIZE);
    bool ready=storage_init(); settings_init(); codeplug_accept(false); codeplug_init();
    if(ready && !codeplug_foreign()) {
        CHECK(ui_svc_name_set(UI_NAME_RADIO,"NEWTEST") && count==0);
        storage_job_poll(); CHECK(count==0);
        char name[CODEPLUG_NAME_LEN+1]; codeplug_radio_name(name); CHECK(strcmp(name,"NEWTEST")==0);
        sim_flash_power_on(); CHECK(storage_init()); settings_init(); codeplug_init();
        codeplug_radio_name(name); CHECK(strcmp(name,"NEWTEST")==0);
        unchanged_private_regions(); sim_flash_free(); free(original);
        printf("Classic existing codeplug: save and reboot: %d failures\n",failures);
        return failures!=0;
    }
    boot(); ask(); CHECK(ui_svc_codeplug_answer(UI_CP_CANCEL)); storage_job_poll();
    CHECK(count==1 && memcmp(original,sim_flash_data,SIZE)==0);
    boot(); ask(); CHECK(ui_svc_codeplug_answer(UI_CP_USE)); storage_job_poll();
    CHECK(count==1 && memcmp(original,sim_flash_data,SIZE)==0);

    boot(); ask();
    settings_t *st=settings_begin(); st->mic_gain=9; settings_end(hal_millis());
    CHECK(ui_svc_codeplug_answer(UI_CP_NEW));
    CHECK(count==1 && memcmp(original,sim_flash_data,SIZE)==0); /* queued is not saved */
    storage_job_poll();
    CHECK(count==2 && events[1].kind==UI_EV_CODEPLUG_STARTED);
    CHECK(storage_ready() && !codeplug_foreign() && codeplug_source()==CODEPLUG_OPENFW);
    char name[CODEPLUG_NAME_LEN+1]; codeplug_radio_name(name); CHECK(strcmp(name,"NEWTEST")==0);
    CHECK(settings_flush());
    sim_flash_power_on(); CHECK(storage_init()); CHECK(settings_init()==SETTINGS_LOADED);
    CHECK(settings_begin()->mic_gain==9); settings_end(hal_millis());
    CHECK(codeplug_init()==CODEPLUG_OPENFW && !codeplug_foreign());
    codeplug_radio_name(name); CHECK(strcmp(name,"NEWTEST")==0);
    unchanged_private_regions();

    /* A failed erase must report failure, without a success event or a held edit. */
    boot(); ask(); CHECK(ui_svc_codeplug_answer(UI_CP_NEW)); sim_flash_cut_after(1); storage_job_poll();
    CHECK(count==2 && events[1].kind==UI_EV_SAVE_FAILED && !storage_job_busy());
    CHECK(!ui_svc_codeplug_answer(UI_CP_NEW)); unchanged_private_regions();
    sim_flash_power_on();

    /* Initialization succeeds but the requested edit fails validation: still no success. */
    boot();
    channel_t bad={.mode=RADIO_MODE_P25,.rx_hz=145550000,.tx_hz=145550000,
        .bw=RADIO_BW_12K5,.p25_nac=0x1000,.p25_tg=1,.p25_id=1};
    CHECK(ui_svc_write_channel(9,&bad)); CHECK(count==1 && events[0].kind==UI_EV_CODEPLUG_FOREIGN);
    CHECK(ui_svc_codeplug_answer(UI_CP_NEW)); storage_job_poll();
    CHECK(storage_ready() && !codeplug_foreign());
    CHECK(count==2 && events[1].kind==UI_EV_SAVE_FAILED); unchanged_private_regions();
    sim_flash_free(); free(original);
    printf("Classic Start new: initialization, reboot, cancel/use and save failures: %d failures\n",failures);
    return failures!=0;
}
