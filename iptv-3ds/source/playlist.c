/**
 * @file playlist.c
 * @brief M3U / M3U8 parser implementation.
 */
#include "playlist.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

static char *xstrdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static int pl_grow(iptv_playlist_t *pl)
{
    if (pl->count < pl->capacity) return 0;
    int cap = pl->capacity ? pl->capacity * 2 : 64;
    if (cap > IPTV_MAX_CHANNELS) cap = IPTV_MAX_CHANNELS;
    if (cap <= pl->capacity) return -1;
    iptv_channel_t *p = (iptv_channel_t *)realloc(
        pl->items, (size_t)cap * sizeof(iptv_channel_t));
    if (!p) return -1;
    pl->items = p;
    pl->capacity = cap;
    return 0;
}

static void pl_add(iptv_playlist_t *pl, const iptv_channel_t *ch)
{
    if (pl_grow(pl) != 0) return;
    pl->items[pl->count++] = *ch;
}

void iptv_playlist_init(iptv_playlist_t *pl)
{
    memset(pl, 0, sizeof(*pl));
}

void iptv_playlist_free(iptv_playlist_t *pl)
{
    free(pl->items);
    pl->items = NULL;
    pl->count = pl->capacity = 0;
}

/* --- attribute parsing ------------------------------------------------ */
int iptv_attr_int(const char *line, const char *key, int defval)
{
    if (!line || !key) return defval;
    const char *p = strstr(line, key);
    if (!p) return defval;
    p += strlen(key);
    if (*p != '=') return defval;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    int v = 0;
    int any = 0;
    while (*p >= '0' && *p <= '9') {
        v = v * 10 + (*p - '0');
        p++;
        any = 1;
    }
    return any ? v : defval;
}

static void attr_str(const char *line, const char *key,
                     char *out, size_t outsz)
{
    out[0] = '\0';
    if (!line || !key) return;
    const char *p = strstr(line, key);
    if (!p) return;
    p += strlen(key);
    if (*p != '=') return;
    p++;
    if (*p == '"') p++;
    size_t i = 0;
    while (*p && *p != '"' && *p != ',' && i + 1 < outsz) {
        out[i++] = *p++;
    }
    out[i] = '\0';
}

static void attr_resolution(const char *line, int *w, int *h)
{
    *w = *h = 0;
    const char *p = strstr(line, "RESOLUTION=");
    if (!p) return;
    p = strchr(p, '=');
    if (!p) return;
    p++;
    int a = 0, b = 0;
    while (*p >= '0' && *p <= '9') a = a * 10 + (*p++ - '0');
    if (*p != 'x') return;
    p++;
    while (*p >= '0' && *p <= '9') b = b * 10 + (*p++ - '0');
    *w = a;
    *h = b;
}

/* Many public lists (iptv-org etc.) put resolution in the channel NAME,
 * e.g. "BBC (360p)", instead of in an EXT-X-STREAM-INF RESOLUTION field.
 * Recover an approximate height from that label so the compatibility
 * filter actually works on real-world playlists. */
static int name_height(const char *name)
{
    if (!name) return 0;
    const char *p = name;
    while ((p = strchr(p, '(')) != NULL) {
        const char *q = p + 1;
        int v = 0;
        while (*q >= '0' && *q <= '9') v = v * 10 + (*q++ - '0');
        if (v >= 144 && v <= 2160 && (*q == 'p' || *q == 'P' ||
            *q == 'i' || *q == 'I')) {
            return v;
        }
        p++;
    }
    /* also handle "[720p]" style */
    p = name;
    while ((p = strchr(p, '[')) != NULL) {
        const char *q = p + 1;
        int v = 0;
        while (*q >= '0' && *q <= '9') v = v * 10 + (*q++ - '0');
        if (v >= 144 && v <= 2160 && (*q == 'p' || *q == 'P')) return v;
        p++;
    }
    return 0;
}

static iptv_codec_t detect_codec(const char *line)
{
    if (!line) return IPTV_CODEC_UNKNOWN;
    if (strcasestr(line, "h265") || strcasestr(line, "hevc"))
        return IPTV_CODEC_H265;
    if (strcasestr(line, "h264") || strcasestr(line, "avc"))
        return IPTV_CODEC_H264;
    if (strcasestr(line, "mpeg4") || strcasestr(line, "mp4v"))
        return IPTV_CODEC_MPEG4;
    return IPTV_CODEC_UNKNOWN;
}

/* --- URL resolution --------------------------------------------------- */
void iptv_resolve_url(const char *base, const char *rel,
                      char *out, size_t outsz)
{
    if (!rel) { out[0] = '\0'; return; }
    if (strstr(rel, "://")) {
        snprintf(out, outsz, "%s", rel);
        return;
    }
    if (!base || !*base) {
        snprintf(out, outsz, "%s", rel);
        return;
    }
    /* scheme://host/path -> scheme://host/ */
    const char *sep = strstr(base, "://");
    const char *slash = NULL;
    if (sep) {
        const char *rest = sep + 3;
        slash = strchr(rest, '/');
        if (slash) {
            size_t rootlen = (size_t)(slash - base) + 1;
            if (rel[0] == '/') {
                snprintf(out, outsz, "%.*s%s", (int)rootlen - 1, base, rel);
            } else {
                /* keep directory of base path */
                const char *q = strrchr(base, '/');
                if (q && q > slash) {
                    snprintf(out, outsz, "%.*s%s",
                             (int)(q - base) + 1, base, rel);
                } else {
                    snprintf(out, outsz, "%.*s%s",
                             (int)rootlen, base, rel);
                }
            }
        } else {
            snprintf(out, outsz, "%s/%s", base, rel);
        }
    } else {
        snprintf(out, outsz, "%s", rel);
    }
}

