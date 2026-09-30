/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "screens.h"
#include "orbit.h"
#include "curvelist.h"
#include "theme.h"
#include <stdlib.h>
#include <string.h>
#include "anim.h"
#include "config.h"
#include "musicdb.h"
#include "ipc.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Two Now Playing side panels:
 *  - SCR_NPMENU  (right "3-dot" icon): context - Song Info / Go to Album / Artist
 *  - SCR_TUNE    (left "tuning" icon): quick playback - Play Mode / Equalizer    */

#define ACC UI_RED


/* ---- shared current-track context (set from main.c) --------------------- */
static char g_album[160];
static char g_artist[160];
static char g_path[256];
static int  g_fav, g_have;
static lv_obj_t *g_fav_lbl;

static orbit_t g_orb;                              /* the menu: seven actions orbiting the cover */
static lv_obj_t *g_cover_img, *g_menu_bg, *g_menu_title;
static void fav_refresh(void){
    if(g_fav_lbl) lv_label_set_text(g_fav_lbl, g_fav ? "Remove from Favourites" : "Add to Favourites");
    if(g_orb.n) orbit_set_on(&g_orb, 0, g_fav, ui_current_accent());   /* a favourite fills the heart button */
}
void npmenu_set(const track_state_t *st, int playing, const void *thumb_src){
    (void)playing; (void)thumb_src;
    if(g_orb.ring && st && st->have_track && st->duration_ms > 0){      /* the cover's ring: the track's progress */
        int v = (int)((long long)st->position_ms * 1000 / st->duration_ms);
        if(abs(v - lv_arc_get_value(g_orb.ring)) >= 3) orbit_hub_ring(&g_orb, ORBIT_RING_PROGRESS, ui_media_accent(), v);
    }
    if(g_menu_title && st){ char t[200]; snprintf(t, sizeof t, "%.90s%s%.90s", st->title, st->artist[0] ? " \xC2\xB7 " : "", st->artist);
        if(strcmp(lv_label_get_text(g_menu_title), t)) lv_label_set_text(g_menu_title, t); }
    g_have = st && st->have_track;
    snprintf(g_album,  sizeof g_album,  "%s", g_have ? st->album  : "");
    snprintf(g_artist, sizeof g_artist, "%s", g_have ? st->artist : "");
    snprintf(g_path,   sizeof g_path,   "%s", g_have ? st->path   : "");
    g_fav = g_have ? st->is_favorite : 0;
    fav_refresh();
}

/* first artist token (so "A, B" -> "A" matches the split Artists browse) */
static void first_artist(const char *raw, char *out, int n){
    int i=0; while(raw[i] && raw[i]!=',' && raw[i]!=';' && i<n-1){ out[i]=raw[i]; i++; }
    out[i]=0;
    while(i>0 && out[i-1]==' ') out[--i]=0;
}

/* ---- generic list row --------------------------------------------------- */
__attribute__((unused)) static lv_obj_t *menu_row(lv_obj_t *list, const char *text, lv_event_cb_t cb, void *ud){
    lv_obj_t *r = lv_button_create(list);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, 268, 52);
    lv_obj_set_style_radius(r, 12, 0);
    lv_obj_set_style_bg_color(r, lv_color_hex(0x1C1C1E), 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_70, 0);
    lv_obj_set_style_bg_color(r, lv_color_hex(0x2C2C2E), LV_STATE_PRESSED);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(r, cb, LV_EVENT_CLICKED, ud);
    lv_obj_t *l = lv_label_create(r);
    lv_label_set_text(l, text);
    lv_obj_set_pos(l, 16, 16);
    lv_obj_set_style_text_font(l, ui_font_cjk(16), 0);   /* rows carry playlist names: chain (issue #3) */
    lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0);
    return r;
}

