/**
 * @file audio.c
 * @brief PBS /audio receiver -> NDSP 48 kHz stereo PCM16 playback.
 *
 * Wire format (from PBS server.c):
 *   GET /audio -> "HTTP/1.1 200 OK\r\n...\r\n\r\n"
 *   body: repeated [u32 length BE][u32 pts BE][length bytes float32 LE]
 *   float32 LE, stereo interleaved, 48000 Hz.
 *
 * We keep a ring of ndspWaveBuf's. The worker reads network blocks, converts
 * float samples to s16 into the "write" buffer, and submits a wave buffer to
 * NDSP whenever it is full (or on a short idle flush). NDSP plays them back
 * with the hardware DSP; the UI thread never touches audio data.
 */
#include "audio.h"

#include <3ds.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>

/* ---- tunables -------------------------------------------------------- */
#define AUDIO_RATE       48000
#define AUDIO_SAMPLES    1600          /* frames per NDSP buffer (~33 ms)   */
#define AUDIO_NBUFS      6             /* ring depth (~200 ms buffering)    */
#define AUDIO_MAX_CHUNK  (64 * 1024)   /* sanity cap on one block payload   */

/* ---- state ----------------------------------------------------------- */
static volatile int s_running = 0;
static Thread  s_thread;
static int     s_sock = -1;
static char    s_host[128] = "192.168.1.50";
static uint16_t s_port = 9999;

static AudioStatus s_status;
static float s_volume = 0.8f;

/* DSP needs physically-contiguous linear memory for wave buffers. */
static s16  *s_bufs = NULL;               /* AUDIO_NBUFS * AUDIO_SAMPLES * 2 */
static ndspWaveBuf s_wb[AUDIO_NBUFS];
static volatile int s_write_idx = 0;      /* buffer currently being filled */
static int     s_fill = 0;                /* frames written into s_write_idx*/

static int s_ndsp_ready = 0;

/* ---- helpers --------------------------------------------------------- */
static void set_status(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_status.status, sizeof(s_status.status), fmt, ap);
    va_end(ap);
}

static int tcp_connect(const char *host, uint16_t port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct hostent *he = gethostbyname(host);
    struct in_addr addr;
    if (he && he->h_addr_list[0]) {
        memcpy(&addr, he->h_addr_list[0], 4);
    } else {
        addr.s_addr = inet_addr(host);
        if (addr.s_addr == INADDR_NONE) { closesocket(fd); return -1; }
    }

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr = addr;
    if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        closesocket(fd);
        return -1;
    }
    return fd;
}

static int recv_exact(int fd, void *buf, size_t n)
{
    size_t got = 0;
    while (got < n) {
        int r = recv(fd, (char *)buf + got, n - got, 0);
        if (r <= 0) return -1;
        got += (size_t)r;
    }
    return 0;
}

static int recv_line(int fd, char *buf, size_t cap)
{
    size_t n = 0;
    char c = 0;
    while (n < cap - 1) {
        int r = recv(fd, &c, 1, 0);
        if (r <= 0) return -1;
        if (c == '\n') break;
        if (c != '\r') buf[n++] = c;
    }
    buf[n] = '\0';
    return (int)n;
}

/* Skip HTTP response headers up to the blank line. */
static int skip_http_headers(int fd)
{
    char line[512];
    if (recv_line(fd, line, sizeof(line)) < 0) return -1;
    if (!strstr(line, "200")) {
        set_status("HTTP error: %s", line);
        return -1;
    }
    for (int i = 0; i < 40; i++) {
        if (recv_line(fd, line, sizeof(line)) < 0) return -1;
        if (line[0] == '\0') return 0;
    }
    return -1;
}

/* float32 little-endian -> int16 with clamp. */
static s16 f32_to_s16(const uint8_t *p)
{
    uint32_t bits = (uint32_t)p[0] |
                    ((uint32_t)p[1] << 8) |
                    ((uint32_t)p[2] << 16) |
                    ((uint32_t)p[3] << 24);
    float f;
    memcpy(&f, &bits, 4);
    if (f > 1.0f) f = 1.0f;
    if (f < -1.0f) f = -1.0f;
    return (s16)(f * 32767.0f);
}

/* Submit the current buffer to NDSP if it has any samples. Does not advance. */
static void flush_current(void)
{
    if (s_fill <= 0) return;
    ndspWaveBuf *wb = &s_wb[s_write_idx];
    wb->nsamples = (u32)s_fill;
    DSP_FlushDataCache(s_bufs + (size_t)s_write_idx * AUDIO_SAMPLES * 2,
                       (size_t)s_fill * 4);
    /* ndspChnWaveBufAdd auto-starts playback when the queue was empty. */
    ndspChnWaveBufAdd(0, wb);
    s_fill = 0;
}

/* Find a free (DONE or FREE) wave buffer and make it the current write target. */
static int acquire_buffer(void)
{
    for (int spin = 0; spin < 2000 && s_running; spin++) {
        for (int i = 1; i <= AUDIO_NBUFS; i++) {
            int cand = (s_write_idx + i) % AUDIO_NBUFS;
            /* Compare constants directly; avoids depending on the enum
             * typedef name, which varies across libctru versions. */
            if (s_wb[cand].status == NDSP_WBUF_DONE ||
                s_wb[cand].status == NDSP_WBUF_FREE) {
                s_write_idx = cand;
                s_fill = 0;
                return 0;
            }
        }
        svcSleepThread(1000000);  /* 1 ms */
    }
    return -1;
}

