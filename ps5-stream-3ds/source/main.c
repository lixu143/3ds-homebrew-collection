/**
 * @file main.c
 * @brief PS5-Stream-3DS: stream PS5 gameplay to the New 3DS and control it.
 *
 * Screen: PS5 (PBS Payload, GET /frames MJPEG) -> 3DS top screen.
 * Input : 3DS buttons + touch virtual sticks -> PS5 (POST /input/state).
 *
 * Controls:
 *   In menu:  Up/Down select field, Left/Right edit value,
 *             A connect / save, START exit.
 *   In stream: A = PS5 Cross, B = Circle, X = Square, Y = Triangle,
 *             L/R = L1/R1, ZL/ZR = L2/R2, START = Options, SELECT = Create,
 *             D-Pad = D-Pad, touch bottom screen = virtual left stick,
 *             hold X(button) + drag for right stick (toggle with R3),
 *             B(with no input focus) or START long-press to disconnect.
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

#include "config.h"
#include "mjpeg.h"
#include "audio.h"
#include "renderer.h"
#include "inputctl.h"

typedef enum { ST_MENU = 0, ST_STREAM } app_state_t;

static C3D_RenderTarget *s_bot = NULL;
static C2D_TextBuf  s_bufBot;
static C2D_Font     s_font = NULL;

static app_state_t s_state = ST_MENU;
static StreamConfig s_cfg;
static int   s_sel = 0;                 /* menu field index */
static char  s_status[256] = "Ready";
static int   s_show_input = 0;          /* toggle input debug overlay */

/* virtual sticks state */
static int   s_lstick_x = 128, s_lstick_y = 128;
static int   s_rstick_x = 128, s_rstick_y = 128;
static int   s_stick_mode = 0;          /* 0 = left stick, 1 = right stick */
static uint64_t s_last_input_tick = 0;

static void set_status(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_status, sizeof(s_status), fmt, ap);
    va_end(ap);
}

static void draw_line(float x, float y, float scale, u32 color, const char *text)
{
    C2D_Text t;
    C2D_TextFontParse(&t, s_font, s_bufBot, text);
    C2D_TextOptimize(&t);
    C2D_DrawText(&t, C2D_WithColor, x, y, 0.0f, scale, scale, color);
}

/* ---- menu editing ---------------------------------------------------- */
/* field 0..3 = host octets, 4 = port thousands, ... keep it simple:
 * field 0: host (whole string via swkbd), 1: port, 2: path, 3: fit */
#define FIELD_COUNT 5   /* 0 host, 1 port, 2 path, 3 fit, 4 volume */

