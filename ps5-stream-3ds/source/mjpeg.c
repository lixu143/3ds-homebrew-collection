/**
 * @file mjpeg.c
 * @brief MJPEG-over-HTTP receiver implementation (socket + libjpeg-turbo).
 */
#include "mjpeg.h"

#include <3ds.h>

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <setjmp.h>
#include <stdarg.h>
#include <ctype.h>
#include <limits.h>

#include <jpeglib.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>

#ifndef SO_RCVTIMEO
#define SO_RCVTIMEO 0x1006
#endif

/* newlib on 3DS has no reliable strcasestr; use our own. */
static char *ci_strstr(const char *hay, const char *needle)
{
    if (!hay || !needle) return NULL;
    size_t nl = strlen(needle);
    for (const char *p = hay; *p; p++) {
        size_t i = 0;
        while (i < nl && p[i] &&
               tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i]))
            i++;
        if (i == nl) return (char *)p;
    }
    return NULL;
}

/* ---- soc service buffer (must be aligned, lives for the process) ------ */
#define SOC_BUFFERSIZE 0x50000
static __attribute__((aligned(0x1000))) u32 s_socbuf[SOC_BUFFERSIZE/4];
static int s_soc_ready = 0;

/* ---- double-buffered decoded frames ---------------------------------- */
typedef struct {
    uint8_t *data;   /* RGBA8888, size MJPEG_MAX_W*MJPEG_MAX_H*4 */
    int      w, h;
    uint64_t seq;
} frame_t;

static frame_t  s_frames[2];
static volatile int s_front = 0;      /* index of the newest complete frame */
static volatile int s_back  = 1;      /* index the worker is writing into   */
static volatile uint64_t s_seq = 0;

/* ---- worker / status ------------------------------------------------- */
static volatile int s_running = 0;
static Thread  s_thread;
static int     s_sock = -1;
static char    s_host[128] = "192.168.1.50";
static uint16_t s_port = 9999;
static char    s_path[128] = "/";

static MJPEGStatus s_status;

/* ---- libjpeg error handling (prevent longjmp out of the thread) ------- */
struct jpeg_err_mgr {
    struct jpeg_error_mgr pub;
    jmp_buf jb;
};
typedef struct jpeg_err_mgr *jpeg_err_ptr;

static void jpeg_error_exit(j_common_ptr cinfo)
{
    jpeg_err_ptr e = (jpeg_err_ptr)cinfo->err;
    longjmp(e->jb, 1);
}

static void set_status(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_status.status, sizeof(s_status.status), fmt, ap);
    va_end(ap);
}

/* ---- socket helpers -------------------------------------------------- */
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

/* Receive exactly n bytes (or fail). */
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

/* Read one CRLF/LF terminated header line (without the terminator). */
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

/* Skip bytes until a "--<boundary>" marker is consumed. */
static int skip_to_boundary(int fd, const char *boundary)
{
    char marker[160];
    snprintf(marker, sizeof(marker), "--%s", boundary);
    size_t mlen = strlen(marker);
    size_t matched = 0;
    char c;

    while (s_running) {
        if (recv_exact(fd, &c, 1) < 0) return -1;
        if (c == marker[matched]) {
            matched++;
            if (matched == mlen) return 0;
        } else {
            matched = (c == marker[0]) ? 1 : 0;
        }
    }
    return -1;
}

