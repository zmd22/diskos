/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "screens.h"
#include "braun.h"
void bhome_create(lv_obj_t *root); void bhome_set_clock(const char *t, const char *s); void bhome_set_weather(const char *s);
void bhome_set_status(int batt, int charging, int wifi, int bt); void bhome_set_now_playing(const char *title, const char *artist, bool playing);
#include "theme.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <math.h>
#include "ipc.h"

LV_FONT_DECLARE(font_weather16)
static lv_font_t s_wfont;   /* montserrat_14 with the weather-icon font as fallback */





static lv_obj_t *g_clock;
static lv_obj_t *g_clock_sub;
static lv_obj_t *g_home_bg;      /* full-screen blurred album backdrop (matches Now Playing) */
static lv_obj_t *g_batt_arc;     /* fine battery arc across the top rim (red + pulsing when low) */
static int       g_batt_low = 0;
static int       g_np_frac = -1; /* now-playing progress 0..1000 drawn round the capsule, -1 = none */
static lv_obj_t *g_home_scrim;   /* dark overlay over the backdrop so text stays readable */
static lv_obj_t *g_status;       /* top status row: wifi / bt / battery */
static lv_obj_t *g_weather;      /* weather line under the date */
static lv_obj_t *g_status_arc;
static lv_obj_t *g_np_capsule;
static lv_obj_t *g_np_thumb;
static lv_obj_t *g_np_art_img;
static lv_obj_t *g_np_thumb_glyph;
static lv_obj_t *g_np_title;
static lv_obj_t *g_np_artist;
static lv_obj_t *g_np_state;
static home_settings_click_cb_t g_settings_cb;
static lv_color_t g_accent;

static void nav_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    screen_show((int)(uintptr_t)lv_event_get_user_data(e));
}

/* Tap the home weather glance -> open the full weather app (the natural glance->detail flow, so the
 * app no longer needs a buried Apps entry). No-op when the line is empty (weather off/not fetched). */
static void weather_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    const char *t = g_weather ? lv_label_get_text(g_weather) : NULL;
    if (t && t[0]) weather_app_open();
}

__attribute__((unused)) static void settings_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    if (g_settings_cb) {
        g_settings_cb();
    } else {
        /* Requested screen enum has no settings screen yet. */
        screen_show(SCR_HOME);
    }
}

static void pp_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED){
        ui_defer_sleep();   /* a play/pause tap changes state -> don't let the sleep check race a stale read */
        ipc_send_cmd("0201000C0000");   /* play/pause toggle */
    }
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text,
                            const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    return label;
}

__attribute__((unused)) static lv_obj_t *make_tile(lv_obj_t *parent, int x, int y, int w, const char *symbol,
                           const char *text, lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, 92);
    lv_obj_set_style_radius(btn, 22, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1C1C1E), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_70, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x2C2C2E), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(btn, LV_OPA_10, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);

    lv_obj_t *ic = make_label(btn, symbol, &lv_font_montserrat_24,
                              lv_color_hex(0xFFFFFF));
    lv_obj_set_pos(ic, 0, 18);
    lv_obj_set_width(ic, w);

    lv_obj_t *title = make_label(btn, text, &lv_font_montserrat_16,
                                 lv_color_hex(0xFFFFFF));
    lv_obj_set_pos(title, 0, 54);
    lv_obj_set_width(title, w);

    return btn;
}

