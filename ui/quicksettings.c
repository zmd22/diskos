/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "screens.h"
#include "braun.h"
#include "theme.h"
#include "orbit.h"
#include "ipc.h"
#include "config.h"
#include "scanner.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Quick Settings, "orbit on rails": six round toggles orbit a hub showing the playing cover inside its
 * progress ring (the Ring Now Playing motif). Brightness is a control arc on the top rim; the battery is
 * an indicator arc on the bottom rim with the Home screen's logic (white, red + slow pulse at <=15%).
 * A tap on the cover goes Home, the small centre button plays/pauses. A short tap on Wi-Fi / Bluetooth
 * toggles the radio, a long press opens its settings. Background: the album's blurred backdrop, dimmed. */

LV_FONT_DECLARE(font_icons_28)          /* FontAwesome 28px */
#define WI_SUN "\xEF\x86\x85"           /* f185 sun */
#define QS_ARC_D     348                /* rim arcs */
#define QS_ARC_W     TH_ARC_CTL         /* brightness: a control arc, rounded ends */

enum { T_WIFI, T_BT, T_LIB, T_RESCAN, T_MODE, T_SET, T_N };
static orbit_t g_orb;
static lv_obj_t *g_bright, *g_batt, *g_bg, *g_dim;
static lv_obj_t *g_cover_clip, *g_cover_img, *g_cover_note, *g_prog, *g_pp, *g_pp_glyph;
static lv_timer_t *g_prog_timer;
static int g_batt_low = -1;
static lv_obj_t *g_batt_icon;
static int g_scanning = -1;
static void rescan_show(int on);

static void bright_change_cb(lv_event_t *e){ ui_backlight(lv_arc_get_value(lv_event_get_target(e))); }
static void bright_release_cb(lv_event_t *e){ ui_set_brightness(lv_arc_get_value(lv_event_get_target(e))); }
static void pp_cb(lv_event_t *e){ (void)e; ipc_send_cmd("0201000C0000"); }
static void home_cb(lv_event_t *e){ (void)e; screen_show(SCR_HOME); }            /* the cover leads Home */

/* the Mode toggle wears the icon of the mode the player is actually in (same set as Working Mode) */
static const char *mode_glyph(void){
    switch(ui_get_source_mode()){
        case 1:  return LV_SYMBOL_USB;
        case 2:  return LV_SYMBOL_BLUETOOTH;
        case 3:  return LV_SYMBOL_DRIVE;
        case 4:  return LV_SYMBOL_VOLUME_MAX;
        case 5:  return LV_SYMBOL_WIFI;
        default: return LV_SYMBOL_SD_CARD;
    }
}
static void paint_radios(void){
    lv_color_t acc = lv_color_hex(TH_ACCENT);
    orbit_set_on(&g_orb, T_WIFI, cfg_get_int("wifi_on", 1), acc);
    int bs = bt_state();                                        /* the real radio, not the saved intent */
    orbit_set_on(&g_orb, T_BT, bs == BT_ON, acc);
    orbit_set_pending(&g_orb, bs == BT_TURNING_ON ? T_BT : -1, acc);   /* ringed while it comes up */
}
static void rescan_go(void){ ui_rescan_library(); ui_toast("Rescan requested"); rescan_show(1); }
static void pick_cb(int i){                                       /* short taps */
    switch(i){
        case T_WIFI: { int on = wifi_toggle(); orbit_set_on(&g_orb, T_WIFI, on, lv_color_hex(TH_ACCENT));
                       ui_toast(on ? "Turning on Wi-Fi\xE2\x80\xA6" : "Wi-Fi off"); } break;
        case T_BT:   { int on = bt_toggle(); paint_radios();
                       ui_toast(on ? "Turning on Bluetooth\xE2\x80\xA6" : "Bluetooth off"); } break;
        case T_LIB:    screen_show(SCR_LIBRARY); break;
        case T_RESCAN: if(scanner_active()){ ui_toast("Already scanning"); break; }
                       fileops_confirm("Rescan library?", "Looks for new and changed music", "Rescan", rescan_go); break;
        case T_MODE:   modes_open(); break;
        case T_SET:    screen_show(SCR_SETTINGS); break;
    }
}
static void wifi_long_cb(lv_event_t *e){ (void)e; wifi_open(); }
static void bt_long_cb(lv_event_t *e){ (void)e; bt_open(); }

