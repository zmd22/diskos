/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* The round equalizer. Ten bands as pizza slices around a centre, each filled out to its gain:
 * a soft accent fill up to the dashed 0 dB ring, a bright one for boosts beyond it, a faint line per dB.
 *   - range -6..+6 dB in 0.1 dB steps; each band's centre frequency is free (Q stays 0.7, peaking filter)
 *   - tap a slice to select it; drag outward / inward to set its gain roughly
 *   - the centre shows the band's frequency and gain: tap one to choose what - / + change
 *     (gain: 0.1 dB a tap; frequency: 1/12 octave a tap; hold to repeat); tap the active value to type it;
 *     long-press the frequency to reset that band to its standard frequency
 *   - the preset button cycles Off -> the player's built-ins -> USER1..USER10
 * Built-in presets (and Off) are the player's own: shown in grey, read-only. USER presets are written to the
 * player's PEQ table in the stock format (per band: filterType 0, frequency, gain "x.y", qValue "0.7") and
 * selected with 0689. A preset whose filters the round editor can't represent (another filter type or Q)
 * is shown and never overwritten. Selecting a preset only selects it - writes happen on an edit. */
#include "screens.h"
#include "theme.h"
#include "config.h"
#include "musicdb.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NB       10
#define NPRESET  21                       /* 0 Off, 1..10 built-in, 11..20 USER1..10 */
#define USER_MIN 11
#define CX 180
#define CY 180
#define RMIN 72                           /* -6 dB */
#define R0   115                          /*  0 dB */
#define RMAX 158                          /* +6 dB */
#define GMAX 60                           /* tenths of a dB */
#define HALF 16.0f                        /* slice half-width in degrees (36 per band, 4 deg gaps) */

static const int STDF[NB] = { 32, 64, 125, 250, 500, 1000, 2000, 4000, 8000, 16000 };
static const char *const BUILTIN[11] = { "Off", "Jazz", "Rock", "R&B", "Hip-Hop", "Pop", "Dance", "Classical", "Retro", "Sibilance 1", "Sibilance 2" };

static int g_preset, g_editable, g_parametric, g_readfail;
static int g_t[NB], g_f[NB];              /* gain in tenths, frequency in Hz */
static double g_master;
static int g_sel = -1, g_mode_freq = 0, g_dirty_draw, g_changed;
static lv_obj_t *g_root, *g_canvas, *g_hit, *g_rim[NB], *g_pill_lbl, *g_flbl, *g_glbl, *g_minus, *g_plus, *g_modecap, *g_note, *g_lock;
static uint8_t *g_buf;
static lv_timer_t *g_draw_tmr;
static int g_press_d, g_dragging, g_drag_ok;
static lv_point_t g_press_pt;                      /* where the touch began (a tap barely moves) */   /* g_drag_ok: this touch began on the ALREADY selected band */

/* ------------------------------------------------------------------ helpers */
static float rr(int tenths){ return R0 + (tenths / 10.0f) * ((RMAX - R0) / 6.0f); }
static void fmt_gain(char *b, size_t n, int t){ snprintf(b, n, "%s%d.%d dB", t < 0 ? "-" : "+", abs(t) / 10, abs(t) % 10); }
static void fmt_freq(char *b, size_t n, int hz, int with_unit){
    if(hz < 1000) snprintf(b, n, with_unit ? "%d Hz" : "%d", hz);
    else if(hz % 1000 == 0) snprintf(b, n, with_unit ? "%d kHz" : "%dk", hz / 1000);
    else { int k = hz / 1000, d = (hz % 1000 + 50) / 100; if(d == 10){ k++; d = 0; }
           if(d == 0) snprintf(b, n, with_unit ? "%d kHz" : "%dk", k); else snprintf(b, n, with_unit ? "%d.%d kHz" : "%d.%dk", k, d); }
}
static void preset_name(int p, char *b, size_t n){
    if(p <= 10) snprintf(b, n, "%s", BUILTIN[p < 0 ? 0 : p]); else snprintf(b, n, "USER%d", p - 10);
}
static int clampi(int v, int lo, int hi){ return v < lo ? lo : v > hi ? hi : v; }
static int band_lo(int i){ return i == 0 ? 20 : (int)(g_f[i - 1] * 1.03f) + 1; }       /* stay between neighbours */
static int band_hi(int i){ return i == NB - 1 ? 20000 : (int)(g_f[i + 1] / 1.03f) - 1; }

