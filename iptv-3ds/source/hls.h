/**
 * @file hls.h
 * @brief Minimal HLS handling: pick the smallest variant, list segments.
 *
 * We deliberately do NOT implement a full HLS demuxer.  We resolve the
 * lowest-bandwidth variant and hand its URL (or a single .ts segment) to
 * FFmpeg, whose mpegts/hls demuxer does the real work.
 */
#ifndef IPTV_HLS_H
#define IPTV_HLS_H

#include <stddef.h>

/* Pick the lowest-bandwidth variant URL from a master playlist.
 * Returns 0 on success and writes an absolute URL into out. */
int  hls_pick_lowest_variant(const char *master_url,
                             const char *body, size_t len,
                             char *out, size_t outsz);

/* Count media segments (#EXTINF lines followed by a URL). */
int  hls_count_segments(const char *body, size_t len);

/* Return the URL of the Nth media segment (0-based). */
int  hls_nth_segment(const char *playlist_url, const char *body, size_t len,
                     int index, char *out, size_t outsz);

/* Is this an HLS playlist body? */
int  hls_is_playlist(const char *body, size_t len);

#endif /* IPTV_HLS_H */
