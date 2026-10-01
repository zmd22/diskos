/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
/* The Queue: songs you enqueue play right after the current song (after the last enqueued one, if any), in
 * the order you enqueued them - Shuffle/Repeat paused - then playback returns to what you were playing.
 *
 * How: the player plays a COPY of a list handed to it (the playback slot) and keeps that copy's order
 * (verified on device). Enqueuing only edits the queue - the song that's playing is never touched. When it
 * ends (or you press Next), the player starts its own next song; at that moment the UI hands it a copy:
 *      [the queued songs] + [that next song and the rest of the list you were playing]
 * and plays the first queued song from its start. No mid-song reload, no seeking back. As queued songs
 * start they drop off the queue; when playback reaches your list again the UI switches back to it at that
 * song, in your own play mode. Going back (Previous) is left alone. The queue is kept in
 * /usr/data/diskos_queue.tsv. */
#include "screens.h"
#include "braun.h"
#include "theme.h"
#include "curvelist.h"
#include "musicdb.h"
#include "ipc.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define QMAX 400
#define RMAX 4000
#ifndef QUEUE_FILE
#define QUEUE_FILE "/usr/data/diskos_queue.tsv"
#endif
typedef struct { char path[256]; char title[96]; char artist[96]; long dur; } qitem_t;
static qitem_t g_q[QMAX];                         /* what's still to come, in play order */
static int g_qn, g_loaded;

/* the session: our copy is what the player is playing */
static int  g_active;

static char (*g_rest)[256]; static int g_nrest;   /* the rest of the original list, after the anchor */
static char (*g_ctx)[256];  static int g_nctx;    /* the original list, whole (for the hand-back position) */
static int  g_ctx_type = -1; static char g_ctx_name[256]; static long g_ctx_pid;
static char g_ctx_label[120];                     /* "then back to ..." */
static char (*g_copy)[256]; static int g_ncopy;   /* the copy the player is playing (while active) */
static char g_last_path[256];
static char g_prev2[256];                        /* the song before the last one: going back to it = Previous */
static uint32_t g_back_at;                       /* the Previous button was just pressed (the next change is a back) */
void queue_note_prev(void){ g_back_at = lv_tick_get() | 1; }
static long g_last_pos, g_last_dur;               /* where the last song was, to tell a natural end */
static int  g_skip_change;                        /* the user just started something: that start isn't ours */

