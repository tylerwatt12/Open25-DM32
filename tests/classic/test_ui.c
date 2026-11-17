/* SPDX-License-Identifier: MIT */
#define main upstream_ui_main
#include "uiFlowTest.c"
#undef main
#include "ui/ui_svc.h"
#include "ui/ui_nav.h"

#define P25_EDIT "ok 5 ok down down ok "
#define P25_TG P25_EDIT "down down down down down down down "

#define RX_D4 "down down down down "
#define RX_D5 RX_D4 "down "
#define RX_D6 RX_D5 "down "
#define RX_D8 RX_D4 RX_D4
#define RX_CHANNEL "ok 5 ok "
#define RX_TOGGLE RX_CHANNEL RX_D6
#define RX_NEW "ok 5 up up up ok "
#define RX_HAM_NEW "1 ok ok 5 up ok "

/* Drive the real editor, then inspect the channel handed to its existing
 * storage service. The simulator already has a TX-enabled non-ham channel3;
 * no policy stand-in or new fixture state is needed. */
static void rx_saved(const char *name,const char *script,uint16_t number,bool rx_only,uint32_t tx_hz)
{
    char error[300];channel_t saved;
    sim_reset(sim_geom_find("240x320i10x5"),SIM_MAP_K);flows_run++;
    if(!sim_run(script,error,sizeof error)) {
        printf("FAIL %s: %s\n",name,error);fails++;return;
    }
    if(!sim_rec.codeplug_writes || !codeplug_channel(number,&saved) ||
       saved.rx_only!=rx_only || saved.tx_hz!=tx_hz) {
        printf("FAIL %s: saved RX-only/frequency intent\n",name);fails++;
    }
}

static void rx_defaults_and_confirmation(void)
{
    rx_saved("New outside ham defaults RX-only",RX_NEW "long:ok",41,true,467375000);
    rx_saved("Copy here outside ham defaults RX-only","ok 4 ctx ctx 3 long:ok",41,true,467375000);
    rx_saved("New ham channel keeps TX permission",RX_HAM_NEW "long:ok",41,false,146340000);
    rx_saved("Existing outside ham unrelated edit preserves TX",RX_CHANNEL RX_D5 "ok up ok long:ok",3,false,467375000);
    rx_saved("TX frequency edit outside ham defaults RX-only","1 ok " RX_CHANNEL RX_D4 "ok 4 6 2 5 5 0 ok long:ok",1,true,462550000);
    rx_saved("RX frequency carries offset outside ham RX-only","1 ok " RX_CHANNEL "down down down ok 1 5 0 9 4 0 ok long:ok",1,true,150340000);
    rx_saved("Talkaround moves effective TX outside ham RX-only",RX_CHANNEL RX_D4 "ok 4 4 6 0 0 0 ok " RX_D8 "ok long:ok",3,true,446000000);
    rx_saved("Confirmation defaults to keep RX-only",RX_NEW RX_D6 "ok expect:screen=confirm expect:focus=Keep_RX-only ok long:ok",41,true,467375000);
    rx_saved("Back leaves outside channel RX-only",RX_NEW RX_D6 "ok back long:ok",41,true,467375000);
    rx_saved("Explicit confirmation enables channel TX",RX_NEW RX_D6 "ok down ok expect:screen=channel long:ok",41,false,467375000);
    rx_saved("Ham RX-only toggle has no confirmation",RX_HAM_NEW RX_D6 "ok ok expect:screen=channel long:ok",41,false,146340000);

    char error[300],issues[SIM_LAYOUT_MAX][160];
    sim_reset(sim_geom_find("240x320i10x5"),SIM_MAP_K);flows_run++;
    sim_rec.shortened=0;
    if(!sim_run("theme:light " RX_NEW RX_D6 "ok expect:screen=confirm expect:shows=Enable_TX? expect:shows=Outside_ham_bands expect:shows=467.375 expect:shows=PTT_may_transmit. expect:focus=Keep_RX-only",error,sizeof error)) {
        printf("FAIL outside-ham confirmation copy: %s\n",error);fails++;
    }
    int layout=sim_layout_issues(issues,SIM_LAYOUT_MAX);
    if(layout || sim_rec.shortened) {
        printf("FAIL outside-ham confirmation readability: %d layout findings, %u shortened\n",layout,(unsigned)sim_rec.shortened);
        for(int i=0;i<layout && i<SIM_LAYOUT_MAX;i++)puts(issues[i]);
        fails++;
    }
    /* The same confirmation owner still treats arg0 as factory reset. */
    sim_reset(sim_geom_find("240x320i10x5"),SIM_MAP_K);flows_run++;
    ui_push(SCR_CONFIRM,0);
    if(!sim_run("wait:1 expect:shows=Factory_Reset expect:focus=No,_keep_settings ok expect:screen=home",error,sizeof error)) {
        printf("FAIL factory reset cancel unchanged: %s\n",error);fails++;
    }
    ui_push(SCR_CONFIRM,0);
    if(!sim_run("wait:1 down ok expect:screen=home expect:toast=Factory_settings_restored",error,sizeof error)) {
        printf("FAIL factory reset acceptance unchanged: %s\n",error);fails++;
    }
}