static void edit_field_swkbd(int field)
{
    SwkbdState sw;
    char buf[160] = "";
    swkbdInit(&sw, SWKBD_TYPE_NORMAL, 2, -1);

    if (field == 0) {
        swkbdSetInitialText(&sw, s_cfg.host);
        swkbdSetHintText(&sw, "PS5 IP address");
        swkbdSetValidation(&sw, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
        swkbdInputText(&sw, buf, sizeof(buf));
        if (buf[0]) snprintf(s_cfg.host, sizeof(s_cfg.host), "%s", buf);
    } else if (field == 2) {
        swkbdSetInitialText(&sw, s_cfg.path);
        swkbdSetHintText(&sw, "HTTP path (usually /frames)");
        swkbdInputText(&sw, buf, sizeof(buf));
        if (buf[0]) snprintf(s_cfg.path, sizeof(s_cfg.path), "%s", buf);
    }
}

static void menu_adjust(int delta)
{
    if (s_sel == 1) {                    /* port */
        int p = (int)s_cfg.port + delta * 1;
        if (p < 1) p = 1;
        if (p > 65535) p = 65535;
        s_cfg.port = (uint16_t)p;
    } else if (s_sel == 3) {             /* fit mode */
        s_cfg.fit += delta;
        if (s_cfg.fit < 0) s_cfg.fit = 2;
        if (s_cfg.fit > 2) s_cfg.fit = 0;
    } else if (s_sel == 4) {             /* volume (0.0 .. 1.0) */
        AudioStatus as;
        audio_get_status(&as);
        float v = as.volume + delta * 0.1f;
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        audio_set_volume(v);
    }
}

static void render_menu(MJPEGStatus *st)
{
    C2D_TextBufClear(s_bufBot);
    C2D_TargetClear(s_bot, C2D_Color32(22, 22, 30, 255));
    C2D_SceneBegin(s_bot);

    draw_line(12, 10, 0.62f, C2D_Color32(120, 200, 255, 255),
              "PS5-Stream-3DS  (PS5 -> 3DS + control)");
    draw_line(12, 30, 0.50f, C2D_Color32(160, 160, 170, 255), s_status);

    char line[200];
    snprintf(line, sizeof(line), "PS5 host   : %s", s_cfg.host);
    draw_line(12, 56, 0.55f, s_sel == 0 ? C2D_Color32(255,220,120,255) : C2D_Color32(220,220,225,255), line);
    snprintf(line, sizeof(line), "Port       : %u", (unsigned)s_cfg.port);
    draw_line(12, 76, 0.55f, s_sel == 1 ? C2D_Color32(255,220,120,255) : C2D_Color32(220,220,225,255), line);
    snprintf(line, sizeof(line), "Path       : %s", s_cfg.path);
    draw_line(12, 96, 0.55f, s_sel == 2 ? C2D_Color32(255,220,120,255) : C2D_Color32(220,220,225,255), line);
    const char *fits[] = { "fit", "fill", "stretch" };
    snprintf(line, sizeof(line), "Fit mode   : %s", fits[s_cfg.fit]);
    draw_line(12, 116, 0.55f, s_sel == 3 ? C2D_Color32(255,220,120,255) : C2D_Color32(220,220,225,255), line);
    {
        AudioStatus as;
        audio_get_status(&as);
        snprintf(line, sizeof(line), "Volume     : %3d%%", (int)(as.volume * 100.0f));
        draw_line(12, 136, 0.55f, s_sel == 4 ? C2D_Color32(255,220,120,255) : C2D_Color32(220,220,225,255), line);
    }

    draw_line(12, 156, 0.48f, C2D_Color32(150, 220, 160, 255),
              "A connect / disconnect   START exit");
    draw_line(12, 174, 0.48f, C2D_Color32(150, 220, 160, 255),
              "Up/Down select  L/R change  X keyboard edit");

    if (st && st->connected) {
        snprintf(line, sizeof(line), "Stream: %dx%d  %.1ffps",
                 st->width, st->height, st->fps);
    } else {
        snprintf(line, sizeof(line), "Input : %s",
                 inputctl_connected() ? "controller ON" : "controller OFF");
    }
    draw_line(12, 194, 0.50f, C2D_Color32(200, 200, 210, 255), line);

    snprintf(line, sizeof(line), "3DS IP: %s", st ? st->ip : "-");
    draw_line(12, 212, 0.46f, C2D_Color32(140, 140, 150, 255), line);
}

/* ---- stream: gather 3DS input -> InputState -------------------------- */
static void gather_input(InputState *out)
{
    hidScanInput();
    u32 k = hidKeysHeld();

    uint32_t b = 0;
    if (k & KEY_A)      b |= INPUT_BTN_CROSS;
    if (k & KEY_B)      b |= INPUT_BTN_CIRCLE;
    if (k & KEY_X)      b |= INPUT_BTN_SQUARE;
    if (k & KEY_Y)      b |= INPUT_BTN_TRIANGLE;
    if (k & KEY_L)      b |= INPUT_BTN_L1;
    if (k & KEY_R)      b |= INPUT_BTN_R1;
    if (k & KEY_ZL)     b |= INPUT_BTN_L2;
    if (k & KEY_ZR)     b |= INPUT_BTN_R2;
    if (k & KEY_START)  b |= INPUT_BTN_OPTIONS;
    if (k & KEY_SELECT) b |= INPUT_BTN_CREATE;
    if (k & KEY_UP)     b |= INPUT_BTN_UP;
    if (k & KEY_DOWN)   b |= INPUT_BTN_DOWN;
    if (k & KEY_LEFT)   b |= INPUT_BTN_LEFT;
    if (k & KEY_RIGHT)  b |= INPUT_BTN_RIGHT;
    /* R3 (stick click) can be added here if your libctru exposes KEY_CS */

    /* Touch bottom screen drives a virtual stick relative to touch origin. */
    touchPosition tp;
    if (hidTouchCount() > 0) {
        hidTouchRead(&tp, 0);
        int cx = 160, cy = 190;             /* touch zone center (bottom 320x240) */
        int dx = (int)tp.px - cx;
        int dy = (int)tp.py - cy;
        int range = 60;
        if (dx > range) dx = range; if (dx < -range) dx = -range;
        if (dy > range) dy = range; if (dy < -range) dy = -range;
        int v = 128 + (dx * 127 / range);
        int vy = 128 - (dy * 127 / range);  /* up = smaller y -> larger value */
        if (s_stick_mode == 0) {
            s_lstick_x = v; s_lstick_y = vy;
        } else {
            s_rstick_x = v; s_rstick_y = vy;
        }
    } else {
        s_lstick_x = s_lstick_y = 128;
        s_rstick_x = s_rstick_y = 128;
    }

    out->buttons = b;
    out->lx = (uint8_t)s_lstick_x; out->ly = (uint8_t)s_lstick_y;
    out->rx = (uint8_t)s_rstick_x; out->ry = (uint8_t)s_rstick_y;
    out->l2 = (k & KEY_ZL) ? 255 : 0;
    out->r2 = (k & KEY_ZR) ? 255 : 0;
}

static void render_stream_hud(void)
{
    C2D_TextBufClear(s_bufBot);
    C2D_TargetClear(s_bot, C2D_Color32(16, 16, 22, 255));
    C2D_SceneBegin(s_bot);
    draw_line(12, 8, 0.55f, C2D_Color32(120, 200, 255, 255),
              "Streaming - B to disconnect");
    char line[160];
    snprintf(line, sizeof(line), "Stick: %s (touch bottom to move)",
             s_stick_mode == 0 ? "LEFT" : "RIGHT");
    draw_line(12, 28, 0.5f, C2D_Color32(200, 200, 210, 255), line);
    snprintf(line, sizeof(line), "LX%3d LY%3d  RX%3d RY%3d",
             s_lstick_x, s_lstick_y, s_rstick_x, s_rstick_y);
    draw_line(12, 48, 0.5f, C2D_Color32(160, 220, 160, 255), line);
    {
        AudioStatus as;
        audio_get_status(&as);
        snprintf(line, sizeof(line), "Audio: %s vol %d%%",
                 as.connected ? "ON " : "OFF", (int)(as.volume * 100.0f));
        draw_line(12, 66, 0.5f,
                  as.connected ? C2D_Color32(160, 200, 255, 255)
                               : C2D_Color32(200, 140, 140, 255), line);
    }

    /* Draw a simple virtual stick dot on the bottom screen. */
    int cx = 160, cy = 150;
    int px = cx + ((s_stick_mode == 0 ? s_lstick_x : s_rstick_x) - 128) / 2;
    int py = cy - ((s_stick_mode == 0 ? s_lstick_y : s_rstick_y) - 128) / 2;
    C2D_DrawRectSolid((float)cx - 46, (float)cy - 46, 0.0f, 92, 92,
                      C2D_Color32(40, 40, 50, 255));
    C2D_DrawRectSolid((float)px - 8, (float)py - 8, 0.0f, 16, 16,
                      C2D_Color32(255, 200, 80, 255));

    draw_line(12, 214, 0.45f, C2D_Color32(140, 140, 150, 255), s_status);
}

/* ---- main ------------------------------------------------------------ */
int main(void)
{
    gfxInitDefault();
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();
    s_bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    s_bufBot = C2D_TextBufNew(2048);
    s_font = C2D_FontLoadSystem(CFG_REGION_USA);

    osSetSpeedupEnable(true);
    config_load(&s_cfg);
    renderer_init();

    int want_exit = 0;
    MJPEGStatus st;
    memset(&st, 0, sizeof(st));

    while (aptMainLoop() && !want_exit) {
        gspWaitForVBlank();
        hidScanInput();
        u32 kDown = hidKeysDown();

        if (s_state == ST_MENU) {
            if (kDown & KEY_START) { want_exit = 1; continue; }

            if (kDown & KEY_UP)    { s_sel--; if (s_sel < 0) s_sel = FIELD_COUNT - 1; }
            if (kDown & KEY_DOWN)  { s_sel++; if (s_sel >= FIELD_COUNT) s_sel = 0; }
            if (kDown & KEY_LEFT)  menu_adjust(-1);
            if (kDown & KEY_RIGHT) menu_adjust(1);
            if (kDown & KEY_X)     edit_field_swkbd(s_sel);
            if (kDown & KEY_A) {
                config_save(&s_cfg);
                set_status("Connecting to %s:%u ...", s_cfg.host, s_cfg.port);
                if (mjpeg_start(s_cfg.host, s_cfg.port, "/frames") == 0) {
                    /* mjpeg_start initialises soc; audio/input then reuse it. */
                    int a = audio_start(s_cfg.host, s_cfg.port);
                    int c = inputctl_connect(s_cfg.host, s_cfg.port);
                    if (a == 0 && c == 0)
                        set_status("Video + audio + controller connected");
                    else if (a == 0)
                        set_status("Video + audio on; controller unavailable");
                    else if (c == 0)
                        set_status("Video + controller on; audio unavailable");
                    else
                        set_status("Video only (audio/controller unavailable)");
                    s_state = ST_STREAM;
                } else {
                    set_status("%s", s_status);
                }
            }

            mjpeg_get_status(&st);
            render_menu(&st);
            renderer_placeholder("PS5 Stream Receiver", "Press A to connect, see bottom screen");
            gfxFlushBuffers();
            gfxSwapBuffers();
        } else { /* ST_STREAM */
            /* local key handling for stick toggle + disconnect */
            if (kDown & KEY_B) {
                inputctl_disconnect();
                audio_stop();
                mjpeg_stop();
                s_state = ST_MENU;
                set_status("Disconnected");
                continue;
            }
            if (kDown & KEY_X) s_stick_mode = !s_stick_mode;

            /* Feed controller input at ~20 Hz (>= 100 ms requirement). */
            uint64_t now = osGetTime();
            if (now - s_last_input_tick >= 50000) {   /* 50 ms */
                InputState ist;
                gather_input(&ist);
                inputctl_send(&ist);
                s_last_input_tick = now;
            }

            /* Draw bottom-screen HUD into its target first (no swap). */
            mjpeg_get_status(&st);
            render_stream_hud();

            /* Present top screen last; renderer_show/placeholder swap buffers. */
            uint8_t *rgba = NULL;
            int w = 0, h = 0;
            if (mjpeg_front(&rgba, &w, &h)) {
                renderer_show(rgba, w, h, s_cfg.fit);
            } else {
                renderer_placeholder("Waiting for video...",
                                     st.status[0] ? st.status : "Connecting");
            }
        }
    }

    inputctl_disconnect();
    audio_stop();
    mjpeg_stop();
    renderer_exit();
    s_bot && C3D_RenderTargetDelete(s_bot);
    C2D_TextBufDelete(s_bufBot);
    C2D_Fini();
    gfxExit();
    return 0;
}
