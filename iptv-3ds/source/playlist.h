/**
 * @file playlist.h
 * @brief M3U / M3U8 playlist parser and low-bitrate channel filter.
 *
 * Parses both simple M3U (a list of stream URLs) and HLS master playlists
 * (#EXT-X-STREAM-INF variants).  The filter is the key piece that makes
 * public IPTV lists usable on a 3DS: it drops streams the console cannot
 * decode (H.265 / >800x240 / very high bandwidth) before they ever reach
 * the player.
 */
#ifndef IPTV_PLAYLIST_H
#define IPTV_PLAYLIST_H

#include <stddef.h>

#define IPTV_MAX_CHANNELS   1024
#define IPTV_MAX_NAME       160
#define IPTV_MAX_URL        768
#define IPTV_MAX_GROUP      80

/* Maximum a New 3DS can hardware-decode (H.264, MVD engine). */
#define IPTV_MAX_WIDTH      800
#define IPTV_MAX_HEIGHT     240
/* Beyond this the 2.4 GHz b/g link starts to stutter. */
#define IPTV_MAX_BANDWIDTH  2500000

typedef enum {
    IPTV_CODEC_UNKNOWN = 0,
    IPTV_CODEC_H264,
    IPTV_CODEC_H265,
    IPTV_CODEC_MPEG4,
    IPTV_CODEC_OTHER
} iptv_codec_t;

typedef struct {
    char         name[IPTV_MAX_NAME];
    char         url[IPTV_MAX_URL];
    char         group[IPTV_MAX_GROUP];
    int          bandwidth;      /* bits/s, 0 when unknown */
    int          width;
    int          height;
    iptv_codec_t codec;
    int          compatible;     /* 1 = playable on New 3DS */
} iptv_channel_t;

typedef struct {
    iptv_channel_t *items;       /* dynamic array */
    int             count;
    int             capacity;
} iptv_playlist_t;

typedef struct {
    int total;
    int compatible;
} iptv_filter_stat_t;

void iptv_playlist_init(iptv_playlist_t *pl);
void iptv_playlist_free(iptv_playlist_t *pl);

/* Parse an in-memory M3U/M3U8 buffer. Returns channel count or -1. */
int  iptv_playlist_parse(iptv_playlist_t *pl, const char *data, size_t len);

/* Load from sdmc:/ path. Returns channel count or -1. */
int  iptv_playlist_load_file(iptv_playlist_t *pl, const char *path);

/* Resolve a possibly relative URL against the playlist base URL. */
void iptv_resolve_url(const char *base, const char *rel,
                      char *out, size_t outsz);

/* Mark items whose codec/resolution/bandwidth fit the console. */
void iptv_playlist_mark_compatible(iptv_playlist_t *pl, iptv_filter_stat_t *st);

/* Extract an attribute value (e.g. BANDWIDTH=) from an #EXT-* line. */
int  iptv_attr_int(const char *line, const char *key, int defval);

#endif /* IPTV_PLAYLIST_H */
