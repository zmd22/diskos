/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 diskOS contributors */
#include "screens.h"
#include "theme.h"
#include "musicdb.h"
#include <sys/stat.h>
#include <string.h>
#include "musicdb.h"   /* mdb_is_book_path: this page doubles as "Book details" for an audiobook */
#include "scanner.h"   /* scan_read_narrator */
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* Song Info: a metadata detail page (title/artist/album/format/rate/duration/
 * file). Opened by tapping the Now Playing album art; back-swipe / back returns.
 * Populated from the live track_state_t via songinfo_set(). For an audiobook it
 * relabels to "Book details" and shows reading progress. */

enum { F_TITLE, F_ARTIST, F_NARRATOR, F_ALBUM, F_FORMAT, F_RATE, F_BITRATE, F_DURATION, F_FILE, F_COUNT };
static const char *KEYS[F_COUNT] = { "Title","Artist","Narrated by","Album","Format","Sample Rate","Bitrate","Duration","File" };
static lv_obj_t *g_val[F_COUNT];
static lv_obj_t *g_key[F_COUNT];
static lv_obj_t *g_cell[F_COUNT];   /* whole rows, so a not-applicable one (e.g. Narrated by for music) can be hidden */
static lv_obj_t *g_header;
static int g_pinned; static char g_pin_path[256];   /* another song's details are pinned until Song Info opens for the playing track */
static lv_obj_t *g_ring, *g_cover, *g_cover_img, *g_cover_note, *g_title, *g_artist;   /* the header: cover in its ring, title, artist */

