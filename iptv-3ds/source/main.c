/**
 * @file main.c
 * @brief IPTV-3DS: watch public free channels directly on the console.
 *
 * No PC relay.  The app loads an M3U playlist from the SD card (or from a
 * URL written in source.txt), filters out streams the 3DS cannot decode,
 * and plays the selected channel through the FFmpeg/MVD player module.
 *
 * Controls:
 *   Up/Down  move selection      A       open / play
 *   Left/Right  page jump        B       back from player / probe
 *   Y        reload playlist     START   exit
 *   X        toggle compatibility filter
 */
#include <3ds.h>
#include <c2d/base.h>
#include <c2d/text.h>
#include <c2d/font.h>
#include <c2d/sprite.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdarg.h>

#include "playlist.h"
#include "net.h"
#include "hls.h"
#include "player.h"

#define PLAYLIST_PATH "sdmc:/3ds/iptv-3ds/channels.m3u"
#define SOURCE_PATH   "sdmc:/3ds/iptv-3ds/source.txt"

typedef enum {
    ST_LIST = 0,
    ST_PROBE,
    ST_PLAY,
    ST_ERROR
} app_state_t;

static C3D_RenderTarget *s_top = NULL, *s_bottom = NULL;
static C2D_TextBuf  s_bufTop, s_bufBot;
static C2D_Font     s_font = NULL;

static iptv_playlist_t s_pl;
static iptv_filter_stat_t s_stat;
static int   s_sel = 0;
static int   s_scroll = 0;
static int   s_filter_only = 1;
static app_state_t s_state = ST_LIST;
static char  s_status[256] = "Ready";
static char  s_selected_url[IPTV_MAX_URL] = "";
static player_probe_t s_probe;

/* ---------- helpers --------------------------------------------------- */
static void set_status(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_status, sizeof(s_status), fmt, ap);
    va_end(ap);
}

static void draw_text(C2D_TextBuf buf, C2D_Font font,
                      float x, float y, float scale, u32 color,
                      const char *text)
{
    C2D_Text t;
    C2D_TextFontParse(&t, font, buf, text);
    C2D_TextOptimize(&t);
    C2D_DrawText(&t, C2D_WithColor, x, y, 0.0f, scale, scale, color);
}

static int visible_index(int n)
{
    /* Map a visible row to a playlist index. */
    int seen = 0;
    for (int i = 0; i < s_pl.count; i++) {
        if (s_filter_only && !s_pl.items[i].compatible) continue;
        if (seen == n) return i;
        seen++;
    }
    return -1;
}

static int visible_count(void)
{
    if (!s_filter_only) return s_pl.count;
    return s_stat.compatible;
}

/* ---------- playlist loading ------------------------------------------ */
static void reload_playlist(void)
{
    iptv_playlist_free(&s_pl);
    iptv_playlist_init(&s_pl);
    s_sel = s_scroll = 0;

    /* Optional: fetch the list from a URL written in source.txt */
    FILE *sf = fopen(SOURCE_PATH, "r");
    if (sf) {
        char url[IPTV_MAX_URL] = "";
        if (fgets(url, sizeof(url), sf)) {
            size_t l = strlen(url);
            while (l && (url[l-1] == '\n' || url[l-1] == '\r'))
                url[--l] = '\0';
            if (l > 8 && strstr(url, "://")) {
                set_status("Downloading playlist...");
                if (net_save_file(url, PLAYLIST_PATH, 30000) == 0)
                    set_status("Playlist updated");
                else
                    set_status("Download failed - using cached list");
            }
        }
        fclose(sf);
    }

    int n = iptv_playlist_load_file(&s_pl, PLAYLIST_PATH);
    if (n < 0) {
        set_status("No playlist. Put channels.m3u in /3ds/iptv-3ds/");
        s_state = ST_ERROR;
        return;
    }
    iptv_playlist_mark_compatible(&s_pl, &s_stat);
    set_status("Loaded %d channels, %d playable on 3DS",
               s_stat.total, s_stat.compatible);
    s_state = ST_LIST;
}

