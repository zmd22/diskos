/* SPDX-License-Identifier: GPL-3.0-or-later */
/* diskOS fork design tokens: every shared colour, size, radius, arc width, duration and icon lives here.
 * Screens use these names instead of raw values, so the interface stays one piece as it grows. */
#ifndef THEME_H
#define THEME_H
#include "lvgl/lvgl.h"

/* ---- colour -------------------------------------------------------------------------------------------
 * Rule: the accent (red) means "on" / selected in the UI. Media surfaces - anything showing the playing
 * track - use the album's own colour instead (ui_media_accent()). Levels (brightness, volume) are text-1. */
#define TH_BG        0x000000
#define TH_SURF1     0x1C1C1E      /* cards, rows, tiles, pills */
#define TH_SURF2     0x2C2C2E      /* pressed / lifted */
#define TH_TRACK     0x3A3A3C      /* the unfilled part of an arc */
#define TH_TXT1      0xFFFFFF      /* primary text and glyphs */
#define TH_TXT2      0xAEAEB2      /* secondary: details, captions */
#define TH_TXT3      0x636366      /* tertiary: least important */
#define TH_ACCENT    0xE4122C      /* = UI_RED */

/* ---- type scale (Montserrat; lists that show user text use ui_font_cjk() at the same sizes) ---------- */
#define TH_F_CLOCK   (&lv_font_montserrat_46)
#define TH_F_TITLE   (&lv_font_montserrat_20)   /* screen headers */
#define TH_F_LIST    (&lv_font_montserrat_18)   /* list and row titles */
#define TH_F_DETAIL  (&lv_font_montserrat_16)   /* values, subtitles, date */
#define TH_F_CAPTION (&lv_font_montserrat_12)   /* tile captions, times, hub captions */
/* poster 28 is s_font28 in ui.c (needs the CJK fallback chain); the Now Playing play/pause stays oversized */

/* ---- shape --------------------------------------------------------------------------------------------- */
#define TH_R_ROW     14
#define TH_R_CARD    18
#define TH_R_PILL    LV_RADIUS_CIRCLE
#define TH_ROW_H     54            /* list rows at the list/detail sizes */

/* ---- rims: what each edge of the round panel is for ----------------------------------------------------
 * top: status & level   bottom: time & amount   left: position in a list   right: jump (A-Z) */
#define TH_ARC_IND   3             /* indicators: battery, progress, immersive progress */
#define TH_ARC_CTL   10            /* controls you drag: brightness, volume (rounded ends) */

/* ---- motion -------------------------------------------------------------------------------------------- */
#define TH_T_PRESS   120
#define TH_T_SCREEN  200

/* ---- icons: Font Awesome 4.7 (font_theme_20 carries the glyphs LVGL's symbol set lacks) ------------ */
LV_FONT_DECLARE(font_theme_20);
#define TH_IC_SEARCH "\xEF\x80\x82"   /* f002 */
#define TH_IC_HEART  "\xEF\x80\x84"   /* f004 solid: state is carried by colour, not by outline */
#define TH_IC_RECORD "\xEF\x86\x92"   /* f192 dot-circle: the immersive (record) button */

/* the playing track's colour, or the accent when the cover's colour is too dull (ui.c) */
lv_color_t ui_media_accent(void);
#endif