static void view_reload(void);
/* ---------------------------------------------------------------- storage */
static void save(void){
    char tmp[64]; snprintf(tmp, sizeof tmp, "%s.tmp", QUEUE_FILE);
    FILE *f = fopen(tmp, "w"); if(!f) return;
    for(int i = 0; i < g_qn; i++) fprintf(f, "%s\t%s\t%s\t%ld\n", g_q[i].path, g_q[i].title, g_q[i].artist, g_q[i].dur);
    if(fclose(f) == 0) rename(tmp, QUEUE_FILE); else unlink(tmp);
}
static void load(void){
    if(g_loaded) return;
    g_loaded = 1; g_qn = 0;
    FILE *f = fopen(QUEUE_FILE, "r"); if(!f) return;
    char line[700];
    while(g_qn < QMAX && fgets(line, sizeof line, f)){
        char *nl = strchr(line, '\n'); if(nl) *nl = 0;
        char *p[4] = { line, 0, 0, 0 }; int k = 1;
        for(char *c = line; *c && k < 4; c++) if(*c == '\t'){ *c = 0; p[k++] = c + 1; }
        if(!p[0][0]) continue;
        qitem_t *q = &g_q[g_qn++];
        snprintf(q->path, sizeof q->path, "%s", p[0]); snprintf(q->title, sizeof q->title, "%s", p[1] ? p[1] : "");
        snprintf(q->artist, sizeof q->artist, "%s", p[2] ? p[2] : ""); q->dur = p[3] ? atol(p[3]) : 0;
    }
    fclose(f);
}
/* ---------------------------------------------------------------- changes */
static void changed(void){                         /* the queue's content changed */
    save();
    ui_queue_changed();                            /* the Now Playing badge */
    /* nothing else: the song that's playing is left alone; the queue is applied when it ends */
    if(screen_current() == SCR_QUEUE) view_reload();
}
int queue_count(void){ load(); return g_qn; }
static int add_one(const char *path, const char *title, const char *artist, long dur){
    if(g_qn >= QMAX || !path || !path[0]) return 0;
    size_t pl = strlen(path); if(pl > 4 && !strcasecmp(path + pl - 4, ".m4b")) return 0;   /* never an audiobook */
    qitem_t *q = &g_q[g_qn++];
    snprintf(q->path, sizeof q->path, "%s", path); snprintf(q->title, sizeof q->title, "%s", title ? title : "");
    snprintf(q->artist, sizeof q->artist, "%s", artist ? artist : ""); q->dur = dur;
    return 1;
}
int queue_add_path(const char *path){
    load();
    mdb_song_t s; int have = mdb_song_by_path(path, &s);
    const char *sl = strrchr(path, '/');
    if(!add_one(path, have && s.title[0] ? s.title : (sl ? sl + 1 : path), have ? s.artist : "", have ? s.dur_ms : 0)) return g_qn >= QMAX ? -1 : 0;
    changed(); return 1;
}
static void folder_cb(void *ud, const char *path, const char *title, const char *artist, const char *album, long dur){
    (void)album; int *n = ud; *n += add_one(path, title, artist, dur);
}
int queue_add_folder(const char *dir){
    load(); int n = 0;
    mdb_folder_rows(dir, folder_cb, &n);           /* in file order */
    if(n > 0) changed();
    return n;
}
int queue_add_group(const char *col, const char *val){ (void)col; (void)val; return 0; }   /* (not used by the menus) */
void queue_clear(void){ load(); g_qn = 0; changed(); }
int  queue_remove_at(int i){ load(); if(i < 0 || i >= g_qn) return 0; memmove(&g_q[i], &g_q[i+1], (size_t)(g_qn - i - 1) * sizeof g_q[0]); g_qn--; changed(); return 1; }
int  queue_move(int from, int to){
    load(); if(from < 0 || from >= g_qn || to < 0 || to >= g_qn || from == to) return 0;
    qitem_t t = g_q[from];
    if(from < to) memmove(&g_q[from], &g_q[from+1], (size_t)(to - from) * sizeof t);
    else          memmove(&g_q[to+1], &g_q[to], (size_t)(from - to) * sizeof t);
    g_q[to] = t; changed(); return 1;
}
void queue_shuffle(void){
    load(); for(int i = g_qn - 1; i > 0; i--){ int j = rand() % (i + 1); qitem_t t = g_q[i]; g_q[i] = g_q[j]; g_q[j] = t; }
    if(g_qn > 1) changed();
}
void queue_touched(void){ ui_queue_changed(); }
void queue_path_moved(const char *o, const char *n){         /* file rename / move */
    load(); size_t ol = strlen(o); int hit = 0;
    for(int i = 0; i < g_qn; i++)
        if(!strncmp(g_q[i].path, o, ol) && (g_q[i].path[ol] == 0 || g_q[i].path[ol] == '/')){
            char np[256]; snprintf(np, sizeof np, "%s%s", n, g_q[i].path + ol); snprintf(g_q[i].path, sizeof g_q[i].path, "%s", np); hit = 1; }
    if(hit){ save(); if(screen_current() == SCR_QUEUE) view_reload(); }
}
void queue_path_removed(const char *p){                      /* file / folder delete */
    load(); size_t pl = strlen(p); int k = 0;
    for(int i = 0; i < g_qn; i++)
        if(!(!strncmp(g_q[i].path, p, pl) && (g_q[i].path[pl] == 0 || g_q[i].path[pl] == '/'))) g_q[k++] = g_q[i];
    if(k != g_qn){ g_qn = k; changed(); }
}
/* the user started something else: the queue stays, and re-anchors after the new song */
void queue_note_external_play(void){
    g_active = 0;                                             /* it plays after the song you just started */
    g_skip_change = 1;
}
long queue_pid(int create){ (void)create; return 0; }        /* the queue is no longer a playlist */
/* ---------------------------------------------------------------- the player's copy */
static int idx_of(char (*rows)[256], int n, const char *p){ for(int i = 0; i < n; i++) if(!strcmp(rows[i], p)) return i; return -1; }
/* capture what the player is playing (its live list), with the rest starting at `from` (inclusive) */
static void capture_context(const char *from, int inclusive){
    if(!g_ctx) g_ctx = malloc(sizeof(*g_ctx) * RMAX);
    if(!g_rest) g_rest = malloc(sizeof(*g_rest) * RMAX);
    if(!g_copy) g_copy = malloc(sizeof(*g_copy) * (QMAX + RMAX));
    g_nctx = g_ctx ? mdb_listsong0_paths(g_ctx, RMAX) : 0;
    ui_play_context(&g_ctx_type, g_ctx_name, sizeof g_ctx_name, &g_ctx_pid);
    int at = idx_of(g_ctx, g_nctx, from);
    g_nrest = 0;
    if(at >= 0 && g_rest) for(int i = inclusive ? at : at + 1; i < g_nctx; i++) memcpy(g_rest[g_nrest++], g_ctx[i], 256);
    if(g_ctx_type < 0) g_nrest = 0;                           /* a single song / book: nothing to continue into */
    g_ctx_label[0] = 0;
    if(g_nrest > 0){ mdb_song_t s; if(mdb_song_by_path(g_rest[0], &s)) snprintf(g_ctx_label, sizeof g_ctx_label, "%.110s", s.title); }
}
/* hand the player [queued songs from `first`] + [the rest], and start the first one from its beginning */
static int play_copy(int first){
    if(!g_copy) return 0;
    {   /* a song the library hasn't indexed (e.g. a folder file not scanned yet) can't be handed to the player:
         * drop it rather than let it block the queue */
        int k = 0, dropped = 0; mdb_song_t tmp;
        for(int i = 0; i < g_qn; i++){ if(i < first || mdb_song_by_path(g_q[i].path, &tmp)) g_q[k++] = g_q[i]; else dropped++; }
        if(dropped){ g_qn = k; save(); ui_queue_changed(); ui_toast(dropped == 1 ? "Skipped a song not in the library yet" : "Skipped songs not in the library yet"); }
        if(first >= g_qn) return 0;
    }
    int n = 0;
    for(int i = first; i < g_qn; i++){ int dup = 0; for(int k = 0; k < n; k++) if(!strcmp(g_copy[k], g_q[i].path)){ dup = 1; break; } if(!dup) memcpy(g_copy[n++], g_q[i].path, 256); }
    for(int i = 0; i < g_nrest; i++){ int dup = 0; for(int k = 0; k < n; k++) if(!strcmp(g_copy[k], g_rest[i])){ dup = 1; break; } if(!dup) memcpy(g_copy[n++], g_rest[i], 256); }
    /* each song once: the player's list table holds a path only once per list; a song queued twice replays
     * when its turn comes (queue_tick), one that's also later in your list plays again there after the hand-back */
    int have = 0, w = mdb_reserved_slot_set_paths(g_copy, n, &have);
    if(w <= 0 || !have) return 0;
    g_ncopy = n; g_active = 1;
    ui_play_slot(1);
    return 1;
}
static void hand_back(const char *song){                   /* the queue is done: back to your list, at `song` */
    g_active = 0;
    int at = idx_of(g_ctx, g_nctx, song);
    if(g_ctx_type >= 0 && at >= 0) ui_play_restore(g_ctx_type, g_ctx_name, g_ctx_pid, at + 1);
    else ui_play_restore(-1, "", 0, 0);                       /* nothing to return to: just the play mode back */
}
static void pop_through(int k){ memmove(&g_q[0], &g_q[k+1], (size_t)(g_qn - k - 1) * sizeof g_q[0]); g_qn -= k + 1; save(); ui_queue_changed();
                                if(screen_current() == SCR_QUEUE) view_reload(); }
