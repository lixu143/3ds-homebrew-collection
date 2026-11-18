/**
 * @file httpshare.c
 * @brief HTTP file server core for Nintendo 3DS.
 *
 * Design notes:
 *  - Single accept loop, one connection at a time (fine for LAN file transfer).
 *  - Sockets are non-blocking on accept; we sleep a bit when idle so the
 *    background thread does not starve the game / UI.
 *  - Every path is sanitized and confined to HS_ROOT. Directory traversal
 *    ("..", absolute paths, backslashes) is rejected.
 *  - Uploads stream straight to SD via fwrite in chunks; nothing is buffered
 *    fully in RAM (3DS only has limited memory).
 */
#include "httpshare.h"

#include <3ds.h>
#include <malloc.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#define SOC_ALIGN     0x1000
/* Socket buffer size. The standalone .3dsx app can afford 1 MiB, but a
 * sysmodule runs in a tiny heap, so its Makefile overrides this to 0x50000
 * (320 KiB) via -DSOC_BUFFERSIZE=... . Overridable by design. */
#ifndef SOC_BUFFERSIZE
#define SOC_BUFFERSIZE 0x100000
#endif
#define RECV_BUFSZ    (16 * 1024)
#define FILE_CHUNK    (16 * 1024)
#define URL_MAX       1024
#define HEADER_MAX    (16 * 1024)

static uint16_t   g_port = HS_DEFAULT_PORT;
static HSLogCb    g_logcb = NULL;
static void      *g_socbuf = NULL;
static int        g_listenfd = -1;
static volatile int g_stop = 0;
static HSStatus   g_status;

/*---------------------------------------------------------------------------*/
/* helpers                                                                    */
/*---------------------------------------------------------------------------*/

/* forward decl: recursive mkdir (defined near the bottom) */
static void mkdir_p(const char *path);

static void logline(const char *s)
{
    if (g_logcb) g_logcb(s);
}

static void set_event(const char *s)
{
    snprintf(g_status.last_event, sizeof(g_status.last_event), "%s", s);
    logline(s);
}

static int set_nonblock(int fd, bool on)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    if (on) flags |= O_NONBLOCK;
    else    flags &= ~O_NONBLOCK;
    return fcntl(fd, F_SETFL, flags);
}

/* percent-decode a URL component into dst (dst at least strlen(src)+1) */
static void url_decode(const char *src, char *dst, size_t dstsz)
{
    size_t j = 0;
    for (size_t i = 0; src[i] && j + 1 < dstsz; i++) {
        if (src[i] == '%' && isxdigit((unsigned char)src[i+1]) &&
            isxdigit((unsigned char)src[i+2])) {
            char h[3] = { src[i+1], src[i+2], 0 };
            dst[j++] = (char)strtol(h, NULL, 16);
            i += 2;
        } else if (src[i] == '+') {
            dst[j++] = ' ';
        } else {
            dst[j++] = src[i];
        }
    }
    dst[j] = 0;
}

/**
 * Build a safe absolute path inside HS_ROOT from a URL path.
 * Reject traversal. Returns 1 on success, 0 if the request is unsafe.
 */
static int safe_path(const char *urlpath, char *out, size_t outsz)
{
    char decoded[URL_MAX];
    url_decode(urlpath, decoded, sizeof(decoded));

    /* strip query string */
    char *q = strchr(decoded, '?');
    if (q) *q = 0;

    /* reject obvious traversal / absolute / backslash */
    if (decoded[0] == '/' || strstr(decoded, "..") || strchr(decoded, '\\'))
        return 0;

    /* root path -> index */
    if (decoded[0] == 0) snprintf(out, outsz, "%s/", HS_ROOT);
    else                 snprintf(out, outsz, "%s/%s", HS_ROOT, decoded);

    /* final safety net */
    if (strstr(out, "..")) return 0;
    return 1;
}

/*---------------------------------------------------------------------------*/
/* HTTP response helpers                                                      */
/*---------------------------------------------------------------------------*/

static void send_all(int fd, const void *buf, size_t len)
{
    const char *p = (const char *)buf;
    while (len > 0) {
        int n = send(fd, p, len, 0);
        if (n <= 0) break;
        p += n; len -= (size_t)n;
    }
}

static void http_header(int fd, int code, const char *reason,
                        const char *ctype, size_t len)
{
    char h[256];
    int n = snprintf(h, sizeof(h),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %u\r\n"
        "Connection: close\r\n"
        "Cache-Control: no-store\r\n"
        "\r\n",
        code, reason, ctype, (unsigned)len);
    send_all(fd, h, (size_t)n);
}