/* ------------------------------------------------------------------ player I/O */
static int apply_now(void){                                /* write this USER preset's curve + select it */
    char json[1200]; int n = snprintf(json, sizeof json, "[");
    for(int i = 0; i < NB; i++){
        int t = g_t[i];
        n += snprintf(json + n, sizeof json - n,
            "%s{\"filterType\":0,\"frequency\":%d,\"position\":%d,\"gain\":\"%s%d.%d\",\"qValue\":\"0.7\"}",
            i ? "," : "", g_f[i], i, t < 0 ? "-" : "", abs(t) / 10, abs(t) % 10);
        if(n >= (int)sizeof json - 4) return 0;
    }
    snprintf(json + n, sizeof json - n, "]");
    if(!mdb_set_peq(g_preset, g_master, json)) return 0;
    return ui_eq_select(g_preset) >= 0;
}
static void persist(void){                                 /* a mirror in cfg (the player's table is the truth) */
    char k[24];
    for(int i = 0; i < NB; i++){
        snprintf(k, sizeof k, "eq%d_t%d", g_preset - 10, i); cfg_set_int_deferred(k, g_t[i]);
        snprintf(k, sizeof k, "eq%d_f%d", g_preset - 10, i); cfg_set_int_deferred(k, g_f[i]);
    }
    cfg_flush();
}
static void load(int preset){
    g_preset = clampi(preset, 0, NPRESET - 1);
    g_editable = g_parametric = g_readfail = 0; g_master = 0;
    for(int i = 0; i < NB; i++){ g_t[i] = 0; g_f[i] = STDF[i]; }
    if(g_preset == 0) return;                               /* Off: flat, grey */
    int ed = 1, r = mdb_get_peq_ex(g_preset, &g_master, g_t, g_f, &ed);
    if(r < 0){ g_readfail = 1; return; }                    /* unknown curve: show flat, never write */
    if(g_preset >= USER_MIN){
        if(!ed) g_parametric = 1;                           /* filters we can't represent: show, don't overwrite */
        else g_editable = 1;
    }
    for(int i = 1; i < NB; i++) if(g_f[i] <= g_f[i - 1]) g_f[i] = g_f[i - 1] + 1;   /* keep order for the dial */
}

/* ------------------------------------------------------------------ drawing */
__attribute__((unused)) static lv_color_t mixc(lv_color_t a, lv_color_t b, float t){
    if(t <= 0) return a;
    if(t >= 1) return b;
    lv_color_t c; c.red = (uint8_t)(a.red + (b.red - a.red) * t); c.green = (uint8_t)(a.green + (b.green - a.green) * t);
    c.blue = (uint8_t)(a.blue + (b.blue - a.blue) * t); return c;
}
static float clampf(float v){ return v < 0 ? 0 : v > 1 ? 1 : v; }
/* The dial's geometry never changes, so it's worked out once: every ring pixel gets its band, its distance
 * from the centre (1/16 px), its edge coverage and whether a dB line or the dashed 0 dB ring crosses it -
 * grouped by band. A change then repaints only that band's pixels (integer maths, no trig) and redraws only
 * that band's area of the screen. (Painting every pixel with atan2/sqrt on each change was far too slow on
 * the player's CPU.) */