/* the Next button with songs queued: straight to the first one (the player never picks its own next) */
int queue_next(void){
    load();
    if(g_qn == 0) return 0;
    track_state_t st; ipc_get_state(&st);
    if(st.have_track && mdb_is_book_path(st.path)) return 0;
    if(!g_active) capture_context(st.path, 0);                 /* you skipped this song: your list resumes after it */
    if(!play_copy(0)) return 0;
    snprintf(g_prev2, sizeof g_prev2, "%s", st.path);
    snprintf(g_last_path, sizeof g_last_path, "%s", g_q[0].path);
    pop_through(0);
    return 1;
}
void queue_jump(int i){                                       /* play queued song i now (the ones before it are skipped) */
    load(); if(i < 0 || i >= g_qn) return;
    track_state_t st; ipc_get_state(&st);
    if(!g_active) capture_context(st.path, 0);                 /* the current song is left: your list resumes after it */
    if(play_copy(i)){ snprintf(g_prev2, sizeof g_prev2, "%s", st.path); snprintf(g_last_path, sizeof g_last_path, "%s", g_q[i].path); pop_through(i); }
}
void queue_tick(const track_state_t *st, int playing){
    (void)playing;
    if(!st || !st->have_track) return;
    load();
    if(strcmp(st->path, g_last_path)){                        /* a new song began */
        char prev[256]; snprintf(prev, sizeof prev, "%s", g_last_path);
        char before[256]; snprintf(before, sizeof before, "%s", g_prev2);
        long prev_pos = g_last_pos, prev_dur = g_last_dur;
        snprintf(g_prev2, sizeof g_prev2, "%s", prev);
        snprintf(g_last_path, sizeof g_last_path, "%s", st->path);
        g_last_pos = st->position_ms; g_last_dur = st->duration_ms;
        if(!prev[0]) return;                                  /* the first song we see: nothing to do */
        if(g_skip_change){ g_skip_change = 0; return; }       /* the user just started this one: the queue waits for it to end */
        if(mdb_is_book_path(st->path)) return;                /* a book plays alone */
        int natural = prev_dur > 0 && prev_pos >= prev_dur - 4000;
        /* forward or back? Works in any play mode (list order means nothing in Shuffle): going back = returning
         * to the song heard before the last one */
        int back = 0;
        if(g_back_at && lv_tick_elaps(g_back_at) < 4000) back = 1;          /* the on-screen Previous */
        g_back_at = 0;
        if(!natural && before[0] && !strcmp(st->path, before)) back = 1;     /* returned to the song heard before */
        if(!natural && !back && cfg_get_int("work_mode", 0) != 1){           /* not Shuffle: the list order says it */
            if(g_active) back = idx_of(g_copy, g_ncopy, st->path) >= 0 && idx_of(g_copy, g_ncopy, st->path) < idx_of(g_copy, g_ncopy, prev);
            else { char (*rows)[256] = malloc(sizeof(*rows) * RMAX); int nr = rows ? mdb_listsong0_paths(rows, RMAX) : 0;
                   int a = idx_of(rows, nr, st->path), b = idx_of(rows, nr, prev); back = a >= 0 && b >= 0 && a < b; free(rows); }
        }
        int forward = !back;
        if(!g_active){
            if(g_qn == 0) return;
            if(!forward) return;                              /* Previous: left alone */
            capture_context(st->path, 1);                     /* the song the player moved to plays after the queue */
            play_copy(0);                                     /* the first queued song starts now */
            return;
        }
        /* our copy is playing */
        if(g_qn > 0 && !strcmp(st->path, g_q[0].path)){ pop_through(0); return; }   /* the next queued song, as planned */
        if(!forward) return;                                  /* Previous: left alone */
        if(g_qn > 0){ play_copy(0); return; }                 /* the copy is out of date (reordered / added / a repeat): the head now */
        hand_back(idx_of(g_rest, g_nrest, st->path) >= 0 ? st->path : (g_nrest ? g_rest[0] : st->path));   /* queue done */
        return;
    }
    g_last_pos = st->position_ms; g_last_dur = st->duration_ms;
}
/* ================================================================ the Queue screen */
#define QW 268
static lv_obj_t *g_root, *g_list, *g_sub, *g_clear_btn;
static curvelist_t g_cl;
static uint32_t g_clear_armed;
static int g_drag = -1, g_drag_row0;
static void open_now(void){ screen_show(SCR_QUEUE); }
void queue_open(void){ open_now(); }
static void back_cb(lv_event_t *e){ (void)e; screen_back(); }
static void play_cb(lv_event_t *e){ (void)e; if(g_qn > 0) queue_jump(0); }
static void shuffle_cb(lv_event_t *e){ (void)e; if(g_qn > 1){ queue_shuffle(); ui_toast("Queue shuffled"); } }
static void clear_cb(lv_event_t *e){
    (void)e; lv_obj_t *l = lv_obj_get_child(g_clear_btn, 1);
    if(!g_qn) return;
    if(!g_clear_armed || lv_tick_elaps(g_clear_armed) > 3000){ g_clear_armed = lv_tick_get(); if(l) lv_label_set_text(l, "Sure?"); return; }
    g_clear_armed = 0; if(l) lv_label_set_text(l, "Clear");
    queue_clear(); ui_toast("Queue cleared");
}
static void row_cb(lv_event_t *e){ int i = (int)(intptr_t)lv_event_get_user_data(e); queue_jump(i); }
/* reorder: press the handle and drag; rows move live (children reordered, nothing recreated) */
static int row_child0;                                          /* child index of the first queued row */
static void handle_cb(lv_event_t *e){
    lv_event_code_t c = lv_event_get_code(e);
    lv_obj_t *row = lv_obj_get_parent(lv_event_get_target(e));
    if(c == LV_EVENT_PRESSED){
        g_drag = (int)lv_obj_get_index(row) - row_child0; g_drag_row0 = g_drag;
        lv_obj_remove_flag(g_list, LV_OBJ_FLAG_SCROLLABLE);       /* the drag moves the row, not the list */
        lv_obj_set_style_bg_color(row, lv_color_hex(0x3A3A3E), 0);
    } else if(c == LV_EVENT_PRESSING && g_drag >= 0){
        lv_indev_t *in = lv_indev_active(); if(!in) return;
        lv_point_t p; lv_indev_get_point(in, &p);
        int best = g_drag;
        for(int i = 0; i < g_qn; i++){
            lv_obj_t *r = lv_obj_get_child(g_list, row_child0 + i); if(!r) break;
            lv_area_t a; lv_obj_get_coords(r, &a);
            if(p.y >= a.y1 && p.y <= a.y2){ best = i; break; }
        }
        if(best != g_drag){ lv_obj_move_to_index(row, row_child0 + best); g_drag = best; }
    } else if((c == LV_EVENT_RELEASED || c == LV_EVENT_PRESS_LOST) && g_drag >= 0){
        int from = g_drag_row0, to = g_drag; g_drag = -1;
        lv_obj_add_flag(g_list, LV_OBJ_FLAG_SCROLLABLE);
        if(from != to) queue_move(from, to); else view_reload();
    }
}
static lv_obj_t *section(const char *t){
    lv_obj_t *l = lv_label_create(g_list); lv_label_set_text(l, t);
    lv_obj_set_width(l, QW - 24);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_10, 0);
    if(!strncmp(t, "then back", 9)){ lv_obj_set_width(l, QW); lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0); lv_obj_set_style_text_font(l, ui_font_cjk(14), 0); } lv_obj_set_style_text_color(l, lv_color_hex(TH_TXT3), 0);
    return l;
}
static lv_obj_t *qrow(const char *t, const char *a, int now, int idx){
    lv_obj_t *r = lv_button_create(g_list);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, QW, 50);
    lv_obj_add_flag(r, LV_OBJ_FLAG_USER_1);
    lv_obj_set_style_radius(r, TH_R_ROW, 0);
    lv_obj_set_style_bg_color(r, lv_color_hex(now ? TH_SURF2 : TH_SURF1), 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(r, lv_color_hex(TH_SURF2), LV_STATE_PRESSED);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    int x = 16;
    if(now){ lv_obj_t *ic = lv_label_create(r); lv_label_set_text(ic, LV_SYMBOL_VOLUME_MAX);
             lv_obj_set_pos(ic, 14, 17); lv_obj_set_style_text_color(ic, ui_current_accent(), 0); x = 40; }
    else lv_obj_add_event_cb(r, row_cb, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)idx);   /* tap: jump to it */
    lv_obj_t *tl = lv_label_create(r); lv_label_set_text(tl, t); lv_label_set_long_mode(tl, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(tl, x, 5); lv_obj_set_size(tl, QW - x - (now ? 16 : 70), 22);
    lv_obj_set_style_text_font(tl, ui_font_cjk(16), 0); lv_obj_set_style_text_color(tl, now ? ui_current_accent() : lv_color_hex(TH_TXT1), 0);
    lv_obj_t *al = lv_label_create(r); lv_label_set_text(al, a); lv_label_set_long_mode(al, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(al, x, 27); lv_obj_set_size(al, QW - x - (now ? 16 : 70), 18);
    lv_obj_set_style_text_font(al, ui_font_cjk(14), 0); lv_obj_set_style_text_color(al, lv_color_hex(TH_TXT2), 0);
    if(!now){                                                 /* the drag handle */
        lv_obj_t *h = lv_obj_create(r); lv_obj_remove_style_all(h);
        lv_obj_set_size(h, 44, 50); lv_obj_align(h, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_flag(h, LV_OBJ_FLAG_CLICKABLE); lv_obj_clear_flag(h, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(h, handle_cb, LV_EVENT_ALL, NULL);
        lv_obj_t *g = lv_label_create(h); lv_label_set_text(g, LV_SYMBOL_BARS);
        lv_obj_set_style_text_color(g, lv_color_hex(0x78787E), 0); lv_obj_center(g);
    }
    return r;
}
static void view_reload(void){
    if(!g_list) return;
    load();
    lv_obj_clean(g_list);
    long total = 0; for(int i = 0; i < g_qn; i++) total += g_q[i].dur;
    char b[64];
    if(g_qn) snprintf(b, sizeof b, "%d up next \xC2\xB7 %ld min", g_qn, (total / 60000) ? total / 60000 : 1);
    else snprintf(b, sizeof b, "Nothing queued");
    lv_label_set_text(g_sub, b);
    track_state_t st; ipc_get_state(&st);
    if(st.have_track){
        section("PLAYING NOW");
        mdb_song_t s; int have = mdb_song_by_path(st.path, &s);
        qrow(st.title[0] ? st.title : (have ? s.title : "-"), st.artist[0] ? st.artist : (have ? s.artist : ""), 1, -1);
    }
    section(g_qn ? "UP NEXT" : "Hold a song in the Library or a folder and choose Add to queue");
    row_child0 = (int)lv_obj_get_child_count(g_list);
    for(int i = 0; i < g_qn; i++) qrow(g_q[i].title[0] ? g_q[i].title : "-", g_q[i].artist, 0, i);
    if(g_qn && g_active && g_ctx_label[0]){ snprintf(b, sizeof b, "then back to  %.44s", g_ctx_label); section(b); }
    lv_obj_update_layout(g_list); curvelist_update(&g_cl);
}
void queue_refresh(void){ g_clear_armed = 0; if(g_clear_btn){ lv_obj_t *l = lv_obj_get_child(g_clear_btn, 1); if(l) lv_label_set_text(l, "Clear"); } view_reload(); }
static lv_obj_t *pill(const char *icon, const char *t, int x, int w, lv_event_cb_t cb, int accent){
    lv_obj_t *b = lv_button_create(g_root);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, w, 28); lv_obj_set_pos(b, x, 64);
    lv_obj_set_style_radius(b, 14, 0);
    lv_obj_set_style_bg_color(b, th_braun() ? lv_color_hex(accent ? BR_ACC : BR_SURF) : accent ? ui_current_accent() : lv_color_hex(TH_SURF1), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(TH_SURF2), LV_STATE_PRESSED);
    lv_obj_set_ext_click_area(b, 4);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *i = lv_label_create(b); lv_label_set_text(i, icon); lv_obj_set_style_text_font(i, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(i, lv_color_hex(th_braun() && !accent ? BR_TXT : 0xFFFFFF), 0);
    if(t){ lv_obj_align(i, LV_ALIGN_LEFT_MID, 12, 0);
           lv_obj_t *l = lv_label_create(b); lv_label_set_text(l, t); lv_obj_set_style_text_font(l, TH_F_CAPTION, 0);
           lv_obj_set_style_text_color(l, lv_color_hex(th_braun() && !accent ? BR_TXT : 0xFFFFFF), 0); lv_obj_align(l, LV_ALIGN_LEFT_MID, 28, 0); }
    else lv_obj_center(i);
    return b;
}
void queue_create(lv_obj_t *root){
    g_root = root;
    lv_obj_set_style_bg_color(root, lv_color_hex(TH_BG), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *hb = lv_button_create(root);                      /* "< Queue": the back arrow and title */
    lv_obj_remove_style_all(hb);
    lv_obj_set_size(hb, 120, 30); lv_obj_align(hb, LV_ALIGN_TOP_MID, 0, 14);
    lv_obj_set_ext_click_area(hb, 6);
    lv_obj_add_event_cb(hb, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ar = lv_label_create(hb); lv_label_set_text(ar, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(ar, lv_color_hex(TH_TXT2), 0); lv_obj_align(ar, LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_t *tt = lv_label_create(hb); lv_label_set_text(tt, "Queue");
    if(th_braun()){ lv_obj_set_style_text_color(ar, lv_color_hex(BR_TXT2), 0); }
    lv_obj_set_style_text_font(tt, th_braun() ? br_font(18, 1) : TH_F_TITLE, 0); lv_obj_set_style_text_color(tt, lv_color_hex(th_braun() ? BR_TXT : TH_TXT1), 0); lv_obj_align(tt, LV_ALIGN_LEFT_MID, 34, 0);
    g_sub = lv_label_create(root); lv_obj_set_style_text_font(g_sub, th_braun() ? br_font(12, 0) : ui_font_cjk(14), 0);   /* has the middle dot */ lv_obj_set_style_text_color(g_sub, lv_color_hex(TH_TXT3), 0);
    lv_obj_align(g_sub, LV_ALIGN_TOP_MID, 0, 44);
    pill(LV_SYMBOL_PLAY, "Play", 92, 68, play_cb, 1);
    pill(LV_SYMBOL_SHUFFLE, NULL, 166, 28, shuffle_cb, 0);        /* icon only */
    g_clear_btn = pill(LV_SYMBOL_TRASH, "Clear", 200, 68, clear_cb, 0);
    g_list = lv_obj_create(root);
    lv_obj_remove_style_all(g_list);
    lv_obj_set_pos(g_list, (360 - QW) / 2, 100); lv_obj_set_size(g_list, QW, 260);
    lv_obj_set_style_pad_bottom(g_list, 40, 0);
    lv_obj_set_style_pad_row(g_list, 4, 0);
    lv_obj_set_flex_flow(g_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(g_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(g_list, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    curvelist_attach(&g_cl, g_list, root, QW);
}
