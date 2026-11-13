/* SPDX-License-Identifier: GPL-3.0-or-later */
#define main upstream_ui_main
#include "uiFlowTest.c"
#undef main
#include "ui/ui_svc.h"

#define P25_EDIT "ok 5 ok down down ok "
#define P25_TG P25_EDIT "down down down down down down down "
int main(void)
{
    const flow_t checks[]={
        {"Classic DMR to P25", "ok 5 ok down down", "ok", "shows=P25 hides=Color_code writes=0", 1, false},
        {"Classic P25 back to FM", P25_EDIT, "ok", "shows=FM hides=P25 writes=0", 1, false},
        {"Classic P25 saves", P25_EDIT, "long:ok", "writes=1 toast=Saved", 1, true},
        {"Classic P25 talkgroup", P25_TG, "ok 4 2 ok", "shows=42", 4, false},
        {"Classic P25 NAC", P25_TG "down", "ok enc+ enc+ ok", "shows=$493", 4, false},
        {"Classic P25 hexadecimal NAC", P25_TG "down", "ok enc- enc- enc- ok", "shows=$F93", 5, false},
        {"Classic P25 ID", P25_TG "down down", "ok 3 1 0 1 2 3 4 ok", "shows=3101234", 9, false},
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
    printf("Classic P25 editor: %d flows, %d failures\n",flows_run,fails);
    return fails!=0;
}
