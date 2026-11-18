/**
 * @file config.h
 * @brief Persist PS5 stream connection settings on the SD card.
 */
#ifndef CONFIG_H
#define CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define CFG_PATH "sdmc:/3ds/ps5-stream/config.txt"

typedef struct {
    char     host[128];     /* PS5 LAN IP, e.g. 192.168.1.50 */
    uint16_t port;          /* stream port, e.g. 9999 */
    char     path[128];     /* HTTP path, usually "/" */
    int      fit;           /* 0 fit, 1 fill, 2 stretch */
} StreamConfig;

/** Load config from SD, falling back to defaults. Returns 0 on success. */
int  config_load(StreamConfig *cfg);

/** Save config to SD (creates directories). Returns 0 on success. */
int  config_save(const StreamConfig *cfg);

/** Fill cfg with defaults. */
void config_defaults(StreamConfig *cfg);

#ifdef __cplusplus
}
#endif

#endif
