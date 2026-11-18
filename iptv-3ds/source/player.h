/**
 * @file player.h
 * @brief Network video playback: FFmpeg decode + 3DS framebuffer output.
 *
 * The FFmpeg build must be the 3DS port used by ThirdTube / Video player
 * for 3DS (with MVD hardware decode enabled).  See README for the exact
 * library source and the flags needed in the Makefile.
 */
#ifndef IPTV_PLAYER_H
#define IPTV_PLAYER_H

typedef struct {
    int     width;
    int     height;
    int     is_h264;
    int     bitrate;      /* bits/s, 0 if unknown */
    int     playable;     /* 1 if the console can decode this stream */
    char    reason[160];  /* why not playable, when playable == 0 */
} player_probe_t;

int  player_init(void);
void player_exit(void);

/* Open a stream URL and probe it WITHOUT starting playback.
 * Returns 0 on success; probe->playable tells if it can be played. */
int  player_open(const char *url, player_probe_t *probe);

/* Decode/render until the user presses B or the stream ends.
 * Returns 0 on normal exit. */
int  player_run(void);

void player_close(void);

#endif /* IPTV_PLAYER_H */
