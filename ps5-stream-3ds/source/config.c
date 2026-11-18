/**
 * @file config.c
 * @brief Simple key=value config file storage on the SD card.
 */
#include "config.h"

#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

static void ensure_dir(void)
{
    /* mkdir -p style: create sdmc:/3ds and sdmc:/3ds/ps5-stream */
    mkdir("sdmc:/3ds", 0777);
    mkdir("sdmc:/3ds/ps5-stream", 0777);
}

void config_defaults(StreamConfig *cfg)
{
    if (!cfg) return;
    strcpy(cfg->host, "192.168.1.50");
    cfg->port = 9999;
    strcpy(cfg->path, "/");
    cfg->fit = 0;
}

int config_load(StreamConfig *cfg)
{
    if (!cfg) return -1;
    config_defaults(cfg);

    FILE *f = fopen(CFG_PATH, "r");
    if (!f) return -1;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line;
        char *val = eq + 1;
        /* trim trailing CR/LF */
        size_t vl = strlen(val);
        while (vl && (val[vl-1] == '\n' || val[vl-1] == '\r')) val[--vl] = '\0';
        while (*val == ' ') val++;

        if (strcmp(key, "host") == 0 && val[0])
            snprintf(cfg->host, sizeof(cfg->host), "%s", val);
        else if (strcmp(key, "port") == 0 && val[0]) {
            int p = atoi(val);
            if (p > 0 && p <= 65535) cfg->port = (uint16_t)p;
        } else if (strcmp(key, "path") == 0 && val[0])
            snprintf(cfg->path, sizeof(cfg->path), "%s", val);
        else if (strcmp(key, "fit") == 0 && val[0]) {
            int v = atoi(val);
            if (v >= 0 && v <= 2) cfg->fit = v;
        }
    }
    fclose(f);
    return 0;
}

int config_save(const StreamConfig *cfg)
{
    if (!cfg) return -1;
    ensure_dir();

    FILE *f = fopen(CFG_PATH, "w");
    if (!f) return -1;
    fprintf(f, "# PS5 stream receiver config\n");
    fprintf(f, "host=%s\n", cfg->host);
    fprintf(f, "port=%u\n", (unsigned)cfg->port);
    fprintf(f, "path=%s\n", cfg->path);
    fprintf(f, "fit=%d\n", cfg->fit);
    fclose(f);
    return 0;
}