static lv_obj_t *panel_header(lv_obj_t *root, const char *title){
    lv_obj_set_style_bg_color(root, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    return ui_header(root, title);   /* shared standard header; returns the title label */
}

static lv_obj_t *panel_list(lv_obj_t *root){
    lv_obj_t *list = lv_obj_create(root);
    lv_obj_remove_style_all(list);
    lv_obj_set_pos(list, 46, 76); lv_obj_set_size(list, 280, 250);
    lv_obj_set_style_pad_bottom(list, 44, 0);   /* last row scrolls clear of the round bottom bezel */
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_row(list, 10, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    return list;
}

/* ---- context menu (SCR_NPMENU) ------------------------------------------ */
__attribute__((unused)) static void ctx_tag_cb(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED){ screen_back(); tagfix_current_track(); } }
__attribute__((unused)) static void ctx_tagalb_cb(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED){ screen_back(); tagfix_current_album(); } }
static void ctx_info_cb(lv_event_t *e){
    if(lv_event_get_code(e) == LV_EVENT_CLICKED) songinfo_unpin();   /* the playing track's details */ if(lv_event_get_code(e)==LV_EVENT_CLICKED){ screen_show(SCR_SONGINFO); } }
static void ctx_lyrics_cb(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED){ lyrics_open(); } }
static void ctx_album_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    /* drill the canonical DB album for this track (its path), not the player's
     * metadata album string, which may not match the DB and show an empty list */
    char dbal[160], dbar[160];
    const char *album = g_album;
    if(mdb_song_meta_by_path(g_path, dbal, sizeof dbal, dbar, sizeof dbar) && dbal[0]) album = dbal;
    if(!album[0]){ ui_toast("No album info"); return; }
    library_open_album(album); screen_show(SCR_LIBRARY);
}
static void ctx_artist_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    char dbal[160], dbar[160];
    const char *src = g_artist;
    if(mdb_song_meta_by_path(g_path, dbal, sizeof dbal, dbar, sizeof dbar) && dbar[0]) src = dbar;
    if(!src[0]){ ui_toast("No artist info"); return; }
    char a[160]; first_artist(src, a, sizeof a);
    library_open_artist(a); screen_show(SCR_LIBRARY);
}
static void plpick_reload(void);
static void ctx_addpl_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    if(!g_path[0]){ ui_toast("No song to add"); return; }   /* nothing playing/loaded */
    plpick_set_song(g_path);          /* add the current track */
    plpick_reload();                  /* refresh list + clear any old toast */
    screen_show(SCR_PLPICK);
}
static void ctx_fav_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    g_fav = !g_fav;                   /* optimistic; player confirms on next a2 */
    ui_set_favorite(g_fav);
    fav_refresh();
    ui_toast(g_fav ? "Added to Favourites" : "Removed from Favourites");   /* match Now Playing */
}
/* the cover in the hub and the album wash behind */
static void npmenu_refresh_art_now(void){
    const void *dsc = ui_current_cover_dsc();
    if(g_cover_img){
        lv_image_set_src(g_cover_img, NULL);
        if(dsc){ const lv_image_dsc_t *d = dsc; lv_image_set_src(g_cover_img, dsc);
                 if(d->header.w > 0) lv_image_set_scale(g_cover_img, (uint32_t)(ORBIT_HUB * 256 / d->header.w) + 2);
                 lv_obj_center(g_cover_img); lv_obj_remove_flag(g_cover_img, LV_OBJ_FLAG_HIDDEN); }
        else lv_obj_add_flag(g_cover_img, LV_OBJ_FLAG_HIDDEN);
    }
    if(g_menu_bg){ const void *bd = ui_current_backdrop_img(); lv_image_set_src(g_menu_bg, NULL);
        if(bd){ lv_image_set_src(g_menu_bg, bd); lv_obj_remove_flag(g_menu_bg, LV_OBJ_FLAG_HIDDEN); } else lv_obj_add_flag(g_menu_bg, LV_OBJ_FLAG_HIDDEN); }
    if(g_orb.hub) lv_obj_set_style_bg_color(g_orb.hub, ui_media_accent(), 0);
}
/* ---- the options orbit: one design for the swipe menu (SCR_NPHUB) and the "..." menu (SCR_NPMENU) ----
 * Built fresh each time either is shown, for the current track: music gets eight actions round the cover,
 * an audiobook its own four. The hub is the cover inside its progress ring; tapping it closes the menu. */
