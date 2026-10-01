/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "screens.h"
#include "braun.h"
#include <string.h>
#include "orbit.h"
#include "theme.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include "ipc.h"

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
static int  cur_mode(void);
static void modeinfo_refresh_and_show(void);
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
    if(show != 0 && screen_current() == SCR_WORKMODE) modeinfo_refresh_and_show();   /* the mode's own screen */
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
    else {
        mark_selected();
        if(cur_mode() != 0){ modeinfo_refresh_and_show(); return; }       /* a mode is active: its screen first */
    }
    screen_show(SCR_WORKMODE);
}

void modes_create(lv_obj_t *root){
    lv_obj_set_style_bg_color(root, lv_color_hex(TH_BG), 0);
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
    if(th_braun()) br_face(root);                          /* Braun: the grille face (knobs come from orbit.c) */
}


/* ================================================================== the active mode's screen (SCR_MODEINFO)
 * One screen for USB DAC / USB storage / BT DAC / BT streaming / AirPlay: a big icon in a status ring (Ring) or
 * on a dark knob with its lamp (Braun), the mode name, one status line and one detail line, and two buttons:
 * Modes (the picker) and Local (back to normal playback). Everything shown is read from the system in
 * cheap, non-blocking ways once a second while the screen is up. */
static lv_obj_t *g_mi_ring, *g_mi_glyph, *g_mi_knob, *g_mi_title, *g_mi_status, *g_mi_detail1, *g_mi_detail2;
static lv_timer_t *g_mi_timer;
static int g_mi_mode = -1, g_mi_wait = -2;

static int cur_mode(void){                                   /* the intent mirror, or the real gadget after a UI restart */
    int m = ui_get_source_mode();
    if(m == 0 && !(g_last_switch && lv_tick_elaps(g_last_switch) < 8000)){   /* not while a switch to Local is still tearing the gadget down */
        int d = ui_detect_source_mode(); if(d) m = d;
    }
    return m;
}
static int udc_host_connected(void){                         /* the USB device controller reports "configured" once a host enumerated us */
    DIR *d = opendir("/sys/class/udc"); if(!d) return 0;
    int ok = 0; struct dirent *e;
    while(!ok && (e = readdir(d))){
        if(e->d_name[0] == '.') continue;
        char p[128], b[32] = {0}; snprintf(p, sizeof p, "/sys/class/udc/%.60s/state", e->d_name);
        FILE *f = fopen(p, "r"); if(!f) continue;
        if(fgets(b, sizeof b, f)) ok = !strncmp(b, "configured", 10);
        fclose(f);
    }
    closedir(d); return ok;
}
static long card_gb(void){                                   /* SD capacity from the block device, readable while it is exported */
    FILE *f = fopen("/sys/block/mmcblk0/size", "r"); if(!f) return 0;
    long long sec = 0; if(fscanf(f, "%lld", &sec) != 1) sec = 0; fclose(f);
    return (long)((sec * 512LL + 500000000LL) / 1000000000LL);
}
static void mi_spin_exec(void *var, int32_t v){ lv_arc_set_rotation((lv_obj_t *)var, v % 360); }
static void mi_set_wait(int wait){                           /* Ring: a turning arc while waiting, a full ring when connected */
    if(wait == g_mi_wait) return;
    g_mi_wait = wait;
    if(g_mi_knob){ br_knob_set_on(g_mi_knob, !wait); return; }
    if(!g_mi_ring) return;
    lv_anim_delete(g_mi_ring, mi_spin_exec);
    lv_obj_set_style_arc_color(g_mi_ring, ui_current_accent(), LV_PART_INDICATOR);
    if(wait){
        lv_arc_set_value(g_mi_ring, 260);
        lv_anim_t a; lv_anim_init(&a); lv_anim_set_var(&a, g_mi_ring); lv_anim_set_exec_cb(&a, mi_spin_exec);
        lv_anim_set_values(&a, 270, 270 + 360); lv_anim_set_duration(&a, 1400);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE); lv_anim_start(&a);
    } else { lv_arc_set_rotation(g_mi_ring, 270); lv_arc_set_value(g_mi_ring, 1000); }
}
static void mi_text(lv_obj_t *l, const char *t){ if(l && strcmp(lv_label_get_text(l), t)) lv_label_set_text(l, t); }

