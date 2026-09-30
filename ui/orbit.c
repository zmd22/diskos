/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "orbit.h"
#include "theme.h"
#include <math.h>
#include <stdint.h>
static void btn_cb(lv_event_t *e){
    orbit_t *o = lv_event_get_user_data(e);
    lv_obj_t *b = lv_event_get_current_target(e);
    for(int i = 0; i < o->n; i++) if(o->btn[i] == b){ if(o->pick) o->pick(i); return; }
}
void orbit_create(orbit_t *o, lv_obj_t *root, const orbit_item_t *it, int n, int first_deg, orbit_pick_cb pick){
    if(n > 8) n = 8;
    o->n = n; o->pick = pick; o->first_deg = first_deg; o->ring = o->hub = o->hub_icon = o->hub_cap = NULL;
    for(int i = 0; i < n; i++){
        float a = (first_deg + i * 360.0f / n) * 3.14159265f / 180.0f;
        int cx = (int)lroundf(ORBIT_R * cosf(a)), cy = (int)lroundf(ORBIT_R * sinf(a));
        lv_obj_t *b = lv_button_create(root);
        o->btn[i] = b;
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, ORBIT_BTN, ORBIT_BTN);
        lv_obj_align(b, LV_ALIGN_CENTER, cx, cy);
        lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(TH_SURF1), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(TH_SURF2), LV_STATE_PRESSED);
        lv_obj_set_style_border_color(b, lv_color_hex(TH_ACCENT), 0);
        lv_obj_set_style_border_width(b, 0, 0);
        lv_obj_set_ext_click_area(b, 8);                       /* a finger-sized target */
        lv_obj_add_event_cb(b, btn_cb, LV_EVENT_SHORT_CLICKED, o);
        o->icon[i] = lv_label_create(b);
        lv_label_set_text(o->icon[i], it[i].glyph);
        lv_obj_set_style_text_font(o->icon[i], &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(o->icon[i], lv_color_hex(TH_TXT1), 0);
        lv_obj_center(o->icon[i]);
        o->cap[i] = NULL;
        if(it[i].cap && it[i].cap[0]){
            o->cap[i] = lv_label_create(root);
            lv_label_set_text(o->cap[i], it[i].cap);
            lv_obj_set_style_text_font(o->cap[i], TH_F_CAPTION, 0);
            lv_obj_set_style_text_color(o->cap[i], lv_color_hex(TH_TXT2), 0);
            lv_obj_align(o->cap[i], LV_ALIGN_CENTER, cx, cy + ORBIT_BTN / 2 + 11);
            lv_obj_clear_flag(o->cap[i], LV_OBJ_FLAG_CLICKABLE);
        }
    }
}
void orbit_cap_width(orbit_t *o, int w, const lv_font_t *font){
    for(int i = 0; i < o->n; i++){
        if(!o->cap[i]) continue;
        float a = (o->first_deg + i * 360.0f / o->n) * 3.14159265f / 180.0f;
        int cx = (int)lroundf(ORBIT_R * cosf(a)), cy = (int)lroundf(ORBIT_R * sinf(a));
        lv_label_set_long_mode(o->cap[i], LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(o->cap[i], LV_TEXT_ALIGN_CENTER, 0);
        if(font) lv_obj_set_style_text_font(o->cap[i], font, 0);
        lv_obj_set_size(o->cap[i], w, lv_font_get_line_height(lv_obj_get_style_text_font(o->cap[i], 0)) + 2);   /* one line: longer names end in ... */
        lv_obj_align(o->cap[i], LV_ALIGN_CENTER, cx, cy + ORBIT_BTN / 2 + 11);
    }
}
void orbit_set_on(orbit_t *o, int i, int on, lv_color_t accent){
    if(i < 0 || i >= o->n) return;
    lv_obj_set_style_bg_color(o->btn[i], on ? accent : lv_color_hex(TH_SURF1), 0);
    lv_obj_set_style_border_width(o->btn[i], 0, 0);
    lv_obj_set_style_text_color(o->icon[i], lv_color_hex(TH_TXT1), 0);
    if(o->cap[i]) lv_obj_set_style_text_color(o->cap[i], lv_color_hex(on ? TH_TXT1 : TH_TXT2), 0);
}
void orbit_set_pending(orbit_t *o, int p, lv_color_t accent){
    for(int i = 0; i < o->n; i++){
        int on = (i == p);
        lv_obj_set_style_border_width(o->btn[i], on ? 3 : 0, 0);
        lv_obj_set_style_border_color(o->btn[i], accent, 0);
        if(on){ lv_obj_set_style_bg_color(o->btn[i], lv_color_hex(TH_SURF2), 0);
                lv_obj_set_style_text_color(o->icon[i], accent, 0); }
    }
}
void orbit_set_glyph(orbit_t *o, int i, const char *glyph){ if(i >= 0 && i < o->n) lv_label_set_text(o->icon[i], glyph); }

static void spin_exec(void *var, int32_t v){ lv_arc_set_rotation((lv_obj_t *)var, v % 360); }
void orbit_hub_create(orbit_t *o, lv_obj_t *root, lv_event_cb_t hub_cb, const char *glyph, const char *cap){
    o->ring = lv_arc_create(root);
    lv_obj_remove_style(o->ring, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(o->ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(o->ring, ORBIT_RING, ORBIT_RING);
    lv_obj_center(o->ring);
    lv_arc_set_bg_angles(o->ring, 0, 360);
    lv_arc_set_range(o->ring, 0, 1000);
    lv_obj_set_style_arc_width(o->ring, TH_ARC_IND, LV_PART_MAIN);
    lv_obj_set_style_arc_color(o->ring, lv_color_hex(TH_TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_width(o->ring, TH_ARC_IND, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(o->ring, true, LV_PART_INDICATOR);
    o->hub = lv_button_create(root);
    lv_obj_remove_style_all(o->hub);
    lv_obj_set_size(o->hub, ORBIT_HUB, ORBIT_HUB);
    lv_obj_center(o->hub);
    lv_obj_set_style_radius(o->hub, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o->hub, lv_color_hex(0x121214), 0);
    lv_obj_set_style_bg_color(o->hub, lv_color_hex(TH_SURF2), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(o->hub, LV_OPA_COVER, 0);
    if(hub_cb) lv_obj_add_event_cb(o->hub, hub_cb, LV_EVENT_CLICKED, NULL);
    o->hub_icon = lv_label_create(o->hub);
    lv_obj_set_style_text_font(o->hub_icon, &lv_font_montserrat_28, 0);
    lv_obj_align(o->hub_icon, LV_ALIGN_CENTER, 0, -9);
    o->hub_cap = lv_label_create(o->hub);
    lv_obj_set_style_text_font(o->hub_cap, TH_F_CAPTION, 0);
    lv_obj_set_style_text_color(o->hub_cap, lv_color_hex(TH_TXT2), 0);
    lv_obj_align(o->hub_cap, LV_ALIGN_CENTER, 0, 20);
    orbit_hub_set(o, glyph, lv_color_hex(TH_TXT1), cap);
    orbit_hub_ring(o, ORBIT_RING_GREY, lv_color_hex(TH_TRACK), 0);
}
void orbit_hub_ring(orbit_t *o, int mode, lv_color_t col, int value){
    if(!o->ring) return;
    lv_anim_delete(o->ring, spin_exec);
    lv_arc_set_rotation(o->ring, 270);                                  /* 12 o'clock */
    lv_obj_set_style_arc_color(o->ring, col, LV_PART_INDICATOR);
    switch(mode){
        case ORBIT_RING_GREY:     lv_arc_set_value(o->ring, 0); break;
        case ORBIT_RING_FULL:     lv_arc_set_value(o->ring, 1000); break;
        case ORBIT_RING_PROGRESS: lv_arc_set_value(o->ring, value < 0 ? 0 : value > 1000 ? 1000 : value); break;
        case ORBIT_RING_SPIN: {
            lv_arc_set_value(o->ring, 280);                             /* a short arc that goes round */
            lv_anim_t a; lv_anim_init(&a);
            lv_anim_set_var(&a, o->ring); lv_anim_set_exec_cb(&a, spin_exec);
            lv_anim_set_values(&a, 270, 270 + 360); lv_anim_set_duration(&a, 1000);
            lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE); lv_anim_start(&a);
        } break;
    }
}
void orbit_hub_set(orbit_t *o, const char *glyph, lv_color_t col, const char *cap){
    if(!o->hub) return;
    lv_label_set_text(o->hub_icon, glyph ? glyph : "");
    lv_obj_set_style_text_color(o->hub_icon, col, 0);
    lv_label_set_text(o->hub_cap, cap ? cap : "");
}
lv_obj_t *orbit_title(lv_obj_t *root, const char *text){
    lv_obj_t *t = lv_label_create(root);
    lv_label_set_text(t, text);
    lv_obj_set_style_text_font(t, TH_F_CAPTION, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(TH_TXT2), 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 16);
    return t;
}
