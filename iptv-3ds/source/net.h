/**
 * @file net.h
 * @brief HTTP/HTTPS downloader built on 3ds-curl.
 *
 * Certificate bundle is read from sdmc:/config/ssl/cacert.pem (the same
 * path Luma3DS / Universal-Updater use).  Without it, or with a wrong
 * system clock, modern HTTPS hosts will fail verification.
 */
#ifndef IPTV_NET_H
#define IPTV_NET_H

#include <stddef.h>

#define IPTV_CA_BUNDLE "sdmc:/config/ssl/cacert.pem"

int net_init(void);
void net_exit(void);

/* Download a URL into a malloc'd NUL-terminated buffer.
 * Caller must net_free() the buffer. Returns 0 on success. */
int net_get_string(const char *url, char **out, size_t *out_len,
                   int timeout_ms);

/* Save a URL to a file on the SD card. Returns 0 on success. */
int net_save_file(const char *url, const char *path, int timeout_ms);

void net_free(void *p);

#endif /* IPTV_NET_H */