/* --- parser ----------------------------------------------------------- */
int iptv_playlist_parse(iptv_playlist_t *pl, const char *data, size_t len)
{
    if (!pl || !data) return -1;
    char *copy = (char *)malloc(len + 1);
    if (!copy) return -1;
    memcpy(copy, data, len);
    copy[len] = '\0';

    char *base = NULL;
    {
        const char *p = strstr(copy, "://");
        if (p) {
            const char *s = p;
            while (s > copy && s[-1] != ' ' && s[-1] != '\n' &&
                   s[-1] != '\r' && s[-1] != '"') s--;
            const char *e = strpbrk(p, " \r\n\"");
            if (e > s) {
                base = xstrdup(s);
                size_t bl = strlen(base);
                if (bl >= 4 && strcmp(base + bl - 4, ".m3u8") == 0) {
                    char *slash = strrchr(base, '/');
                    if (slash) slash[1] = '\0';
                }
            }
        }
    }

    char *line = copy;
    char pending_name[IPTV_MAX_NAME] = "";
    char pending_group[IPTV_MAX_GROUP] = "";
    int  pending_bw = 0, pending_w = 0, pending_h = 0;
    iptv_codec_t pending_codec = IPTV_CODEC_UNKNOWN;
    int was_master_variant = 0;
    int parsed = 0;

    while (line && *line && parsed < IPTV_MAX_CHANNELS) {
        char *end = strchr(line, '\n');
        if (end) *end = '\0';
        char *cur = line;
        line = end ? end + 1 : NULL;

        /* strip CR and surrounding whitespace */
        size_t l = strlen(cur);
        while (l && (cur[l-1] == '\r' || cur[l-1] == ' ' ||
                     cur[l-1] == '\t')) cur[--l] = '\0';
        while (*cur == ' ' || *cur == '\t') cur++;
        if (!*cur) continue;

        if (cur[0] == '#') {
            if (strncasecmp(cur, "#EXTINF:", 8) == 0) {
                const char *comma = strchr(cur + 8, ',');
                if (comma) {
                    snprintf(pending_name, sizeof(pending_name), "%s",
                             comma + 1);
                }
                attr_str(cur, "group-title", pending_group,
                         sizeof(pending_group));
                pending_bw = iptv_attr_int(cur, "BANDWIDTH", 0);
                pending_codec = detect_codec(cur);
                /* Fall back to the resolution label in the name. */
                if (pending_h == 0) {
                    int h = name_height(pending_name);
                    if (h > 0) {
                        pending_h = h;
                        pending_w = h * 16 / 9;   /* approximate 16:9 */
                    }
                }
            } else if (strncasecmp(cur, "#EXT-X-STREAM-INF:", 18) == 0) {
                pending_bw = iptv_attr_int(cur, "BANDWIDTH", 0);
                attr_resolution(cur, &pending_w, &pending_h);
                pending_codec = detect_codec(cur);
                if (!pending_name[0])
                    snprintf(pending_name, sizeof(pending_name), "variant");
                was_master_variant = 1;
            }
            continue;
        }

        /* a URL line */
        iptv_channel_t ch;
        memset(&ch, 0, sizeof(ch));
        snprintf(ch.name, sizeof(ch.name), "%s",
                 pending_name[0] ? pending_name : "channel");
        snprintf(ch.group, sizeof(ch.group), "%s", pending_group);
        ch.bandwidth = pending_bw;
        ch.width  = pending_w;
        ch.height = pending_h;
        ch.codec  = pending_codec;
        iptv_resolve_url(base, cur, ch.url, sizeof(ch.url));
        pl_add(pl, &ch);
        parsed++;

        pending_name[0] = '\0';
        pending_group[0] = '\0';
        pending_bw = pending_w = pending_h = 0;
        pending_codec = IPTV_CODEC_UNKNOWN;
        was_master_variant = 0;
    }
    (void)was_master_variant;

    free(base);
    free(copy);
    return pl->count;
}

int iptv_playlist_load_file(iptv_playlist_t *pl, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long sz = ftell(f);
    if (sz <= 0) { fclose(f); return -1; }
    rewind(f);
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return -1; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[got] = '\0';
    int n = iptv_playlist_parse(pl, buf, got);
    free(buf);
    return n;
}

void iptv_playlist_mark_compatible(iptv_playlist_t *pl,
                                   iptv_filter_stat_t *st)
{
    if (st) { st->total = 0; st->compatible = 0; }
    for (int i = 0; i < pl->count; i++) {
        iptv_channel_t *c = &pl->items[i];
        int ok = 1;
        if (c->codec == IPTV_CODEC_H265) ok = 0;         /* no HEVC hardware */
        if (c->codec == IPTV_CODEC_OTHER) ok = 0;
        if (c->bandwidth > IPTV_MAX_BANDWIDTH) ok = 0;
        /* Judge by total pixel area, not a single dimension: 270p
         * (480x270) fits, while 360p (640x360) exceeds N3DS software
         * decode throughput.  Unknown dimensions (0) pass through and
         * are probed again at playback time. */
        if (c->width > 0 && c->height > 0) {
            long pixels = (long)c->width * c->height;
            if (pixels > (long)IPTV_MAX_WIDTH * IPTV_MAX_HEIGHT) ok = 0;
        }
        c->compatible = ok;
        if (st) {
            st->total++;
            if (ok) st->compatible++;
        }
    }
}