static void http_simple(int fd, int code, const char *reason, const char *msg)
{
    http_header(fd, code, reason, "text/plain; charset=utf-8", strlen(msg));
    send_all(fd, msg, strlen(msg));
}

/* HTML-escape minimal (/, <, >, &, ") for embedding filenames in HTML */
static void html_escape(const char *s, char *out, size_t outsz)
{
    size_t j = 0;
    for (size_t i = 0; s[i] && j + 6 < outsz; i++) {
        switch (s[i]) {
            case '&':  memcpy(out+j, "&amp;", 5);  j += 5; break;
            case '<':  memcpy(out+j, "&lt;", 4);   j += 4; break;
            case '>':  memcpy(out+j, "&gt;", 4);   j += 4; break;
            case '"':  memcpy(out+j, "&quot;", 6); j += 6; break;
            default:   out[j++] = s[i]; break;
        }
    }
    out[j] = 0;
}

/* percent-encode a path component for use in href attributes */
static void url_encode(const char *s, char *out, size_t outsz)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t j = 0;
    for (size_t i = 0; s[i] && j + 3 < outsz; i++) {
        unsigned char c = (unsigned char)s[i];
        if (isalnum(c) || c=='-' || c=='_' || c=='.' || c=='/' ) {
            out[j++] = (char)c;
        } else {
            out[j++] = '%';
            out[j++] = hex[c >> 4];
            out[j++] = hex[c & 0xF];
        }
    }
    out[j] = 0;
}

/*---------------------------------------------------------------------------*/
/* GET: directory listing                                                     */
/*---------------------------------------------------------------------------*/

static void handle_index(int fd, const char *relpath)
{
    /* Build page body in a heap buffer (bounded). */
    size_t cap = 16 * 1024, used = 0;
    char *body = (char *)malloc(cap);
    if (!body) { http_simple(fd, 500, "Internal Error", "out of memory"); return; }

    #define APPEND(...) do { \
        int _n = snprintf(body+used, cap-used, __VA_ARGS__); \
        if (_n > 0) { used += (size_t)_n; } \
        if (used >= cap) { \
            cap += 16*1024; char *_b = (char*)realloc(body, cap); \
            if (!_b) { free(body); http_simple(fd,500,"Internal Error","oom"); return; } \
            body = _b; \
        } \
    } while (0)

    APPEND("<!doctype html><html><head><meta charset='utf-8'>"
           "<meta name='viewport' content='width=device-width,initial-scale=1'>"
           "<title>HTTP Share 3DS</title>"
           "<style>body{font-family:sans-serif;margin:2em;background:#fafafa;color:#222}"
           "h1{font-size:1.3em}a{color:#06c;text-decoration:none}li{margin:.3em 0}"
           ".d{color:#888}form{margin:1.5em 0;padding:1em;border:1px dashed #bbb}"
           "input[type=file]{margin-right:.5em}button{background:#06c;color:#fff;border:0;padding:.4em 1em;cursor:pointer}"
           "</style></head><body>");
    APPEND("<h1>HTTP Share 3DS</h1>");
    APPEND("<p class='d'>Root: %s &middot; path: /%s</p>", HS_ROOT,
           relpath[0] ? relpath : "");

    APPEND("<form method='post' enctype='multipart/form-data' action='/upload'>"
           "<input type='hidden' name='path' value='%s'>"
           "<input type='file' name='file' multiple onchange='this.form.submit()'>"
           "<button type='submit'>Upload</button></form>", relpath);

    APPEND("<h2>Files</h2><ul>");

    DIR *dir = opendir(HS_ROOT);
    if (dir) {
        struct dirent *e;
        while ((e = readdir(dir)) != NULL) {
            if (e->d_name[0] == '.') continue;
            char full[URL_MAX], enc[URL_MAX], esc[URL_MAX];
            snprintf(full, sizeof(full), "%s/%s", HS_ROOT, e->d_name);
            url_encode(e->d_name, enc, sizeof(enc));
            html_escape(e->d_name, esc, sizeof(esc));
            struct stat st;
            if (stat(full, &st) != 0) continue;
            if (S_ISDIR(st.st_mode))
                APPEND("<li class='d'>[dir] %s/</li>", esc);
            else
                APPEND("<li><a href='/download?file=%s'>%s</a> "
                       "<span class='d'>(%u KB)</span> "
                       "<a href='/delete?file=%s' onclick=\"return confirm('Delete?')\">[del]</a></li>",
                       enc, esc, (unsigned)(st.st_size/1024), enc);
        }
        closedir(dir);
    } else {
        APPEND("<li class='d'>(cannot open root dir)</li>");
    }
    APPEND("</ul><p class='d'>Uploaded files land in %s/Downloads/ via the API.</p>", HS_ROOT);
    APPEND("</body></html>");

    http_header(fd, 200, "OK", "text/html; charset=utf-8", used);
    send_all(fd, body, used);
    free(body);
    #undef APPEND
}