int main(void)
{
    const flow_t checks[]={
        {"Classic boot identity", "theme:light", "splash", "shows=Open25-DM32_Classic hides=RadioFoundry", 0, false},
        {"Classic About identity", "theme:light", "ok 7 0 ok", "shows=Open25-DM32_Classic shows=Open25-DM32/issues", 4, false},
        {"Classic DMR to P25", "ok 5 ok down down", "ok", "shows=P25 hides=Color_code writes=0", 1, false},
        {"Classic P25 back to FM", P25_EDIT, "ok", "shows=FM hides=P25 writes=0", 1, false},
        {"Classic P25 saves", P25_EDIT, "long:ok", "writes=1 toast=Saved", 1, true},
        {"Classic P25 talkgroup", P25_TG, "ok 4 2 ok", "shows=42", 4, false},
        {"Classic P25 NAC", P25_TG "down", "ok enc+ enc+ ok", "shows=$493", 4, false},
        {"Classic P25 hexadecimal NAC", P25_TG "down", "ok enc- enc- enc- ok", "shows=$F93", 5, false},
        {"Classic P25 ID", P25_TG "down down down", "ok 3 1 0 1 2 3 4 ok", "shows=3101234", 9, false},
        {"Classic P25 alias switch", P25_TG "down down down down", "ok long:ok", "writes=1 toast=Saved", 2, true},
        {"Classic split TX", P25_TG "down down", "ok 4 5 6 ok", "shows=$456", 5, false},
        {"Classic inherited TX unchanged", P25_TG "down down", "ok ok", "shows=Same_as_RX", 2, false},
        {"Classic explicit TX independent", P25_TG "down down", "ok 2 9 3 ok up ok 4 5 6 ok down", "shows=$293", 12, false},
        {"Classic TX inherit selected", P25_TG "down down", "ok 4 5 6 ok ok down ok", "shows=Same_as_RX", 8, false},
        {"Classic P25 talkgroup limit", P25_TG, "ok 6 5 5 3 6", "toast=Value_too_large", 6, false},
        {"Classic P25 zero talkgroup", P25_TG, "ok 0 ok long:ok", "writes=0 toast=Check_P25", 4, false},
        {"Classic Start new waits for storage", "foreign quick 2 long:ok", "down ok", "writes=1 toast=Creating_codeplug", 2, true},
    };
    for(unsigned i=0;i<sizeof checks/sizeof checks[0];i++)
        run_flow(&checks[i],sim_geom_find("240x320"),SIM_MAP_K,"Classic DM32");
    ui_event_t result={.kind=UI_EV_CODEPLUG_STARTED};
    char error[160];
    if(!ui_notify(&result) || !sim_run("wait:1 expect:toast=New_codeplug_started",error,sizeof error)) {
        puts("FAIL Classic Start new completion event"); fails++;
    }
    result.kind=UI_EV_SAVE_FAILED;
    strcpy(result.text,"Couldn't write the codeplug: not saved");
    if(!ui_notify(&result) || !sim_run("wait:1 expect:toast=Couldn't_write",error,sizeof error)) {
        puts("FAIL Classic Start new failure event"); fails++;
    }
    sim_reset(sim_geom_find("240x320i10x5"), SIM_MAP_K);
    radio_view_t *v = (radio_view_t *)sim_view();
    v->ch.mode = RADIO_MODE_P25;
    v->p25_source = 1234567;
    strcpy(v->p25_contact, "Saved caller");
    if (!sim_run("fmrx expect:shows=Saved_caller", error, sizeof error)) {
        printf("FAIL P25 contact fallback: %s\n", error);
        fails++;
    }
    strcpy(v->p25_alias, "M0ABC");
    if (!sim_run("fmrx expect:shows=M0ABC expect:hides=Saved_caller", error, sizeof error)) {
        printf("FAIL P25 over-the-air alias: %s\n", error);
        fails++;
    }
    rx_defaults_and_confirmation();
    printf("Classic P25 editor: %d flows, %d failures\n",flows_run,fails);
    return fails!=0;
}

/* The UI fixture owns no flash; use its existing radio-ID stand-in for inherited identity. */
uint32_t codeplug_p25_unit_id(uint32_t own) { return own ? own : codeplug_dmr_id(); }
