/**
 * @file hls.c
 * @brief Lightweight HLS playlist helpers.
 */
#include "hls.h"
#include "playlist.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

int hls_is_playlist(const char *body, size_t len)
{
    if (!body || len < 7) return 0;
    return strncmp(body, "#EXTM3U", 7) == 0;
}

int hls_pick_lowest_variant(const char *master_url,
                            const char *body, size_t len,
                            char *out, size_t outsz)
{
    if (!body) return -1;
    char *copy = (char *)malloc(len + 1);
    if (!copy) return -1;
    memcpy(copy, body, len);
    copy[len] = '\0';

    long best = -1;
    char best_rel[IPTV_MAX_URL] = "";
    int  best_w = 0, best_h = 0;

    char *line = copy;
    int want_url = 0;
    int cur_bw = 0, cur_w = 0, cur_h = 0;

    while (line && *line) {
        char *end = strchr(line, '\n');
        if (end) *end = '\0';
        char *cur = line;
        line = end ? end + 1 : NULL;
        size_t l = strlen(cur);
        while (l && (cur[l-1] == '\r' || cur[l-1] == ' ')) cur[--l] = '\0';
        while (*cur == ' ') cur++;
        if (!*cur) continue;

        if (cur[0] == '#') {
            if (strncasecmp(cur, "#EXT-X-STREAM-INF:", 18) == 0) {
                const char *p = strstr(cur, "BANDWIDTH=");
                cur_bw = 0;
                if (p) {
                    p += 10;
                    while (*p >= '0' && *p <= '9') cur_bw = cur_bw * 10 + (*p++ - '0');
                }
                p = strstr(cur, "RESOLUTION=");
                cur_w = cur_h = 0;
                if (p) {
                    p = strchr(p, '=') + 1;
                    while (*p >= '0' && *p <= '9') cur_w = cur_w * 10 + (*p++ - '0');
                    if (*p == 'x') {
                        p++;
                        while (*p >= '0' && *p <= '9') cur_h = cur_h * 10 + (*p++ - '0');
                    }
                }
                want_url = 1;
            }
            continue;
        }

        if (want_url) {
            want_url = 0;
            /* Score: prefer bandwidth that fits <= 2.5 Mbps and <= 800x240.
             * Among acceptable variants pick the smallest bandwidth.
             * If none acceptable, pick the absolute smallest bandwidth. */
            long score = cur_bw > 0 ? cur_bw : 999999999L;
            int fits = (cur_w == 0 || (cur_w <= 800 && cur_h <= 240)) &&
                       (cur_bw == 0 || cur_bw <= 2500000);
            long key = fits ? score : (1000000000L + score);
            if (best < 0 || key < best) {
                best = key;
                snprintf(best_rel, sizeof(best_rel), "%s", cur);
                best_w = cur_w;
                best_h = cur_h;
            }
        }
    }

    free(copy);
    if (best < 0 || !best_rel[0]) return -1;
    (void)best_w;
    (void)best_h;
    iptv_resolve_url(master_url, best_rel, out, outsz);
    return 0;
}

int hls_count_segments(const char *body, size_t len)
{
    if (!body) return 0;
    int n = 0;
    const char *p = body;
    const char *end = body + len;
    int saw_extinf = 0;
    while (p < end) {
        const char *e = p;
        while (e < end && *e != '\n') e++;
        char line[512];
        size_t l = (size_t)(e - p);
        if (l >= sizeof(line)) l = sizeof(line) - 1;
        memcpy(line, p, l);
        line[l] = '\0';
        size_t t = l;
        while (t && (line[t-1] == '\r' || line[t-1] == ' ')) line[--t] = '\0';
        if (strncmp(line, "#EXTINF:", 8) == 0) {
            saw_extinf = 1;
        } else if (line[0] && line[0] != '#' && saw_extinf) {
            n++;
            saw_extinf = 0;
        }
        p = (e < end) ? e + 1 : end;
    }
    return n;
}

int hls_nth_segment(const char *playlist_url, const char *body, size_t len,
                    int index, char *out, size_t outsz)
{
    if (!body || index < 0) return -1;
    const char *p = body;
    const char *end = body + len;
    int saw_extinf = 0, cur = 0;
    while (p < end) {
        const char *e = p;
        while (e < end && *e != '\n') e++;
        char line[IPTV_MAX_URL];
        size_t l = (size_t)(e - p);
        if (l >= sizeof(line)) l = sizeof(line) - 1;
        memcpy(line, p, l);
        line[l] = '\0';
        size_t t = l;
        while (t && (line[t-1] == '\r' || line[t-1] == ' ')) line[--t] = '\0';
        if (strncmp(line, "#EXTINF:", 8) == 0) {
            saw_extinf = 1;
        } else if (line[0] && line[0] != '#' && saw_extinf) {
            if (cur == index) {
                iptv_resolve_url(playlist_url, line, out, outsz);
                return 0;
            }
            cur++;
            saw_extinf = 0;
        }
        p = (e < end) ? e + 1 : end;
    }
    return -1;
}
