/**
 * @file renderer.c
 * @brief citro3d/citro2d based frame presentation for PS5 stream.
 *
 * Mirrors the rendering approach used in the IPTV-3DS player.c so the two
 * projects stay consistent: a C3D_RenderTarget for the top screen, a C3D_Tex
 * uploaded from an RGBA buffer, and a C2D_Image drawn with citro2d.
 */
#include "renderer.h"

#include <c2d/base.h>
#include <c2d/text.h>
#include <c2d/font.h>
#include <c2d/sprite.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Top screen render target */
static C3D_RenderTarget *s_target = NULL;

/* Texture + upload buffer. C3D textures need power-of-two dimensions. */
static C3D_Tex   s_tex;
static C2D_Image s_img;
static Tex3DS_SubTexture s_subtex;   /* describes the visible (non-padding) area */
static uint8_t  *s_upload = NULL;   /* POT-sized RGBA buffer */
static size_t    s_upload_cap = 0;
static int       s_tex_w = 0, s_tex_h = 0;
static int       s_tex_init = 0;

static C2D_TextBuf s_textbuf = NULL;
static C2D_Font    s_font = NULL;

/* Fit modes */
enum { FIT_FIT = 0, FIT_FILL = 1, FIT_STRETCH = 2 };

int renderer_init(void)
{
    s_target = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    if (!s_target) return -1;

    memset(&s_tex, 0, sizeof(s_tex));
    memset(&s_img, 0, sizeof(s_img));

    s_textbuf = C2D_TextBufNew(512);
    s_font = C2D_FontLoadSystem(CFG_REGION_USA);
    return 0;
}

void renderer_exit(void)
{
    if (s_tex_init) { C3D_TexDelete(&s_tex); s_tex_init = 0; }
    free(s_upload); s_upload = NULL; s_upload_cap = 0;
    if (s_textbuf) { C2D_TextBufDelete(s_textbuf); s_textbuf = NULL; }
    if (s_target) { C3D_RenderTargetDelete(s_target); s_target = NULL; }
    s_tex_w = s_tex_h = 0;
}

/* Ensure the texture + scratch buffer can hold a w x h frame. */
static int ensure_texture(int w, int h)
{
    int tw = 16, th = 16;
    while (tw < w) tw <<= 1;
    while (th < h) th <<= 1;
    if (tw > 1024 || th > 1024) return -1;

    if (tw == s_tex_w && th == s_tex_h && s_tex_init) return 0;

    size_t need = (size_t)tw * th * 4;
    uint8_t *nb = (uint8_t *)realloc(s_upload, need);
    if (!nb) return -1;
    s_upload = nb;
    s_upload_cap = need;

    if (s_tex_init) C3D_TexDelete(&s_tex);
    if (!C3D_TexInit(&s_tex, tw, th, GPU_RGBA8)) {
        s_tex_init = 0;
        return -1;
    }
    C3D_TexSetFilter(&s_tex, GPU_LINEAR, GPU_LINEAR);
    s_tex_init = 1;
    s_tex_w = tw;
    s_tex_h = th;

    /* C2D image descriptor: only the visible (non-padding) area.
     * UV coordinates are u20 fixed point (1.0 == 1<<20). */
    memset(&s_subtex, 0, sizeof(s_subtex));
    s_subtex.width  = (u16)w;
    s_subtex.height = (u16)h;
    s_subtex.left  = 0;
    s_subtex.top   = 0;
    s_subtex.right  = (u32)((float)w / tw * (float)(1u << 20));
    s_subtex.bottom = (u32)((float)h / th * (float)(1u << 20));
    s_img.tex = &s_tex;
    s_img.subtex = &s_subtex;
    return 0;
}

void renderer_show(uint8_t *rgba, int w, int h, int fit_mode)
{
    if (!rgba || w <= 0 || h <= 0) return;
    if (ensure_texture(w, h) != 0) return;

    /* Copy tightly packed rows into the POT-sized upload buffer. */
    memset(s_upload, 0, s_upload_cap);
    for (int y = 0; y < h; y++) {
        memcpy(s_upload + (size_t)y * s_tex_w * 4,
               rgba + (size_t)y * w * 4,
               (size_t)w * 4);
    }
    C3D_TexUpload(&s_tex, s_upload);
    C3D_TexFlush(&s_tex);

    C2D_TargetClear(s_target, C2D_Color32(0, 0, 0, 255));
    C2D_SceneBegin(s_target);

    /* Top screen in landscape is 400 x 240. */
    const float SW = 400.0f, SH = 240.0f;
    float scale, dw, dh, dx, dy;

    if (fit_mode == FIT_STRETCH) {
        dw = SW; dh = SH;
    } else {
        scale = SW / (float)w;
        dw = (float)w * scale;
        dh = (float)h * scale;
        if (fit_mode == FIT_FILL && dh < SH) {
            scale = SH / (float)h;
            dw = (float)w * scale;
            dh = SH;
        }
        if (dw > SW) { scale = SW / (float)w; dw = SW; dh = (float)h * scale; }
        if (dh > SH) { scale = SH / (float)h; dh = SH; dw = (float)w * scale; }
    }
    dx = (SW - dw) / 2.0f;
    dy = (SH - dh) / 2.0f;

    /* New citro2d: C2D_DrawImageAt(img, x, y, depth, tint, scaleX, scaleY).
     * The subtex already carries the true w/h, so scale relative to it. */
    float scaleX = dw / (float)w;
    float scaleY = dh / (float)h;
    C2D_DrawImageAt(s_img, dx, dy, 0.0f, NULL, scaleX, scaleY);
    gfxFlushBuffers();
    gfxSwapBuffers();
}

void renderer_placeholder(const char *line1, const char *line2)
{
    C2D_TargetClear(s_target, C2D_Color32(18, 18, 26, 255));
    C2D_SceneBegin(s_target);

    if (s_textbuf && line1 && line1[0]) {
        C2D_Text t;
        C2D_TextBufClear(s_textbuf);
        C2D_TextFontParse(&t, s_font, s_textbuf, line1);
        C2D_TextOptimize(&t);
        C2D_DrawText(&t, C2D_WithColor, 24.0f, 96.0f, 0.0f,
                     0.7f, 0.7f, C2D_Color32(230, 230, 235, 255));

        if (line2 && line2[0]) {
            C2D_Text t2;
            C2D_TextFontParse(&t2, s_font, s_textbuf, line2);
            C2D_TextOptimize(&t2);
            C2D_DrawText(&t2, C2D_WithColor, 24.0f, 124.0f, 0.0f,
                         0.55f, 0.55f, C2D_Color32(150, 150, 160, 255));
        }
    }
    gfxFlushBuffers();
    gfxSwapBuffers();
}
