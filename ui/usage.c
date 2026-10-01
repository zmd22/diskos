/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* Battery & usage: a 24-hour dial (midnight at the top, hours clockwise).
 *   outer band - battery level (empty at the inner edge, full at the outer), line + soft fill; green = charging
 *   amber ring - screen on        red ring - playing
 *   centre     - charge now, a time-left estimate from the recent drain, and the day's totals
 * The recorder runs inside the UI: one sample a minute (battery %, charging, screen on at any point in that
 * minute, playing at any point), kept in RAM for 7 days and saved to /usr/data/usage.bin every 10 minutes
 * (and before an auto power-off). Nothing is recorded while the clock isn't set. */
#include "screens.h"
#include "theme.h"
#include "braun.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef USAGE_FILE
#define USAGE_FILE "/usr/data/usage.bin"
#endif
#define US_CAP   (7 * 1440)
#define FL_CHG   1
#define FL_SCR   2
#define FL_PLAY  4

typedef struct { uint32_t t; uint8_t pct, fl; uint16_t pad; } usmp_t;
static usmp_t g_s[US_CAP];                 /* ring, oldest at g_head when full */
static int g_head, g_n;
static int g_batt = -1, g_chg;
static uint32_t g_cur_min;                 /* minute being accumulated */
static uint8_t g_acc;                      /* flags seen during it */
static int g_unsaved;

static usmp_t *at(int i){ return &g_s[(g_head + i) % US_CAP]; }   /* i = 0 oldest .. g_n-1 newest */
static void push(uint32_t t, int pct, uint8_t fl){
    usmp_t s = { t, (uint8_t)pct, fl, 0 };
    if(g_n < US_CAP){ g_s[(g_head + g_n) % US_CAP] = s; g_n++; }
    else { g_s[g_head] = s; g_head = (g_head + 1) % US_CAP; }
}
void usage_save(void){
    char tmp[128]; snprintf(tmp, sizeof tmp, "%s.tmp", USAGE_FILE);
    FILE *f = fopen(tmp, "wb"); if(!f) return;
    uint32_t hdr[2] = { 0x31475355u /* "USG1" */, (uint32_t)g_n };
    int ok = fwrite(hdr, sizeof hdr, 1, f) == 1;
    for(int i = 0; ok && i < g_n; i++) ok = fwrite(at(i), sizeof(usmp_t), 1, f) == 1;
    if(fflush(f) != 0) ok = 0;
    fsync(fileno(f));
    if(fclose(f) != 0) ok = 0;
    if(ok) rename(tmp, USAGE_FILE); else unlink(tmp);
    g_unsaved = 0;
}
static void load(void){
    FILE *f = fopen(USAGE_FILE, "rb"); if(!f) return;
    uint32_t hdr[2];
    if(fread(hdr, sizeof hdr, 1, f) == 1 && hdr[0] == 0x31475355u){
        uint32_t n = hdr[1] > US_CAP ? US_CAP : hdr[1];
        /* a file with more than we keep: skip to its newest US_CAP samples */
        if(hdr[1] > US_CAP) fseek(f, (long)(hdr[1] - US_CAP) * (long)sizeof(usmp_t), SEEK_CUR);
        usmp_t s;
        for(uint32_t i = 0; i < n && fread(&s, sizeof s, 1, f) == 1; i++) if(s.pct <= 100) push(s.t, s.pct, s.fl);
    }
    fclose(f);
}
/* from the 3 s status poll */
void usage_note_battery(int pct, int charging){ if(pct >= 0 && pct <= 100){ g_batt = pct; g_chg = charging; } }
/* from every main-loop pass: cheap unless the minute just turned over */
void usage_tick(int screen_on, int playing){
    static int loaded;
    if(!loaded){ loaded = 1; load(); }
    time_t now = time(NULL);
    if(now < 1600000000) return;                           /* the clock isn't set yet: record nothing */
    uint32_t m = (uint32_t)(now / 60);
    if(!g_cur_min) g_cur_min = m;
    if(m != g_cur_min){
        if(g_batt >= 0) push(g_cur_min * 60u, g_batt, g_acc);
        g_cur_min = m; g_acc = 0;
        if(++g_unsaved >= 10) usage_save();
    }
    g_acc |= (g_chg ? FL_CHG : 0) | (screen_on ? FL_SCR : 0) | (playing ? FL_PLAY : 0);
}

