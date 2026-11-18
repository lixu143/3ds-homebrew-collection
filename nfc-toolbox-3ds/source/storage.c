#include "storage.h"

#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>

static int ensure_dir(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) return 0;
    return mkdir(path, 0777);
}

int storage_init(void)
{
    int rc = 0;
    rc |= ensure_dir(NFC_DIR);
    rc |= ensure_dir(NFC_DUMP_DIR);
    return rc;
}

int storage_append_history(const HistoryEntry *entry)
{
    if (!entry) return -1;
    FILE *f = fopen(NFC_HISTORY, "a");
    if (!f) return -1;
    // 用制表符分隔，读取时用 fscanf %[^\t]
    fprintf(f, "%s\t%s\t%s\t%s\n",
            entry->uid, entry->name, entry->time, entry->note);
    fclose(f);
    return 0;
}

int storage_load_history(HistoryEntry *out, int max)
{
    if (!out || max <= 0) return 0;
    FILE *f = fopen(NFC_HISTORY, "r");
    if (!f) return 0;

    char line[256];
    int count = 0;
    while (count < max && fgets(line, sizeof(line), f)) {
        HistoryEntry *e = &out[count];
        memset(e, 0, sizeof(*e));
        // 解析 4 个 tab 分隔字段
        char *p = line;
        char *fields[4] = { e->uid, e->name, e->time, e->note };
        int sizes[4] = { 32, 24, 24, 64 };
        for (int i = 0; i < 4; i++) {
            int j = 0;
            while (*p && *p != '\t' && *p != '\n' && j < sizes[i] - 1) {
                fields[i][j++] = *p++;
            }
            fields[i][j] = '\0';
            if (*p == '\t') p++;
        }
        if (e->uid[0]) count++;
    }
    fclose(f);
    return count;
}

int storage_clear_history(void)
{
    return remove(NFC_HISTORY);
}

int storage_save_dump(const u8 *data, int len, const char *uid_hex, char *outPath, int outPathLen)
{
    if (!data || len <= 0) return -1;

    // 找可用序号
    char clean[32];
    int ci = 0;
    for (int i = 0; uid_hex && uid_hex[i] && ci < (int)sizeof(clean) - 4; i++) {
        char c = uid_hex[i];
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F')) clean[ci++] = c;
    }
    clean[ci] = '\0';
    if (ci == 0) strcpy(clean, "tag");

    char path[256];
    int idx = 1;
    for (;; idx++) {
        snprintf(path, sizeof(path), "%s/%s_%02d.bin", NFC_DUMP_DIR, clean, idx);
        FILE *chk = fopen(path, "rb");
        if (!chk) break;
        fclose(chk);
        if (idx > 999) return -1;
    }

    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t written = fwrite(data, 1, (size_t)len, f);
    fclose(f);
    if (written != (size_t)len) return -1;

    if (outPath && outPathLen > 0) {
        snprintf(outPath, outPathLen, "%s", path);
    }
    return 0;
}

void storage_now_string(char *out, int len)
{
    time_t t = time(NULL);
    struct tm *lt = localtime(&t);
    if (lt) {
        snprintf(out, len, "%04d-%02d-%02d %02d:%02d:%02d",
                 lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday,
                 lt->tm_hour, lt->tm_min, lt->tm_sec);
    } else {
        snprintf(out, len, "unknown");
    }
}