/* ---- decode one JPEG buffer into the back frame (RGBA8888) ----------- */
static int decode_jpeg_to_frame(const uint8_t *jpg, size_t len, frame_t *out)
{
    struct jpeg_decompress_struct cinfo;
    struct jpeg_err_mgr jerr;

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = jpeg_error_exit;

    if (setjmp(jerr.jb)) {
        jpeg_destroy_decompress(&cinfo);
        return -1;
    }

    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, (const unsigned char *)jpg, (unsigned long)len);
    jpeg_read_header(&cinfo, TRUE);

    /* Pick a 1/1, 1/2, 1/4 or 1/8 scale so the image fits the 3DS screen. */
    int denom = 1;
    while (denom < 8 &&
           (cinfo.image_width / denom > MJPEG_MAX_W ||
            cinfo.image_height / denom > MJPEG_MAX_H)) {
        denom <<= 1;
    }
    cinfo.scale_num = 1;
    cinfo.scale_denom = denom;
    cinfo.out_color_space = JCS_RGB;

    jpeg_start_decompress(&cinfo);

    int w = (int)cinfo.output_width;
    int h = (int)cinfo.output_height;
    if (w <= 0 || h <= 0 || w > MJPEG_MAX_W || h > MJPEG_MAX_H) {
        jpeg_destroy_decompress(&cinfo);
        return -1;
    }

    JSAMPLE *row = (JSAMPLE *)malloc((size_t)w * 3);
    if (!row) { jpeg_destroy_decompress(&cinfo); return -1; }

    while (cinfo.output_scanline < cinfo.output_height) {
        int y = (int)cinfo.output_scanline;
        jpeg_read_scanlines(&cinfo, &row, 1);
        uint8_t *dst = out->data + (size_t)y * w * 4;
        for (int x = 0; x < w; x++) {
            dst[x * 4 + 0] = row[x * 3 + 0];
            dst[x * 4 + 1] = row[x * 3 + 1];
            dst[x * 4 + 2] = row[x * 3 + 2];
            dst[x * 4 + 3] = 255;
        }
    }

    free(row);
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);

    out->w = w;
    out->h = h;
    return 0;
}

/* ---- read HTTP headers, extract multipart boundary ------------------ */
static int read_http_headers(int fd, char *boundary, size_t bcap)
{
    char line[512];
    boundary[0] = '\0';

    /* status line */
    if (recv_line(fd, line, sizeof(line)) < 0) return -1;
    if (!strstr(line, "200")) {
        set_status("HTTP error: %s", line);
        return -1;
    }

    int in_multipart = 0;
    for (int i = 0; i < 40; i++) {
        if (recv_line(fd, line, sizeof(line)) < 0) return -1;
        if (line[0] == '\0') break;              /* end of headers */

        if (strncasecmp(line, "Content-Type:", 13) == 0 &&
            ci_strstr(line, "multipart/x-mixed-replace")) {
            in_multipart = 1;
            const char *b = ci_strstr(line, "boundary=");
            if (b) {
                b += 9;
                const char *end = strchr(b, ';');
                size_t blen = end ? (size_t)(end - b) : strlen(b);
                while (blen && (*b == '"' || *b == ' ')) { b++; blen--; }
                while (blen && (b[blen-1] == '"' || b[blen-1] == '\r' ||
                                b[blen-1] == ' ')) blen--;
                if (blen && blen < bcap) {
                    memcpy(boundary, b, blen);
                    boundary[blen] = '\0';
                }
            }
        }
    }

    if (!in_multipart) {
        set_status("Not a multipart MJPEG stream");
        return -1;
    }
    if (!boundary[0]) {
        /* Most PS5 payloads use a fixed boundary; fall back to common ones. */
        strcpy(boundary, "--myboundary");
    }
    return 0;
}