static void hub_eq_open_cb(lv_event_t *e);
static void hub_chapters_cb(lv_event_t *e);
static void hub_fsart_cb(lv_event_t *e);
static lv_obj_t *g_tags_ov;
static void tags_close(void){ if(g_tags_ov){ lv_obj_delete(g_tags_ov); g_tags_ov = NULL; } }
static void tags_pick_cb(lv_event_t *e){
    int which = (int)(intptr_t)lv_event_get_user_data(e);
    tags_close();
    if(which == 1){ screen_back(); tagfix_current_track(); }
    else if(which == 2){ screen_back(); tagfix_current_album(); }
}
static lv_obj_t *tags_btn(lv_obj_t *p, const char *t, int y, uint32_t bg, int which){
    lv_obj_t *b = lv_button_create(p);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 180, 44);
    lv_obj_align(b, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_radius(b, TH_R_PILL, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(TH_SURF2), LV_STATE_PRESSED);
    lv_obj_add_event_cb(b, tags_pick_cb, LV_EVENT_CLICKED, (void *)(intptr_t)which);
    lv_obj_t *l = lv_label_create(b); lv_label_set_text(l, t);
    lv_obj_set_style_text_font(l, TH_F_DETAIL, 0); lv_obj_set_style_text_color(l, lv_color_hex(TH_TXT1), 0); lv_obj_center(l);
    return b;
}
static void tags_cb(lv_event_t *e){                           /* Tags: this track, or the whole album */
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    tags_close();
    g_tags_ov = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g_tags_ov);
    lv_obj_set_size(g_tags_ov, 360, 360);
    lv_obj_set_style_bg_color(g_tags_ov, lv_color_hex(TH_BG), 0);
    lv_obj_set_style_bg_opa(g_tags_ov, 240, 0);
    lv_obj_add_flag(g_tags_ov, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *h = lv_label_create(g_tags_ov); lv_label_set_text(h, "Add lyrics & artwork");
    lv_obj_set_style_text_font(h, TH_F_LIST, 0); lv_obj_set_style_text_color(h, lv_color_hex(TH_TXT1), 0);
    lv_obj_align(h, LV_ALIGN_TOP_MID, 0, 92);
    lv_obj_t *n = lv_label_create(g_tags_ov); lv_label_set_text(n, "only what's missing");
    lv_obj_set_style_text_font(n, TH_F_CAPTION, 0); lv_obj_set_style_text_color(n, lv_color_hex(TH_TXT3), 0);
    lv_obj_align(n, LV_ALIGN_TOP_MID, 0, 118);
    tags_btn(g_tags_ov, "This track", 146, TH_SURF1, 1);
    tags_btn(g_tags_ov, "Whole album", 198, TH_SURF1, 2);
    tags_btn(g_tags_ov, "Cancel", 262, TH_BG, 0);
}
static void close_cb(lv_event_t *e){ (void)e; tags_close(); screen_back(); }
static lv_obj_t *g_box_menu, *g_box_hub;
static void build_menu(lv_obj_t *box);
void npmenu_refresh_art(void){ build_menu(g_box_menu); }   /* SCR_NPMENU shown */
static void build_menu(lv_obj_t *box){
    if(!box) return;
    tags_close();
    lv_obj_clean(box);
    memset(&g_orb, 0, sizeof g_orb); g_cover_img = g_menu_bg = g_menu_title = NULL;
    track_state_t st; ipc_get_state(&st);
    int book = st.path[0] && mdb_is_book_path(st.path);
    g_menu_bg = lv_image_create(box);                           /* the album's blurred wash, dimmed */
    lv_obj_set_size(g_menu_bg, 360, 360); lv_obj_clear_flag(g_menu_bg, LV_OBJ_FLAG_CLICKABLE); lv_obj_add_flag(g_menu_bg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *dim = lv_obj_create(box); lv_obj_remove_style_all(dim); lv_obj_set_size(dim, 360, 360);
    lv_obj_set_style_bg_color(dim, lv_color_hex(TH_BG), 0); lv_obj_set_style_bg_opa(dim, 215, 0); lv_obj_clear_flag(dim, LV_OBJ_FLAG_CLICKABLE);
    g_menu_title = lv_label_create(box);
    lv_label_set_text(g_menu_title, "");
    lv_label_set_long_mode(g_menu_title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(g_menu_title, 200);
    lv_obj_set_style_text_align(g_menu_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(g_menu_title, ui_font_cjk(14), 0);
    lv_obj_set_style_text_color(g_menu_title, lv_color_hex(TH_TXT2), 0);
    lv_obj_align(g_menu_title, LV_ALIGN_TOP_MID, 0, 16);
    if(book){
        static const orbit_item_t it[4] = { { LV_SYMBOL_LIST, "Chapters" }, { LV_SYMBOL_FILE, "Details" },
                                            { LV_SYMBOL_SETTINGS, "Equalizer" }, { LV_SYMBOL_IMAGE, "Full art" } };
        static lv_event_cb_t const CB[4] = { hub_chapters_cb, ctx_info_cb, hub_eq_open_cb, hub_fsart_cb };
        orbit_create(&g_orb, box, it, 4, -45, NULL);
        for(int i = 0; i < 4; i++) lv_obj_add_event_cb(g_orb.btn[i], CB[i], LV_EVENT_CLICKED, NULL);
    } else {
        static const orbit_item_t it[8] = {
            { TH_IC_HEART, "Favourite" }, { LV_SYMBOL_PLUS, "Playlist" }, { LV_SYMBOL_AUDIO, "Lyrics" }, { LV_SYMBOL_SETTINGS, "Equalizer" },
            { LV_SYMBOL_FILE, "Info" }, { LV_SYMBOL_IMAGE, "Album" }, { LV_SYMBOL_HOME, "Artist" }, { LV_SYMBOL_DOWNLOAD, "Tags" } };
        static lv_event_cb_t const CB[8] = { ctx_fav_cb, ctx_addpl_cb, ctx_lyrics_cb, hub_eq_open_cb, ctx_info_cb, ctx_album_cb, ctx_artist_cb, tags_cb };
        orbit_create(&g_orb, box, it, 8, -90, NULL);           /* each button runs its own action */
        lv_obj_set_style_text_font(g_orb.icon[0], &font_theme_20, 0);
        for(int i = 0; i < 8; i++) lv_obj_add_event_cb(g_orb.btn[i], CB[i], LV_EVENT_CLICKED, NULL);
    }
    orbit_hub_create(&g_orb, box, close_cb, "", "");            /* the hub: the cover inside its progress ring */
    lv_obj_set_style_clip_corner(g_orb.hub, true, 0);
    g_cover_img = lv_image_create(g_orb.hub);
    lv_obj_clear_flag(g_cover_img, LV_OBJ_FLAG_CLICKABLE); lv_obj_add_flag(g_cover_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *x = lv_obj_create(g_orb.hub);                     /* close: a dark disc with x over the cover */
    lv_obj_remove_style_all(x); lv_obj_set_size(x, 32, 32); lv_obj_center(x);
    lv_obj_set_style_radius(x, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_bg_color(x, lv_color_hex(TH_BG), 0);
    lv_obj_set_style_bg_opa(x, 190, 0); lv_obj_clear_flag(x, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *xl = lv_label_create(x); lv_label_set_text(xl, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_font(xl, &lv_font_montserrat_14, 0); lv_obj_set_style_text_color(xl, lv_color_hex(0xFFFFFF), 0); lv_obj_center(xl);
    orbit_hub_ring(&g_orb, ORBIT_RING_PROGRESS, ui_media_accent(), 0);
    if(book) g_fav = 0;
    fav_refresh();
    npmenu_set(&st, 1, NULL);                                   /* title + ring right away */
    npmenu_refresh_art_now();
}
void npmenu_create(lv_obj_t *root){
    lv_obj_set_style_bg_color(root, lv_color_hex(TH_BG), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    g_box_menu = lv_obj_create(root); lv_obj_remove_style_all(g_box_menu); lv_obj_set_size(g_box_menu, 360, 360);
    lv_obj_clear_flag(g_box_menu, LV_OBJ_FLAG_SCROLLABLE);
}

/* ---- add-to-playlist picker (SCR_PLPICK) -------------------------------- */
static char g_pick_path[256];
static lv_obj_t *g_pick_list;
static char g_pick_folder[512];
void plpick_set_song(const char *path){ snprintf(g_pick_path, sizeof g_pick_path, "%s", path?path:""); g_pick_folder[0] = 0; }
void plpick_set_folder(const char *dir){ snprintf(g_pick_folder, sizeof g_pick_folder, "%s", dir?dir:""); g_pick_path[0] = 0; }

static lv_timer_t *g_pick_timer;
static void pick_toast(const char *msg){ ui_toast_icon(LV_SYMBOL_OK, lv_color_hex(0x34C759), msg); }
/* duplicate-confirm dialog */
static lv_obj_t *g_dup_dlg;
static long g_dup_pid;
static void dup_close(void){ if(g_dup_dlg){ lv_obj_del(g_dup_dlg); g_dup_dlg=NULL; } }
/* Public: dismiss transient popups on any screen navigation, so the duplicate-add
 * confirm dialog (on lv_layer_top, no scrim) can't float onto a later screen and act
 * on a stale g_dup_pid/g_pick_path. Called from the screen-manager transition. */
void npmenu_close_transients(void){ dup_close(); }
static void dup_add_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    dup_close();
    /* "Add anyway" on a KNOWN duplicate: the song is in the playlist either way, so
     * "Kept in playlist" is accurate; only invalidate the scope if the row actually changed. */
    if(g_pick_path[0] && mdb_playlist_add_song(g_dup_pid, g_pick_path))
        ui_invalidate_play_scope();
    pick_toast("Kept in playlist");
}
static void dup_cancel_cb(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED) dup_close(); }
static lv_obj_t *dlg_btn(lv_obj_t *p, int y, const char *txt, lv_color_t bg, lv_color_t fg, lv_event_cb_t cb){
    lv_obj_t *b = lv_button_create(p);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 200, 38);
    lv_obj_set_ext_click_area(b, 4);   /* 38px pill -> ~46px touch target */
    lv_obj_align(b, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(b); lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(l, fg, 0); lv_obj_center(l);
    return b;
}
static void show_dup(long pid){
    g_dup_pid = pid;
    dup_close();
    g_dup_dlg = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g_dup_dlg);
    lv_obj_set_size(g_dup_dlg, 248, 184);
    lv_obj_center(g_dup_dlg);
    lv_obj_set_style_radius(g_dup_dlg, 16, 0);
    lv_obj_set_style_bg_color(g_dup_dlg, lv_color_hex(0x1C1C1E), 0);
    lv_obj_set_style_bg_opa(g_dup_dlg, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_dup_dlg, 1, 0);
    lv_obj_set_style_border_color(g_dup_dlg, lv_color_hex(0x2C2C2E), 0);
    lv_obj_clear_flag(g_dup_dlg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *t = lv_label_create(g_dup_dlg);
    lv_label_set_text(t, "Already in this playlist");
    lv_obj_set_style_text_font(t, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 24);
    dlg_btn(g_dup_dlg, 70,  "Add anyway", lv_color_hex(0x2C2C2E), lv_color_hex(0xFFFFFF), dup_add_cb);
    dlg_btn(g_dup_dlg, 116, "Cancel",     lv_color_hex(0x2C2C2E), lv_color_hex(0x8E8E93), dup_cancel_cb);
}
static void pick_add_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    long pid = (long)(intptr_t)lv_event_get_user_data(e);
    if(g_pick_folder[0]){                                   /* a whole folder (folder browser long-press) */
        int k = mdb_playlist_add_folder(pid, g_pick_folder); ui_invalidate_play_scope();
        char t[48]; snprintf(t, sizeof t, k > 0 ? "Added %d songs" : "Nothing new to add", k); pick_toast(t);
        return;
    }
    if(!g_pick_path[0]) return;
    if(mdb_playlist_has_song(pid, g_pick_path)){ show_dup(pid); return; }
    if(mdb_playlist_add_song(pid, g_pick_path)){
        ui_invalidate_play_scope(); pick_toast("Added to playlist");
    }
    else pick_toast("Couldn't add");
}
static void pick_newname_done(const char *name){
    if(!name) return;
    long pid = mdb_playlist_create(name);
    plpick_reload();
    if(pid <= 0){ pick_toast("Couldn't create"); return; }
    if(g_pick_path[0])
        pick_toast(mdb_playlist_add_song(pid, g_pick_path) ? "Created + added" : "Created (song not added)");
    else
        pick_toast("Playlist created");
}
static void pick_new_cb(lv_event_t *e){
    if(lv_event_get_code(e)==LV_EVENT_CLICKED) kbinput_open("Playlist name", "", pick_newname_done);
}
static curvelist_t g_pcl;
/* one picker row: curved with the circle like the Library's - an optional icon, the name, a songs line, and a + */
static lv_obj_t *pick_row(lv_obj_t *list, const char *name, const char *sub, const char *icon, lv_color_t icol, int plus, lv_event_cb_t cb, void *ud){
    lv_obj_t *r = lv_button_create(list);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, 268, TH_ROW_H);
    lv_obj_add_flag(r, LV_OBJ_FLAG_USER_1);                     /* curves with the circle */
    lv_obj_set_style_radius(r, TH_R_ROW, 0);
    lv_obj_set_style_bg_color(r, lv_color_hex(TH_SURF1), 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(r, lv_color_hex(TH_SURF2), LV_STATE_PRESSED);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(r, cb, LV_EVENT_CLICKED, ud);
    int x = 16;
    if(icon){ lv_obj_t *ic = lv_label_create(r); lv_label_set_text(ic, icon);
              lv_obj_set_pos(ic, 14, sub ? 18 : 17); lv_obj_set_style_text_color(ic, icol, 0); x = 42; }
    lv_obj_t *l = lv_label_create(r);
    lv_label_set_text(l, name); lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(l, x, sub ? 6 : 15); lv_obj_set_size(l, 268 - x - (plus ? 40 : 16), 24);
    lv_obj_set_style_text_font(l, ui_font_cjk(18), 0);          /* playlist names: the CJK chain */
    lv_obj_set_style_text_color(l, lv_color_hex(TH_TXT1), 0);
    if(sub){ lv_obj_t *sl = lv_label_create(r); lv_label_set_text(sl, sub);
             lv_obj_set_pos(sl, x, 31); lv_obj_set_style_text_font(sl, TH_F_DETAIL, 0); lv_obj_set_style_text_color(sl, lv_color_hex(TH_TXT2), 0); }
    if(plus){ lv_obj_t *pl = lv_label_create(r); lv_label_set_text(pl, LV_SYMBOL_PLUS);
              lv_obj_align(pl, LV_ALIGN_RIGHT_MID, -16, 0); lv_obj_set_style_text_color(pl, lv_color_hex(TH_TXT3), 0); }
    return r;
}
static void plpick_reload(void){
    if(!g_pick_list) return;
    if(g_pick_timer){ lv_timer_delete(g_pick_timer); g_pick_timer = NULL; }
    lv_obj_clean(g_pick_list);
    pick_row(g_pick_list, "New playlist...", NULL, LV_SYMBOL_PLUS, ui_current_accent(), 0, pick_new_cb, NULL);
    /* size to the real playlist count (no 64 cap). pick_add_cb takes the id by value, so
     * names/ids are only needed during this build loop -> malloc + free here. */
    int num = mdb_playlist_num();
    int okn = num>0 && (size_t)num <= ((size_t)-1) / MDB_STR;   /* 32-bit multiply overflow guard */
    char (*names)[MDB_STR] = okn ? malloc((size_t)num*MDB_STR)     : NULL;
    long  *ids            = okn ? malloc((size_t)num*sizeof(long)) : NULL;
    int n = (names && ids) ? mdb_playlists(names, ids, num) : 0;
    for(int pass = 0; pass < 1; pass++)
        for(int i=0;i<n;i++){
            int isq = 0;
            char sub[24]; int c = mdb_playlist_count(ids[i]); snprintf(sub, sizeof sub, "%d song%s", c < 0 ? 0 : c, c == 1 ? "" : "s");
            pick_row(g_pick_list, names[i], sub, isq ? LV_SYMBOL_LIST : NULL, ui_current_accent(), 1, pick_add_cb, (void*)(intptr_t)ids[i]);
        }
    free(names); free(ids);
    lv_obj_update_layout(g_pick_list); curvelist_update(&g_pcl);
}
void plpick_create(lv_obj_t *root){
    panel_header(root, "Add to playlist");
    g_pick_list = panel_list(root);
    lv_obj_set_pos(g_pick_list, 46, 72); lv_obj_set_size(g_pick_list, 268, 276);
    lv_obj_set_style_pad_row(g_pick_list, 4, 0);
    lv_obj_add_flag(g_pick_list, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    curvelist_attach(&g_pcl, g_pick_list, root, 268);
    plpick_reload();
}

/* ---- Now Playing hub (SCR_NPHUB): swipe-right page. Secondary actions for
 * the current track (play-mode & favorite live on the Now Playing screen). - */
__attribute__((unused)) static void hub_eq_cb(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED){ screen_show(SCR_TUNE); } }

static void fsart_deferred_cb(lv_timer_t *t){ lv_timer_delete(t); ui_np_fsart_open(); }
static void hub_fsart_cb(lv_event_t *e){
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    screen_show(SCR_NOWPLAYING);   /* fsart lives on the NP root -> make NP the active screen first */
    /* open after the NP slide-in settles so the transition can't stomp the overlay's z-order/opacity */
    lv_timer_t *t = lv_timer_create(fsart_deferred_cb, 360, NULL);
    lv_timer_set_repeat_count(t, 1);
}
static void hub_chapters_cb(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED) chapters_open(); }

static void hub_eq_open_cb(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED){ screen_show(SCR_EQ); } }   /* the round EQ */
void nphub_refresh(void){ build_menu(g_box_hub); }   /* called on each SCR_NPHUB show (screenmgr) */
void nphub_create(lv_obj_t *root){
    lv_obj_set_style_bg_color(root, lv_color_hex(TH_BG), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    g_box_hub = lv_obj_create(root); lv_obj_remove_style_all(g_box_hub); lv_obj_set_size(g_box_hub, 360, 360);
    lv_obj_clear_flag(g_box_hub, LV_OBJ_FLAG_SCROLLABLE);
}
/* ---- tuning menu (SCR_TUNE): Play Mode + Equalizer cyclers --------------- */
static const char *const T_MODE[] = { "Sequential","Shuffle","Repeat One","Repeat All","Single" };
static const char *const T_EQ[]   = { "Off","Jazz","Rock","R&B","Hip-Hop","Pop","Dance","Classical","Retro","Sibilance 1","Sibilance 2",
                                      "USER1","USER2","USER3","USER4","USER5","USER6","USER7","USER8","USER9","USER10" };  /* 11..20 = user PEQ slots */

static lv_obj_t *g_mode_val, *g_eq_val;
static int g_tune_built = 0;

/* re-sync the Tune panel's Play Mode / EQ labels from live cfg. The panel is built once
 * but work_mode/eq_preset also change from the NP mode button, Settings and play paths,
 * so without this the labels go stale on re-open. Called on SCR_TUNE show. */
void tune_refresh(void){
    if(!g_tune_built) return;
    int m = cfg_get_int("work_mode", 0); if(m<0||m>4) m=0;
    int q = cfg_get_int("eq_preset", 0); if(q<0||q>20) q=0;
    if(g_mode_val) lv_label_set_text(g_mode_val, T_MODE[m]);
    if(g_eq_val)   lv_label_set_text(g_eq_val,   T_EQ[q]);
}

static void cyc_apply(const char *key, const char *const *opts, int n, void *valobj, int dir){
    int base = cfg_get_int(key, 0); if(base < 0 || base >= n) base = 0;   /* normalize a corrupt stored value BEFORE stepping (no signed overflow, no OOB) */
    int v = base + dir;
    if(v<0) v=n-1; else if(v>=n) v=0;
    if(strcmp(key,"eq_preset")) cfg_set_int(key, v);   /* EQ persists on a successful send via ui_eq_select */
    if(!strcmp(key,"work_mode")){ if(!ui_book_active()) ui_set_workmode(v); }   /* an active book stays in Single; cfg still updates so music later uses the chosen mode */
    else if(!strcmp(key,"eq_preset")){ if(ui_eq_select(v) < 0){ v = cfg_get_int("eq_preset", 0); if(v<0||v>=n) v=0; } }   /* failed send -> label keeps the real preset (clamped: a corrupt stored value must not index opts[] OOB) */
    if(valobj) lv_label_set_text((lv_obj_t*)valobj, opts[v]);
}
static void mode_dir_cb(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED) cyc_apply("work_mode", T_MODE, 5, g_mode_val, (int)(intptr_t)lv_event_get_user_data(e)); }
static void eq_dir_cb(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED) cyc_apply("eq_preset", T_EQ, 21, g_eq_val, (int)(intptr_t)lv_event_get_user_data(e)); }
static void eq_custom_cb(lv_event_t *e){ if(lv_event_get_code(e)==LV_EVENT_CLICKED) screen_show(SCR_EQ); }

/* a "< label : value >" stepper row */
static lv_obj_t *cyc_row(lv_obj_t *parent, int y, const char *label,
                         lv_event_cb_t cb, lv_obj_t **out_val, const char *cur){
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, 300, 64); lv_obj_align(row, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lb = lv_label_create(row);
    lv_label_set_text(lb, label);
    lv_obj_align(lb, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_text_font(lb, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lb, lv_color_hex(0x8E8E93), 0);

    lv_obj_t *val = lv_label_create(row);
    lv_label_set_text(val, cur);
    lv_obj_align(val, LV_ALIGN_TOP_MID, 0, 26);
    lv_obj_set_style_text_font(val, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(val, lv_color_hex(ACC), 0);
    *out_val = val;

    lv_obj_t *l = lv_button_create(row); lv_obj_remove_style_all(l);
    lv_obj_set_size(l, 44, 44); lv_obj_align(l, LV_ALIGN_LEFT_MID, 4, 8);
    lv_obj_t *li=lv_label_create(l); lv_label_set_text(li, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(li, lv_color_hex(0xFFFFFF), 0); lv_obj_center(li);
    lv_obj_add_event_cb(l, cb, LV_EVENT_CLICKED, (void*)(intptr_t)-1);
    lv_obj_t *r = lv_button_create(row); lv_obj_remove_style_all(r);
    lv_obj_set_size(r, 44, 44); lv_obj_align(r, LV_ALIGN_RIGHT_MID, -4, 8);
    lv_obj_t *ri=lv_label_create(r); lv_label_set_text(ri, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(ri, lv_color_hex(0xFFFFFF), 0); lv_obj_center(ri);
    lv_obj_add_event_cb(r, cb, LV_EVENT_CLICKED, (void*)(intptr_t)1);
    return row;
}

void tune_create(lv_obj_t *root){
    panel_header(root, "Playback");
    int m = cfg_get_int("work_mode", 0); if(m<0||m>4) m=0;
    int q = cfg_get_int("eq_preset", 0); if(q<0||q>20) q=0;
    cyc_row(root, 84,  "PLAY MODE",  mode_dir_cb, &g_mode_val, T_MODE[m]);
    cyc_row(root, 158, "EQUALIZER",  eq_dir_cb,   &g_eq_val,   T_EQ[q]);
    g_tune_built = 1;

    lv_obj_t *cust = lv_button_create(root);
    lv_obj_remove_style_all(cust);
    lv_obj_set_size(cust, 180, 40); lv_obj_align(cust, LV_ALIGN_TOP_MID, 0, 236);
    lv_obj_set_style_radius(cust, 20, 0);
    lv_obj_set_style_bg_color(cust, lv_color_hex(0x1C1C1E), 0);
    lv_obj_set_style_bg_opa(cust, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(cust, lv_color_hex(0x2C2C2E), LV_STATE_PRESSED);
    lv_obj_add_event_cb(cust, eq_custom_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cl=lv_label_create(cust); lv_label_set_text(cl, "Custom EQ");
    lv_obj_set_style_text_font(cl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(cl, lv_color_hex(0xFFFFFF), 0); lv_obj_center(cl);
}