/* ---------- selection ------------------------------------------------- */
static void try_open_selected(void)
{
    int idx = visible_index(s_sel);
    if (idx < 0) { set_status("No channel selected"); return; }
    iptv_channel_t *ch = &s_pl.items[idx];

    set_status("Resolving %s...", ch->name);
    snprintf(s_selected_url, sizeof(s_selected_url), "%s", ch->url);

    /* HLS master playlist: collapse to the smallest variant first.
     * Only fetch the URL body when it actually looks like a playlist;
     * a direct .ts/.mp4 stream must go straight to the player. */
    int looks_m3u8 = (strcasestr(ch->url, ".m3u8") != NULL);
    if (looks_m3u8) {
        char *pbody = NULL;
        size_t blen = 0;
        if (net_get_string(ch->url, &pbody, &blen, 20000) == 0 && pbody) {
            if (hls_is_playlist(pbody, blen)) {
                char picked[IPTV_MAX_URL] = "";
                if (hls_pick_lowest_variant(ch->url, pbody, blen,
                                            picked, sizeof(picked)) == 0) {
                    snprintf(s_selected_url, sizeof(s_selected_url), "%s", picked);
                }
            }
            net_free(pbody);
        }
    }

    if (player_open(s_selected_url, &s_probe) != 0) {
        set_status("Cannot open stream");
        s_state = ST_ERROR;
        return;
    }
    if (!s_probe.playable) {
        set_status("Not playable: %s", s_probe.reason);
        s_state = ST_PROBE;   /* show details, let user go back */
        return;
    }
    set_status("Playing");
    s_state = ST_PLAY;
}

/* ---------- rendering ------------------------------------------------- */
static void render_top(void)
{
    C2D_TargetClear(s_top, C2D_Color32(18, 18, 24, 255));
    C2D_SceneBegin(s_top);

    char line[256];

    if (s_state == ST_LIST) {
        snprintf(line, sizeof(line), "IPTV-3DS   %d/%d playable  [%s]",
                 s_stat.compatible, s_stat.total,
                 s_filter_only ? "compatible only" : "all");
        draw_text(s_bufTop, s_font, 10.0f, 8.0f, 0.55f,
                  C2D_Color32(120, 200, 255, 255), line);

        int vc = visible_count();
        for (int row = 0; row < 12; row++) {
            int v = s_scroll + row;
            int idx = visible_index(v);
            if (idx < 0) break;
            iptv_channel_t *c = &s_pl.items[idx];
            u32 col = (v == s_sel) ? C2D_Color32(255, 220, 90, 255)
                        : (c->compatible ? C2D_Color32(230, 230, 230, 255)
                                         : C2D_Color32(130, 130, 130, 255));
            if (c->bandwidth > 0) {
                snprintf(line, sizeof(line), "%3d  %-34s %4d kb/s",
                         v + 1, c->name, c->bandwidth / 1000);
            } else {
                snprintf(line, sizeof(line), "%3d  %s", v + 1, c->name);
            }
            line[42] = '\0';
            draw_text(s_bufTop, s_font, 10.0f, 30.0f + row * 17.0f,
                      0.50f, col, line);
        }
        (void)vc;
    } else if (s_state == ST_PROBE) {
        draw_text(s_bufTop, s_font, 12.0f, 20.0f, 0.60f,
                  C2D_Color32(255, 120, 120, 255), "Stream not playable");
        snprintf(line, sizeof(line), "%dx%d  h264=%d  %d kb/s",
                 s_probe.width, s_probe.height, s_probe.is_h264,
                 s_probe.bitrate / 1000);
        draw_text(s_bufTop, s_font, 12.0f, 50.0f, 0.55f,
                  C2D_Color32(230, 230, 230, 255), line);
        draw_text(s_bufTop, s_font, 12.0f, 72.0f, 0.50f,
                  C2D_Color32(200, 200, 200, 255), s_probe.reason);
        draw_text(s_bufTop, s_font, 12.0f, 100.0f, 0.50f,
                  C2D_Color32(160, 160, 160, 255),
                  "B: back. Use a low-bitrate H.264 source.");
    } else if (s_state == ST_ERROR) {
        draw_text(s_bufTop, s_font, 12.0f, 40.0f, 0.60f,
                  C2D_Color32(255, 140, 140, 255), "Error");
        draw_text(s_bufTop, s_font, 12.0f, 70.0f, 0.50f,
                  C2D_Color32(220, 220, 220, 255), s_status);
    } else if (s_state == ST_PLAY) {
        /* The player draws the frame itself; just an overlay hint here. */
        draw_text(s_bufTop, s_font, 8.0f, 220.0f, 0.45f,
                  C2D_Color32(255, 255, 255, 255),
                  "B: stop");
    }
}

