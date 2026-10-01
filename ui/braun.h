/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The Braun theme (Settings > Display > Theme): 70s Braun / Dieter Rams - warm off-white, a speaker-grille
 * dot texture, dark-grey knobs, a solid lower "segment" panel following the round edge, Inter-like type,
 * and one orange accent meaning only "on / primary / focused". Everything here is used only when the
 * Braun theme is active (th_braun()); the Ring theme is untouched. */
#pragma once
#include "lvgl/lvgl.h"
#define BR_BG      0xECE8E0   /* the face */
#define BR_PANEL   0xF5F2EC   /* solid panels on the grille */
#define BR_SURF    0xDED8CD   /* buttons, tracks */
#define BR_DOT     0xC4BEB4   /* the grille dots */
#define BR_RULE    0xD6D0C6
#define BR_TXT     0x1C1B19
#define BR_TXT2    0x68645E
#define BR_TXT3    0x96928B
#define BR_ACC     0xE85A16   /* orange: on / primary / focus */
#define BR_KNOB    0x3A3936
#define BR_KNOB_L  0x54524E
#define BR_KNOB_IC 0xFFFFFF   /* icons on the dark knobs: white */
int  th_braun(void);
const lv_font_t *br_font(int size, int bold);                /* Inter: Medium 12/14/16, Bold 16/18/22, 44 (digits) */                                        /* 1 when the Braun theme is active (read once at boot) */
void br_face(lv_obj_t *root);                               /* off-white face + the grille (shared image) */
lv_obj_t *br_segment(lv_obj_t *root, int y);                /* the lower panel: straight top at y, down to the rim */
lv_obj_t *br_disc(lv_obj_t *parent, int cx, int cy, int r, uint32_t col);   /* a solid disc (panels behind content) */
lv_obj_t *br_knob(lv_obj_t *parent, int cx, int cy, int r, const char *icon, const lv_font_t *font);  /* a dark knob */
void br_knob_set_on(lv_obj_t *knob, int on);                /* pointer orange + lamp lit */
lv_obj_t *br_button(lv_obj_t *parent, int cx, int cy, int r, const char *icon, const lv_font_t *font, int primary);  /* flat disc button */
void br_style_switch(lv_obj_t *sw);                         /* an lv_switch as a Braun sliding switch */
lv_obj_t *br_clock_face(lv_obj_t *parent, int cx, int cy, int rd, int numerals);   /* the Braun clock face, drawn once */
lv_obj_t *br_weather_window(lv_obj_t *parent, int x, int y, lv_obj_t **label_out);   /* the window at 3 o'clock */
void br_weather_text(lv_obj_t *label, const char *s);   /* "<icon>  14°C  Sunny" -> "<icon> 14°" */
lv_obj_t *br_label(lv_obj_t *parent, const char *txt, const lv_font_t *font, uint32_t col);
