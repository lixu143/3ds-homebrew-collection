#include "ui.h"
#include "qrcodegen.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static C3D_RenderTarget *s_top = NULL;
static C3D_RenderTarget *s_bottom = NULL;
static C2D_TextBuf s_buf = NULL;

bool ui_init(void)
{
    gfxInitDefault();
    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) return false;
    if (!C2D_Init(C2D_DEFAULT_MAX_OBJECTS)) return false;
    C2D_Prepare();

    s_top    = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    s_bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    s_buf = C2D_TextBufNew(4096);
    return s_buf != NULL;
}

void ui_exit(void)
{
    if (s_buf) C2D_TextBufDelete(s_buf);
    C2D_Fini();
    C3D_Fini();
    gfxExit();
}

void ui_begin_frame(void)
{
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TargetClear(s_top, CLR_BG);
    C2D_TargetClear(s_bottom, CLR_BG);
}

void ui_end_frame(void)
{
    C3D_FrameEnd(0);
}

void ui_rect(float x, float y, float w, float h, u32 color)
{
    C2D_SceneBegin(s_top);
    C2D_DrawRectSolid(x, y, 0.5f, w, h, color);
}

void ui_text(float x, float y, float scale, u32 color, const char *fmt, ...)
{
    char str[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(str, sizeof(str), fmt, ap);
    va_end(ap);

    C2D_Text text;
    C2D_TextBufClear(s_buf);
    C2D_TextParse(&text, s_buf, str);
    C2D_TextOptimize(&text);
    C2D_DrawText(&text, C2D_WithColor, x, y, 0.5f, scale, scale, color);
}

void ui_header(const char *title)
{
    C3D_RenderTarget *target = s_top;
    C2D_SceneBegin(target);
    C2D_DrawRectSolid(0, 0, 0.5f, 400, 40, CLR_PANEL);
    C2D_DrawRectSolid(0, 38, 0.5f, 400, 2, CLR_ACCENT);

    C2D_Text text;
    C2D_TextBufClear(s_buf);
    C2D_TextParse(&text, s_buf, title);
    C2D_TextOptimize(&text);
    float tw;
    C2D_TextGetDimensions(&text, 0.6f, 0.6f, &tw, NULL);
    C2D_DrawText(&text, C2D_WithColor, (400 - tw) / 2, 6, 0.5f, 0.6f, 0.6f, CLR_TEXT);
}

void ui_menu_item(float y, const char *label, bool selected)
{
    C2D_SceneBegin(s_bottom);
    if (selected) {
        C2D_DrawRectSolid(12, y - 4, 0.5f, 296, 40, CLR_PANEL);
        C2D_DrawRectSolid(12, y - 4, 0.5f, 4, 40, CLR_ACCENT);
    }
    C2D_Text text;
    C2D_TextBufClear(s_buf);
    C2D_TextParse(&text, s_buf, label);
    C2D_TextOptimize(&text);
    C2D_DrawText(&text, C2D_WithColor, 24, y, 0.5f, 0.55f, 0.55f,
                 selected ? CLR_TEXT : CLR_MUTED);
}

bool ui_qrcode(const char *content)
{
    // 只编码到版本 10，用对应更小的缓冲区，节省栈空间
    uint8_t qrcode[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];
    uint8_t temp[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];

    bool ok = qrcodegen_encodeText(content, temp, qrcode,
                                   qrcodegen_Ecc_MEDIUM,
                                   qrcodegen_VERSION_MIN, 10,
                                   qrcodegen_Mask_AUTO, true);
    if (!ok) return false;

    int size = qrcodegen_getSize(qrcode);

    C2D_SceneBegin(s_top);
    // 白色底 + 留白（quiet zone）
    C2D_DrawRectSolid(40, 20, 0.5f, 320, 200, C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF));

    float module = 176.0f / (float)size;
    if (module > 8.0f) module = 8.0f;
    float grid = module * size;
    float ox = 40 + (320 - grid) / 2;
    float oy = 20 + (200 - grid) / 2;

    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            if (qrcodegen_getModule(qrcode, x, y)) {
                C2D_DrawRectSolid(ox + x * module, oy + y * module, 0.5f,
                                  module, module, C2D_Color32(0, 0, 0, 0xFF));
            }
        }
    }
    return true;
}

u32 ui_wait_key(void)
{
    while (aptMainLoop()) {
        gspWaitForVBlank();
        hidScanInput();
        u32 k = hidKeysDown();
        if (k) return k;
    }
    return 0;
}