/* A wide rounded "pill" button: icon + label, left-aligned, vertically centered. */
__attribute__((unused)) static lv_obj_t *make_pill(lv_obj_t *parent, int x, int y, int w, int h,
                           const char *symbol, const char *text,
                           lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_radius(btn, h / 2, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1C1C1E), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_70, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x2C2C2E), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_opa(btn, LV_OPA_10, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);

    lv_obj_t *ic = lv_label_create(btn);
    lv_label_set_text(ic, symbol);
    lv_obj_set_style_text_font(ic, w < 180 ? &lv_font_montserrat_18 : &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(ic, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(ic, LV_ALIGN_LEFT_MID, w < 180 ? 24 : 30, 0);

    lv_obj_t *title = lv_label_create(btn);
    lv_label_set_text(title, text);
    lv_obj_set_style_text_font(title, w < 180 ? &lv_font_montserrat_14 : &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, w < 180 ? 52 : 76, 0);   /* compact pill when paired */
    return btn;
}

void home_set_settings_click_cb(home_settings_click_cb_t cb)
{
    g_settings_cb = cb;
}

void home_set_clock(const char *time_text, const char *sub_text)
{
    if(th_braun()){ bhome_set_clock(time_text, sub_text); return; }
    if (g_clock) lv_label_set_text(g_clock, time_text ? time_text : "--:--");
    if (g_clock_sub) lv_label_set_text(g_clock_sub, sub_text ? sub_text : "");
}

/* "<icon>  14°C  Partly cloudy" -> "<icon>  14°C": the icon and temperature only (the condition dropped) */
static void wx_short(const char *in, char *out, size_t cap){
    if(!in){ out[0] = 0; return; }
    const char *a = strstr(in, "  "); const char *b = a ? strstr(a + 2, "  ") : NULL;
    size_t n = b ? (size_t)(b - in) : strlen(in); if(n >= cap) n = cap - 1;
    memcpy(out, in, n); out[n] = 0;
}
void home_set_weather(const char *text)
{
    char s[96]; wx_short(text, s, sizeof s);
    if(th_braun()){ const char *p = s; while(*p == ' ') p++; bhome_set_weather(p); return; }
    if (g_weather) lv_label_set_text(g_weather, s);          /* icon + temperature, above the clock */
}

/* Update the top status row. batt 0-100, charging/wifi/bt are booleans. */

/* ---- battery arc: glow when low -------------------------------------------------------------------- */
static void batt_pulse_exec(void *obj, int32_t v){ lv_obj_set_style_arc_opa((lv_obj_t *)obj, (lv_opa_t)v, LV_PART_INDICATOR); }
static void batt_pulse_done(lv_anim_t *a){ lv_obj_set_style_arc_opa((lv_obj_t *)a->var, LV_OPA_COVER, LV_PART_INDICATOR); }
static void batt_set_low(int low){
    if(!g_batt_arc || low == g_batt_low) return;
    g_batt_low = low;
    lv_anim_delete(g_batt_arc, batt_pulse_exec);
    lv_obj_set_style_arc_color(g_batt_arc, lv_color_hex(low ? UI_RED : 0xFFFFFF), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(g_batt_arc, LV_OPA_COVER, LV_PART_INDICATOR);
    if(low){                                   /* slow breathing glow below 15% */
        lv_anim_t a; lv_anim_init(&a);
        lv_anim_set_var(&a, g_batt_arc);
        lv_anim_set_exec_cb(&a, batt_pulse_exec);
        lv_anim_set_values(&a, LV_OPA_40, LV_OPA_COVER);
        lv_anim_set_time(&a, 900); lv_anim_set_playback_time(&a, 900);
        lv_anim_set_repeat_count(&a, 3);          /* a few breaths, then steady red: no endless redraws on a low battery */
        lv_anim_set_completed_cb(&a, batt_pulse_done);
        lv_anim_start(&a);
    }
}

/* ---- now-playing progress traced round the capsule --------------------------------------------------
 * Starts at the art circle (left end, mid-height), runs right along the top, round the right end, back
 * along the bottom, and closes at the art when the track ends. Drawn as short line segments. */
__attribute__((unused)) static void np_outline_cb(lv_event_t *e){
    if(g_np_frac <= 0) return;
    lv_obj_t *obj = lv_event_get_target(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a; lv_obj_get_coords(obj, &a);
    const float w = 2.5f, r = (float)(lv_area_get_height(&a)) / 2.0f - w / 2.0f;
    const float x1 = a.x1 + w / 2 + r, x2 = a.x2 - w / 2 - r, cy = (a.y1 + a.y2) / 2.0f;
    const float top = cy - r, bot = cy + r, straight = x2 - x1, halfc = 3.14159265f * r / 2.0f;
    const float total = 2 * straight + 4 * halfc;
    float want = total * (float)g_np_frac / 1000.0f;
    lv_draw_line_dsc_t d; lv_draw_line_dsc_init(&d);
    d.color = g_accent; d.width = (int32_t)w; d.round_start = 1; d.round_end = 1;   /* media surface: album colour */
    float px = x1 - r, py = cy, walked = 0;
    const int STEPS = 24;
    #define SEG(nx, ny) do{ float dx=(nx)-px, dy=(ny)-py, sl=sqrtf(dx*dx+dy*dy); \
        if(walked + sl > want){ float k=(want-walked)/sl; d.p1.x=(lv_value_precise_t)px; d.p1.y=(lv_value_precise_t)py; \
            d.p2.x=(lv_value_precise_t)(px+dx*k); d.p2.y=(lv_value_precise_t)(py+dy*k); lv_draw_line(layer,&d); return; } \
        d.p1.x=(lv_value_precise_t)px; d.p1.y=(lv_value_precise_t)py; d.p2.x=(lv_value_precise_t)(nx); d.p2.y=(lv_value_precise_t)(ny); \
        lv_draw_line(layer,&d); walked += sl; px=(nx); py=(ny); }while(0)
    for(int i = 1; i <= STEPS; i++){ float t = 3.14159265f + 1.5707963f * i / STEPS; SEG(x1 + r*cosf(t), cy + r*sinf(t)); }  /* left cap, up */
    SEG(x2, top);                                                                                           /* top edge */
    for(int i = 1; i <= STEPS*2; i++){ float t = -1.5707963f + 3.14159265f * i / (STEPS*2); SEG(x2 + r*cosf(t), cy + r*sinf(t)); } /* right cap */
    SEG(x1, bot);                                                                                           /* bottom edge */
    for(int i = 1; i <= STEPS; i++){ float t = 1.5707963f + 1.5707963f * i / STEPS; SEG(x1 + r*cosf(t), cy + r*sinf(t)); }  /* left cap, back */
    #undef SEG
}
/* poll position once a second; only repaint the capsule when the drawn length would change */
static void disc_place(int frac);
static lv_obj_t *g_ring;
static lv_timer_t *g_settle;
static void np_progress_tick(lv_timer_t *t){
    (void)t;
    if(!g_np_capsule) return;
    track_state_t st; ipc_get_state(&st);
    int f = (st.have_track && st.duration_ms > 0) ? (int)(st.position_ms * 1000 / st.duration_ms) : -1;
    if(f > 1000) f = 1000;
    if(f != g_np_frac && (f < 0 || g_np_frac < 0 || abs(f - g_np_frac) >= 3)){
        g_np_frac = f;
        if(g_ring) lv_arc_set_value(g_ring, f < 0 ? 0 : f);   /* only the ring's and the disc's areas redraw */
        disc_place(f < 0 ? 0 : f);
    }
}

void home_set_status(int batt, int charging, int wifi, int bt)
{
    if(th_braun()){ bhome_set_status(batt, charging, wifi, bt); return; }
    if (!g_status) return;
    char buf[80]; char *p = buf; *p = 0;
    if (wifi) { p += sprintf(p, LV_SYMBOL_WIFI "  "); }
    if (bt)   { p += sprintf(p, LV_SYMBOL_BLUETOOTH "  "); }
    const char *bs = batt >= 90 ? LV_SYMBOL_BATTERY_FULL :
                     batt >= 65 ? LV_SYMBOL_BATTERY_3 :
                     batt >= 40 ? LV_SYMBOL_BATTERY_2 :
                     batt >= 15 ? LV_SYMBOL_BATTERY_1 : LV_SYMBOL_BATTERY_EMPTY;
    /* while charging show just the bolt + % (the % gives the level); a bolt jammed against the
     * battery glyph looked mashed. Not charging -> the battery-level glyph. */
    if (batt >= 0) p += sprintf(p, "%s", charging ? LV_SYMBOL_CHARGE : bs);   /* the arc shows the level */
    lv_label_set_text(g_status, buf);
    if (g_batt_arc && batt >= 0) {
        lv_arc_set_value(g_batt_arc, batt);
        batt_set_low(batt <= 15 && !charging);
    }
}

void home_set_now_playing(const char *title, const char *artist,
                          lv_color_t accent, bool playing)
{
    if(th_braun()){ bhome_set_now_playing(title, artist, playing); return; }
    g_accent = accent;

    if (g_np_title){
        const char *nt = title ? title : "Not Playing";
        if(strcmp(lv_label_get_text(g_np_title), nt)){
            lv_label_set_text(g_np_title, nt);
            lv_label_set_long_mode(g_np_title, LV_LABEL_LONG_SCROLL_CIRCULAR);   /* scroll a while, then settle */
            if(g_settle){ lv_timer_reset(g_settle); lv_timer_resume(g_settle); }
        }
        lv_obj_set_style_text_color(g_np_title, title ? accent : lv_color_hex(TH_TXT3), 0);   /* idle: quiet grey */
    }
    if (g_ring) lv_obj_set_style_arc_color(g_ring, accent, LV_PART_INDICATOR);
    if (g_np_state) lv_obj_set_style_text_color(g_np_state, accent, 0);
    if (g_np_artist) lv_label_set_text(g_np_artist, artist ? artist : "");
    if (g_np_state) lv_label_set_text(g_np_state, playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);

    if (g_np_thumb) lv_obj_set_style_bg_color(g_np_thumb, accent, 0);
    if (g_status_arc) lv_obj_set_style_arc_color(g_status_arc, accent, LV_PART_INDICATOR);
}

/* repaint the now-playing capsule with the live accent (called from ui.c apply_accent so
 * Home tracks the same colour as Now Playing - static or album-dynamic). */
void home_set_accent(lv_color_t accent)
{
    if(th_braun()) return;                                   /* Braun: orange only, no album colour */
    g_accent = accent;
    if (g_ring) lv_obj_set_style_arc_color(g_ring, accent, LV_PART_INDICATOR);
    if (g_np_title) lv_obj_set_style_text_color(g_np_title, accent, 0);
    if (g_np_state) lv_obj_set_style_text_color(g_np_state, accent, 0);
    if (g_np_thumb) lv_obj_set_style_bg_color(g_np_thumb, accent, 0);
    if (g_status_arc) lv_obj_set_style_arc_color(g_status_arc, accent, LV_PART_INDICATOR);
}

/* full-screen blurred backdrop (the 360px gblur'd cover), or NULL to clear -> black */
void home_set_backdrop(const void *src)
{
    if(th_braun()) return;
    if (g_home_bg) lv_image_set_src(g_home_bg, NULL);   /* same RAM buffer, new pixels: force a redraw */
    if (!g_home_bg || !g_home_scrim) return;
    if (src) {
        lv_image_set_src(g_home_bg, src);
        lv_obj_clear_flag(g_home_bg, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_home_scrim, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(g_home_bg, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_home_scrim, LV_OBJ_FLAG_HIDDEN);
    }
}

void home_set_art_src(const void *src)
{
    if(th_braun()) return;
    if (!g_np_art_img || !g_np_thumb_glyph) return;

    if (src) {
        /* src is a native-size 42px thumb (decoded by ui.c) - display 1:1, no
         * runtime scaling. The 42px circular thumb clips it to a disc. */
        lv_image_set_src(g_np_art_img, src);
        lv_obj_center(g_np_art_img);                          /* 42px thumb, clipped to the 38px disc */
        lv_obj_clear_flag(g_np_art_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_np_thumb_glyph, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(g_np_art_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_np_thumb_glyph, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ---- Ring clock helpers ---- */
#define HR_R    108                                         /* progress ring radius */
#define HR_DISC 38                                          /* cover disc diameter */
static void disc_place(int frac){                           /* put the cover on the ring at frac (0..1000) */
    if(!g_np_thumb) return;
    float a = (-90.0f + frac * 0.36f) * 0.0174533f;
    lv_obj_set_pos(g_np_thumb, 180 + (int)lroundf(HR_R * cosf(a)) - HR_DISC / 2, 180 + (int)lroundf(HR_R * sinf(a)) - HR_DISC / 2);
}
static void settle_cb(lv_timer_t *t){ (void)t; if(g_np_title) lv_label_set_long_mode(g_np_title, LV_LABEL_LONG_DOT); lv_timer_pause(g_settle); }
/* Home's bottom row: Library and Search are 56 px circles, play/pause a 68 px box; all three share ONE top
 * edge (HB_TOP), so the smaller buttons hang from the play button's top, not from its middle. */
#define HB_TOP  276
#define HB_SIDE 56
#define HB_PLAY 68
static lv_obj_t *round_btn(lv_obj_t *root, const char *sym, int dx){
    lv_obj_t *b = lv_button_create(root);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, HB_SIDE, HB_SIDE);
    lv_obj_align(b, LV_ALIGN_TOP_MID, dx, HB_TOP);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(TH_SURF1), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(TH_SURF2), LV_STATE_PRESSED);
    lv_obj_set_ext_click_area(b, 6);
    lv_obj_t *l = lv_label_create(b); lv_label_set_text(l, sym);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0); lv_obj_center(l);
    return b;
}

void home_create(lv_obj_t *root)
{
    if(th_braun()){ bhome_create(root); return; }
    g_accent = lv_color_hex(UI_RED);

    lv_obj_set_style_bg_color(root, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    /* full-screen blurred album backdrop (same image Now Playing uses), behind everything;
     * a dark scrim over it keeps the clock/text readable. Both created first = lowest z. */
    g_home_bg = lv_image_create(root);
    lv_obj_remove_style_all(g_home_bg);
    lv_obj_set_pos(g_home_bg, 0, 0); lv_obj_set_size(g_home_bg, 360, 360);
    lv_obj_clear_flag(g_home_bg, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(g_home_bg, LV_OBJ_FLAG_HIDDEN);
    g_home_scrim = lv_obj_create(root);
    lv_obj_remove_style_all(g_home_scrim);
    lv_obj_set_pos(g_home_scrim, 0, 0); lv_obj_set_size(g_home_scrim, 360, 360);
    lv_obj_set_style_bg_color(g_home_scrim, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(g_home_scrim, LV_OPA_50, 0);
    lv_obj_clear_flag(g_home_scrim, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(g_home_scrim, LV_OBJ_FLAG_HIDDEN);

    g_status_arc = NULL;   /* decorative status arc removed (read as a stray progress line) */

    /* fine battery arc across the top rim; level via home_set_status(), red + pulsing at 15% and below */
    g_batt_arc = lv_arc_create(root);
    lv_obj_remove_style(g_batt_arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(g_batt_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(g_batt_arc, 328, 328);
    lv_obj_center(g_batt_arc);
    lv_arc_set_rotation(g_batt_arc, 0);
    lv_arc_set_bg_angles(g_batt_arc, 236, 304);
    lv_arc_set_range(g_batt_arc, 0, 100);
    lv_arc_set_value(g_batt_arc, 0);
    lv_obj_set_style_arc_width(g_batt_arc, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_color(g_batt_arc, lv_color_hex(0x3A3A3C), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(g_batt_arc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_width(g_batt_arc, 3, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(g_batt_arc, lv_color_hex(0xFFFFFF), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(g_batt_arc, true, LV_PART_INDICATOR);

    /* status glyphs in one row under the arc: Wi-Fi / Bluetooth when on, battery (no percentage) */
    g_status = make_label(root, "", &lv_font_montserrat_14, lv_color_hex(0x8E8E93));
    lv_obj_set_pos(g_status, 0, 38);
    lv_obj_set_width(g_status, 360);

    /* ---- the Ring clock: clock + date/weather + the playing track inside the track's progress ring,
     * the cover riding the ring at "now"; Library, play/pause and Search below. Nothing animates while
     * idle: the ring and the cover move once a second (only their small area redraws), and a long title
     * scrolls for a while after a change, then settles. */
    g_ring = lv_arc_create(root);
    lv_obj_remove_style(g_ring, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(g_ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(g_ring, 2 * HR_R, 2 * HR_R);
    lv_obj_center(g_ring);
    lv_arc_set_rotation(g_ring, 270);
    lv_arc_set_bg_angles(g_ring, 0, 360);
    lv_arc_set_range(g_ring, 0, 1000);
    lv_arc_set_value(g_ring, 0);
    lv_obj_set_style_arc_width(g_ring, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_color(g_ring, lv_color_hex(TH_TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_width(g_ring, 3, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(g_ring, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(g_ring, g_accent, LV_PART_INDICATOR);

    g_clock = make_label(root, "--:--", &lv_font_montserrat_46, lv_color_hex(0xFFFFFF));
    lv_obj_set_width(g_clock, 360);
    lv_obj_align(g_clock, LV_ALIGN_TOP_MID, 0, 116);

    /* date and weather on one centred row: "Mon 28 Sep  [icon] 14°" */
    lv_obj_t *row = lv_obj_create(root);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, 260, 26);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 176);
    lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);              /* no date line any more: weather sits above the clock */
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 10, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    g_clock_sub = lv_label_create(row);
    lv_label_set_text(g_clock_sub, "");
    lv_obj_set_style_text_font(g_clock_sub, TH_F_LIST, 0);
    lv_obj_set_style_text_color(g_clock_sub, lv_color_hex(TH_TXT2), 0);
    s_wfont = lv_font_montserrat_18;                        /* weather icon glyphs via the fallback */
    s_wfont.fallback = &font_weather16;
    g_weather = lv_label_create(row);
    lv_label_set_text(g_weather, "");
    lv_obj_set_style_text_font(g_weather, &s_wfont, 0);
    lv_obj_set_style_text_color(g_weather, lv_color_hex(TH_TXT2), 0);
    lv_obj_add_flag(g_weather, LV_OBJ_FLAG_CLICKABLE);     /* glance -> tap opens the weather app */
    lv_obj_set_ext_click_area(g_weather, 12);
    lv_obj_add_event_cb(g_weather, weather_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_parent(g_weather, root);                    /* out of the (hidden) date row: above the clock */
    lv_obj_align(g_weather, LV_ALIGN_TOP_MID, 0, 90);

    /* the playing track: title in the album colour, artist under it; tap -> Now Playing */
    lv_obj_t *trk = lv_button_create(root);
    lv_obj_remove_style_all(trk);
    lv_obj_set_size(trk, 230, 58);
    lv_obj_align(trk, LV_ALIGN_TOP_MID, 0, 176);          /* up into the space the date left */
    lv_obj_add_event_cb(trk, nav_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)SCR_NOWPLAYING);
    g_np_title = lv_label_create(trk);
    lv_obj_set_width(g_np_title, 230);
    lv_obj_set_style_text_align(g_np_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(g_np_title, ui_font_cjk(24), 0);
    lv_obj_set_style_text_color(g_np_title, g_accent, 0);
    lv_label_set_long_mode(g_np_title, LV_LABEL_LONG_DOT);
    lv_obj_align(g_np_title, LV_ALIGN_TOP_MID, 0, 0);
    g_np_artist = lv_label_create(trk);
    lv_obj_set_width(g_np_artist, 210);
    lv_obj_set_style_text_align(g_np_artist, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(g_np_artist, ui_font_cjk(20), 0);
    lv_obj_set_style_text_color(g_np_artist, lv_color_hex(TH_TXT2), 0);
    lv_label_set_long_mode(g_np_artist, LV_LABEL_LONG_DOT);
    lv_obj_align(g_np_artist, LV_ALIGN_TOP_MID, 0, 31);

    /* the cover riding the ring at the current position (the 42px thumb, clipped to a disc) */
    g_np_thumb = lv_button_create(root);
    lv_obj_remove_style_all(g_np_thumb);
    lv_obj_set_size(g_np_thumb, HR_DISC, HR_DISC);
    lv_obj_set_style_radius(g_np_thumb, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(g_np_thumb, true, 0);
    lv_obj_set_style_bg_color(g_np_thumb, g_accent, 0);
    lv_obj_set_style_bg_opa(g_np_thumb, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(g_np_thumb, lv_color_hex(TH_BG), 0);
    lv_obj_set_style_border_width(g_np_thumb, 2, 0);
    lv_obj_set_ext_click_area(g_np_thumb, 8);
    lv_obj_add_event_cb(g_np_thumb, nav_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)SCR_NOWPLAYING);
    g_np_thumb_glyph = lv_label_create(g_np_thumb);
    lv_label_set_text(g_np_thumb_glyph, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_font(g_np_thumb_glyph, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(g_np_thumb_glyph, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(g_np_thumb_glyph);
    g_np_art_img = lv_image_create(g_np_thumb);
    lv_obj_clear_flag(g_np_art_img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(g_np_art_img, LV_OBJ_FLAG_HIDDEN);
    disc_place(0);

    /* bottom: Library, play/pause (album colour), Search */
    lv_obj_t *lib = round_btn(root, LV_SYMBOL_AUDIO, -72);
    lv_obj_add_event_cb(lib, nav_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)SCR_LIBRARY);
    lv_obj_t *srch = round_btn(root, TH_IC_SEARCH, 72);
    lv_obj_set_style_text_font(lv_obj_get_child(srch, 0), &font_theme_24, 0);
    lv_obj_add_event_cb(srch, nav_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)SCR_SEARCH);
    lv_obj_t *pp = lv_button_create(root);
    lv_obj_remove_style_all(pp);
    lv_obj_set_size(pp, HB_PLAY, HB_PLAY);
    lv_obj_align(pp, LV_ALIGN_TOP_MID, 0, HB_TOP);
    lv_obj_set_ext_click_area(pp, 6);
    lv_obj_add_event_cb(pp, pp_event_cb, LV_EVENT_CLICKED, NULL);
    g_np_state = lv_label_create(pp);
    lv_label_set_text(g_np_state, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_font(g_np_state, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(g_np_state, g_accent, 0);
    lv_obj_center(g_np_state);

    g_np_capsule = g_ring;                                   /* the progress tick below keys off this */
    lv_timer_create(np_progress_tick, 1000, NULL);
    if(!g_settle) g_settle = lv_timer_create(settle_cb, 15000, NULL);
    lv_timer_pause(g_settle);
}
