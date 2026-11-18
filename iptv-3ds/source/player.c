/**
 * @file player.c
 * @brief FFmpeg-based network stream playback for the 3DS.
 *
 * TWO-LAYER DESIGN
 * ----------------
 *  1. Probe layer: open input, read stream info, decide whether the format
 *     is something a New 3DS can decode (H.264, <= 800x240, sane bitrate).
 *     This runs when a channel is selected, before any full-screen playback.
 *  2. Playback layer: decode frames and present them via citro2d.
 *
 * HARDWARE DECODE
 * ---------------
 *  The MVD engine is exposed through a patched FFmpeg (the "3ds" branch
 *  used by ThirdTube and Video player for 3DS).  With that library linked,
 *  avcodec_open2() on h264 automatically selects the MVD hwaccel when the
 *  context is configured with AV_HWDEVICE_TYPE_MVD.  If the stock libavcodec
 *  is linked instead, the same code still works but falls back to software
 *  decoding (much slower, 800x240 @ ~20 fps on New 3DS).
 *
 *  The exact hwaccel wiring is intentionally kept in one function,
 *  player_configure_hwdec(), so it can be adapted to whichever FFmpeg
 *  build you link without touching the rest of the file.
 */
#include "player.h"

#include <3ds.h>
#include <c2d/base.h>
#include <c2d/text.h>
#include <c2d/font.h>
#include <c2d/sprite.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>

#include <stdio.h>
#include <string.h>

/* ---- state ----------------------------------------------------------- */
static AVFormatContext *s_fmt   = NULL;
static AVCodecContext  *s_vdec  = NULL;
static int              s_vstream = -1;
static int              s_open  = 0;

static C3D_Tex       s_tex;
static C2D_Image     s_img;
static u8           *s_rgba   = NULL;
static struct SwsContext *s_sws = NULL;
static int           s_tex_w = 0, s_tex_h = 0;
static C3D_RenderTarget *s_target = NULL;

/* Configure hardware decode when the linked FFmpeg supports it.
 *
 * The 3DS MVD hardware decoder is wired up differently across FFmpeg
 * branches (some use a hwaccel struct, some a custom 'h264_mvd' decoder).
 * Rather than hard-code an API that may not exist in your build, this
 * hook is left as a no-op: the patched FFmpeg used by ThirdTube /
 * Video player for 3DS selects MVD automatically in avcodec_open2().
 * If your build needs explicit setup, add it here.
 */
static int player_configure_hwdec(AVCodecContext *ctx)
{
    (void)ctx;
    return 0;
}

int player_init(void)
{
    avformat_network_init();
    memset(&s_tex, 0, sizeof(s_tex));
    memset(&s_img, 0, sizeof(s_img));
    s_target = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    return s_target ? 0 : -1;
}

void player_exit(void)
{
    player_close();
    if (s_target) { C3D_RenderTargetDelete(s_target); s_target = NULL; }
    avformat_network_deinit();
}

int player_open(const char *url, player_probe_t *probe)
{
    if (probe) memset(probe, 0, sizeof(*probe));
    if (!url || !*url) return -1;
    player_close();

    s_fmt = avformat_alloc_context();
    if (!s_fmt) return -1;

    /* Small probesize so we don't buffer a whole TS chunk just to inspect. */
    s_fmt->probesize = 2 * 1024 * 1024;
    s_fmt->max_analyze_duration = 3 * AV_TIME_BASE;

    if (avformat_open_input(&s_fmt, url, NULL, NULL) < 0) {
        avformat_free_context(s_fmt);
        s_fmt = NULL;
        return -1;
    }
    if (avformat_find_stream_info(s_fmt, NULL) < 0) {
        avformat_close_input(&s_fmt);
        return -1;
    }

    s_vstream = av_find_best_stream(s_fmt, AVMEDIA_TYPE_VIDEO,
                                    -1, -1, NULL, 0);
    if (s_vstream < 0) {
        avformat_close_input(&s_fmt);
        return -1;
    }

    AVStream *st = s_fmt->streams[s_vstream];
    const AVCodec *dec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!dec) {
        if (probe) {
            snprintf(probe->reason, sizeof(probe->reason),
                     "unsupported codec id %d", (int)st->codecpar->codec_id);
        }
        avformat_close_input(&s_fmt);
        return -1;
    }

    s_vdec = avcodec_alloc_context3(dec);
    if (!s_vdec) {
        avformat_close_input(&s_fmt);
        return -1;
    }
    if (avcodec_parameters_to_context(s_vdec, st->codecpar) < 0) {
        avcodec_free_context(&s_vdec);
        avformat_close_input(&s_fmt);
        return -1;
    }
    player_configure_hwdec(s_vdec);
    if (avcodec_open2(s_vdec, dec, NULL) < 0) {
        avcodec_free_context(&s_vdec);
        s_vdec = NULL;
        avformat_close_input(&s_fmt);
        return -1;
    }

    s_open = 1;

    if (probe) {
        probe->width   = s_vdec->width;
        probe->height  = s_vdec->height;
        probe->is_h264 = (st->codecpar->codec_id == AV_CODEC_ID_H264);
        probe->bitrate = (int)(s_fmt->bit_rate / 1000) * 1000;

        int ok = 1;
        const char *why = "";
        if (!probe->is_h264) { ok = 0; why = "not H.264 (no hw decode)"; }
        if (probe->width > 800 || probe->height > 240) {
            ok = 0; why = "resolution above 800x240";
        }
        if (probe->bitrate > 2500000) {
            ok = 0; why = "bitrate too high for WiFi b/g";
        }
        probe->playable = ok;
        if (!ok) snprintf(probe->reason, sizeof(probe->reason), "%s", why);
    }
    return 0;
}