/* ================================ the screen ================================ */
/* the dial's geometry: Ring fills the screen; Braun draws a smaller dial on the grille, figures on the segment */
static int CX = 180, CY = 180, R0 = 118, R1 = 168, RS = 106, RP = 96;
static lv_obj_t *g_root, *g_canvas, *g_pct, *g_sub, *g_title, *g_leg_s, *g_leg_p, *g_prev, *g_next, *g_hours[4];
static uint8_t *g_buf;
static int g_day;                                           /* 0 = last 24 h, 1 = yesterday, ... */
static lv_timer_t *g_tmr;

static float ang_of(uint32_t t){                            /* LVGL degrees (0 = 3 o'clock, clockwise) */
    time_t tt = (time_t)t; struct tm lt; localtime_r(&tt, &lt);
    float frac = (lt.tm_hour * 60 + lt.tm_min) / 1440.0f;
    return -90.0f + frac * 360.0f;
}
static lv_point_precise_t pol(float r, float a){
    float rad = a * 3.14159265f / 180.0f;
    lv_point_precise_t p = { (lv_value_precise_t)(CX + r * cosf(rad)), (lv_value_precise_t)(CY + r * sinf(rad)) };
    return p;
}
static void window_of(int day, uint32_t *from, uint32_t *to){
    time_t now = time(NULL);
    if(day == 0){ *to = (uint32_t)now; *from = *to - 86400u + 60u; return; }
    struct tm lt; localtime_r(&now, &lt); lt.tm_hour = lt.tm_min = lt.tm_sec = 0; lt.tm_isdst = -1;
    time_t mid = mktime(&lt);                               /* today 00:00 */
    *from = (uint32_t)(mid - (time_t)day * 86400); *to = *from + 86400u - 60u;
}
static void arc(lv_layer_t *L, float r, int w, float a0, float a1, lv_color_t c, lv_opa_t opa){
    lv_draw_arc_dsc_t d; lv_draw_arc_dsc_init(&d);
    d.center.x = CX; d.center.y = CY; d.radius = (uint16_t)r; d.width = (uint16_t)w;
    while(a0 < 0){ a0 += 360; a1 += 360; }
    d.start_angle = (lv_value_precise_t)a0; d.end_angle = (lv_value_precise_t)a1;
    d.color = c; d.opa = opa; d.rounded = 0;
    lv_draw_arc(L, &d);
}
static void line(lv_layer_t *L, lv_point_precise_t a, lv_point_precise_t b, int w, lv_color_t c, lv_opa_t opa){
    lv_draw_line_dsc_t d; lv_draw_line_dsc_init(&d);
    d.p1 = a; d.p2 = b; d.width = (int32_t)w; d.color = c; d.opa = opa; d.round_start = d.round_end = 1;
    lv_draw_line(L, &d);
}
__attribute__((unused)) static void tri(lv_layer_t *L, lv_point_precise_t a, lv_point_precise_t b, lv_point_precise_t c, lv_color_t col, lv_opa_t opa){
    lv_draw_triangle_dsc_t d; lv_draw_triangle_dsc_init(&d);
    d.p[0] = a; d.p[1] = b; d.p[2] = c; d.bg_color = col; d.bg_opa = opa;
    lv_draw_triangle(L, &d);
}
static void fmt_dur(char *b, size_t n, int mins){
    if(mins < 60) snprintf(b, n, "%d min", mins);
    else snprintf(b, n, "%d h %02d m", mins / 60, mins % 60);
}
static int tod_min(uint32_t t){ time_t tt = (time_t)t; struct tm lt; localtime_r(&tt, &lt); return lt.tm_hour * 60 + lt.tm_min; }
/* Paint the area under the battery line straight into the canvas: each pixel of the band looks up the
 * sample for its angle (minute of the day) and is filled if it lies below that minute's level. */
