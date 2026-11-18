/**
 * @file main.c
 * @brief Standalone HTTP Share 3DS app (.3dsx / .cia)
 *
 * A foreground UI wrapper around the shared httpshare core.
 * The actual server runs on a background thread so the UI stays responsive.
 * For true in-game background operation, use the 3GX plugin build instead.
 */
#include <3ds.h>
#include <3ds/archive.h>
#include <c2d/base.h>
#include <c2d/text.h>
#include <c2d/font.h>
#include <c2d/sprite.h>
#include <string.h>
#include <stdio.h>
#include <malloc.h>

#include "../../common/httpshare.h"

#define SOC_ALIGN 0x1000
#define MAX_LOG   12

/* libctru's default __appInit() calls archiveMountSdmc(). If that fails to
 * open ARCHIVE_SDMC the console throws the "card was removed" fatal screen.
 * We override __appInit/__appExit with the exact same sequence as upstream
 * but capture the SD mount result, so a bad/missing mount shows a clear
 * message in the UI instead of a black error screen. */
static Result s_sdInitResult = 0;

void __appInit(void)
{
    srvInit();
    aptInit();
    hidInit();
    fsInit();
    s_sdInitResult = archiveMountSdmc();
}

void __appExit(void)
{
    archiveUnmountAll();
    fsExit();
    hidExit();
    aptExit();
    srvExit();
}

static C3D_RenderTarget *s_top = NULL, *s_bottom = NULL;
static C2D_TextBuf  s_bufTop, s_bufBot;
static C2D_Font     s_font = NULL;

static Handle   s_logMutex;
static char     s_logs[MAX_LOG][160];
static int      s_logCount = 0, s_logHead = 0;

static Thread   s_serverThread = NULL;
static u8       s_threadStack[0x10000] __attribute__((aligned(8)));
static int      s_serverStarted = 0;

/* ---------- logging callback from server thread ---------- */
static void on_log(const char *line)
{
    svcWaitSynchronization(s_logMutex, U64_MAX);
    snprintf(s_logs[s_logHead], sizeof(s_logs[0]), "%s", line);
    s_logHead = (s_logHead + 1) % MAX_LOG;
    if (s_logCount < MAX_LOG) s_logCount++;
    svcReleaseMutex(s_logMutex);
}

static void server_thread_entry(void *arg)
{
    (void)arg;
    hs_server_loop(NULL);
}

static void draw_text(C2D_TextBuf buf, C2D_Font font,
                      float x, float y, float scale, u32 color, const char *text)
{
    C2D_Text t;
    C2D_TextFontParse(&t, font, buf, text);
    C2D_TextOptimize(&t);
    C2D_DrawText(&t, C2D_WithColor, x, y, 0.0f, scale, scale, color);
}