static int setup_texture(int w, int h)
{
    int tw = 16, th = 16;
    while (tw < w) tw <<= 1;
    while (th < h) th <<= 1;
    if (tw > 1024 || th > 512) return -1;

    free(s_rgba);
    s_rgba = (u8 *)malloc((size_t)tw * th * 4);
    if (!s_rgba) return -1;

    if (s_tex_w || s_tex_h) C3D_TexDelete(&s_tex);
    if (!C3D_TexInit(&s_tex, tw, th, GPU_RGBA8)) {
        free(s_rgba); s_rgba = NULL;
        return -1;
    }
    C3D_TexSetFilter(&s_tex, GPU_LINEAR, GPU_LINEAR);
    s_tex_w = tw;
    s_tex_h = th;

    /* citro2d image descriptor covering only the visible area */
    s_img.tex = &s_tex;
    s_img.subrect = (C2D_TexCoords){
        .ul = { 0.0f, 0.0f },
        .ur = { (float)w / tw, 0.0f },
        .lr = { (float)w / tw, (float)h / th },
        .ll = { 0.0f, (float)h / th }
    };
    return 0;
}

int player_run(void)
{
    if (!s_open || !s_vdec || !s_fmt) return -1;
    int w = s_vdec->width, h = s_vdec->height;
    if (w <= 0 || h <= 0 || w > 800 || h > 240) return -1;
    if (setup_texture(w, h) != 0) return -1;

    AVFrame *frame = av_frame_alloc();
    AVPacket *pkt = av_packet_alloc();
    if (!frame || !pkt) {
        av_frame_free(&frame);
        av_packet_free(&pkt);
        return -1;
    }

    s_sws = sws_getContext(w, h, AV_PIX_FMT_YUV420P,
                           w, h, AV_PIX_FMT_RGBA,
                           SWS_FAST_BILINEAR, NULL, NULL, NULL);

    int linesize[1] = { w * 4 };
    int rc = 0;

    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_B) { rc = 0; break; }

        int got = av_read_frame(s_fmt, pkt);
        if (got < 0) { rc = 1; break; }   /* EOS / network error */

        if (pkt->stream_index == s_vstream) {
            int sr = avcodec_send_packet(s_vdec, pkt);
            if (sr >= 0) {
                int rr = avcodec_receive_frame(s_vdec, frame);
                if (rr == 0) {
                    if (s_sws) {
                        sws_scale(s_sws,
                                  (const uint8_t * const *)frame->data,
                                  frame->linesize, 0, h,
                                  (uint8_t * const *)&s_rgba, linesize);
                        C3D_TexUpload(&s_tex, s_rgba);
                        C2D_TargetClear(s_target, C2D_Color32(0, 0, 0, 255));
                        C2D_SceneBegin(s_target);
                        float scale = 400.0f / w;
                        C2D_DrawImage(s_img, 0.0f,
                                      (240.0f - h * scale) / 2.0f,
                                      0.0f, (float)w * scale,
                                      (float)h * scale, C2D_DefaultTextureColor);
                        gfxFlushBuffers();
                        gfxSwapBuffers();
                        gspWaitForVBlank();
                    }
                }
                av_frame_unref(frame);
            }
        }
        av_packet_unref(pkt);
    }

    if (s_sws) { sws_freeContext(s_sws); s_sws = NULL; }
    av_frame_free(&frame);
    av_packet_free(&pkt);
    return rc;
}

void player_close(void)
{
    if (s_tex_w || s_tex_h) {
        C3D_TexDelete(&s_tex);
        s_tex_w = s_tex_h = 0;
    }
    free(s_rgba);
    s_rgba = NULL;
    memset(&s_img, 0, sizeof(s_img));

    if (s_vdec) { avcodec_free_context(&s_vdec); s_vdec = NULL; }
    if (s_fmt)  { avformat_close_input(&s_fmt);  s_fmt = NULL;  }
    s_vstream = -1;
    s_open = 0;
}
