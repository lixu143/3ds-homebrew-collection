/**
 * @file inputctl.c
 * @brief HTTP client for PBS Payload's PS5 virtual-controller input API.
 *
 * Uses one short-lived TCP connection per request (simple and robust).
 * socInit is expected to have been done already by the stream receiver.
 */
#include "inputctl.h"

#include <3ds.h>

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

static char    s_host[128] = "192.168.1.50";
static uint16_t s_port = 9999;
static char    s_token[128] = "";
static uint32_t s_seq = 0;
static int     s_connected = 0;

static int tcp_connect(void)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct hostent *he = gethostbyname(s_host);
    struct in_addr addr;
    if (he && he->h_addr_list[0]) {
        memcpy(&addr, he->h_addr_list[0], 4);
    } else {
        addr.s_addr = inet_addr(s_host);
        if (addr.s_addr == INADDR_NONE) { closesocket(fd); return -1; }
    }

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(s_port);
    sa.sin_addr = addr;
    if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        closesocket(fd);
        return -1;
    }
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    return fd;
}

/* Drain the HTTP response into buf; return body length, or -1. */
static int http_request(const char *method, const char *path,
                        const char *extra_headers, const char *body,
                        char *out, size_t outcap)
{
    int fd = tcp_connect();
    if (fd < 0) return -1;

    char req[1024];
    int n = snprintf(req, sizeof(req),
                     "%s %s HTTP/1.1\r\n"
                     "Host: %s:%u\r\n"
                     "X-PBS-Input: 1\r\n"
                     "%s"
                     "Connection: close\r\n",
                     method, path, s_host, s_port,
                     extra_headers ? extra_headers : "");
    if (body) {
        n += snprintf(req + n, sizeof(req) - (size_t)n,
                      "Content-Type: text/plain\r\n"
                      "Content-Length: %u\r\n",
                      (unsigned)strlen(body));
    }
    snprintf(req + n, sizeof(req) - (size_t)n, "\r\n");
    if (body) {
        strncat(req, body, sizeof(req) - strlen(req) - 1);
    }

    if (send(fd, req, strlen(req), 0) < 0) {
        closesocket(fd);
        return -1;
    }

    size_t total = 0;
    char tmp[512];
    int r;
    while ((r = recv(fd, tmp, sizeof(tmp), 0)) > 0) {
        if (out && total < outcap - 1) {
            size_t copy = (size_t)r;
            if (copy > outcap - 1 - total) copy = outcap - 1 - total;
            memcpy(out + total, tmp, copy);
            total += copy;
        }
    }
    closesocket(fd);
    if (out) out[total] = '\0';
    return (int)total;
}

/* Extract the body (after \r\n\r\n) from an HTTP response string. */
static const char *http_body(const char *resp)
{
    const char *p = strstr(resp, "\r\n\r\n");
    return p ? p + 4 : resp;
}

int inputctl_connect(const char *host, uint16_t port)
{
    snprintf(s_host, sizeof(s_host), "%s", host ? host : "192.168.1.50");
    s_port = port;
    s_token[0] = '\0';
    s_connected = 0;
    s_seq = 0;

    char resp[2048];
    int len = http_request("POST", "/input/connect", NULL, NULL,
                           resp, sizeof(resp));
    if (len <= 0) return -1;

    const char *body = http_body(resp);
    /* token is returned as plain text; trim whitespace/CRLF */
    size_t j = 0;
    for (const char *p = body; *p && j < sizeof(s_token) - 1; p++) {
        if (*p == '\r' || *p == '\n' || *p == ' ' || *p == '\t') {
            if (j == 0) continue;
            break;
        }
        s_token[j++] = *p;
    }
    s_token[j] = '\0';

    if (strncmp(body, "HTTP/1.", 7) == 0 && s_token[0] == '\0') {
        /* response had no usable body -> likely an error status */
        return -1;
    }
    if (s_token[0] == '\0') return -1;

    s_connected = 1;
    return 0;
}

int inputctl_send(const InputState *st)
{
    if (!s_connected || !st) return -1;

    char body[128];
    snprintf(body, sizeof(body), "%u,%u,%u,%u,%u,%u,%u,%u",
             (unsigned)s_seq++,
             (unsigned)st->buttons,
             (unsigned)st->lx, (unsigned)st->ly,
             (unsigned)st->rx, (unsigned)st->ry,
             (unsigned)st->l2, (unsigned)st->r2);

    char hdr[256];
    snprintf(hdr, sizeof(hdr), "X-PBS-Controller: %s\r\n", s_token);
    char resp[512];
    int len = http_request("POST", "/input/state", hdr, body,
                           resp, sizeof(resp));
    return len > 0 ? 0 : -1;
}

void inputctl_disconnect(void)
{
    if (!s_connected) return;
    char hdr[256];
    snprintf(hdr, sizeof(hdr), "X-PBS-Controller: %s\r\n", s_token);
    char resp[512];
    http_request("POST", "/input/disconnect", hdr, NULL, resp, sizeof(resp));
    s_connected = 0;
    s_token[0] = '\0';
}

int inputctl_connected(void)
{
    return s_connected;
}