/*---------------------------------------------------------------------------*/
/* GET: download a file                                                       */
/*---------------------------------------------------------------------------*/

static void handle_download(int fd, const char *fileparam)
{
    char path[URL_MAX];
    if (!safe_path(fileparam, path, sizeof(path))) {
        http_simple(fd, 403, "Forbidden", "unsafe path"); return;
    }
    FILE *fp = fopen(path, "rb");
    if (!fp) { http_simple(fd, 404, "Not Found", "file not found"); return; }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz < 0) { fclose(fp); http_simple(fd, 500, "Error", "stat failed"); return; }

    char disp[URL_MAX];
    const char *base = strrchr(path, '/');
    base = base ? base+1 : path;
    snprintf(disp, sizeof(disp), "attachment; filename=\"%s\"", base);

    char h[320];
    int n = snprintf(h, sizeof(h),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/octet-stream\r\n"
        "Content-Length: %ld\r\n"
        "Content-Disposition: %s\r\n"
        "Connection: close\r\n\r\n", sz, disp);
    send_all(fd, h, (size_t)n);

    char buf[FILE_CHUNK];
    size_t r;
    while ((r = fread(buf, 1, sizeof(buf), fp)) > 0)
        send_all(fd, buf, r);
    fclose(fp);

    g_status.downloads++;
    g_status.bytes_tx += (uint64_t)sz;
    char ev[160];
    snprintf(ev, sizeof(ev), "Downloaded %s (%ld KB)", base, sz/1024);
    set_event(ev);
}

/*---------------------------------------------------------------------------*/
/* GET: delete a file                                                         */
/*---------------------------------------------------------------------------*/

static void handle_delete(int fd, const char *fileparam)
{
    char path[URL_MAX];
    if (!safe_path(fileparam, path, sizeof(path))) {
        http_simple(fd, 403, "Forbidden", "unsafe path"); return;
    }
    /* never delete a directory through HTTP */
    struct stat st;
    if (stat(path, &st) != 0 || S_ISDIR(st.st_mode)) {
        http_simple(fd, 400, "Bad Request", "not a file"); return;
    }
    if (remove(path) != 0) {
        http_simple(fd, 500, "Error", "delete failed"); return;
    }
    g_status.deletes++;
    const char *base = strrchr(path, '/'); base = base ? base+1 : path;
    char ev[160];
    snprintf(ev, sizeof(ev), "Deleted %s", base);
    set_event(ev);

    const char *msg = "OK deleted. <a href='/'>Back</a>";
    http_header(fd, 200, "OK", "text/html; charset=utf-8", strlen(msg));
    send_all(fd, msg, strlen(msg));
}

/*---------------------------------------------------------------------------*/
/* POST: multipart/form-data upload                                           */
/*---------------------------------------------------------------------------*/

/* Find a substring in a buffer of known length. */
static const char *memmem2(const char *hay, size_t hlen, const char *needle, size_t nlen)
{
    if (nlen == 0 || hlen < nlen) return NULL;
    for (size_t i = 0; i <= hlen - nlen; i++)
        if (memcmp(hay+i, needle, nlen) == 0) return hay+i;
    return NULL;
}

/* case-insensitive substring search (newlib has no reliable strcasestr) */
static char *ci_strstr(const char *hay, const char *needle)
{
    size_t nl = strlen(needle);
    if (!nl) return (char*)hay;
    for (const char *p = hay; *p; p++) {
        size_t i = 0;
        while (i < nl && p[i] && tolower((unsigned char)p[i]) ==
                                  tolower((unsigned char)needle[i])) i++;
        if (i == nl) return (char*)p;
    }
    return NULL;
}

/* last occurrence of c in first n bytes (newlib may lack memrchr) */
static const void *memrchr2(const void *mem, int c, size_t n)
{
    const unsigned char *p = (const unsigned char *)mem;
    while (n--) if (p[n] == (unsigned char)c) return p+n;
    return NULL;
}