int main(void)
{
    gfxInitDefault();
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();
    s_top    = C2D_CreateScreenTarget(GFX_TOP,    GFX_LEFT);
    s_bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    s_bufTop = C2D_TextBufNew(512);
    s_bufBot = C2D_TextBufNew(512);
    s_font = C2D_FontLoadSystem(CFG_REGION_USA);

    svcCreateMutex(&s_logMutex, false);
    osSetSpeedupEnable(true);  /* N3DS clock speedup while app is open */

    on_log("HTTP Share 3DS ready");
    if (R_FAILED(s_sdInitResult)) {
        on_log("WARNING: SD mount failed - check card");
    } else {
        on_log("SD card mounted OK");
    }

    while (aptMainLoop()) {
        gspWaitForVBlank();
        hidScanInput();
        u32 kDown = hidKeysDown();

        if (kDown & KEY_START) break;

        if (kDown & KEY_A) {
            if (!s_serverStarted) {
                if (hs_init(HS_DEFAULT_PORT, on_log) == 0) {
                    s_serverThread = threadCreate(server_thread_entry, NULL,
                                                  sizeof(s_threadStack), 0x3F,
                                                  -2, false);
                    s_serverStarted = (s_serverThread != NULL);
                    on_log("Server started");
                } else {
                    on_log("ERROR: socInit failed (Wi-Fi off?)");
                }
            } else {
                hs_request_stop();
                if (s_serverThread) {
                    threadJoin(s_serverThread, U64_MAX);
                    threadFree(s_serverThread);
                    s_serverThread = NULL;
                }
                hs_exit();
                s_serverStarted = 0;
                on_log("Server stopped");
            }
        }

        HSStatus st;
        hs_get_status(&st);

        /* ---- render top screen ---- */
        C2D_TargetClear(s_top, C2D_Color32(0x18, 0x1a, 0x22, 0xFF));
        C2D_SceneBegin(s_top);
        C2D_TextBufClear(s_bufTop);
        draw_text(s_bufTop, s_font, 16, 12, 0.7f, C2D_Color32(0xFF,0xFF,0xFF,0xFF),
                  "HTTP Share 3DS - Standalone");
        char line[200];
        snprintf(line, sizeof(line), "Status : %s", s_serverStarted ? "RUNNING" : "STOPPED");
        draw_text(s_bufTop, s_font, 16, 56, 0.6f,
                  s_serverStarted ? C2D_Color32(0x6f,0xe0,0x6f,0xFF) : C2D_Color32(0xaa,0xaa,0xaa,0xFF), line);
        snprintf(line, sizeof(line), "IP     : %s", st.ip);
        draw_text(s_bufTop, s_font, 16, 84, 0.6f, C2D_Color32(0xdd,0xdd,0xdd,0xFF), line);
        snprintf(line, sizeof(line), "URL    : http://%s:%u/", st.ip, st.port);
        draw_text(s_bufTop, s_font, 16, 110, 0.55f, C2D_Color32(0x7f,0xc7,0xff,0xFF), line);
        snprintf(line, sizeof(line), "Folder : " HS_ROOT);
        draw_text(s_bufTop, s_font, 16, 140, 0.5f, C2D_Color32(0x99,0x99,0x99,0xFF), line);
        snprintf(line, sizeof(line), "UP %u  DN %u  DEL %u", st.uploads, st.downloads, st.deletes);
        draw_text(s_bufTop, s_font, 16, 168, 0.55f, C2D_Color32(0xff,0xd1,0x66,0xFF), line);
        snprintf(line, sizeof(line), "RX %llu KB  TX %llu KB",
                 (unsigned long long)(st.bytes_rx/1024), (unsigned long long)(st.bytes_tx/1024));
        draw_text(s_bufTop, s_font, 16, 194, 0.55f, C2D_Color32(0xff,0xd1,0x66,0xFF), line);

        /* ---- render bottom screen: log ---- */
        C2D_TargetClear(s_bottom, C2D_Color32(0x10,0x11,0x16,0xFF));
        C2D_SceneBegin(s_bottom);
        C2D_TextBufClear(s_bufBot);
        draw_text(s_bufBot, s_font, 12, 10, 0.5f, C2D_Color32(0xff,0xff,0xff,0xFF),
                  "Activity log");
        svcWaitSynchronization(s_logMutex, U64_MAX);
        int shown = 0;
        for (int i = 0; i < s_logCount && shown < 11; i++) {
            int idx = (s_logHead - s_logCount + i + MAX_LOG) % MAX_LOG;
            char l[168];
            snprintf(l, sizeof(l), "> %s", s_logs[idx]);
            draw_text(s_bufBot, s_font, 12, 40 + shown*18, 0.45f,
                      C2D_Color32(0xb0,0xe0,0xb0,0xFF), l);
            shown++;
        }
        svcReleaseMutex(s_logMutex);
        draw_text(s_bufBot, s_font, 12, 222, 0.5f, C2D_Color32(0x88,0x88,0x88,0xFF),
                  "A: start/stop   START: exit");

        C2D_Flush();
        gfxFlushBuffers();
        gfxSwapBuffers();
    }

    if (s_serverStarted) {
        hs_request_stop();
        if (s_serverThread) { threadJoin(s_serverThread, U64_MAX); threadFree(s_serverThread); }
        hs_exit();
    }
    svcCloseHandle(s_logMutex);
    if (s_font) C2D_FontFree(s_font);
    C2D_TextBufDelete(s_bufTop);
    C2D_TextBufDelete(s_bufBot);
    C2D_Fini();
    gfxExit();
    return 0;
}