typedef struct { uint32_t off; uint16_t r16; uint8_t cov; int8_t kind; } px_t;   /* kind: 0 none, 1..12 dB line, 13 dashed 0 dB */
static px_t *g_px; static int g_bstart[NB + 1];
static lv_area_t g_bbox[NB];
static int g_lvl16[NB] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };   /* last painted level (1/16 px) per band */
static int g_lastsel = -2, g_lastgrey = -1; static uint32_t g_lastacc;
static void geometry(void){
    if(g_px) return;
    int cnt[NB] = {0}, total = 0;
    /* two passes: count per band, then fill in band order */
    for(int pass = 0; pass < 2; pass++){
        int pos[NB];
        if(pass == 1){
            g_px = malloc(sizeof(px_t) * (size_t)(total ? total : 1)); if(!g_px) return;
            g_bstart[0] = 0; for(int b = 0; b < NB; b++){ g_bstart[b + 1] = g_bstart[b] + cnt[b]; pos[b] = g_bstart[b];
                g_bbox[b].x1 = g_bbox[b].y1 = 400; g_bbox[b].x2 = g_bbox[b].y2 = -1; }
        }
        for(int y = CY - RMAX - 2; y <= CY + RMAX + 2; y++){
            float dy = y + 0.5f - CY;
            for(int x = CX - RMAX - 2; x <= CX + RMAX + 2; x++){
                float dx = x + 0.5f - CX, d2 = dx * dx + dy * dy;
                if(d2 < (RMIN - 1.5f) * (RMIN - 1.5f) || d2 > (RMAX + 1.5f) * (RMAX + 1.5f)) continue;
                float d = sqrtf(d2), a = atan2f(dx, -dy) * 57.29578f; if(a < 0) a += 360.0f;
                int b = (int)floorf(a / 36.0f + 0.5f) % NB;
                float delta = a - b * 36.0f; if(delta > 180) delta -= 360; if(delta < -180) delta += 360;
                float cov = clampf((HALF - fabsf(delta)) * 0.017453f * d + 0.5f) * clampf(fminf(d - RMIN, RMAX - d) + 0.5f);
                if(cov <= 0) continue;
                if(pass == 0){ cnt[b]++; total++; continue; }
                px_t *p = &g_px[pos[b]++];
                p->off = (uint32_t)(y * 360 + x); p->r16 = (uint16_t)(d * 16.0f + 0.5f); p->cov = (uint8_t)(cov * 255.0f + 0.5f); p->kind = 0;
                for(int k = -6; k <= 6; k++) if(k && fabsf(d - rr(k * 10)) < 0.7f){ p->kind = (int8_t)(k + 7 - (k > 0)); break; }
                if(fabsf(d - (R0 + 1)) < 1.0f && fmodf(a, 5.0f) < 2.5f) p->kind = 13;
                if(x < g_bbox[b].x1) g_bbox[b].x1 = x; if(x > g_bbox[b].x2) g_bbox[b].x2 = x;
                if(y < g_bbox[b].y1) g_bbox[b].y1 = y; if(y > g_bbox[b].y2) g_bbox[b].y2 = y;
            }
        }
    }
}
static inline uint32_t mix32(uint32_t a, uint32_t b, int t256){       /* 0..256 */
    uint32_t ra = (a >> 16) & 255, ga = (a >> 8) & 255, ba = a & 255, rb = (b >> 16) & 255, gb = (b >> 8) & 255, bb = b & 255;
    return ((ra + (((int)rb - (int)ra) * t256 >> 8)) << 16) | ((ga + (((int)gb - (int)ga) * t256 >> 8)) << 8) | (ba + (((int)bb - (int)ba) * t256 >> 8));
}
static uint32_t c32(lv_color_t c){ return ((uint32_t)c.red << 16) | ((uint32_t)c.green << 8) | c.blue; }
static void paint_band(int b, uint32_t *fb){
    int grey = !g_editable, s = (b == g_sel);
    lv_color_t accc = grey ? lv_color_make(128, 128, 132) : ui_current_accent();
    uint32_t acc = c32(accc), empty = 0x1A1A1C, lineE = 0x2E2E32, dash = 0x78787C;
    uint32_t soft = mix32(empty, acc, s ? 171 : 110), bright = s ? acc : mix32(empty, acc, 220);
    int lvl16 = (int)(rr(clampi(g_t[b], -GMAX, GMAX)) * 16.0f + 0.5f), r0_16 = R0 * 16;
    for(int i = g_bstart[b]; i < g_bstart[b + 1]; i++){
        const px_t *p = &g_px[i];
        int f = lvl16 - p->r16 + 8; f = f < 0 ? 0 : f > 16 ? 16 : f;             /* fill, 0..16 */
        uint32_t base = mix32(empty, p->r16 <= r0_16 ? soft : bright, f * 16);
        if(p->kind == 13) base = dash;
        else if(p->kind) base = f > 8 ? mix32(base, 0, 61) : lineE;
        fb[p->off] = 0xFF000000u | mix32(0, base, p->cov + (p->cov >> 7));
    }
    g_lvl16[b] = lvl16;
}
/* repaint only what changed: bands whose level moved, the old/new selection, everything on a colour change */
static void paint(void){
    if(!g_canvas) return;
    geometry(); if(!g_px) return;
    lv_draw_buf_t *db = lv_canvas_get_draw_buf(g_canvas); if(!db) return;
    uint32_t *fb = (uint32_t *)db->data;                                     /* 360 px rows, XRGB8888 */
    uint32_t acc = c32(ui_current_accent()); int grey = !g_editable, all = (grey != g_lastgrey || acc != g_lastacc);
    for(int b = 0; b < NB; b++){
        int lvl16 = (int)(rr(clampi(g_t[b], -GMAX, GMAX)) * 16.0f + 0.5f);
        if(all || lvl16 != g_lvl16[b] || b == g_sel || b == g_lastsel){
            if(!all && lvl16 == g_lvl16[b] && (b == g_sel) == (b == g_lastsel)) continue;   /* selection unchanged for it */
            paint_band(b, fb);
            lv_obj_invalidate_area(g_canvas, &g_bbox[b]);
        }
    }
    g_lastsel = g_sel; g_lastgrey = grey; g_lastacc = acc;
}
static void refresh_labels(void){
    char b[40];
    for(int i = 0; i < NB; i++){                                    /* only labels that changed are touched */
        fmt_freq(b, sizeof b, g_f[i], 0);
        if(strcmp(lv_label_get_text(g_rim[i]), b)) lv_label_set_text(g_rim[i], b);
        lv_color_t want = i == g_sel ? ui_current_accent() : lv_color_hex(TH_TXT3);
        if(!lv_color_eq(lv_obj_get_style_text_color(g_rim[i], 0), want)) lv_obj_set_style_text_color(g_rim[i], want, 0);
    }
    preset_name(g_preset, b, sizeof b);
    if(strcmp(lv_label_get_text(g_pill_lbl), b)) lv_label_set_text(g_pill_lbl, b);
    int show_edit = g_editable && g_sel >= 0;
    lv_obj_t *edit_objs[5] = { g_flbl, g_glbl, g_minus, g_plus, g_modecap };
    for(int k = 0; k < 5; k++){ if(show_edit) lv_obj_remove_flag(edit_objs[k], LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(edit_objs[k], LV_OBJ_FLAG_HIDDEN); }
    if(show_edit){
        fmt_freq(b, sizeof b, g_f[g_sel], 1); if(strcmp(lv_label_get_text(g_flbl), b)) lv_label_set_text(g_flbl, b);
        fmt_gain(b, sizeof b, clampi(g_t[g_sel], -GMAX, GMAX)); if(strcmp(lv_label_get_text(g_glbl), b)) lv_label_set_text(g_glbl, b);   /* old +-12 presets show at the edge */
        lv_obj_set_style_text_font(g_flbl, g_mode_freq ? TH_F_TITLE : &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(g_flbl, g_mode_freq ? ui_current_accent() : lv_color_hex(TH_TXT2), 0);
        lv_obj_set_style_text_font(g_glbl, g_mode_freq ? &lv_font_montserrat_14 : TH_F_TITLE, 0);
        lv_obj_set_style_text_color(g_glbl, g_mode_freq ? lv_color_hex(TH_TXT2) : ui_current_accent(), 0);
        lv_label_set_text(g_modecap, g_mode_freq ? "FREQ" : "GAIN");
    }
    const char *note = NULL;
    if(!g_editable) note = g_preset == 0 ? "EQ off" : g_readfail ? "Couldn't read this preset" : g_parametric ? "Advanced preset" : "built-in preset";
    else if(g_sel < 0) note = "tap a band";
    if(note){ lv_label_set_text(g_note, note); lv_obj_remove_flag(g_note, LV_OBJ_FLAG_HIDDEN); }
    else lv_obj_add_flag(g_note, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_lock, LV_OBJ_FLAG_HIDDEN);   /* no lock glyph in the fonts: the grey + caption say it */
}
static void redraw(void){ paint(); refresh_labels(); }
static void draw_tmr_cb(lv_timer_t *t){ (void)t; if(g_dirty_draw){ g_dirty_draw = 0; redraw(); } }   /* drags repaint at most ~20x/s */

/* ------------------------------------------------------------------ edits */
static void commit(void){
    if(!g_changed) return;
    g_changed = 0;
    if(apply_now()) persist(); else ui_toast("Couldn't apply EQ");
}
static void step(int dir){
    if(!g_editable || g_sel < 0) return;
    if(g_mode_freq){
        float f = g_f[g_sel] * powf(2.0f, dir / 12.0f);
        int nf = clampi((int)(f + 0.5f), band_lo(g_sel), band_hi(g_sel));
        if(nf == g_f[g_sel]) nf = clampi(g_f[g_sel] + dir, band_lo(g_sel), band_hi(g_sel));
        g_f[g_sel] = nf;
    } else g_t[g_sel] = clampi(clampi(g_t[g_sel], -GMAX, GMAX) + dir, -GMAX, GMAX);   /* from the shown (clamped) value */
    g_changed = 1; redraw();
}
static void pm_cb(lv_event_t *e){
    int dir = (int)(intptr_t)lv_event_get_user_data(e);
    lv_event_code_t c = lv_event_get_code(e);
    if(c == LV_EVENT_SHORT_CLICKED || c == LV_EVENT_LONG_PRESSED || c == LV_EVENT_LONG_PRESSED_REPEAT) step(dir);
    else if(c == LV_EVENT_RELEASED || c == LV_EVENT_PRESS_LOST) commit();
}

/* the dial: select a band; drag out / in for its gain */
static int hit_band(int x, int y, float *dist){
    float dx = x - CX, dy = y - CY, d = sqrtf(dx * dx + dy * dy);
    if(d < RMIN - 6 || d > RMAX + 14) return -1;
    float a = atan2f(dx, -dy) * 57.29578f; if(a < 0) a += 360.0f;
    if(dist) *dist = d;
    return (int)floorf(a / 36.0f + 0.5f) % NB;
}
static void hit_cb(lv_event_t *e){
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *in = lv_indev_active(); if(!in) return;
    lv_point_t p; lv_indev_get_point(in, &p);
    float d = 0; int b = hit_band(p.x, p.y, &d);
    if(code == LV_EVENT_PRESSED){
        /* A touch on another band only SELECTS it - it never changes a gain. Only a touch that starts on the
         * band that was already selected can drag its level, so a swipe across the dial (or a back-swipe from
         * the left edge) can't move anything by accident. */
        g_dragging = 0; g_press_d = (int)d; g_press_pt = p;
        g_drag_ok = (b >= 0 && b == g_sel);
        if(b >= 0 && b != g_sel){ g_sel = b; redraw(); }
    } else if(code == LV_EVENT_PRESSING){
        if(!g_editable || g_sel < 0 || b < 0 || !g_drag_ok) return;
        if(!g_dragging && abs((int)d - g_press_d) < 5) return;         /* a tap is not a drag */
        g_dragging = 1;
        int nt = clampi((int)lroundf((d - R0) / ((RMAX - R0) / 6.0f) * 10.0f), -GMAX, GMAX);
        if(nt != g_t[g_sel]){ g_t[g_sel] = nt; g_changed = 1; g_dirty_draw = 1; }
    } else if(code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST){
        if(g_dirty_draw){ g_dirty_draw = 0; redraw(); }
        commit();
        /* double-tap a band (two quick taps, no drag): its gain back to 0 dB */
        static uint32_t last_tap; static int last_band = -1;
        int moved = abs(p.x - g_press_pt.x) + abs(p.y - g_press_pt.y) > 12;   /* a swipe is not a tap */
        if(code == LV_EVENT_RELEASED && !g_dragging && !moved && b >= 0 && b == g_sel){
            if(last_band == b && lv_tick_elaps(last_tap) < 400 && g_editable && g_t[b] != 0){
                g_t[b] = 0; g_changed = 1; commit(); redraw(); last_band = -1;
            } else { last_band = b; last_tap = lv_tick_get(); }
        } else if(g_dragging || moved) last_band = -1;
    }
}
/* main.c asks whether a press belongs to the dial: only a press on the selected (editable) band does - a drag
 * there adjusts it. Anywhere else the normal gestures work, including the back-swipe from the left edge. */
int eqcustom_owns_point(int x, int y){ return g_editable && g_sel >= 0 && hit_band(x, y, NULL) == g_sel; }

/* the preset button: next preset (only selects - never writes) */
static void pad_close(void);
static void pill_cb(lv_event_t *e){
    (void)e;
    int next = (g_preset + 1) % NPRESET;
    if(ui_eq_select(next) < 0){ ui_toast("Couldn't switch EQ"); return; }
    load(next);
    if(!g_editable) g_sel = -1;
    redraw();
}

/* ------------------------------------------------------------------ the number pad */
static lv_obj_t *g_pad, *g_pad_disp, *g_unit_hz, *g_unit_khz, *g_sign;
static char g_pad_txt[12];
static int g_pad_khz, g_pad_neg;
static void pad_close(void){ if(g_pad){ lv_obj_delete(g_pad); g_pad = NULL; } }
static void pad_show(void){
    if(!g_pad) return;
    lv_label_set_text(g_pad_disp, g_pad_txt[0] ? g_pad_txt : "0");
    if(g_mode_freq){
        lv_obj_set_style_bg_color(g_unit_hz,  g_pad_khz ? lv_color_hex(0x28282C) : ui_current_accent(), 0);
        lv_obj_set_style_bg_color(g_unit_khz, g_pad_khz ? ui_current_accent() : lv_color_hex(0x28282C), 0);
    } else lv_label_set_text(lv_obj_get_child(g_sign, 0), g_pad_neg ? "-" : "+");
}
static void pad_key_cb(lv_event_t *e){
    const char *k = lv_event_get_user_data(e); size_t n = strlen(g_pad_txt);
    if(!strcmp(k, "del")){ if(n) g_pad_txt[n - 1] = 0; }
    else if(!strcmp(k, ".")){ if(!strchr(g_pad_txt, '.') && n < sizeof g_pad_txt - 2){ if(!n) strcat(g_pad_txt, "0"); strcat(g_pad_txt, "."); } }
    else if(n < 6){ const char *dot = strchr(g_pad_txt, '.'); if(!dot || strlen(dot) < (size_t)(g_mode_freq ? 3 : 2)) strcat(g_pad_txt, k); }
    pad_show();
}
static void pad_unit_cb(lv_event_t *e){ g_pad_khz = (int)(intptr_t)lv_event_get_user_data(e); pad_show(); }
static void pad_sign_cb(lv_event_t *e){ (void)e; g_pad_neg = !g_pad_neg; pad_show(); }
static void pad_cancel_cb(lv_event_t *e){ (void)e; pad_close(); }
static void pad_set_cb(lv_event_t *e){
    (void)e;
    double v = atof(g_pad_txt[0] ? g_pad_txt : "0");
    if(g_sel >= 0 && g_editable && g_pad_txt[0]){
        if(g_mode_freq){ int hz = (int)lround(v * (g_pad_khz ? 1000.0 : 1.0)); g_f[g_sel] = clampi(hz, band_lo(g_sel), band_hi(g_sel)); }
        else { int t = (int)lround(v * 10.0); if(g_pad_neg) t = -t; g_t[g_sel] = clampi(t, -GMAX, GMAX); }
        g_changed = 1; commit(); redraw();
    }
    pad_close();
}
static lv_obj_t *pad_btn(lv_obj_t *p, const char *txt, int x, int y, int w, int h, lv_color_t bg, lv_event_cb_t cb, void *ud){
    lv_obj_t *b = lv_button_create(p);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, w, h);
    lv_obj_set_pos(b, x - w / 2, y - h / 2);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(TH_SURF2), LV_STATE_PRESSED);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    lv_obj_t *l = lv_label_create(b); lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, h >= 38 ? &lv_font_montserrat_18 : TH_F_CAPTION, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(TH_TXT1), 0); lv_obj_center(l);
    return b;
}
static void pad_open(void){
    if(!g_editable || g_sel < 0) return;
    pad_close();
    g_pad = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g_pad);
    lv_obj_set_size(g_pad, 360, 360);
    lv_obj_set_style_bg_color(g_pad, lv_color_hex(TH_BG), 0);
    lv_obj_set_style_bg_opa(g_pad, LV_OPA_COVER, 0);
    lv_obj_add_flag(g_pad, LV_OBJ_FLAG_CLICKABLE);
    char t[40]; snprintf(t, sizeof t, "BAND %d %s", g_sel + 1, g_mode_freq ? "FREQUENCY" : "GAIN");
    lv_obj_t *h = lv_label_create(g_pad); lv_label_set_text(h, t);
    lv_obj_set_style_text_font(h, TH_F_CAPTION, 0); lv_obj_set_style_text_color(h, lv_color_hex(TH_TXT3), 0);
    lv_obj_align(h, LV_ALIGN_TOP_MID, 0, 36);
    lv_obj_t *field = lv_obj_create(g_pad);
    lv_obj_remove_style_all(field); lv_obj_set_size(field, 168, 40); lv_obj_set_pos(field, 96, 56);
    lv_obj_set_style_radius(field, 20, 0); lv_obj_set_style_bg_color(field, lv_color_hex(TH_SURF1), 0); lv_obj_set_style_bg_opa(field, LV_OPA_COVER, 0);
    lv_obj_clear_flag(field, LV_OBJ_FLAG_SCROLLABLE);
    g_pad_disp = lv_label_create(field);
    lv_obj_set_style_text_font(g_pad_disp, TH_F_TITLE, 0); lv_obj_set_style_text_color(g_pad_disp, lv_color_hex(TH_TXT1), 0);
    lv_obj_align(g_pad_disp, LV_ALIGN_LEFT_MID, 16, 0);
    g_pad_txt[0] = 0;
    lv_color_t chip = lv_color_hex(0x28282C);
    if(g_mode_freq){
        g_pad_khz = g_f[g_sel] >= 1000;
        g_unit_hz  = pad_btn(field, "Hz",  112, 20, 30, 24, chip, pad_unit_cb, (void *)0);
        g_unit_khz = pad_btn(field, "kHz", 145, 20, 34, 24, chip, pad_unit_cb, (void *)1);
    } else {
        g_pad_neg = g_t[g_sel] < 0;
        g_sign = pad_btn(field, "+", 146, 20, 30, 24, chip, pad_sign_cb, NULL);
    }
    static const char *const K[12] = { "1","2","3","4","5","6","7","8","9",".","0","del" };
    for(int i = 0; i < 12; i++)
        pad_btn(g_pad, !strcmp(K[i], "del") ? LV_SYMBOL_BACKSPACE : K[i], 132 + (i % 3) * 48, 124 + (i / 3) * 42, 42, 38,
                lv_color_hex(TH_SURF1), pad_key_cb, (void *)K[i]);
    pad_btn(g_pad, "Cancel", 143, 294, 62, 24, lv_color_hex(TH_SURF1), pad_cancel_cb, NULL);
    pad_btn(g_pad, "Set", 217, 294, 62, 24, ui_current_accent(), pad_set_cb, NULL);
    char lo[16], hi[16], r[64];
    if(g_mode_freq){ fmt_freq(lo, sizeof lo, band_lo(g_sel), 0); fmt_freq(hi, sizeof hi, band_hi(g_sel), 0);
                     snprintf(r, sizeof r, "%s - %s (between neighbours)", lo, hi); }
    else snprintf(r, sizeof r, "-6.0 to +6.0 dB, 0.1 steps");
    lv_obj_t *rl = lv_label_create(g_pad); lv_label_set_text(rl, r);
    lv_obj_set_style_text_font(rl, TH_F_CAPTION, 0); lv_obj_set_style_text_color(rl, lv_color_hex(TH_TXT3), 0);
    lv_obj_align(rl, LV_ALIGN_TOP_MID, 0, 316);
    pad_show();
}
/* tapping a value: the inactive one becomes active; the active one opens the pad */
static void flbl_cb(lv_event_t *e){
    lv_event_code_t c = lv_event_get_code(e);
    if(c == LV_EVENT_LONG_PRESSED){                                   /* reset this band's frequency */
        if(g_editable && g_sel >= 0){ g_f[g_sel] = clampi(STDF[g_sel], band_lo(g_sel), band_hi(g_sel)); g_changed = 1; commit(); redraw(); }
        return;
    }
    if(c != LV_EVENT_SHORT_CLICKED) return;
    if(g_mode_freq) pad_open(); else { g_mode_freq = 1; refresh_labels(); }
}
static void glbl_cb(lv_event_t *e){
    if(lv_event_get_code(e) != LV_EVENT_SHORT_CLICKED) return;
    if(!g_mode_freq) pad_open(); else { g_mode_freq = 0; refresh_labels(); }
}