void songinfo_create(lv_obj_t *root)
{
    lv_obj_set_style_bg_color(root, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    g_header = ui_header(root, "Song Info");   /* kept for the book relabel; the cover ring replaces it visually */
    lv_obj_add_flag(g_header, LV_OBJ_FLAG_HIDDEN);
    /* the cover inside its progress ring, then title and artist */
    g_ring = lv_arc_create(root);
    lv_obj_remove_style(g_ring, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(g_ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(g_ring, 104, 104); lv_obj_align(g_ring, LV_ALIGN_TOP_MID, 0, 34);
    lv_arc_set_rotation(g_ring, 270); lv_arc_set_bg_angles(g_ring, 0, 360); lv_arc_set_range(g_ring, 0, 1000);
    lv_obj_set_style_arc_width(g_ring, 3, LV_PART_MAIN); lv_obj_set_style_arc_color(g_ring, lv_color_hex(TH_TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_width(g_ring, 3, LV_PART_INDICATOR); lv_obj_set_style_arc_rounded(g_ring, true, LV_PART_INDICATOR);
    g_cover = lv_obj_create(root);
    lv_obj_remove_style_all(g_cover);
    lv_obj_set_size(g_cover, 88, 88); lv_obj_align(g_cover, LV_ALIGN_TOP_MID, 0, 42);
    lv_obj_set_style_radius(g_cover, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_clip_corner(g_cover, true, 0);
    lv_obj_set_style_bg_opa(g_cover, LV_OPA_COVER, 0);
    lv_obj_clear_flag(g_cover, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    g_cover_note = lv_label_create(g_cover); lv_label_set_text(g_cover_note, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_font(g_cover_note, &lv_font_montserrat_20, 0); lv_obj_set_style_text_color(g_cover_note, lv_color_hex(0xFFFFFF), 0); lv_obj_center(g_cover_note);
    g_cover_img = lv_image_create(g_cover); lv_obj_add_flag(g_cover_img, LV_OBJ_FLAG_HIDDEN);
    g_title = lv_label_create(root);
    lv_obj_set_width(g_title, 240); lv_label_set_long_mode(g_title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(g_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(g_title, ui_font_cjk(18), 0); lv_obj_set_style_text_color(g_title, lv_color_hex(TH_TXT1), 0);
    lv_obj_align(g_title, LV_ALIGN_TOP_MID, 0, 144);
    g_artist = lv_label_create(root);
    lv_obj_set_width(g_artist, 220); lv_label_set_long_mode(g_artist, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(g_artist, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(g_artist, ui_font_cjk(14), 0); lv_obj_set_style_text_color(g_artist, lv_color_hex(TH_TXT2), 0);
    lv_obj_align(g_artist, LV_ALIGN_TOP_MID, 0, 166);

    lv_obj_t *list = lv_obj_create(root);
    lv_obj_remove_style_all(list);
    lv_obj_set_pos(list, 58, 190); lv_obj_set_size(list, 244, 118);   /* inside the wide part of the circle; scrolls */
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_bottom(list, 30, 0);
    lv_obj_set_style_pad_row(list, 9, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLL_MOMENTUM);

    for(int i=0;i<F_COUNT;i++){
        lv_obj_t *cell = lv_obj_create(list);
        lv_obj_remove_style_all(cell);
        lv_obj_set_width(cell, 244);
        lv_obj_set_height(cell, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_ROW);                 /* key | value, on one line */
        lv_obj_set_style_pad_column(cell, 10, 0);
        lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
        g_cell[i] = cell;

        lv_obj_t *k = lv_label_create(cell);
        lv_label_set_text(k, KEYS[i]);
        lv_obj_set_width(k, 78);
        lv_obj_set_style_text_align(k, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_font(k, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(k, lv_color_hex(TH_TXT3), 0);
        g_key[i] = k;

        lv_obj_t *v = lv_label_create(cell);
        lv_obj_set_width(v, 156);
        lv_label_set_long_mode(v, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(v, ui_font_cjk(14), 0);   /* CJK titles/artist/album via Source Han Sans fallback (was montserrat_16 -> boxes) */
        lv_obj_set_style_text_color(v, lv_color_hex(0xFFFFFF), 0);
        lv_label_set_text(v, "-");
        g_val[i] = v;
        if(i == F_TITLE || i == F_ARTIST) lv_obj_add_flag(cell, LV_OBJ_FLAG_HIDDEN);   /* shown in the header instead */
    }
}

/* Another song's details (from a long-press menu): shown until Song Info is opened for the playing track. */
static void header_update(const track_state_t *st){         /* cover (the playing track's), ring, title, artist */
    if(!g_ring) return;
    int current = !g_pinned;
    lv_label_set_text(g_title, st->title[0] ? st->title : "Untitled");
    lv_label_set_text(g_artist, st->artist);
    int v = (current && st->duration_ms > 0) ? (int)((long long)st->position_ms * 1000 / st->duration_ms) : 0;
    lv_arc_set_value(g_ring, v > 1000 ? 1000 : v);
    lv_obj_set_style_arc_color(g_ring, ui_media_accent(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(g_cover, current ? ui_media_accent() : lv_color_hex(TH_SURF2), 0);
    const void *dsc = current ? ui_current_cover_dsc() : NULL;
    lv_image_set_src(g_cover_img, NULL);
    if(dsc){ const lv_image_dsc_t *d = dsc; lv_image_set_src(g_cover_img, dsc);
             if(d->header.w > 0) lv_image_set_scale(g_cover_img, (uint32_t)(88 * 256 / d->header.w) + 2);
             lv_obj_center(g_cover_img); lv_obj_remove_flag(g_cover_img, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(g_cover_note, LV_OBJ_FLAG_HIDDEN); }
    else { lv_obj_add_flag(g_cover_img, LV_OBJ_FLAG_HIDDEN); lv_obj_remove_flag(g_cover_note, LV_OBJ_FLAG_HIDDEN); }
}
void songinfo_unpin(void){ g_pinned = 0; }
void songinfo_show_song(const track_state_t *st){
    if(!st) return;
    g_pinned = 0; songinfo_set(st);
    g_pinned = 1; snprintf(g_pin_path, sizeof g_pin_path, "%s", st->path);
    screen_show(SCR_SONGINFO);
}
void songinfo_set(const track_state_t *st)
{
    if(g_pinned && st && strcmp(st->path, g_pin_path)) return;   /* showing another song: ignore the live updates */
    if(st) header_update(st);
    if(!g_val[0]) return;
    if(!st || !st->have_track){
        for(int i=0;i<F_COUNT;i++) if(g_val[i]) lv_label_set_text(g_val[i], "-");
        if(g_cell[F_NARRATOR]) lv_obj_add_flag(g_cell[F_NARRATOR], LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_label_set_text(g_val[F_TITLE],  st->title[0]?st->title:"Untitled");
    lv_label_set_text(g_val[F_ARTIST], st->artist[0]?st->artist:"-");
    lv_label_set_text(g_val[F_ALBUM],  st->album[0]?st->album:"-");

    /* format from extension only - this is the CONTAINER, not the codec. Don't claim
     * "ALAC" for .m4a: an m4a holds AAC (lossy) or ALAC (lossless) and the extension
     * can't distinguish them, so show the honest container label rather than guess. */
    char fmt[12]="-"; const char *ext=strrchr(st->path,'.');
    const char *sl=strrchr(st->path,'/');
    if(ext && (!sl || ext>sl) && ext[1]){   /* dot must follow the last '/' + have a suffix, else it's a dir dot */
        ext++; size_t i; for(i=0;i<sizeof(fmt)-1&&ext[i];i++) fmt[i]=toupper((unsigned char)ext[i]); fmt[i]=0; }
    lv_label_set_text(g_val[F_FORMAT], st->is_dsd?"DSD":fmt);

    char rate[24]="-";
    if(st->sample_rate>0){
        int khz=st->sample_rate/1000, frac=(st->sample_rate%1000)/100;
        if(frac) snprintf(rate,sizeof rate,"%d.%d kHz", khz, frac);
        else     snprintf(rate,sizeof rate,"%d kHz", khz);
    }
    lv_label_set_text(g_val[F_RATE], rate);
    {   /* bitrate: the scanner's value when it has one, else the file's average (size / length) */
        int br = 0, sr = 0; char b[32] = "-";
        mdb_song_rates(st->path, &br, &sr);
        if(br <= 0 && st->duration_ms > 0){ struct stat fs; if(stat(st->path, &fs) == 0) br = (int)((long long)fs.st_size * 8000 / st->duration_ms); }
        if(br > 0) snprintf(b, sizeof b, "%d kbps", (br + 500) / 1000);
        if(st->sample_rate <= 0 && sr > 0){ char r2[24]; snprintf(r2, sizeof r2, "%d.%d kHz", sr / 1000, (sr % 1000) / 100); lv_label_set_text(g_val[F_RATE], r2); }
        lv_label_set_text(g_val[F_BITRATE], b);
    }

    char dur[20]="-";
    if(st->duration_ms>0){
        long t=st->duration_ms/1000, h=t/3600, m=(t/60)%60, s=t%60;
        if(h>0) snprintf(dur,sizeof dur,"%ld:%02ld:%02ld", h, m, s);   /* long books: H:MM:SS */
        else    snprintf(dur,sizeof dur,"%ld:%02ld", m, s);
    }
    lv_label_set_text(g_val[F_DURATION], dur);

    /* file basename */
    const char *base=strrchr(st->path,'/'); base = base?base+1:st->path;
    lv_label_set_text(g_val[F_FILE], base[0]?base:"-");

    /* Audiobook: relabel to "Book details" and turn the Album slot into reading progress. */
    int book = mdb_is_book_path(st->path);
    if(g_header) lv_label_set_text(g_header, book ? "Book details" : "Song Info");
    lv_label_set_text(g_key[F_ARTIST],   book ? "Author" : "Artist");
    lv_label_set_text(g_key[F_ALBUM],    book ? "Progress" : "Album");
    lv_label_set_text(g_key[F_DURATION], book ? "Total length" : "Duration");
    if(book){
        char pg[56]="-";
        if(st->duration_ms>0){
            long pos = st->position_ms<0 ? 0 : st->position_ms;
            if(pos>st->duration_ms) pos=st->duration_ms;
            int pct = (int)((long long)pos*100/st->duration_ms);
            long rem = (st->duration_ms-pos)/1000, rh=rem/3600, rm=(rem/60)%60;
            if(rh>0) snprintf(pg,sizeof pg,"%d%%  \xC2\xB7  %ldh %02ldm left", pct, rh, rm);
            else     snprintf(pg,sizeof pg,"%d%%  \xC2\xB7  %ldm left", pct, rm>0?rm:1);
        }
        lv_label_set_text(g_val[F_ALBUM], pg);
    } else {
        lv_label_set_text(g_val[F_ALBUM], st->album[0]?st->album:"-");   /* restore album for music */
    }

    /* Narrated by: only for a book that actually names a narrator (composer tag); load once per book. */
    if(book){
        static char narr_path[512]; static char narr[128];
        if(strcmp(narr_path, st->path) != 0){
            if(!scan_read_narrator(st->path, narr, sizeof narr)) narr[0] = 0;
            snprintf(narr_path, sizeof narr_path, "%s", st->path);
        }
        if(narr[0]){ lv_label_set_text(g_val[F_NARRATOR], narr); lv_obj_remove_flag(g_cell[F_NARRATOR], LV_OBJ_FLAG_HIDDEN); }
        else       { lv_obj_add_flag(g_cell[F_NARRATOR], LV_OBJ_FLAG_HIDDEN); }
    } else {
        lv_obj_add_flag(g_cell[F_NARRATOR], LV_OBJ_FLAG_HIDDEN);
    }
}