/* the hub's progress ring follows the track (1 s poll; only redraws on a visible change) */
static void prog_tick(lv_timer_t *t){
    (void)t;
    if(!g_prog || screen_current() != SCR_QUICK) return;
    rescan_show(scanner_active() ? 1 : 0);                     /* also picks up a scan started elsewhere */
    paint_radios();                                             /* BT comes up in the background: follow it */
    track_state_t st; ipc_get_state(&st);
    int v = (st.have_track && st.duration_ms > 0) ? (int)((long long)st.position_ms * 1000 / st.duration_ms) : 0;
    if(abs(v - lv_arc_get_value(g_prog)) >= 3) lv_arc_set_value(g_prog, v);
}
/* rescan: while the library scan runs, the Rescan icon turns red and spins (1 turn/s) */
static void spin_exec(void *var, int32_t v){ lv_obj_set_style_transform_rotation((lv_obj_t *)var, v, 0); }
static void rescan_show(int on){
    if(on == g_scanning) return;
    g_scanning = on;
    lv_obj_t *ic = g_orb.icon[T_RESCAN];
    lv_anim_delete(ic, spin_exec);
    lv_obj_set_style_text_color(ic, lv_color_hex(on ? TH_ACCENT : (th_braun() ? BR_KNOB_IC : TH_TXT1)), 0);
    if(g_orb.cap[T_RESCAN]){ lv_label_set_text(g_orb.cap[T_RESCAN], on ? "Scanning" : "Rescan");
                            lv_obj_set_style_text_color(g_orb.cap[T_RESCAN], lv_color_hex(on ? TH_TXT1 : TH_TXT2), 0); }
    if(on){
        lv_obj_update_layout(ic);
        lv_obj_set_style_transform_pivot_x(ic, lv_obj_get_width(ic) / 2, 0);
        lv_obj_set_style_transform_pivot_y(ic, lv_obj_get_height(ic) / 2, 0);
        lv_anim_t a; lv_anim_init(&a);
        lv_anim_set_var(&a, ic); lv_anim_set_exec_cb(&a, spin_exec);
        lv_anim_set_values(&a, 0, 3600); lv_anim_set_duration(&a, 1000);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE); lv_anim_start(&a);
    } else lv_obj_set_style_transform_rotation(ic, 0, 0);
}
/* battery: the Home screen's logic - white, red with a slow breathing pulse at 15% and below */
static void batt_pulse(void *var, int32_t v){ lv_obj_set_style_arc_opa((lv_obj_t *)var, (lv_opa_t)v, LV_PART_INDICATOR); }
static void batt_set_low(int low){
    if(!g_batt || low == g_batt_low) return;
    g_batt_low = low;
    lv_anim_delete(g_batt, batt_pulse);
    lv_obj_set_style_arc_color(g_batt, lv_color_hex(th_braun() ? (low ? BR_ACC : BR_TXT) : (low ? TH_ACCENT : TH_TXT1)), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(g_batt, LV_OPA_COVER, LV_PART_INDICATOR);
    if(low){
        lv_anim_t a; lv_anim_init(&a);
        lv_anim_set_var(&a, g_batt); lv_anim_set_exec_cb(&a, batt_pulse);
        lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_30); lv_anim_set_duration(&a, 900);
        lv_anim_set_playback_duration(&a, 900); lv_anim_set_repeat_count(&a, 3);   /* ends at full opacity */
        lv_anim_start(&a);
    }
}