/* Extract value of a named field from the multipart body (for the 'path' field). */
static void get_field(const char *body, size_t blen, const char *name,
                      char *out, size_t outsz)
{
    out[0] = 0;
    char key[64];
    snprintf(key, sizeof(key), "name=\"%s\"", name);
    const char *p = memmem2(body, blen, key, strlen(key));
    if (!p) return;
    const char *hc = memmem2(p, blen - (size_t)(p-body), "\r\n\r\n", 4);
    if (!hc) return;
    const char *val = hc + 4;
    const char *end = memmem2(val, blen - (size_t)(val-body), "\r\n", 2);
    size_t n = end ? (size_t)(end-val) : 0;
    if (n >= outsz) n = outsz-1;
    memcpy(out, val, n);
    out[n] = 0;
}

static void handle_upload(int fd, const char *body, size_t blen,
                          const char *boundary)
{
    char delim[128];
    snprintf(delim, sizeof(delim), "--%s", boundary);
    size_t dlen = strlen(delim);

    /* target subdir from the hidden 'path' field */
    char subdir[URL_MAX] = "";
    get_field(body, blen, "path", subdir, sizeof(subdir));
    url_decode(subdir, subdir, sizeof(subdir));

    char dirpath[URL_MAX];
    if (subdir[0] && strstr(subdir, "..") == 0) {
        snprintf(dirpath, sizeof(dirpath), "%s/%s", HS_ROOT, subdir);
    } else {
        snprintf(dirpath, sizeof(dirpath), "%s", HS_ROOT);
    }
    mkdir_p(dirpath);

    /* locate the file part headers */
    const char *namekey = "name=\"file\"";
    const char *fp = memmem2(body, blen, namekey, strlen(namekey));
    if (!fp) {
        /* still allow single file without the field name check */
        fp = memmem2(body, blen, "filename=", 9);
    }
    if (!fp) { http_simple(fd, 400, "Bad Request", "no file field"); return; }

    /* filename="..." */
    const char *fn = memmem2(fp, blen - (size_t)(fp-body), "filename=\"", 10);
    char filename[256] = "upload.bin";
    if (fn) {
        const char *s = fn + 10;
        const char *e = memchr(s, '"', blen - (size_t)(s-body));
        size_t n = e ? (size_t)(e-s) : 0;
        /* keep only basename (use original pointer for offset math) */
        const char *slash = (const char*)memrchr2(s, '/', n);
        if (slash) { size_t off = (size_t)(slash - s) + 1; s += off; n -= off; }
        const char *bslash = (const char*)memrchr2(s, '\\', n);
        if (bslash) { size_t off = (size_t)(bslash - s) + 1; s += off; n -= off; }
        if (n > 0 && n < sizeof(filename)) {
            memcpy(filename, s, n); filename[n] = 0;
        }
    }
    if (filename[0] == 0 || strstr(filename, "..")) strcpy(filename, "upload.bin");

    /* content of the file starts after the part's header terminator */
    const char *hc = memmem2(fp, blen - (size_t)(fp-body), "\r\n\r\n", 4);
    if (!hc) { http_simple(fd, 400, "Bad Request", "bad multipart"); return; }
    const char *start = hc + 4;

    /* file ends at "\r\n--boundary" */
    char endmark[160];
    snprintf(endmark, sizeof(endmark), "\r\n--%s", boundary);
    const char *end = memmem2(start, blen - (size_t)(start-body),
                              endmark, strlen(endmark));
    size_t flen = end ? (size_t)(end-start)
                      : blen - (size_t)(start-body);
    /* strip one trailing CRLF if present */
    if (flen >= 2 && start[flen-2]=='\r' && start[flen-1]=='\n') flen -= 2;

    char outpath[URL_MAX];
    snprintf(outpath, sizeof(outpath), "%s/%s", dirpath, filename);
    FILE *ofp = fopen(outpath, "wb");
    if (!ofp) { http_simple(fd, 500, "Error", "cannot create file"); return; }
    size_t wrote = fwrite(start, 1, flen, ofp);
    fclose(ofp);

    if (wrote != flen) {
        http_simple(fd, 500, "Error", "short write"); return;
    }

    g_status.uploads++;
    g_status.bytes_rx += flen;
    char ev[160];
    snprintf(ev, sizeof(ev), "Uploaded %s (%u KB) -> /%s", filename,
             (unsigned)(flen/1024), subdir);
    set_event(ev);

    const char *msg = "OK uploaded. <a href='/'>Back</a>";
    http_header(fd, 200, "OK", "text/html; charset=utf-8", strlen(msg));
    send_all(fd, msg, strlen(msg));
}

