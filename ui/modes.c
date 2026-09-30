/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "screens.h"
#include <string.h>
#include "orbit.h"
#include "theme.h"
#include <stdint.h>
#include <stdio.h>

/* Working Mode (audio source) picker - mirrors stock's "Working mode" list. Tapping a mode replays
 * the captured V2.28 switch sequence via ui_set_source_mode() and marks it selected.
 * 0=Local 1=USB-DAC 2=BT-Receiving 3=USB-Storage. */

typedef struct { const char *l1, *l2, *glyph; int mode; } modeinfo_t;
/* 2x3 grid. `mode` is the index ui_set_source_mode() takes; grid order is presentation only.
 * Row 1: Local / USB DAC / BT DAC   Row 2: BT streaming / AirPlay / USB storage
 * BT streaming (4) and AirPlay (5) use the V2.40-verified 0657 values 07 and 0A. */
static const modeinfo_t MODES[] = {
    { "Local",   "playback",  LV_SYMBOL_SD_CARD,    0 },
    { "USB",     "DAC",       LV_SYMBOL_USB,        1 },
    { "BT",      "DAC",       LV_SYMBOL_BLUETOOTH,  2 },
    { "BT",      "streaming", LV_SYMBOL_VOLUME_MAX, 4 },
    { "AirPlay", "",          LV_SYMBOL_WIFI,       5 },
    { "USB",     "storage",   LV_SYMBOL_DRIVE,      3 },
};
#define N_MODES ((int)(sizeof(MODES)/sizeof(MODES[0])))

static orbit_t g_orb;                /* six modes orbiting a hub; the hub's ring shows the state */
static int slot_of_mode(int m){ for(int i=0;i<N_MODES;i++) if(MODES[i].mode == m) return i; return -1; }

static void paint(int slot, const char *glyph){   /* slot = orbit index, or -1 for none */
    int pending = (glyph && !strcmp(glyph, LV_SYMBOL_REFRESH));
    lv_color_t acc = lv_color_hex(TH_ACCENT);
    if(pending){                                  /* the tapped mode gets a ring; the hub's ring spins */
        orbit_set_pending(&g_orb, slot, acc);
        orbit_hub_set(&g_orb, LV_SYMBOL_REFRESH, acc, "Switching...");   /* ASCII: the caption font has no ellipsis */
        orbit_hub_ring(&g_orb, ORBIT_RING_SPIN, acc, 0);
        return;
    }
    orbit_set_pending(&g_orb, -1, acc);
    for(int i = 0; i < N_MODES; i++) orbit_set_on(&g_orb, i, i == slot, acc);   /* the active mode: filled */
    if(slot >= 0){ orbit_hub_set(&g_orb, MODES[slot].glyph, acc, "Back"); orbit_hub_ring(&g_orb, ORBIT_RING_FULL, acc, 0); }
    else { orbit_hub_set(&g_orb, LV_SYMBOL_SD_CARD, lv_color_hex(TH_TXT1), "Back"); orbit_hub_ring(&g_orb, ORBIT_RING_GREY, acc, 0); }
}
static void mark_selected_mode(int cur){ paint(slot_of_mode(cur), LV_SYMBOL_OK); }
static void mark_selected(void){ mark_selected_mode(ui_get_source_mode()); }


static void hub_back_cb(lv_event_t *e){ (void)e; screen_back(); }
static uint32_t g_last_switch = 0;   /* debounce: a switch takes a few seconds to apply in the player */

/* Pending state: the tapped row shows a "switching" glyph (not the confirmed checkmark) while the
 * gadget switch is in flight - there is no source-mode completion readback, so after the switch
 * window we settle to the selection best-effort (matches the honest "Switching..." toast). */
static void mark_pending(int m){ paint(slot_of_mode(m), LV_SYMBOL_REFRESH); }

static lv_timer_t *g_settle = NULL;
static int g_pending_mode = -1;   /* the mode a switch is settling to (so reopening the screen keeps showing "switching") */
/* M17: after the switch window, settle to the CONFIRMED mode read from the real USB gadget state,
 * not a blind assumption. If the gadget shows the switch didn't take, reflect reality + say so. */
static void settle_cb(lv_timer_t *t){
    (void)t; lv_timer_del(g_settle); g_settle = NULL;
    int intended = g_pending_mode; g_pending_mode = -1;
    /* M17: DISPLAY the ACTUAL gadget state (read-only) instead of a blind timer assumption. Do NOT
     * mutate the intent mirror (g_source_mode) - it also guards coldplug, and a transient mid-transition
     * sample must not flip that guard. */
    int show = (intended >= 0) ? intended : ui_get_source_mode();
    if(intended == 0 || intended == 1 || intended == 3){   /* USB gadget modes are readback-confirmable */
        int actual = ui_detect_source_mode();
        show = actual;
        if(actual != intended) ui_toast("Mode didn't switch");
    }
    /* intended == 2 (BT receiving) is gadget-invisible and needs a phone to connect - no reliable
     * readback here, so show the intent without asserting a false confirmation. */
    mark_selected_mode(show);
}

static void row_cb(int slot){                 /* a tap inside a slice (the wheel does the hit-testing) */
    if(slot < 0 || slot >= N_MODES) return;
    int m = MODES[slot].mode;
    /* Serialise: ignore taps while the previous switch is still applying (the player's gadget
     * state-machine is asynchronous). NB we do NOT early-return on "same mode" - re-issuing must
     * always be allowed so Local works as a recover even if our cached mode is stale. */
    if(g_last_switch && lv_tick_elaps(g_last_switch) < 3000){ ui_toast("Switching..."); return; }
    g_last_switch = lv_tick_get();
    if(ui_set_source_mode(m) == 0){
        g_pending_mode = m;
        mark_pending(m);      /* async switch in flight: show "switching", not a confirmed selection */
        if(g_settle) lv_timer_del(g_settle);
        g_settle = lv_timer_create(settle_cb, 3200, NULL);   /* settle to the checkmark after the switch window */
        /* honest wording: the frames are queued; the async switch completes a moment later. */
        static const char *msg[] = {
            "Switching to local playback", "Switching to USB DAC",
            "Switching to Bluetooth receiving", "Switching to USB storage",
            "Bluetooth streaming on", "AirPlay on \xE2\x80\x93 pick the Disc on your device" };
        ui_toast(msg[m]);
    } else {
        ui_toast("Couldn't switch mode");
    }
}

void modes_open(void){
    if(g_settle && g_pending_mode >= 0) mark_pending(g_pending_mode);  /* a switch is still settling - keep showing it */
    else mark_selected();
    screen_show(SCR_WORKMODE);
}

void modes_create(lv_obj_t *root){
    lv_obj_set_style_bg_color(root, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    /* back button (kept out of the clipped top-left corner) */
    /* no header: the wheel fills the panel and its hub is the way back */

    /* orbit: six modes, clockwise from the upper right; the top stays open for the title */
    orbit_title(root, "Working mode");
    static char caps[N_MODES][32];
    orbit_item_t it[N_MODES];
    for(int i = 0; i < N_MODES; i++){
        snprintf(caps[i], sizeof caps[i], "%s%s%s", MODES[i].l1, MODES[i].l2[0] ? " " : "", MODES[i].l2);
        it[i].glyph = MODES[i].glyph; it[i].cap = caps[i];
    }
    orbit_create(&g_orb, root, it, N_MODES, -60, row_cb);
    orbit_hub_create(&g_orb, root, hub_back_cb, LV_SYMBOL_SD_CARD, "Back");
    mark_selected();
}
