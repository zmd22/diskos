/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The Braun theme (Settings > Display > Theme): 70s Braun / Dieter Rams - warm off-white, a speaker-grille
 * dot texture, dark-grey knobs, a solid lower "segment" panel following the round edge, Inter-like type,
 * and one orange accent meaning only "on / primary / focused". Everything here is used only when the
 * Braun theme is active (th_braun()); the Ring theme is untouched. */
#pragma once
#include "lvgl/lvgl.h"
/* Two palettes: light (the original) and dark (Settings > Display > Theme > Braun Dark, or Auto day/night).
 * br_pick(light, dark) returns the one for the running variant; every Braun colour goes through it. */
uint32_t br_pick(uint32_t light, uint32_t dark);
int  br_dark(void);                                          /* 1 when Braun runs in its dark variant */
#define BR_BG      br_pick(0xECE8E0, 0x1F1E1C)   /* the face */
#define BR_PANEL   br_pick(0xF5F2EC, 0x2A2926)   /* solid panels on the grille */
#define BR_SURF    br_pick(0xDED8CD, 0x34332F)   /* buttons, tracks */
#define BR_DOT     br_pick(0xC4BEB4, 0x3A3935)   /* the grille dots */
#define BR_RULE    br_pick(0xD6D0C6, 0x3A3935)
#define BR_TXT     br_pick(0x1C1B19, 0xECE8E0)
#define BR_TXT2    br_pick(0x68645E, 0xA8A39A)
#define BR_TXT3    br_pick(0x96928B, 0x77736C)
#define BR_ACC     0xE85A16                     /* orange: on / primary / focus (both variants) */
#define BR_KNOB    br_pick(0x3A3936, 0xC9C5BD)   /* light: dark-grey knobs; dark: aluminium knobs */
#define BR_KNOB_L  br_pick(0x54524E, 0xE2DED6)
#define BR_KNOB_IC br_pick(0xFFFFFF, 0x1C1B19)   /* icons on the knobs */
#define BR_PTR     br_pick(0xC8C4BC, 0x5E5C57)   /* a knob's pointer when off */
#define BR_PRESS   br_pick(0xCFC8BC, 0x46453F)   /* a pressed flat button */
#define BR_SHADOW  br_pick(0xA8A296, 0x000000)   /* knob / toast shadow */
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
