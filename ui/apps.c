/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "screens.h"
#include "anim.h"
#include "theme.h"
#include "orbit.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>

/* Homebrew app launcher. Lists /usr/data/apps/<name>/ entries; each may carry an
 * app.conf ("name=...", "exec=..."), else defaults to dir name + .../app. Tapping
 * a row asks main.c to fork/exec it (app owns fb0 + touch while running). */

#define APPS_DIR "/usr/data/apps"
#define MAX_APPS 24

LV_FONT_DECLARE(font_icons_28)          /* FontAwesome 28px: play-mode + app-tile glyphs */
#define LFM_ICON "\xEF\x88\x82"         /* f202 lastfm    -> Last.fm */
/* Settings/File reuse LV_SYMBOL_SETTINGS (f013) / LV_SYMBOL_FILE (f15b), now in this font */

typedef struct { char name[64]; char exec[256]; } app_t;
static app_t g_apps[MAX_APPS];
static int g_napps;

static void scan_apps(void){
    g_napps = 0;
    DIR *d = opendir(APPS_DIR);
    if(!d) return;
    struct dirent *de;
    while((de = readdir(d)) && g_napps < MAX_APPS){
        if(de->d_name[0] == '.') continue;
        app_t *a = &g_apps[g_napps];
        snprintf(a->name, sizeof a->name, "%.60s", de->d_name);
        snprintf(a->exec, sizeof a->exec, APPS_DIR "/%.200s/app", de->d_name);
        char conf[320]; snprintf(conf, sizeof conf, APPS_DIR "/%.200s/app.conf", de->d_name);
        FILE *f = fopen(conf, "r");
        if(f){
            char line[320];
            while(fgets(line, sizeof line, f)){
                char *nl = strpbrk(line, "\r\n"); if(nl) *nl = 0;   /* strip CRLF too (Windows-edited app.conf) */
                if(!strncmp(line, "name=", 5)) snprintf(a->name, sizeof a->name, "%.63s", line+5);
                else if(!strncmp(line, "exec=", 5)) snprintf(a->exec, sizeof a->exec, "%.255s", line+5);
            }
            fclose(f);
        }
        g_napps++;
    }
    closedir(d);
}

/* ---- the orbit ----------------------------------------------------------------------------------------
 * Last.fm, the homebrew apps and Settings orbit a hub, six to a page. With more than six the hub becomes
 * "next page" (1/2, 2/2, ...); with one page it's Back. The edge swipe always goes back. */
#define PER_PAGE 6
typedef struct { const char *icon; char name[64]; int kind; int idx; } aitem_t;   /* kind: 0 Last.fm, 1 app, 2 Settings */
static aitem_t g_items[MAX_APPS + 2];
static int g_nitems, g_page;
static lv_obj_t *g_box;
static orbit_t g_orb;
static void build_page(void);
static void apps_pick(int i){
    int k = g_page * PER_PAGE + i;
    if(k < 0 || k >= g_nitems) return;
    switch(g_items[k].kind){
        case 0: lastfm_open(); break;
        case 1: if(g_items[k].idx >= 0 && g_items[k].idx < g_napps) app_launch(g_apps[g_items[k].idx].exec); break;
        case 2: screen_show(SCR_SETTINGS); break;
    }
}
static void hub_cb(lv_event_t *e){
    (void)e;
    int pages = (g_nitems + PER_PAGE - 1) / PER_PAGE;
    if(pages > 1){ g_page = (g_page + 1) % pages; build_page(); }
    else screen_back();
}
static void build_page(void){
    if(!g_box) return;
    lv_obj_clean(g_box);
    memset(&g_orb, 0, sizeof g_orb);
    int pages = (g_nitems + PER_PAGE - 1) / PER_PAGE; if(pages < 1) pages = 1;
    if(g_page >= pages) g_page = 0;
    int first = g_page * PER_PAGE, cnt = g_nitems - first; if(cnt > PER_PAGE) cnt = PER_PAGE;
    orbit_title(g_box, "Apps");
    orbit_item_t it[PER_PAGE];
    for(int i = 0; i < cnt; i++){ it[i].glyph = g_items[first + i].icon; it[i].cap = g_items[first + i].name; }
    if(cnt < 1){ orbit_hub_create(&g_orb, g_box, hub_cb, LV_SYMBOL_LEFT, "Back"); return; }
    orbit_create(&g_orb, g_box, it, cnt, -90, apps_pick);
    for(int i = 0; i < cnt; i++){
        lv_obj_set_style_text_font(g_orb.icon[i], &font_icons_28, 0);
        lv_obj_set_style_text_color(g_orb.icon[i], ui_current_accent(), 0);
    }
    orbit_cap_width(&g_orb, 96, ui_font_cjk(14));
    char pg[24]; snprintf(pg, sizeof pg, "%d/%d", g_page + 1, pages);
    orbit_hub_create(&g_orb, g_box, hub_cb, pages > 1 ? LV_SYMBOL_RIGHT : LV_SYMBOL_LEFT, pages > 1 ? pg : "Back");
}

void apps_reload(void){
    if(!g_box) return;
    scan_apps();
    g_nitems = 0;
    /* built-in: Last.fm scrobbling (the FA lastfm brand glyph) */
    g_items[g_nitems].icon = LFM_ICON; snprintf(g_items[g_nitems].name, sizeof g_items[0].name, "Last.fm"); g_items[g_nitems].kind = 0; g_items[g_nitems].idx = -1; g_nitems++;
    /* homebrew apps from /usr/data/apps */
    for(int i = 0; i < g_napps && g_nitems < MAX_APPS + 1; i++){
        g_items[g_nitems].icon = LV_SYMBOL_FILE; snprintf(g_items[g_nitems].name, sizeof g_items[0].name, "%.63s", g_apps[i].name);
        g_items[g_nitems].kind = 1; g_items[g_nitems].idx = i; g_nitems++;
    }
    /* built-in: Settings */
    g_items[g_nitems].icon = LV_SYMBOL_SETTINGS; snprintf(g_items[g_nitems].name, sizeof g_items[0].name, "Settings"); g_items[g_nitems].kind = 2; g_items[g_nitems].idx = -1; g_nitems++;
    g_page = 0;
    build_page();
}

void apps_create(lv_obj_t *root){
    lv_obj_set_style_bg_color(root, lv_color_hex(TH_BG), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    g_box = lv_obj_create(root);
    lv_obj_remove_style_all(g_box);
    lv_obj_set_size(g_box, 360, 360);
    lv_obj_clear_flag(g_box, LV_OBJ_FLAG_SCROLLABLE);
    apps_reload();
}

lv_obj_t *apps_scroller(void){ return NULL; }      /* an orbit has no long list */
