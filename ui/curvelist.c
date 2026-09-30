/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "curvelist.h"
#include "theme.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#define NB 5
static void curve(curvelist_t *c){
    uint32_t n = lv_obj_get_child_count(c->list);
    for(uint32_t i = 0; i < n; i++){
        lv_obj_t *r = lv_obj_get_child(c->list, i);
        if(!lv_obj_has_flag(r, LV_OBJ_FLAG_USER_1)) continue;
        lv_area_t a; lv_obj_get_coords(r, &a);
        if(a.y2 < -40 || a.y1 > 400) continue;                      /* off-screen: nothing to do */
        int dy = abs((a.y1 + a.y2) / 2 - 180) + lv_area_get_height(&a) / 2;
        int half = dy < 176 ? (int)sqrtf((float)(180 * 180 - dy * dy)) - 10 : 0;
        int band = NB - 1;
        for(int b = 0; b < NB; b++) if(c->full_w - b * 20 <= 2 * half){ band = b; break; }
        if((intptr_t)lv_obj_get_user_data(r) == band + 1) continue;  /* unchanged band: no work */
        lv_obj_set_user_data(r, (void *)(intptr_t)(band + 1));
        int w = c->full_w - band * 20, d = c->full_w - w;
        lv_obj_set_width(r, w);
        for(uint32_t k = 0; k < lv_obj_get_child_count(r); k++){       /* keep text inside the row */
            lv_obj_t *ch = lv_obj_get_child(r, k);
            if(!lv_obj_check_type(ch, &lv_label_class)) continue;
            intptr_t orig = (intptr_t)lv_obj_get_user_data(ch);
            if(!orig){ orig = ((intptr_t)lv_obj_get_x(ch) << 16) | (lv_obj_get_width(ch) & 0xFFFF); lv_obj_set_user_data(ch, (void *)orig); }
            int ox = (int)(orig >> 16);
            if(ox >= 130) lv_obj_set_x(ch, ox - d);                   /* right-hand value slides in */
        }
    }
}
static void dots(curvelist_t *c){
    int32_t sy = lv_obj_get_scroll_y(c->list), sb = lv_obj_get_scroll_bottom(c->list), tot = sy + sb;
    int idx = (tot <= 24) ? -1 : (int)((sy * 10 + tot / 2) / tot);
    if(idx > 10) idx = 10;
    if(idx == c->cur) return;
    c->cur = idx;
    for(int k = 0; k < 11; k++){
        if(idx < 0){ lv_obj_add_flag(c->dot[k], LV_OBJ_FLAG_HIDDEN); continue; }
        lv_obj_remove_flag(c->dot[k], LV_OBJ_FLAG_HIDDEN);
        int on = (k == idx), sz = on ? 8 : 5;
        float a = (212.0f - 6.4f * k) * 3.14159265f / 180.0f;         /* top to bottom along the left rim */
        lv_obj_set_size(c->dot[k], sz, sz);
        lv_obj_set_pos(c->dot[k], (int)(180 + 168 * cosf(a)) - sz / 2, (int)(180 + 168 * sinf(a)) - sz / 2);
        lv_obj_set_style_bg_color(c->dot[k], lv_color_hex(on ? TH_ACCENT : 0x5A5A60), 0);
    }
}
static void scroll_cb(lv_event_t *e){ curvelist_t *c = lv_event_get_user_data(e); curve(c); dots(c); }
void curvelist_attach(curvelist_t *c, lv_obj_t *list, lv_obj_t *root, int full_w){
    c->list = list; c->full_w = full_w; c->cur = -2;
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);   /* narrowed rows stay centred */
    lv_obj_add_event_cb(list, scroll_cb, LV_EVENT_SCROLL, c);
    for(int k = 0; k < 11; k++){
        c->dot[k] = lv_obj_create(root);
        lv_obj_remove_style_all(c->dot[k]);
        lv_obj_set_style_radius(c->dot[k], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(c->dot[k], LV_OPA_COVER, 0);
        lv_obj_clear_flag(c->dot[k], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(c->dot[k], LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_IGNORE_LAYOUT);
    }
}
void curvelist_update(curvelist_t *c){
    if(!c->list) return;
    lv_obj_update_layout(c->list);
    curve(c); c->cur = -2; dots(c);
}