static void render_bottom(void)
{
    C2D_TargetClear(s_bottom, C2D_Color32(24, 24, 30, 255));
    C2D_SceneBegin(s_bottom);

    draw_text(s_bufBot, s_font, 8.0f, 8.0f, 0.48f,
              C2D_Color32(150, 220, 160, 255), s_status);

    char line[200];
    snprintf(line, sizeof(line),
             "Up/Down select  A play  X filter  Y reload  START exit");
    draw_text(s_bufBot, s_font, 8.0f, 200.0f, 0.45f,
              C2D_Color32(170, 170, 170, 255), line);
    draw_text(s_bufBot, s_font, 8.0f, 216.0f, 0.45f,
              C2D_Color32(130, 130, 130, 255),
              "CA: /config/ssl/cacert.pem  + correct clock");
}

/* ---------- input ----------------------------------------------------- */
static void handle_input(u32 kDown)
{
    int vc = visible_count();

    if (s_state == ST_LIST) {
        if (kDown & KEY_DOWN) {
            if (s_sel < vc - 1) s_sel++;
            if (s_sel >= s_scroll + 12) s_scroll = s_sel - 11;
        }
        if (kDown & KEY_UP) {
            if (s_sel > 0) s_sel--;
            if (s_sel < s_scroll) s_scroll = s_sel;
        }
        if ((kDown & KEY_RIGHT) && vc > 0) {
            s_sel += 12; if (s_sel >= vc) s_sel = vc - 1;
            s_scroll = s_sel - (s_sel % 12);
        }
        if (kDown & KEY_LEFT) {
            s_sel -= 12; if (s_sel < 0) s_sel = 0;
            s_scroll = s_sel - (s_sel % 12);
        }
        if (kDown & KEY_X) {
            s_filter_only = !s_filter_only;
            s_sel = s_scroll = 0;
            set_status(s_filter_only ? "Compatible channels only"
                                     : "Showing all channels");
        }
        if (kDown & KEY_Y) reload_playlist();
        if (kDown & KEY_A) try_open_selected();
    } else if (s_state == ST_PROBE || s_state == ST_ERROR) {
        if (kDown & KEY_B) {
            s_state = ST_LIST;
            set_status("Select a channel");
        }
        if ((kDown & KEY_Y) && s_state == ST_ERROR) reload_playlist();
    }
}

/* ---------- main ------------------------------------------------------ */
int main(void)
{
    gfxInitDefault();
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();
    s_top    = C2D_CreateScreenTarget(GFX_TOP,    GFX_LEFT);
    s_bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    s_bufTop = C2D_TextBufNew(2048);
    s_bufBot = C2D_TextBufNew(1024);
    s_font   = C2D_FontLoadSystem(CFG_REGION_USA);

    osSetSpeedupEnable(true);
    iptv_playlist_init(&s_pl);
    net_init();
    player_init();

    reload_playlist();
    int want_exit = 0;

    while (aptMainLoop() && !want_exit) {
        gspWaitForVBlank();
        hidScanInput();
        u32 kDown = hidKeysDown();

        if (kDown & KEY_START) { want_exit = 1; continue; }

        if (s_state == ST_PLAY) {
            if (kDown & KEY_B) {
                player_close();
                s_state = ST_LIST;
                set_status("Playback stopped");
            } else {
                player_run();
            }
        } else {
            handle_input(kDown);
            render_top();
            render_bottom();
            gfxFlushBuffers();
            gfxSwapBuffers();
        }
    }

    player_close();
    player_exit();
    net_exit();
    iptv_playlist_free(&s_pl);
    C2D_Fini();
    gfxExit();
    return 0;
}