/* ---- worker ---------------------------------------------------------- */
static void audio_thread(void *arg)
{
    (void)arg;
    uint8_t *chunk = (uint8_t *)malloc(AUDIO_MAX_CHUNK);
    if (!chunk) {
        set_status("Out of memory for audio buffer");
        s_status.connected = 0;
        return;
    }

    s_sock = tcp_connect(s_host, s_port);
    if (s_sock < 0) {
        set_status("Audio connect failed: %s:%u", s_host, s_port);
        free(chunk);
        s_status.connected = 0;
        return;
    }

    char req[256];
    snprintf(req, sizeof(req),
             "GET /audio HTTP/1.1\r\n"
             "Host: %s:%u\r\n"
             "User-Agent: 3DS-PS5-Stream/1.0\r\n"
             "Accept: application/octet-stream\r\n"
             "Connection: close\r\n\r\n",
             s_host, s_port);
    if (send(s_sock, req, strlen(req), 0) < 0 || skip_http_headers(s_sock) < 0) {
        set_status("Audio request/headers failed");
        goto done;
    }

    struct timeval tv;
    tv.tv_sec = 3;
    tv.tv_usec = 0;
    setsockopt(s_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    set_status("Audio streaming 48kHz stereo");

    uint8_t hdr[8];
    while (s_running) {
        if (recv_exact(s_sock, hdr, 8) < 0) {
            if (s_running) set_status("Audio stream ended");
            break;
        }
        uint32_t len = ((uint32_t)hdr[0] << 24) | ((uint32_t)hdr[1] << 16) |
                       ((uint32_t)hdr[2] << 8)  | (uint32_t)hdr[3];
        /* hdr[4..7] is PTS, unused. */
        if (len == 0 || len > AUDIO_MAX_CHUNK) continue;
        if (recv_exact(s_sock, chunk, len) < 0) {
            set_status("Audio block truncated");
            break;
        }
        s_status.bytes_rx += len;
        s_status.chunks++;

        /* Each stereo float32 frame = 8 bytes (L float + R float). */
        size_t frames = len / 8;
        size_t off = 0;
        while (off < frames) {
            if (s_fill >= AUDIO_SAMPLES) {
                flush_current();
                if (acquire_buffer() < 0) goto done;
            }
            s16 *dst = s_bufs + (size_t)s_write_idx * AUDIO_SAMPLES * 2
                              + (size_t)s_fill * 2;
            dst[0] = f32_to_s16(chunk + off * 8 + 0);
            dst[1] = f32_to_s16(chunk + off * 8 + 4);
            s_fill++;
            off++;
        }
    }

    flush_current();
done:
    if (s_sock >= 0) { closesocket(s_sock); s_sock = -1; }
    free(chunk);
    s_status.connected = 0;
}

/* ---- public API ------------------------------------------------------ */
int audio_start(const char *host, uint16_t port)
{
    if (s_running) return 0;

    if (!s_ndsp_ready) {
        ndspInit();
        size_t bytes = (size_t)AUDIO_NBUFS * AUDIO_SAMPLES * 2 * sizeof(s16);
        s_bufs = (s16 *)linearAlloc(bytes);
        if (!s_bufs) {
            set_status("linearAlloc failed for audio buffers");
            ndspExit();
            return -1;
        }
        memset(s_bufs, 0, bytes);

        ndspChnReset(0);
        ndspSetOutputMode(NDSP_OUTPUT_STEREO);
        ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
        ndspChnSetRate(0, (float)AUDIO_RATE);
        ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
        float mix[12] = {0};
        mix[0] = 1.0f; mix[1] = 1.0f;   /* front-left, front-right */
        ndspChnSetMix(0, mix);
        ndspSetMasterVol(s_volume);

        memset(s_wb, 0, sizeof(s_wb));
        for (int i = 0; i < AUDIO_NBUFS; i++) {
            s_wb[i].data_vaddr = s_bufs + (size_t)i * AUDIO_SAMPLES * 2;
            s_wb[i].nsamples = AUDIO_SAMPLES;
            s_wb[i].status = NDSP_WBUF_DONE;  /* available to fill */
            s_wb[i].looping = false;
        }
        s_ndsp_ready = 1;
    }

    snprintf(s_host, sizeof(s_host), "%s", host ? host : "192.168.1.50");
    s_port = port;
    memset(&s_status, 0, sizeof(s_status));
    s_status.volume = s_volume;
    s_status.connected = 1;
    s_write_idx = 0;
    s_fill = 0;
    set_status("Connecting audio...");

    s_running = 1;
    s_thread = threadCreate(audio_thread, NULL, 0x10000, 0x20, 1, true);
    if (!s_thread) {
        s_running = 0;
        s_status.connected = 0;
        set_status("Audio threadCreate failed");
        return -1;
    }
    return 0;
}

void audio_stop(void)
{
    s_running = 0;
    if (s_sock >= 0) shutdown(s_sock, SHUT_RDWR);
    if (s_thread) {
        threadJoin(s_thread, ULLONG_MAX);
        threadFree(s_thread);
        s_thread = NULL;
    }
    if (s_ndsp_ready) {
        ndspChnWaveBufClear(0);
        ndspChnReset(0);
        ndspExit();
        linearFree(s_bufs);
        s_bufs = NULL;
        s_ndsp_ready = 0;
    }
    s_status.connected = 0;
}

void audio_get_status(AudioStatus *out)
{
    if (out) *out = s_status;
}

void audio_set_volume(float v)
{
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    s_volume = v;
    s_status.volume = v;
    if (s_ndsp_ready) ndspSetMasterVol(v);
}