static void fill_band(uint32_t from, uint32_t to){
    static int16_t lvl[1440]; static uint8_t chg[1440];
    for(int i = 0; i < 1440; i++) lvl[i] = -1;
    for(int i = 0; i < g_n; i++){ const usmp_t *s = at(i); if(s->t < from || s->t > to) continue;
        int m = tod_min(s->t); lvl[m] = s->pct; chg[m] = (s->fl & FL_CHG) != 0; }
    lv_draw_buf_t *db = lv_canvas_get_draw_buf(g_canvas); if(!db) return;
    uint8_t *px = db->data; uint32_t stride = db->header.stride;
    for(int y = CY - R1; y <= CY + R1; y++){
        uint32_t *row = (uint32_t *)(px + (size_t)y * stride);
        int dy = y - CY;
        for(int x = CX - R1; x <= CX + R1; x++){
            int dx = x - CX; int d2 = dx * dx + dy * dy;
            if(d2 < R0 * R0 || d2 > R1 * R1) continue;
            float a = atan2f((float)dx, (float)-dy);                 /* 0 at the top, clockwise */
            if(a < 0) a += 2 * 3.14159265f;
            int m = (int)(a / (2 * 3.14159265f) * 1440.0f); if(m > 1439) m = 1439;
            if(lvl[m] < 0) continue;
            float r = R0 + (R1 - R0) * lvl[m] / 100.0f;
            if(d2 > r * r) continue;
            row[x] = th_braun() ? (chg[m] ? (0xFF000000u | br_pick(0xC6DEC9, 0x22352A)) : (0xFF000000u | br_pick(0xD2CCC2, 0x3E3D39))) : (chg[m] ? 0x0F3A1B : 0x2E2E32);   /* charging: green tint, else soft grey */
        }
    }
}
static void draw(void){
    if(!g_canvas) return;
    uint32_t from, to; window_of(g_day, &from, &to);
    lv_canvas_fill_bg(g_canvas, lv_color_hex(TH_BG), LV_OPA_COVER);
    if(th_braun()){                                          /* Braun: the grille, the dial's panel, the lower segment */
        lv_draw_buf_t *db = lv_canvas_get_draw_buf(g_canvas);
        if(db){ uint8_t *px = db->data; uint32_t st = db->header.stride;
            for(int y = 0; y < 360; y++){ uint32_t *row = (uint32_t *)(px + (size_t)y * st);
                for(int x = 0; x < 360; x++){
                    int dx = x - CX, dy = y - CY;
                    if(y >= 252) row[x] = 0xFF000000u | BR_PANEL;
                    else if(dx * dx + dy * dy <= (R1 + 14) * (R1 + 14)) row[x] = 0xFF000000u | BR_PANEL;
                    else { int gx = x % 8, gy = y % 8; row[x] = 0xFF000000u | ((gx >= 3 && gx <= 5 && gy >= 3 && gy <= 5 && !(gx != 4 && gy != 4)) ? BR_DOT : BR_BG); }
                }
                if(y == 252) for(int x = 0; x < 360; x++) row[x] = 0xFF000000u | BR_RULE;
            } }
    }
    fill_band(from, to);
    lv_layer_t L; lv_canvas_init_layer(g_canvas, &L);
    int br = th_braun();                                     /* Braun: ink on paper - black level line, grey screen-on, orange playing */
    lv_color_t grid = lv_color_hex(br ? BR_RULE : 0x28282A), white = lv_color_hex(br ? BR_TXT : 0xF0F0F5), green = lv_color_hex(0x34C759),
               amber = lv_color_hex(br ? BR_TXT2 : 0xFFD60A), red = lv_color_hex(br ? BR_ACC : TH_ACCENT);
    lv_color_t faint = lv_color_hex(br ? br_pick(0xE4DED4, 0x2A2926) : 0x1E1E20);
    for(int k = 0; k <= 4; k++) arc(&L, R0 + (R1 - R0) * k / 4.0f, 1, 0, 360, k % 4 ? faint : grid, k % 4 ? LV_OPA_60 : LV_OPA_COVER);
    arc(&L, RS, br ? 4 : 5, 0, 360, faint, LV_OPA_COVER);
    arc(&L, RP, br ? 4 : 5, 0, 360, faint, LV_OPA_COVER);
    for(int h = 0; h < 24; h++){                            /* hour ticks, longer every 6 h */
        float a = -90.0f + h * 15.0f;
        line(&L, pol(R1 + 2, a), pol(h % 6 ? R1 + 5 : R1 + 9, a), h % 6 ? 1 : 2, lv_color_hex(br && h % 6 == 0 ? BR_TXT : TH_TXT3), LV_OPA_COVER);
    }
    /* samples in the window, oldest first */
    int first = -1, last = -1, scr = 0, play = 0;
    for(int i = 0; i < g_n; i++){ uint32_t t = at(i)->t; if(t >= from && t <= to){ if(first < 0) first = i; last = i; } }
    if(first >= 0){
        const usmp_t *p = NULL; float pa = 0, pr = 0;
        int run_s = -1, run_p = -1; float sa0 = 0, pa0 = 0; uint32_t prev_t = 0;
        for(int i = first; i <= last; i++){
            const usmp_t *s = at(i);
            if(s->t < from || s->t > to) continue;
            float a = ang_of(s->t), r = R0 + (R1 - R0) * s->pct / 100.0f;
            int contiguous = p && s->t - prev_t <= 180 && s->t > prev_t;
            if(s->fl & FL_SCR) scr++;
            if(s->fl & FL_PLAY) play++;
            if(contiguous){
                float a0 = pa, a1 = a; if(a1 < a0) a1 += 360.0f;          /* across midnight */
                if(a1 - a0 < 5.0f) line(&L, pol(pr, a0), pol(r, a1), 2, (s->fl & FL_CHG) ? green : white, LV_OPA_COVER);
            }
            /* screen / playing runs -> arcs */
            if((s->fl & FL_SCR) && run_s < 0){ run_s = i; sa0 = a; }
            if(!(s->fl & FL_SCR) && run_s >= 0){ float a1 = a; if(a1 < sa0) a1 += 360; arc(&L, RS, 5, sa0, a1, amber, LV_OPA_COVER); run_s = -1; }
            if((s->fl & FL_PLAY) && run_p < 0){ run_p = i; pa0 = a; }
            if(!(s->fl & FL_PLAY) && run_p >= 0){ float a1 = a; if(a1 < pa0) a1 += 360; arc(&L, RP, 5, pa0, a1, red, LV_OPA_COVER); run_p = -1; }
            p = s; pa = a; pr = r; prev_t = s->t;
        }
        if(run_s >= 0){ float a1 = pa + 0.25f; if(a1 < sa0) a1 += 360; arc(&L, RS, 5, sa0, a1, amber, LV_OPA_COVER); }
        if(run_p >= 0){ float a1 = pa + 0.25f; if(a1 < pa0) a1 += 360; arc(&L, RP, 5, pa0, a1, red, LV_OPA_COVER); }
        if(g_day == 0 && p){                                 /* now: a hairline and a dot on the line */
            line(&L, pol(R0 - 2, pa), pol(R1 + 10, pa), 1, lv_color_hex(TH_MUTED), LV_OPA_COVER);
            lv_draw_rect_dsc_t dd; lv_draw_rect_dsc_init(&dd);
            dd.bg_color = br ? lv_color_hex(BR_ACC) : white; dd.radius = LV_RADIUS_CIRCLE; dd.border_color = lv_color_hex(br ? BR_PANEL : TH_BG); dd.border_width = 2;
            lv_point_precise_t c = pol(pr, pa);
            lv_area_t ar = { (int32_t)c.x - 5, (int32_t)c.y - 5, (int32_t)c.x + 5, (int32_t)c.y + 5 };
            lv_draw_rect(&L, &dd, &ar);
        }
    }
    lv_canvas_finish_layer(g_canvas, &L);

    /* the centre */
    static const char *const NAMES[] = { "LAST 24 H", "YESTERDAY" };
    char t[48];
    if(g_day < 2) snprintf(t, sizeof t, "%s", NAMES[g_day]);
    else { time_t f = (time_t)from; struct tm lt; localtime_r(&f, &lt); strftime(t, sizeof t, "%a %d %b", &lt);
           for(char *c = t; *c; c++) if(*c >= 'a' && *c <= 'z') *c -= 32; }
    lv_label_set_text(g_title, t);
    char b[64], d1[24], d2[24];
    if(first < 0){
        lv_label_set_text(g_pct, g_day == 0 && g_batt >= 0 ? "" : "-");
        if(g_day == 0 && g_batt >= 0){ snprintf(b, sizeof b, "%d%%", g_batt); lv_label_set_text(g_pct, b); }
        lv_label_set_text(g_sub, g_day == 0 ? "Recording started" : "No data for this day");
        lv_obj_add_flag(g_leg_s, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(g_leg_p, LV_OBJ_FLAG_HIDDEN);
    } else {
        const usmp_t *s_last = at(last), *s_first = at(first);
        if(g_day == 0){
            snprintf(b, sizeof b, "%d%%", g_batt >= 0 ? g_batt : s_last->pct); lv_label_set_text(g_pct, b);
            if(g_chg) lv_label_set_text(g_sub, "Charging");
            else {                                             /* drain over the last 3 h of battery-only samples */
                int j = last; uint32_t lim = s_last->t > 3 * 3600 ? s_last->t - 3 * 3600 : 0;
                while(j > first && !(at(j - 1)->fl & FL_CHG) && at(j - 1)->t >= lim) j--;
                float mins = (s_last->t - at(j)->t) / 60.0f, drop = (float)at(j)->pct - s_last->pct;
                if(mins >= 30 && drop >= 1){
                    float left_h = s_last->pct / (drop / mins) / 60.0f;
                    if(left_h >= 48) snprintf(b, sizeof b, "about %d days left at this pace", (int)(left_h / 24 + 0.5f));
                    else snprintf(b, sizeof b, "about %d h left at this pace", (int)(left_h + 0.5f));
                } else snprintf(b, sizeof b, mins >= 30 ? "Barely draining" : "Estimating...");
                lv_label_set_text(g_sub, b);
            }
        } else {
            int used = (int)s_first->pct - (int)s_last->pct;
            snprintf(b, sizeof b, "%d%%", s_last->pct); lv_label_set_text(g_pct, b);
            if(used > 0) snprintf(b, sizeof b, "%d%% used that day", used); else snprintf(b, sizeof b, "end of the day");
            lv_label_set_text(g_sub, b);
        }
        fmt_dur(d1, sizeof d1, scr); fmt_dur(d2, sizeof d2, play);
        snprintf(b, sizeof b, "Screen on  %s", d1); lv_label_set_text(lv_obj_get_child(g_leg_s, 1), b);
        snprintf(b, sizeof b, "Playing  %s", d2);   lv_label_set_text(lv_obj_get_child(g_leg_p, 1), b);
        lv_obj_remove_flag(g_leg_s, LV_OBJ_FLAG_HIDDEN); lv_obj_remove_flag(g_leg_p, LV_OBJ_FLAG_HIDDEN);
    }
    /* older / newer: only where there is somewhere to go */
    int oldest_day = 0;
    if(g_n){ time_t now = time(NULL); oldest_day = (int)((now - (time_t)at(0)->t) / 86400) + 1; if(oldest_day > 6) oldest_day = 6; }
    if(g_day < oldest_day) lv_obj_remove_flag(g_prev, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(g_prev, LV_OBJ_FLAG_HIDDEN);
    if(g_day > 0) lv_obj_remove_flag(g_next, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(g_next, LV_OBJ_FLAG_HIDDEN);
}
static void prev_cb(lv_event_t *e){ (void)e; if(g_day < 6){ g_day++; draw(); } }
static void next_cb(lv_event_t *e){ (void)e; if(g_day > 0){ g_day--; draw(); } }
static void us_tick_cb(lv_timer_t *t){ (void)t; if(screen_current() == SCR_USAGE && g_day == 0) draw(); }   /* a new minute */
static lv_obj_t *legend(lv_obj_t *root, uint32_t col, int y){
    lv_obj_t *row = lv_obj_create(root);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, 170, 16);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 8, y);
    lv_obj_t *d = lv_obj_create(row);
    lv_obj_remove_style_all(d); lv_obj_set_size(d, 7, 7);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_bg_color(d, lv_color_hex(col), 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0); lv_obj_align(d, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *l = lv_label_create(row);
    lv_obj_set_style_text_font(l, TH_F_CAPTION, 0); lv_obj_set_style_text_color(l, lv_color_hex(TH_TXT2), 0);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 13, 0);
    return row;
}
static lv_obj_t *chev(lv_obj_t *root, const char *sym, int x, lv_event_cb_t cb){
    lv_obj_t *b = lv_button_create(root);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 32, 28);
    lv_obj_align(b, LV_ALIGN_TOP_MID, x, 228);
    lv_obj_set_style_radius(b, TH_R_PILL, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(TH_SURF1), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(TH_SURF2), LV_STATE_PRESSED);
    lv_obj_set_ext_click_area(b, 6);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(b); lv_label_set_text(l, sym);
    lv_obj_set_style_text_color(l, lv_color_hex(TH_TXT2), 0); lv_obj_center(l);
    return b;
}
void usage_create(lv_obj_t *root){
    g_root = root;
    if(th_braun()){ CX = 180; CY = 124; R0 = 72; R1 = 102; RS = 63; RP = 55; }   /* the smaller dial, on the grille above */
    lv_obj_set_style_bg_color(root, lv_color_hex(TH_BG), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    g_buf = malloc(LV_CANVAS_BUF_SIZE(360, 360, 32, LV_DRAW_BUF_STRIDE_ALIGN));
    if(g_buf){
        g_canvas = lv_canvas_create(root);
        lv_canvas_set_buffer(g_canvas, g_buf, 360, 360, LV_COLOR_FORMAT_XRGB8888);
        lv_obj_set_pos(g_canvas, 0, 0);
        lv_obj_clear_flag(g_canvas, LV_OBJ_FLAG_CLICKABLE);
    }
    static const char *const H[4] = { "0", "6", "12", "18" };
    for(int k = 0; k < 4; k += 2){
        g_hours[k] = lv_label_create(root);
        lv_label_set_text(g_hours[k], H[k]);
        lv_obj_set_style_text_font(g_hours[k], TH_F_CAPTION, 0);
        lv_obj_set_style_text_color(g_hours[k], lv_color_hex(TH_TXT3), 0);
        float a = (-90.0f + k * 90.0f) * 3.14159265f / 180.0f;
        lv_obj_align(g_hours[k], LV_ALIGN_CENTER, (int32_t)(84 * cosf(a)), (int32_t)(84 * sinf(a)));
    }
    g_title = lv_label_create(root);
    lv_obj_set_style_text_font(g_title, TH_F_CAPTION, 0);
    lv_obj_set_style_text_color(g_title, lv_color_hex(TH_TXT3), 0);
    lv_obj_align(g_title, LV_ALIGN_TOP_MID, 0, 114);
    g_pct = lv_label_create(root);
    lv_obj_set_style_text_font(g_pct, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(g_pct, lv_color_hex(TH_TXT1), 0);
    lv_obj_align(g_pct, LV_ALIGN_TOP_MID, 0, 130);
    g_sub = lv_label_create(root);
    lv_label_set_long_mode(g_sub, LV_LABEL_LONG_DOT);
    lv_obj_set_width(g_sub, 170);
    lv_obj_set_style_text_align(g_sub, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(g_sub, TH_F_CAPTION, 0);
    lv_obj_set_style_text_color(g_sub, lv_color_hex(TH_TXT2), 0);
    lv_obj_align(g_sub, LV_ALIGN_TOP_MID, 0, 165);
    g_leg_s = legend(root, 0xFFD60A, 186);
    g_leg_p = legend(root, TH_ACCENT, 203);
    g_prev = chev(root, LV_SYMBOL_LEFT, -20, prev_cb);       /* older day */
    g_next = chev(root, LV_SYMBOL_RIGHT, 20, next_cb);       /* newer day */
    if(th_braun()){                                          /* Braun: % and the window inside the dial, the rest on the segment */
        for(int k = 0; k < 4; k += 2){ float a = (-90.0f + k * 90.0f) * 3.14159265f / 180.0f;
            lv_obj_set_style_text_font(g_hours[k], br_font(12, 0), 0);
            lv_obj_align(g_hours[k], LV_ALIGN_CENTER, (int32_t)((R1 + 22) * cosf(a)), (int32_t)(CY - 180 + (R1 + 22) * sinf(a))); }
        lv_obj_set_style_text_font(g_pct, br_font(22, 1), 0); lv_obj_align(g_pct, LV_ALIGN_TOP_MID, 0, CY - 20);
        lv_obj_set_style_text_font(g_title, br_font(12, 0), 0); lv_obj_align(g_title, LV_ALIGN_TOP_MID, 0, CY + 8);
        lv_obj_set_width(g_sub, 250); lv_obj_set_style_text_font(g_sub, br_font(14, 0), 0);
        lv_obj_set_style_text_color(g_sub, lv_color_hex(BR_TXT), 0); lv_obj_align(g_sub, LV_ALIGN_TOP_MID, 0, 264);
        lv_obj_align(g_leg_s, LV_ALIGN_TOP_MID, 8, 288); lv_obj_align(g_leg_p, LV_ALIGN_TOP_MID, 8, 306);
        lv_obj_set_style_bg_color(lv_obj_get_child(g_leg_s, 0), lv_color_hex(BR_TXT2), 0);
        lv_obj_align(g_prev, LV_ALIGN_TOP_MID, -74, 318); lv_obj_align(g_next, LV_ALIGN_TOP_MID, 74, 318);
    }
    if(!g_tmr) g_tmr = lv_timer_create(us_tick_cb, 60000, NULL);
}
void usage_refresh(void){ g_day = 0; draw(); }               /* shown: back to the last 24 h */
