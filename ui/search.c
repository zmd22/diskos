/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* Search, laid out for the round panel: the query field at the top (Backspace inside it), live results
 * under it, and a big alphabetical keyboard (7 keys a row, 40 px keys) in the wide middle of the circle.
 * Results refresh as you type (after a short pause, so typing stays smooth); tap one to play it.
 * "123" swaps the letters for digits and common punctuation. Other text entry (Wi-Fi passwords,
 * playlist names) keeps the full QWERTY keyboard in kbinput.c. */
#include "screens.h"
#include "theme.h"
#include "musicdb.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_RESULTS 80
#define KEY 40
#define GRID_Y 150
/* key width and gap per row, so every key sits fully inside the circle (the rows narrow downwards) */
static const int KW[4] = { 40, 40, 38, 30 };
static const int KG[4] = {  5,  5,  4,  4 };

static lv_obj_t *g_field, *g_query_lbl, *g_caret, *g_bksp, *g_results, *g_mode_btn, *g_mode_lbl;
static lv_obj_t *g_keys[4][7], *g_key_lbl[4][7];
static lv_timer_t *g_debounce;
static library_song_click_cb_t g_song_cb;
static char g_query[96];
static int g_digits;                                       /* 0 = letters, 1 = digits & punctuation */

/* '_' is the space key; '\0' leaves a slot unused */
static const char *const LET[4] = { "ABCDEFG", "HIJKLMN", "OPQRSTU", "VWXYZ_" };
static const char *const DIG[4] = { "1234567", "890'&-.", "!?,:()/", "+#@_"   };

void search_set_song_click_cb(library_song_click_cb_t cb){ g_song_cb = cb; }
lv_obj_t *search_scroller(void){ return g_results; }