/* ------------------------------------------------------------------ building the screen */
static lv_obj_t *round_btn(lv_obj_t *p, const char *sym, int dx, int dir){
    lv_obj_t *b = lv_button_create(p);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 30, 30);
    lv_obj_align(b, LV_ALIGN_CENTER, dx, 39);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(TH_SURF2), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(TH_TRACK), LV_STATE_PRESSED);
    lv_obj_set_ext_click_area(b, 4);
    lv_obj_add_event_cb(b, pm_cb, LV_EVENT_ALL, (void *)(intptr_t)dir);
    lv_obj_t *l = lv_label_create(b); lv_label_set_text(l, sym);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0); lv_obj_set_style_text_color(l, lv_color_hex(TH_TXT1), 0); lv_obj_center(l);
    return b;
}
static lv_obj_t *centre_lbl(lv_obj_t *p, int dy, lv_event_cb_t cb){
    lv_obj_t *l = lv_label_create(p);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, dy);
    lv_obj_add_flag(l, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(l, 8);
    lv_obj_add_event_cb(l, cb, LV_EVENT_ALL, NULL);
    return l;
}
void eqcustom_create(lv_obj_t *root){
    g_root = root;
    lv_obj_set_style_bg_color(root, lv_color_hex(TH_BG), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    g_buf = malloc(LV_CANVAS_BUF_SIZE(360, 360, 32, LV_DRAW_BUF_STRIDE_ALIGN));
    if(g_buf){
        g_canvas = lv_canvas_create(root);
        lv_canvas_set_buffer(g_canvas, g_buf, 360, 360, LV_COLOR_FORMAT_XRGB8888);
        lv_canvas_fill_bg(g_canvas, lv_color_hex(TH_BG), LV_OPA_COVER);
        lv_obj_clear_flag(g_canvas, LV_OBJ_FLAG_CLICKABLE);
    }
    g_hit = lv_obj_create(root);                          /* the dial's touch layer, under the centre controls */
    lv_obj_remove_style_all(g_hit);
    lv_obj_set_size(g_hit, 360, 360);
    lv_obj_add_flag(g_hit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(g_hit, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(g_hit, hit_cb, LV_EVENT_ALL, NULL);
    for(int i = 0; i < NB; i++){
        g_rim[i] = lv_label_create(root);
        lv_obj_set_style_text_font(g_rim[i], TH_F_CAPTION, 0);
        float a = (-90.0f + i * 36.0f) * 0.017453f;
        lv_obj_align(g_rim[i], LV_ALIGN_CENTER, (int32_t)lroundf(171 * cosf(a)), (int32_t)lroundf(171 * sinf(a)));
    }
    lv_obj_t *pill = lv_button_create(root);
    lv_obj_remove_style_all(pill);
    lv_obj_set_size(pill, 96, 24);
    lv_obj_align(pill, LV_ALIGN_CENTER, 0, -51);
    lv_obj_set_style_radius(pill, 12, 0);
    lv_obj_set_style_bg_color(pill, lv_color_hex(0x28282C), 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(pill, lv_color_hex(TH_SURF2), LV_STATE_PRESSED);
    lv_obj_set_ext_click_area(pill, 6);
    lv_obj_add_event_cb(pill, pill_cb, LV_EVENT_CLICKED, NULL);
    g_pill_lbl = lv_label_create(pill);
    lv_obj_set_style_text_font(g_pill_lbl, TH_F_CAPTION, 0); lv_obj_set_style_text_color(g_pill_lbl, lv_color_hex(TH_TXT1), 0);
    lv_obj_align(g_pill_lbl, LV_ALIGN_CENTER, -6, 0);
    lv_obj_t *chev = lv_label_create(pill); lv_label_set_text(chev, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_font(chev, &lv_font_montserrat_10, 0); lv_obj_set_style_text_color(chev, lv_color_hex(TH_TXT2), 0);
    lv_obj_align(chev, LV_ALIGN_RIGHT_MID, -9, 0);
    g_flbl = centre_lbl(root, -20, flbl_cb);
    g_glbl = centre_lbl(root, 8, glbl_cb);
    g_minus = round_btn(root, LV_SYMBOL_MINUS, -34, -1);
    g_plus  = round_btn(root, LV_SYMBOL_PLUS, 34, +1);
    g_modecap = lv_label_create(root);
    lv_obj_set_style_text_font(g_modecap, &lv_font_montserrat_10, 0); lv_obj_set_style_text_color(g_modecap, lv_color_hex(TH_TXT3), 0);
    lv_obj_align(g_modecap, LV_ALIGN_CENTER, 0, 39);
    g_lock = lv_label_create(root); lv_label_set_text(g_lock, LV_SYMBOL_EYE_CLOSE);
    lv_obj_set_style_text_color(g_lock, lv_color_hex(TH_TXT3), 0); lv_obj_align(g_lock, LV_ALIGN_CENTER, 0, -10);
    g_note = lv_label_create(root);
    lv_obj_set_style_text_font(g_note, TH_F_CAPTION, 0); lv_obj_set_style_text_color(g_note, lv_color_hex(TH_TXT3), 0);
    lv_obj_align(g_note, LV_ALIGN_CENTER, 0, 4);
    if(!g_draw_tmr) g_draw_tmr = lv_timer_create(draw_tmr_cb, 50, NULL);
    load(cfg_get_int("eq_preset", 0));    /* painted on first show (eqcustom_refresh), not at boot */
}
/* shown: follow whatever preset is active now (Settings or the stock player may have changed it) */
void eqcustom_refresh(void){ pad_close(); load(cfg_get_int("eq_preset", 0)); if(!g_editable) g_sel = -1; g_mode_freq = 0; redraw(); }