/* ---- worker thread --------------------------------------------------- */
static void stream_thread(void *arg)
{
    (void)arg;
    uint8_t *jpg = (uint8_t *)malloc(MJPEG_JPEG_BUF);
    if (!jpg) {
        set_status("Out of memory for JPEG buffer");
        s_status.connected = 0;
        return;
    }

    s_sock = tcp_connect(s_host, s_port);
    if (s_sock < 0) {
        set_status("Connect failed: %s:%u", s_host, s_port);
        free(jpg);
        s_status.connected = 0;
        return;
    }
    set_status("Connected to %s:%u", s_host, s_port);

    char req[256];
    snprintf(req, sizeof(req),
             "GET %s HTTP/1.1\r\n"
             "Host: %s:%u\r\n"
             "User-Agent: 3DS-PS5-Stream/1.0\r\n"
             "Accept: multipart/x-mixed-replace\r\n"
             "Connection: close\r\n\r\n",
             s_path[0] ? s_path : "/", s_host, s_port);
    if (send(s_sock, req, strlen(req), 0) < 0) {
        set_status("Send request failed");
        goto done;
    }

    char boundary[160];
    if (read_http_headers(s_sock, boundary, sizeof(boundary)) < 0) goto done;
    set_status("Streaming (boundary=%s)", boundary);

    /* Set a receive timeout so a stalled PS5 doesn't freeze the thread. */
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(s_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    uint64_t last_fps_tick = osGetTime();
    uint64_t frames_since_tick = 0;

    while (s_running) {
        if (skip_to_boundary(s_sock, boundary) < 0) {
            if (s_running) set_status("Stream ended / connection lost");
            break;
        }

        /* part headers until blank line; find Content-Length */
        char line[512];
        int clen = -1;
        for (int i = 0; i < 20; i++) {
            if (recv_line(s_sock, line, sizeof(line)) < 0) goto done;
            if (line[0] == '\0') break;
            if (strncasecmp(line, "Content-Length:", 15) == 0)
                clen = atoi(line + 15);
        }

        if (clen <= 0 || clen > MJPEG_JPEG_BUF) {
            /* No usable Content-Length: skip this part and try next. */
            continue;
        }
        if (recv_exact(s_sock, jpg, (size_t)clen) < 0) {
            set_status("Frame truncated");
            break;
        }
        s_status.bytes_rx += (uint32_t)clen;

        frame_t *back = &s_frames[s_back];
        if (decode_jpeg_to_frame(jpg, (size_t)clen, back) == 0) {
            back->seq = ++s_seq;
            /* publish: swap buffers atomically (single writer) */
            int old = s_front;
            s_front = s_back;
            s_back = old;

            s_status.width = back->w;
            s_status.height = back->h;
            s_status.frames++;
            frames_since_tick++;

            uint64_t now = osGetTime();
            uint64_t dt = now - last_fps_tick;
            if (dt >= 1000000) {                 /* osGetTime is in µs */
                s_status.fps = (float)frames_since_tick * 1000000.0f / (float)dt;
                frames_since_tick = 0;
                last_fps_tick = now;
            }
        }
    }

done:
    if (s_sock >= 0) { closesocket(s_sock); s_sock = -1; }
    free(jpg);
    s_status.connected = 0;
}

/* ---- public API ------------------------------------------------------ */
int mjpeg_start(const char *host, uint16_t port, const char *path)
{
    if (s_running) return 0;

    if (!s_soc_ready) {
        if (!socInit(s_socbuf, SOC_BUFFERSIZE)) {
            set_status("socInit failed");
            return -1;
        }
        s_soc_ready = 1;
    }

    /* Allocate both frame buffers once. */
    for (int i = 0; i < 2; i++) {
        if (!s_frames[i].data) {
            s_frames[i].data = (uint8_t *)malloc(
                (size_t)MJPEG_MAX_W * MJPEG_MAX_H * 4);
            if (!s_frames[i].data) {
                set_status("Out of memory for frame buffers");
                return -1;
            }
            s_frames[i].w = s_frames[i].h = 0;
        }
    }

    snprintf(s_host, sizeof(s_host), "%s", host ? host : "192.168.1.50");
    s_port = port;
    snprintf(s_path, sizeof(s_path), "%s", path ? path : "/");

    memset(&s_status, 0, sizeof(s_status));
    s_status.connected = 1;
    s_status.port = port;
    snprintf(s_status.ip, sizeof(s_status.ip), "%s", s_host);
    set_status("Connecting...");

    s_running = 1;
    s_thread = threadCreate(stream_thread, NULL, 0x20000, 0x20, 1, true);
    if (!s_thread) {
        s_running = 0;
        s_status.connected = 0;
        set_status("threadCreate failed");
        return -1;
    }
    return 0;
}

void mjpeg_stop(void)
{
    s_running = 0;
    if (s_sock >= 0) {
        shutdown(s_sock, SHUT_RDWR);   /* unblock a pending recv() */
    }
    if (s_thread) {
        threadJoin(s_thread, ULLONG_MAX);
        threadFree(s_thread);
        s_thread = NULL;
    }
    s_status.connected = 0;
}

void mjpeg_get_status(MJPEGStatus *out)
{
    if (out) *out = s_status;
}

int mjpeg_front(uint8_t **rgba, int *w, int *h)
{
    frame_t *f = &s_frames[s_front];
    if (!f->data || f->w <= 0) return 0;
    if (rgba) *rgba = f->data;
    if (w) *w = f->w;
    if (h) *h = f->h;
    return 1;
}
