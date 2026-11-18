/**
 * @file net.c
 * @brief 3ds-curl based HTTP/HTTPS client.
 *
 * Build dependency: 3ds-curl and 3ds-mbedtls from devkitPro.
 *   pacman -S 3ds-curl 3ds-mbedtls
 */
#include "net.h"

#include <3ds.h>
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int s_net_ready = 0;

typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} membuf_t;

typedef struct {
    FILE  *f;
    int    ok;
} filebuf_t;

int net_init(void)
{
    if (s_net_ready) return 0;
    /* 3ds-curl initialises soc internally with its own buffer; calling
     * socInit ourselves here would fight it for the same resource. */
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0) return -1;
    s_net_ready = 1;
    return 0;
}

void net_exit(void)
{
    if (!s_net_ready) return;
    curl_global_cleanup();
    s_net_ready = 0;
}

static size_t cb_mem(void *ptr, size_t size, size_t nmemb, void *user)
{
    size_t n = size * nmemb;
    membuf_t *m = (membuf_t *)user;
    if (m->len + n + 1 > m->cap) {
        size_t cap = m->cap ? m->cap : 4096;
        while (cap < m->len + n + 1) cap *= 2;
        if (cap > 8 * 1024 * 1024) return 0;   /* playlist size guard */
        char *p = (char *)realloc(m->data, cap);
        if (!p) return 0;
        m->data = p;
        m->cap = cap;
    }
    memcpy(m->data + m->len, ptr, n);
    m->len += n;
    m->data[m->len] = '\0';
    return n;
}

static size_t cb_file(void *ptr, size_t size, size_t nmemb, void *user)
{
    filebuf_t *fb = (filebuf_t *)user;
    size_t n = size * nmemb;
    if (!fb->f) return 0;
    if (fwrite(ptr, 1, n, fb->f) != n) { fb->ok = 0; return 0; }
    return n;
}

static CURL *make_curl(const char *url, int timeout_ms)
{
    CURL *c = curl_easy_init();
    if (!c) return NULL;
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, (long)timeout_ms);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, (long)(timeout_ms / 2));
    curl_easy_setopt(c, CURLOPT_USERAGENT,
                     "Mozilla/5.0 (Nintendo 3DS; IPTV-3DS)");
    /* CA bundle shipped by Luma3DS / Universal-Updater */
    curl_easy_setopt(c, CURLOPT_CAINFO, IPTV_CA_BUNDLE);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(c, CURLOPT_HTTPGET, 1L);
    return c;
}

int net_get_string(const char *url, char **out, size_t *out_len,
                   int timeout_ms)
{
    if (!s_net_ready || !url || !out) return -1;
    CURL *c = make_curl(url, timeout_ms);
    if (!c) return -1;

    membuf_t m;
    memset(&m, 0, sizeof(m));
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, cb_mem);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &m);

    CURLcode res = curl_easy_perform(c);
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(c);

    if (res != CURLE_OK || (code != 200 && code != 0)) {
        free(m.data);
        return -1;
    }
    if (!m.data) {
        m.data = (char *)malloc(1);
        if (!m.data) return -1;
        m.data[0] = '\0';
    }
    *out = m.data;
    if (out_len) *out_len = m.len;
    return 0;
}

int net_save_file(const char *url, const char *path, int timeout_ms)
{
    if (!s_net_ready || !url || !path) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    CURL *c = make_curl(url, timeout_ms);
    if (!c) { fclose(f); return -1; }

    filebuf_t fb;
    fb.f = f;
    fb.ok = 1;
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, cb_file);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &fb);

    CURLcode res = curl_easy_perform(c);
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(c);
    fclose(f);

    if (res != CURLE_OK || !fb.ok || (code != 200 && code != 0)) return -1;
    return 0;
}

void net_free(void *p)
{
    free(p);
}
