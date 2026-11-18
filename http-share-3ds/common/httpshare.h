/**
 * @file httpshare.h
 * @brief HTTP file server core for Nintendo 3DS (shared by .3dsx app and 3GX plugin)
 *
 * Pure POSIX sockets + libctru soc service. No UI dependency.
 * All file operations are sandboxed under HS_ROOT on the SD card.
 */
#ifndef HTTPSARE_H
#define HTTPSARE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <3ds.h>
#include <stdint.h>
#include <stddef.h>

/** Root directory exposed over HTTP. Nothing outside this can be touched. */
#define HS_ROOT        "sdmc:/3ds/httpshare"
#define HS_DEFAULT_PORT 8080

/** Status reported to the UI each loop. */
typedef struct {
    int     running;        /* 1 while the server thread is alive */
    uint16_t port;
    char    ip[32];         /* console LAN IP, e.g. 192.168.1.23 */
    uint32_t uploads;       /* completed uploads */
    uint32_t downloads;     /* completed downloads */
    uint32_t deletes;       /* completed deletes */
    uint64_t bytes_rx;      /* bytes received (uploads) */
    uint64_t bytes_tx;      /* bytes sent (downloads) */
    char    last_event[160];/* human readable last action */
} HSStatus;

/**
 * Optional hook called whenever something happens (connect, upload, ...).
 * Implemented by the UI layer. Must be fast and thread-safe-ish (best effort).
 */
typedef void (*HSLogCb)(const char *line);

/**
 * Initialize the HTTP server core (soc service, root dir).
 * Must be called before hs_server_loop(). Call once per process.
 * @return 0 on success, negative on error.
 */
int  hs_init(uint16_t port, HSLogCb logcb);

/**
 * Cleanup soc service and free resources.
 */
void hs_exit(void);

/**
 * Run the server loop FOREVER until hs_request_stop() is called.
 * Intended to be the entry point of a background thread.
 * The socket is non-blocking so it yields between accept polls.
 */
void hs_server_loop(void *arg);

/**
 * Ask the server loop to stop (cooperative). Safe to call from main thread.
 */
void hs_request_stop(void);

/**
 * Snapshot the current status (thread-safe copy).
 */
void hs_get_status(HSStatus *out);

/**
 * Ensure the sandbox root + Downloads exist. Safe to call repeatedly.
 */
int  hs_ensure_dirs(void);

#ifdef __cplusplus
}
#endif

#endif