static void result_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    int id = (int)(uintptr_t)lv_event_get_user_data(e);
    if(g_song_cb) g_song_cb(id);
    screen_show(SCR_NOWPLAYING);
}
static void note(const char *msg){
    lv_obj_t *l = lv_label_create(g_results);
    lv_label_set_text(l, msg);
    lv_obj_set_width(l, 240);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(TH_TXT2), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
}
static void rebuild(void){
    lv_obj_clean(g_results);
    lv_obj_scroll_to_y(g_results, 0, LV_ANIM_OFF);
    if(!g_query[0]){ note("Songs, artists, albums"); return; }
    static const mdb_song_t *buf[MAX_RESULTS + 1];   /* +1 to detect "more than 80" vs "exactly 80" */
    int n = mdb_search(g_query, buf, MAX_RESULTS + 1);
    int shown = n > MAX_RESULTS ? MAX_RESULTS : n;
    if(n <= 0){ note("No results"); return; }
    for(int i = 0; i < shown; i++){
        const mdb_song_t *s = buf[i];
        lv_obj_t *row = lv_obj_create(g_results);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, 250, 34);
        lv_obj_set_style_radius(row, TH_R_ROW, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(TH_SURF1), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(TH_SURF2), LV_STATE_PRESSED);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, result_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)s->id);
        lv_obj_t *t = lv_label_create(row);
        lv_label_set_text(t, s->title[0] ? s->title : "Untitled");
        lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(t, 14, 0); lv_obj_set_size(t, 226, 19);
        lv_obj_set_style_text_font(t, ui_font_cjk(16), 0);   /* CJK titles via the fallback chain */
        lv_obj_set_style_text_color(t, lv_color_hex(TH_TXT1), 0);
        lv_obj_t *a = lv_label_create(row);
        lv_label_set_text(a, s->artist);
        lv_label_set_long_mode(a, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(a, 14, 17); lv_obj_set_size(a, 226, 16);
        lv_obj_set_style_text_font(a, ui_font_cjk(14), 0);
        lv_obj_set_style_text_color(a, lv_color_hex(TH_TXT2), 0);
    }
    if(n > MAX_RESULTS) note("First 80 - type more to narrow");
}
static void debounce_cb(lv_timer_t *t){ (void)t; lv_timer_pause(g_debounce); rebuild(); }
static void query_changed(void){
    lv_label_set_text(g_query_lbl, g_query[0] ? g_query : "Search");
    lv_obj_set_style_text_color(g_query_lbl, lv_color_hex(g_query[0] ? TH_TXT1 : TH_TXT3), 0);
    if(g_query[0]) lv_obj_remove_flag(g_bksp, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(g_bksp, LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(g_query_lbl);                          /* the caret sits right after the text */
    int w = g_query[0] ? lv_obj_get_width(g_query_lbl) : 0;
    lv_obj_set_x(g_caret, g_query[0] ? 47 + (w > 170 ? 170 : w) + 1 : 43);
    if(!g_debounce) g_debounce = lv_timer_create(debounce_cb, 180, NULL);   /* results after a short pause */
    else { lv_timer_reset(g_debounce); lv_timer_resume(g_debounce); }
}
static void key_press(char c){
    size_t n = strlen(g_query);
    if(n + 1 >= sizeof g_query) return;
    if(c == '_') c = ' ';
    if(c == ' ' && (n == 0 || g_query[n-1] == ' ')) return;    /* no leading or double spaces */
    g_query[n] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; g_query[n+1] = 0;
    query_changed();
}
static void bksp_cb(lv_event_t *e){
    (void)e;
    size_t n = strlen(g_query);
    if(!n) return;
    do { n--; } while(n > 0 && ((unsigned char)g_query[n] & 0xC0) == 0x80);   /* whole UTF-8 character */
    g_query[n] = 0;
    query_changed();
}
static void bksp_long_cb(lv_event_t *e){ (void)e; g_query[0] = 0; query_changed(); }   /* hold: clear all */
static void paint_keys(void){
    const char *const *set = g_digits ? DIG : LET;
    for(int r = 0; r < 4; r++) for(int c = 0; c < 7; c++){
        lv_obj_t *k = g_keys[r][c]; if(!k) continue;
        char ch = c < (int)strlen(set[r]) ? set[r][c] : 0;
        if(!ch){ lv_obj_add_flag(k, LV_OBJ_FLAG_HIDDEN); continue; }
        lv_obj_remove_flag(k, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_user_data(k, (void*)(uintptr_t)ch);
        char s[2] = { ch, 0 };
        lv_label_set_text(g_key_lbl[r][c], ch == '_' ? "space" : s);
        lv_obj_set_style_text_font(g_key_lbl[r][c], ch == '_' ? &lv_font_montserrat_10 : &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(g_key_lbl[r][c], lv_color_hex(ch == '_' ? TH_TXT2 : TH_TXT1), 0);
    }
    /* re-centre each row for its key count (the last row differs between the sets) */
    for(int r = 0; r < 4; r++){
        int cnt = (int)strlen(set[r]), kw = KW[r], g = KG[r];
        int x = 180 - (cnt * kw + (cnt - 1) * g) / 2;
        for(int c = 0; c < cnt; c++){ lv_obj_set_pos(g_keys[r][c], x, GRID_Y + r * (KEY + 5)); x += kw + g; }
    }
    lv_label_set_text(g_mode_lbl, g_digits ? "ABC" : "123");
}
static void keyev_cb(lv_event_t *e){ key_press((char)(uintptr_t)lv_obj_get_user_data(lv_event_get_current_target(e))); }
static void mode_cb(lv_event_t *e){ (void)e; g_digits = !g_digits; paint_keys(); }
static void back_cb(lv_event_t *e){ (void)e; screen_back(); }

void search_create(lv_obj_t *root){
    lv_obj_set_style_bg_color(root, lv_color_hex(TH_BG), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    /* the field: magnifier, query, caret, Backspace inside (tap = one character, hold = clear) */
    g_field = lv_obj_create(root);
    lv_obj_remove_style_all(g_field);
    lv_obj_set_size(g_field, 236, 40);
    lv_obj_align(g_field, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_set_style_radius(g_field, TH_R_PILL, 0);
    lv_obj_set_style_bg_color(g_field, lv_color_hex(TH_SURF1), 0);
    lv_obj_set_style_bg_opa(g_field, LV_OPA_COVER, 0);
    lv_obj_clear_flag(g_field, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *mg = lv_label_create(g_field);
    lv_label_set_text(mg, TH_IC_SEARCH);
    lv_obj_set_style_text_font(mg, &font_theme_20, 0);
    lv_obj_set_style_text_color(mg, lv_color_hex(TH_TXT2), 0);
    lv_obj_align(mg, LV_ALIGN_LEFT_MID, 14, 0);
    g_query_lbl = lv_label_create(g_field);
    lv_label_set_long_mode(g_query_lbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_max_width(g_query_lbl, 170, 0);
    lv_obj_set_style_text_font(g_query_lbl, ui_font_cjk(18), 0);
    lv_obj_align(g_query_lbl, LV_ALIGN_LEFT_MID, 47, 0);   /* clear of the caret when empty */
    g_caret = lv_obj_create(g_field);
    lv_obj_remove_style_all(g_caret);
    lv_obj_set_size(g_caret, 2, 20);
    lv_obj_set_style_bg_color(g_caret, lv_color_hex(TH_ACCENT), 0);
    lv_obj_set_style_bg_opa(g_caret, LV_OPA_COVER, 0);
    lv_obj_align(g_caret, LV_ALIGN_LEFT_MID, 42, 0);
    g_bksp = lv_button_create(g_field);
    lv_obj_remove_style_all(g_bksp);
    lv_obj_set_size(g_bksp, 30, 30);
    lv_obj_align(g_bksp, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_obj_set_style_radius(g_bksp, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(g_bksp, lv_color_hex(TH_SURF2), 0);
    lv_obj_set_style_bg_opa(g_bksp, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(g_bksp, lv_color_hex(TH_TRACK), LV_STATE_PRESSED);
    lv_obj_set_ext_click_area(g_bksp, 6);
    lv_obj_add_event_cb(g_bksp, bksp_cb, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_add_event_cb(g_bksp, bksp_long_cb, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_t *bl = lv_label_create(g_bksp);
    lv_label_set_text(bl, LV_SYMBOL_BACKSPACE);
    lv_obj_set_style_text_font(bl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(bl, lv_color_hex(TH_TXT1), 0);
    lv_obj_center(bl);
    /* live results: two rows visible, the rest scroll */
    g_results = lv_obj_create(root);
    lv_obj_remove_style_all(g_results);
    lv_obj_set_size(g_results, 256, 74);
    lv_obj_align(g_results, LV_ALIGN_TOP_MID, 0, 73);
    lv_obj_set_flex_flow(g_results, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_results, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(g_results, 4, 0);
    lv_obj_set_style_pad_top(g_results, 1, 0);
    lv_obj_set_scroll_dir(g_results, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_results, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(g_results, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    /* the keyboard: 7 big keys a row in the wide middle of the circle */
    for(int r = 0; r < 4; r++) for(int c = 0; c < 7; c++){
        int kw = KW[r];
        lv_obj_t *k = lv_button_create(root);
        lv_obj_remove_style_all(k);
        lv_obj_set_size(k, kw, KEY);
        lv_obj_set_style_radius(k, 12, 0);
        lv_obj_set_style_bg_color(k, lv_color_hex(TH_SURF1), 0);
        lv_obj_set_style_bg_opa(k, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(k, lv_color_hex(TH_SURF2), LV_STATE_PRESSED);
        lv_obj_add_event_cb(k, keyev_cb, LV_EVENT_CLICKED, NULL);
        g_keys[r][c] = k;
        g_key_lbl[r][c] = lv_label_create(k);
        lv_obj_center(g_key_lbl[r][c]);
    }
    /* bottom: back, and the letters/digits switch */
    lv_obj_t *bk = lv_button_create(root);
    lv_obj_remove_style_all(bk);
    lv_obj_set_size(bk, 36, 26); lv_obj_align(bk, LV_ALIGN_TOP_MID, -26, 326);
    lv_obj_set_ext_click_area(bk, 8);
    lv_obj_add_event_cb(bk, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bkl = lv_label_create(bk); lv_label_set_text(bkl, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(bkl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(bkl, lv_color_hex(TH_TXT2), 0); lv_obj_center(bkl);
    g_mode_btn = lv_button_create(root);
    lv_obj_remove_style_all(g_mode_btn);
    lv_obj_set_size(g_mode_btn, 40, 26); lv_obj_align(g_mode_btn, LV_ALIGN_TOP_MID, 22, 326);
    lv_obj_set_style_radius(g_mode_btn, 13, 0);
    lv_obj_set_style_bg_color(g_mode_btn, lv_color_hex(TH_SURF1), 0);
    lv_obj_set_style_bg_opa(g_mode_btn, LV_OPA_COVER, 0);
    lv_obj_set_ext_click_area(g_mode_btn, 6);
    lv_obj_add_event_cb(g_mode_btn, mode_cb, LV_EVENT_CLICKED, NULL);
    g_mode_lbl = lv_label_create(g_mode_btn);
    lv_obj_set_style_text_font(g_mode_lbl, TH_F_CAPTION, 0);
    lv_obj_set_style_text_color(g_mode_lbl, lv_color_hex(TH_TXT2), 0);
    lv_obj_center(g_mode_lbl);
    g_digits = 0; paint_keys();
    g_query[0] = 0; query_changed(); rebuild();
}