/* portable recursive mkdir helper (forward declared above handle_upload) */
static void mkdir_p(const char *path)
{
    char tmp[URL_MAX];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 5; *p; p++) {   /* skip "sdmc:" */
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
    mkdir(tmp, 0777);
}

/*---------------------------------------------------------------------------*/
/* request dispatch                                                           */
/*---------------------------------------------------------------------------*/

static void handle_request(int fd)
{
    static char *req = NULL;
    static size_t reqcap = 0;
    if (!req) { reqcap = HEADER_MAX + FILE_CHUNK; req = (char*)malloc(reqcap); }
    if (!req) return;

    size_t used = 0;
    /* read headers first */
    while (used < HEADER_MAX) {
        int n = recv(fd, req+used, FILE_CHUNK, 0);
        if (n <= 0) return;
        used += (size_t)n;
        req[used] = 0;
        if (strstr(req, "\r\n\r\n")) break;
    }
    if (used >= HEADER_MAX || !req || !strstr(req, "\r\n\r\n")) {
        http_simple(fd, 413, "Payload Too Large", "headers too big"); return;
    }

    char *hdr_end = strstr(req, "\r\n\r\n");
    size_t hlen = (size_t)(hdr_end - req);
    /* copy headers so we can parse safely */
    char headers[HEADER_MAX];
    if (hlen >= sizeof(headers)) { http_simple(fd, 413,"Payload Too Large","hdr"); return; }
    memcpy(headers, req, hlen); headers[hlen] = 0;

    char method[16] = {0}, url[URL_MAX] = {0};
    if (sscanf(headers, "%15s %1023s", method, url) != 2) {
        http_simple(fd, 400, "Bad Request", "bad request line"); return;
    }

    /* find Content-Length */
    size_t clen = 0;
    char *cl = ci_strstr(headers, "Content-Length:");
    if (cl) {
        cl += strlen("Content-Length:");
        while (*cl == ' ') cl++;
        clen = strtoul(cl, NULL, 10);
    }

    size_t body_start = hlen + 4;
    size_t have_body = used > body_start ? used - body_start : 0;

    /* POST: need to read the rest of the body */
    if (strcmp(method, "POST") == 0) {
        /* grow buffer if needed */
        size_t need = body_start + clen + 4;
        if (need > reqcap) {
            while (reqcap < need) reqcap *= 2;
            char *nb = (char*)realloc(req, reqcap);
            if (!nb) { http_simple(fd,500,"Error","oom"); return; }
            req = nb;
        }
        while (have_body < clen) {
            int n = recv(fd, req + body_start + have_body,
                         FILE_CHUNK, 0);
            if (n <= 0) break;
            have_body += (size_t)n;
        }
        req[body_start + have_body] = 0;

        char boundary[128] = {0};
        char *ct = ci_strstr(headers, "boundary=");
        if (ct) {
            ct += strlen("boundary=");
            while (*ct == ' ') ct++;
            size_t i = 0;
            while (ct[i] && ct[i] != '\r' && ct[i] != ';' && i < sizeof(boundary)-1) {
                boundary[i] = ct[i]; i++;
            }
            boundary[i] = 0;
        }
        if (strstr(url, "upload")) {
            handle_upload(fd, req + body_start, have_body, boundary);
        } else {
            http_simple(fd, 404, "Not Found", "unknown post");
        }
        return;
    }

    /* GET / DELETE with query params */
    if (strcmp(method, "GET") == 0 || strcmp(method, "HEAD") == 0) {
        char rel[URL_MAX] = {0};
        char *q = strchr(url, '?');
        if (q) {
            snprintf(rel, sizeof(rel), "%s", q+1);
            if (strncmp(rel, "file=", 5) == 0) {
                handle_download(fd, rel+5);
            } else if (strncmp(rel, "delete=", 7) == 0) {
                handle_delete(fd, rel+7);
            } else {
                handle_index(fd, "");
            }
        } else {
            handle_index(fd, "");
        }
        return;
    }
    if (strcmp(method, "DELETE") == 0) {
        /* optional: DELETE /?file=... */
        char *q = strchr(url, '?');
        if (q && strncmp(q+1, "file=", 5) == 0)
            handle_delete(fd, q+6);
        else
            http_simple(fd, 400, "Bad Request", "no file");
        return;
    }

    http_simple(fd, 405, "Method Not Allowed", "method not allowed");
}