void modeinfo_refresh(void){
    int m = cur_mode();
    if(m == 0){ screen_back(); return; }                     /* nothing active: nothing to show */
    if(m < 0 || m > 5) m = 1;
    int slot = slot_of_mode(m); if(slot < 0) slot = 1;       /* the SAME table the orbit menu is built from: identical icon + name */
    if(m != g_mi_mode){
        char nm[40]; snprintf(nm, sizeof nm, "%s%s%s", MODES[slot].l1, MODES[slot].l2[0] ? " " : "", MODES[slot].l2);
        g_mi_mode = m; g_mi_wait = -2; mi_text(g_mi_glyph, MODES[slot].glyph); mi_text(g_mi_title, nm);
    }
    char st[64] = "", d1[200] = "", d2[200] = ""; int wait = 1;
    track_state_t ts; ipc_get_state(&ts);
    switch(m){
        case 1: {                                            /* USB DAC */
            int host = udc_host_connected(); wait = !host;
            snprintf(st, sizeof st, host ? "Connected" : "Waiting for USB host");
            if(host){
                snprintf(d1, sizeof d1, "Volume %d", ts.volume > 0 ? ts.volume : 0);
                if(ts.sample_rate > 0) snprintf(d2, sizeof d2, "%d.%d kHz", ts.sample_rate / 1000, (ts.sample_rate % 1000) / 100);
            } else snprintf(d1, sizeof d1, "Plug the Disc into a computer");
        } break;
        case 3: {                                            /* USB storage */
            int host = udc_host_connected(); wait = !host;
            snprintf(st, sizeof st, host ? "Connected to computer" : "Waiting for USB host");
            long gb = card_gb(); if(gb > 0) snprintf(d1, sizeof d1, "SD card %ld GB", gb);
            snprintf(d2, sizeof d2, "Eject on the computer first");
        } break;
        case 2: {                                            /* BT DAC: a phone / computer plays to the Disc */
            char nm[64]; int c = bt_peer_name(nm, sizeof nm); wait = !c;
            snprintf(st, sizeof st, c ? "Connected" : "Waiting for a device");
            if(c) snprintf(d1, sizeof d1, "%s", nm); else snprintf(d1, sizeof d1, "Pair from the other device");
            snprintf(d2, sizeof d2, "Volume %d", ts.volume > 0 ? ts.volume : 0);
        } break;
        case 4: {                                            /* BT streaming: the Disc plays to a speaker */
            char nm[64]; int c = bt_peer_name(nm, sizeof nm); wait = !c;
            snprintf(st, sizeof st, c ? "Streaming to" : "No speaker connected");
            if(c) snprintf(d1, sizeof d1, "%s", nm); else snprintf(d1, sizeof d1, "Pair one in Bluetooth settings");
        } break;
        case 5: {                                            /* AirPlay */
            int has = ts.have_track && ts.title[0]; wait = !has;
            snprintf(st, sizeof st, has ? "Playing" : "Ready");
            if(has){ snprintf(d1, sizeof d1, "%s", ts.title); snprintf(d2, sizeof d2, "%s", ts.artist); }
            else snprintf(d1, sizeof d1, "Pick the Disc on your device");
        } break;
    }
    mi_set_wait(wait);
    mi_text(g_mi_status, st); mi_text(g_mi_detail1, d1); mi_text(g_mi_detail2, d2);
}
static void modeinfo_refresh_and_show(void){ screen_show(SCR_MODEINFO); }
static void mi_tick(lv_timer_t *t){ (void)t; if(screen_current() == SCR_MODEINFO) modeinfo_refresh(); }
static void mi_modes_cb(lv_event_t *e){ if(lv_event_get_code(e) == LV_EVENT_CLICKED) screen_show(SCR_WORKMODE); }
static void mi_local_cb(lv_event_t *e){
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if(g_last_switch && lv_tick_elaps(g_last_switch) < 3000){ ui_toast("Switching..."); return; }
    g_last_switch = lv_tick_get();
    if(ui_set_source_mode(0) == 0){ ui_toast("Switching to local playback"); g_mi_mode = -1; screen_back(); }
    else ui_toast("Couldn't switch mode");
}
static lv_obj_t *mi_pill(lv_obj_t *root, const char *txt, int x, int primary, lv_event_cb_t cb){
    lv_obj_t *b = lv_button_create(root);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 104, 40); lv_obj_align(b, LV_ALIGN_CENTER, x, 128);
    lv_obj_set_ext_click_area(b, 4);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    uint32_t bg = th_braun() ? (primary ? BR_ACC : BR_KNOB) : (primary ? TH_ACCENT : TH_SURF1);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0); lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(b); lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, th_braun() ? br_font(14, 0) : TH_F_DETAIL, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(th_braun() ? BR_KNOB_IC : TH_TXT1), 0);
    lv_obj_center(l);
    return b;
}
static lv_obj_t *mi_label(lv_obj_t *root, const lv_font_t *font, uint32_t col, int y, int w){
    lv_obj_t *l = lv_label_create(root);
    lv_label_set_text(l, "");
    lv_obj_set_style_text_font(l, font, 0); lv_obj_set_style_text_color(l, lv_color_hex(col), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_width(l, w); lv_obj_align(l, LV_ALIGN_CENTER, 0, y);
    return l;
}
void modeinfo_create(lv_obj_t *root){
    lv_obj_set_style_bg_color(root, lv_color_hex(TH_BG), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    int br = th_braun();
    if(br) br_face(root);
    if(br){
        g_mi_knob = br_knob(root, 180, 104, 56, LV_SYMBOL_USB, &lv_font_montserrat_48);
        g_mi_glyph = lv_obj_get_child(g_mi_knob, 0);                        /* the icon label the knob made */
        if(g_mi_glyph && !lv_obj_check_type(g_mi_glyph, &lv_label_class)) g_mi_glyph = NULL;
    } else {
        g_mi_ring = lv_arc_create(root);
        lv_obj_remove_style(g_mi_ring, NULL, LV_PART_KNOB);
        lv_obj_remove_flag(g_mi_ring, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(g_mi_ring, 132, 132); lv_obj_align(g_mi_ring, LV_ALIGN_CENTER, 0, -76);
        lv_arc_set_bg_angles(g_mi_ring, 0, 360); lv_arc_set_range(g_mi_ring, 0, 1000); lv_arc_set_rotation(g_mi_ring, 270);
        lv_obj_set_style_arc_width(g_mi_ring, 6, LV_PART_MAIN); lv_obj_set_style_arc_color(g_mi_ring, lv_color_hex(TH_TRACK), LV_PART_MAIN);
        lv_obj_set_style_arc_width(g_mi_ring, 6, LV_PART_INDICATOR); lv_obj_set_style_arc_rounded(g_mi_ring, true, LV_PART_INDICATOR);
        g_mi_glyph = lv_label_create(root);
        lv_label_set_text(g_mi_glyph, LV_SYMBOL_USB);
        lv_obj_set_style_text_font(g_mi_glyph, &lv_font_montserrat_48, 0);
        lv_obj_set_style_text_color(g_mi_glyph, lv_color_hex(TH_TXT1), 0);
        lv_obj_align(g_mi_glyph, LV_ALIGN_CENTER, 0, -76);
    }
    g_mi_title   = mi_label(root, br ? br_font(22, 1) : TH_F_TITLE,  br ? BR_TXT  : TH_TXT1, 6,  260);
    g_mi_status  = mi_label(root, br ? br_font(16, 0) : &lv_font_montserrat_16, br ? BR_ACC : TH_ACCENT, 36, 260);
    g_mi_detail1 = mi_label(root, br ? br_font(14, 0) : TH_F_DETAIL, br ? BR_TXT2 : TH_TXT2, 64, 250);
    g_mi_detail2 = mi_label(root, br ? br_font(14, 0) : TH_F_DETAIL, br ? BR_TXT3 : TH_TXT3, 86, 250);
    mi_pill(root, "Modes", -58, 0, mi_modes_cb);
    mi_pill(root, "Local",  58, 1, mi_local_cb);
    g_mi_timer = lv_timer_create(mi_tick, 1000, NULL);
}
