/**
 * @file mjpeg.h
 * @brief MJPEG-over-HTTP receiver for Nintendo 3DS / New 3DS.
 *
 * Connects to a PS5-side streaming payload (PBS Payload / PS5 MCP /
 * PSCapture in MJPEG mode) that serves a multipart/x-mixed-replace JPEG
 * stream over plain HTTP, decodes frames with libjpeg-turbo (scaled down
 * to fit the 3DS screen), and exposes the latest frame for rendering.
 *
 * A worker thread does all network I/O + decoding. The UI thread calls
 * mjpeg_front() to get the most recent completed frame. Frames are
 * double-buffered so the UI never blocks on the network.
 */
#ifndef MJPEG_H
#define MJPEG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <3ds.h>
#include <stdint.h>

/** Max decoded frame dimensions (the 3DS top screen is ~400x240 landscape). */
#define MJPEG_MAX_W 480
#define MJPEG_MAX_H 320

/** Max size of a single incoming JPEG (720p JPEG rarely exceeds 400 KB). */
#define MJPEG_JPEG_BUF (768 * 1024)

typedef struct {
    int      width;        /* decoded frame width  (already scaled) */
    int      height;       /* decoded frame height (already scaled) */
    uint64_t seq;          /* increments on every decoded frame      */
    uint64_t frames;       /* total frames decoded this session     */
    uint32_t bytes_rx;     /* bytes pulled from the socket          */
    float    fps;          /* measured frames per second            */
    uint16_t port;         /* remote stream port                    */
    char     ip[64];       /* remote host string                    */
    int      connected;    /* 1 while the stream thread is online   */
    char     status[160];  /* short human-readable status           */
} MJPEGStatus;

/**
 * Start the receiver thread. host may be an IPv4 string or hostname.
 * path is usually "/" (PBS payload) or "/stream" depending on the payload.
 * Returns 0 if the thread was created, negative on failure.
 */
int  mjpeg_start(const char *host, uint16_t port, const char *path);

/** Stop the receiver thread and close the socket. Safe to call repeatedly. */
void mjpeg_stop(void);

/** Copy a status snapshot (thread-safe). */
void mjpeg_get_status(MJPEGStatus *out);

/**
 * Get the newest completed frame (RGBA8888, top-down, row stride = width*4).
 * Returns 1 and fills *w/*h if a frame exists; returns 0 if none yet.
 * The returned pointer stays valid until the next call from this thread,
 * so copy it into a C3D texture immediately (see renderer).
 */
int  mjpeg_front(uint8_t **rgba, int *w, int *h);

#ifdef __cplusplus
}
#endif

#endif