/*---------------------------------------------------------------------------*/
/* public API                                                                 */
/*---------------------------------------------------------------------------*/

int hs_init(uint16_t port, HSLogCb logcb)
{
    g_port = port ? port : HS_DEFAULT_PORT;
    g_logcb = logcb;
    g_stop = 0;
    memset(&g_status, 0, sizeof(g_status));
    g_status.port = g_port;

    g_socbuf = memalign(SOC_ALIGN, SOC_BUFFERSIZE);
    if (!g_socbuf) return -1;
    Result r = socInit((u32*)g_socbuf, SOC_BUFFERSIZE);
    if (R_FAILED(r)) { free(g_socbuf); g_socbuf = NULL; return -2; }

    /* figure out our LAN IP */
    struct in_addr ip, nm, bc;
    if (SOCU_GetIPInfo(&ip, &nm, &bc) == 0)
        snprintf(g_status.ip, sizeof(g_status.ip), "%s", inet_ntoa(ip));
    else
        snprintf(g_status.ip, sizeof(g_status.ip), "unknown");

    hs_ensure_dirs();
    set_event("Server initialized");
    return 0;
}

int hs_ensure_dirs(void)
{
    mkdir_p(HS_ROOT);
    char dl[URL_MAX];
    snprintf(dl, sizeof(dl), "%s/Downloads", HS_ROOT);
    mkdir_p(dl);
    return 0;
}

void hs_request_stop(void) { g_stop = 1; }

void hs_get_status(HSStatus *out)
{
    if (out) *out = g_status;
}

void hs_exit(void)
{
    g_stop = 1;
    if (g_listenfd >= 0) { shutdown(g_listenfd, SHUT_RDWR); close(g_listenfd); g_listenfd = -1; }
    socExit();
    if (g_socbuf) { free(g_socbuf); g_socbuf = NULL; }
}

/* Try to (re)open the listening socket. Returns 1 on success.
 * In a always-on sysmodule the WiFi link may be down at boot or may drop
 * while a game runs, so failing to bind must never kill the process. */
static int open_listener(void)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 0;

    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(g_port);

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0 ||
        listen(fd, 2) < 0 || set_nonblock(fd, true) < 0) {
        close(fd);
        return 0;
    }
    g_listenfd = fd;
    return 1;
}

void hs_server_loop(void *arg)
{
    (void)arg;
    g_status.running = 1;
    u64 next_retry = 0;

    while (!g_stop) {
        /* (re)establish listener; retry every 2 s while WiFi is unavailable */
        if (g_listenfd < 0) {
            u64 now = svcGetSystemTick();
            if (now < next_retry) { svcSleepThread(100 * 1000 * 1000); continue; }
            if (!open_listener()) {
                struct in_addr ip, nm, bc;
                if (SOCU_GetIPInfo(&ip, &nm, &bc) == 0)
                    snprintf(g_status.ip, sizeof(g_status.ip), "%s", inet_ntoa(ip));
                else
                    snprintf(g_status.ip, sizeof(g_status.ip), "unknown");
                set_event("Waiting for WiFi / bind (retry in 2s)");
                next_retry = now + 2 * SYSCLOCK_ARM11;
                continue;
            }
            char ev[160];
            snprintf(ev, sizeof(ev), "Serving http://%s:%u/", g_status.ip, g_port);
            set_event(ev);
        }

        struct sockaddr_in peer;
        socklen_t plen = sizeof(peer);
        int cf = accept(g_listenfd, (struct sockaddr*)&peer, &plen);
        if (cf < 0) {
            /* EAGAIN is the normal idle case. Any other error most likely
             * means the link went down; drop the listener and rebuild it. */
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                close(g_listenfd);
                g_listenfd = -1;
                set_event("Link lost, rebuilding listener");
            }
            svcSleepThread(50 * 1000 * 1000); /* 50 ms, be nice to the game */
            continue;
        }
        char cev[160];
        snprintf(cev, sizeof(cev), "Client %s connected", inet_ntoa(peer.sin_addr));
        set_event(cev);
        set_nonblock(cf, false);   /* blocking read for this client */
        handle_request(cf);
        shutdown(cf, SHUT_RDWR);
        close(cf);
    }

    if (g_listenfd >= 0) { close(g_listenfd); g_listenfd = -1; }
    g_status.running = 0;
    set_event("Server stopped");
}
