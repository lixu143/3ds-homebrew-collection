#pragma once

#include <3ds.h>

// ---------------- SD 卡存储模块 ----------------
// 所有文件放在 sdmc:/NFC-Toolbox/ 下

#define NFC_DIR        "sdmc:/NFC-Toolbox"
#define NFC_HISTORY    "sdmc:/NFC-Toolbox/history.txt"
#define NFC_DUMP_DIR   "sdmc:/NFC-Toolbox/dumps"

// 历史记录条目
typedef struct {
    char uid[32];        // "04 A2 3C ..."
    char name[24];       // 标签类型名
    char time[24];       // "2026-11-17 14:08:00"
    char note[64];       // 备注（如 amiibo 角色）
} HistoryEntry;

// 初始化目录结构（不存在则创建），返回 0 成功
int  storage_init(void);

// 追加一条历史记录
int  storage_append_history(const HistoryEntry *entry);

// 读取历史记录到数组，返回读取条数（最多 max 条）
int  storage_load_history(HistoryEntry *out, int max);

// 清空历史记录
int  storage_clear_history(void);

// 导出 dump 文件，文件名会自动加序号避免覆盖
// 返回完整文件路径写入 outPath，成功返回 0
int  storage_save_dump(const u8 *data, int len, const char *uid_hex, char *outPath, int outPathLen);

// 生成当前时间字符串 "YYYY-MM-DD HH:MM:SS"
void storage_now_string(char *out, int len);