void quicksettings_create(lv_obj_t *root)
{
    lv_obj_set_style_bg_color(root, lv_color_hex(TH_BG), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    /* media surface: the album's blurred backdrop, dimmed well down (hidden when there is no art) */
    g_bg = lv_image_create(root);
    lv_obj_set_size(g_bg, 360, 360); lv_obj_center(g_bg);
    lv_obj_clear_flag(g_bg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(g_bg, LV_OBJ_FLAG_HIDDEN);
    g_dim = lv_obj_create(root);
    lv_obj_remove_style_all(g_dim);
    lv_obj_set_size(g_dim, 360, 360);
    lv_obj_set_style_bg_color(g_dim, lv_color_hex(TH_BG), 0);
    lv_obj_set_style_bg_opa(g_dim, 210, 0);
    lv_obj_clear_flag(g_dim, LV_OBJ_FLAG_CLICKABLE);

    /* brightness: a control arc on the top rim, rounded ends; ADV_HITTEST keeps its touch area on the band */
    g_bright = lv_arc_create(root);
    lv_obj_remove_style(g_bright, NULL, LV_PART_KNOB);
    lv_obj_set_size(g_bright, QS_ARC_D, QS_ARC_D);
    lv_obj_center(g_bright);
    lv_obj_add_flag(g_bright, LV_OBJ_FLAG_ADV_HITTEST);
    lv_arc_set_rotation(g_bright, 0);
    lv_arc_set_bg_angles(g_bright, 228, 312);
    lv_arc_set_range(g_bright, 4, 40);
    lv_arc_set_value(g_bright, ui_get_brightness());
    lv_obj_set_style_arc_width(g_bright, QS_ARC_W, LV_PART_MAIN);
    lv_obj_set_style_arc_color(g_bright, lv_color_hex(th_braun() ? BR_SURF : TH_TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(g_bright, true, LV_PART_MAIN);
    lv_obj_set_style_arc_width(g_bright, QS_ARC_W, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(g_bright, lv_color_hex(th_braun() ? BR_TXT : TH_TXT1), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(g_bright, true, LV_PART_INDICATOR);
    lv_obj_add_event_cb(g_bright, bright_change_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(g_bright, bright_release_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(g_bright, bright_release_cb, LV_EVENT_PRESS_LOST, NULL);
    lv_obj_t *sun = lv_label_create(root);
    lv_obj_clear_flag(sun, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text(sun, WI_SUN);
    lv_obj_set_style_text_font(sun, &font_icons_28, 0);
    lv_obj_set_style_transform_scale(sun, 128, 0);            /* half size: a small marker under the arc */
    lv_obj_set_style_text_color(sun, lv_color_hex(TH_TXT2), 0);
    lv_obj_align(sun, LV_ALIGN_TOP_MID, 0, 18);

    /* battery: an indicator arc on the bottom rim, filling left to right; display only */
    g_batt = lv_arc_create(root);
    lv_obj_remove_style(g_batt, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(g_batt, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(g_batt, QS_ARC_D, QS_ARC_D);
    lv_obj_center(g_batt);
    lv_arc_set_rotation(g_batt, 0);
    lv_arc_set_bg_angles(g_batt, 48, 132);
    lv_arc_set_mode(g_batt, LV_ARC_MODE_REVERSE);
    lv_arc_set_range(g_batt, 0, 100);
    lv_obj_set_style_arc_width(g_batt, QS_ARC_W, LV_PART_MAIN);
    lv_obj_set_style_arc_color(g_batt, lv_color_hex(th_braun() ? BR_SURF : TH_TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(g_batt, true, LV_PART_MAIN);
    lv_obj_set_style_arc_width(g_batt, QS_ARC_W, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(g_batt, true, LV_PART_INDICATOR);
    batt_set_low(0);
    g_batt_icon = lv_label_create(root);                        /* battery glyph above the arc, the sun's mirror */
    lv_obj_clear_flag(g_batt_icon, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text(g_batt_icon, LV_SYMBOL_BATTERY_FULL);
    lv_obj_set_style_text_font(g_batt_icon, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(g_batt_icon, lv_color_hex(TH_TXT2), 0);
    lv_obj_align(g_batt_icon, LV_ALIGN_BOTTOM_MID, 0, -20);

    /* the six toggles, clockwise from the upper right, with captions */
    static const orbit_item_t it[T_N] = {
        { LV_SYMBOL_WIFI, "Wi-Fi" }, { LV_SYMBOL_BLUETOOTH, "Bluetooth" }, { LV_SYMBOL_DIRECTORY, "Library" },
        { LV_SYMBOL_REFRESH, "Rescan" }, { LV_SYMBOL_SD_CARD, "Mode" }, { LV_SYMBOL_SETTINGS, "Settings" } };
    orbit_create(&g_orb, root, it, T_N, -60, pick_cb);
    lv_obj_add_event_cb(g_orb.btn[T_WIFI], wifi_long_cb, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_add_event_cb(g_orb.btn[T_BT],   bt_long_cb,   LV_EVENT_LONG_PRESSED, NULL);
    orbit_set_glyph(&g_orb, T_MODE, mode_glyph());
    paint_radios();

    /* hub: progress ring around the cover; the cover goes Home, the centre button plays/pauses */
    g_prog = lv_arc_create(root);
    lv_obj_remove_style(g_prog, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(g_prog, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(g_prog, ORBIT_RING, ORBIT_RING);
    lv_obj_center(g_prog);
    lv_arc_set_rotation(g_prog, 270);
    lv_arc_set_bg_angles(g_prog, 0, 360);
    lv_arc_set_range(g_prog, 0, 1000);
    lv_obj_set_style_arc_width(g_prog, TH_ARC_IND, LV_PART_MAIN);
    lv_obj_set_style_arc_color(g_prog, lv_color_hex(th_braun() ? BR_SURF : TH_TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_width(g_prog, TH_ARC_IND, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(g_prog, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(g_prog, th_braun() ? lv_color_hex(BR_ACC) : ui_media_accent(), LV_PART_INDICATOR);
    g_cover_clip = lv_button_create(root);
    lv_obj_remove_style_all(g_cover_clip);
    lv_obj_set_size(g_cover_clip, ORBIT_HUB, ORBIT_HUB);
    lv_obj_center(g_cover_clip);
    lv_obj_set_style_radius(g_cover_clip, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(g_cover_clip, true, 0);
    lv_obj_set_style_bg_color(g_cover_clip, ui_media_accent(), 0);
    lv_obj_set_style_bg_opa(g_cover_clip, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(g_cover_clip, home_cb, LV_EVENT_CLICKED, NULL);
    g_cover_note = lv_label_create(g_cover_clip);                 /* shown when there's no cover */
    lv_label_set_text(g_cover_note, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_font(g_cover_note, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(g_cover_note, lv_color_hex(TH_TXT1), 0);
    lv_obj_align(g_cover_note, LV_ALIGN_CENTER, 0, -22);
    g_cover_img = lv_image_create(g_cover_clip);
    lv_obj_clear_flag(g_cover_img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(g_cover_img, LV_OBJ_FLAG_HIDDEN);
    g_pp = lv_button_create(root);
    lv_obj_remove_style_all(g_pp);
    lv_obj_set_size(g_pp, 40, 40);
    lv_obj_center(g_pp);
    lv_obj_set_style_radius(g_pp, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(g_pp, lv_color_hex(TH_BG), 0);
    lv_obj_set_style_bg_opa(g_pp, 170, 0);
    lv_obj_set_style_bg_opa(g_pp, 230, LV_STATE_PRESSED);
    lv_obj_set_ext_click_area(g_pp, 4);
    lv_obj_add_event_cb(g_pp, pp_cb, LV_EVENT_CLICKED, NULL);
    g_pp_glyph = lv_label_create(g_pp);
    lv_label_set_text(g_pp_glyph, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_font(g_pp_glyph, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(g_pp_glyph, lv_color_hex(TH_TXT1), 0);
    lv_obj_center(g_pp_glyph);

    if(!g_prog_timer) g_prog_timer = lv_timer_create(prog_tick, 1000, NULL);
    if(th_braun()){ br_face(root); if(g_dim) lv_obj_add_flag(g_dim, LV_OBJ_FLAG_HIDDEN);   /* Braun: the grille face, no dimming veil */
                    lv_obj_set_style_text_color(g_batt_icon, lv_color_hex(BR_TXT2), 0);
                    for(uint32_t k = 0; k < lv_obj_get_child_count(root); k++){ lv_obj_t *o = lv_obj_get_child(root, k);   /* the sun marker */
                        if(lv_obj_check_type(o, &lv_label_class) && lv_obj_get_style_text_font(o, 0) == &font_icons_28) lv_obj_set_style_text_color(o, lv_color_hex(BR_TXT2), 0); } }
}

/* the cover (RAM decode, scaled into the hub) and the blurred backdrop; called on track / art changes */
void quicksettings_set_art(const void *cover_dsc, const void *backdrop_src)
{
    if(!g_cover_img) return;
    lv_image_set_src(g_cover_img, NULL);
    if(cover_dsc){
        const lv_image_dsc_t *d = cover_dsc;
        lv_image_set_src(g_cover_img, cover_dsc);
        if(d->header.w > 0) lv_image_set_scale(g_cover_img, (uint32_t)(ORBIT_HUB * 256 / d->header.w) + 2);
        lv_obj_center(g_cover_img);
        lv_obj_remove_flag(g_cover_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_cover_note, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(g_cover_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(g_cover_note, LV_OBJ_FLAG_HIDDEN);
    }
    lv_image_set_src(g_bg, NULL);
    if(backdrop_src && !th_braun()){ lv_image_set_src(g_bg, backdrop_src); lv_obj_remove_flag(g_bg, LV_OBJ_FLAG_HIDDEN); }   /* Braun: the grille, no album wash */
    else lv_obj_add_flag(g_bg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(g_cover_clip, ui_media_accent(), 0);
    lv_obj_set_style_arc_color(g_prog, th_braun() ? lv_color_hex(BR_ACC) : ui_media_accent(), LV_PART_INDICATOR);
}
void quicksettings_set_battery(int pct, int charging)
{
    if(!g_batt) return;
    if(pct < 0) pct = 0;
    if(pct > 100) pct = 100;
    lv_arc_set_value(g_batt, pct);
    batt_set_low(pct <= 15);
    if(g_batt_icon){
        const char *g = charging ? LV_SYMBOL_CHARGE : pct > 80 ? LV_SYMBOL_BATTERY_FULL : pct > 55 ? LV_SYMBOL_BATTERY_3
                      : pct > 30 ? LV_SYMBOL_BATTERY_2 : pct > 10 ? LV_SYMBOL_BATTERY_1 : LV_SYMBOL_BATTERY_EMPTY;
        if(strcmp(lv_label_get_text(g_batt_icon), g)) lv_label_set_text(g_batt_icon, g);
        lv_obj_set_style_text_color(g_batt_icon, lv_color_hex(pct <= 15 && !charging ? TH_ACCENT : TH_TXT2), 0);
    }
}
void quicksettings_set_now_playing(const char *title, const char *artist, int playing)
{
    (void)title; (void)artist;
    if(g_pp_glyph) lv_label_set_text(g_pp_glyph, playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    if(g_prog) lv_obj_set_style_arc_color(g_prog, th_braun() ? lv_color_hex(BR_ACC) : ui_media_accent(), LV_PART_INDICATOR);
}
void quicksettings_set_volume(int vol){ (void)vol; }       /* the volume arc gave way to the battery */
void quicksettings_refresh(int playing)
{
    if(g_pp_glyph) lv_label_set_text(g_pp_glyph, playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    if(g_bright && !lv_obj_has_state(g_bright, LV_STATE_PRESSED))   /* don't fight an active drag */
        lv_arc_set_value(g_bright, ui_get_brightness());
    orbit_set_glyph(&g_orb, T_MODE, mode_glyph());
    paint_radios();
}

/* ---- compatibility with 1.1's screen manager ----------------------------------------------------
 * Upstream's Quick Settings is user-configurable: a tile palette plus a config screen (SCR_QSCONFIG),
 * and the manager rebuilds the panel when that changes. This fork's panel is a fixed layout, so the
 * rebuild is a no-op and the config screen just says so rather than offering switches that do nothing. */
void quicksettings_build(void){ }

static lv_obj_t *g_qscfg_root;
void qsconfig_create(lv_obj_t *root){
    g_qscfg_root = root;
    lv_obj_set_style_bg_color(root, lv_color_hex(TH_BG), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    ui_header(root, "Quick Settings");
    lv_obj_t *m = lv_label_create(root);
    lv_label_set_text(m, "This build uses a fixed panel:\n"
                         "brightness and battery arcs,\nthe cover and six toggles.");
    lv_label_set_long_mode(m, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(m, 50, 120); lv_obj_set_size(m, 260, 120);
    lv_obj_set_style_text_align(m, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(m, TH_F_DETAIL, 0);
    lv_obj_set_style_text_color(m, lv_color_hex(TH_TXT2), 0);
}
void qsconfig_refresh(void){ }
