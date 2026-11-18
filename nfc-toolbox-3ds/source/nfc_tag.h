#pragma once

#include <3ds.h>
#include <3ds/services/nfc.h>

// ---------------------------------------------------------------------------
// NFC 工具箱核心模块
// 基于 libctru 的 nfc:u 服务（系统版本 >= 9.3.0-X，New 3DS >= 8.1.0）
// NTAG 原始页读写命令时序参考开源项目 Thenaya (MIT) 验证过的实现
// ---------------------------------------------------------------------------

#define NFC_TIMEOUT_NS      (200ULL * 1000ULL * 1000ULL)  // 200ms，与 Thenaya 一致
#define NTAG_PAGE_SIZE      4
#define NTAG_FAST_READ_PAGES 15                            // FAST_READ 一次读 15 页 = 60 字节
#define NTAG215_LAST_PAGE   0x86                           // NTAG215 末页
#define NFCTAG_MAX_SIZE     540                            // 215 * 4 = 540 字节

// 标签识别结果
typedef struct {
    u8  uid[8];          // 7 字节 UID（NTAG215 为 7 字节），uid[7] 保留
    int uid_len;         // 实际 UID 长度
    int kind;            // 见 NFC_TagKind
    char name[24];       // 显示名称，如 "NTAG215 (540B)"
} NFCTagInfo;

typedef enum {
    TAGKIND_UNKNOWN = 0,
    TAGKIND_NTAG213,
    TAGKIND_NTAG215,
    TAGKIND_NTAG216,
    TAGKIND_AMIIBO,
} NFC_TagKind;

// 操作结果（0 表示成功）
typedef enum {
    NFCTOOL_OK = 0,
    NFCTOOL_ERR_INIT = -1,
    NFCTOOL_ERR_STATE = -2,
    NFCTOOL_ERR_CMD = -3,
    NFCTOOL_ERR_SIZE = -4,
    NFCTOOL_ERR_CANCEL = -5,
    NFCTOOL_ERR_NOTAG = -6,
} NFCToolResult;

// ---- 生命周期 ----
// 两种模式互斥：RawNFC（原始页读写）与 NFCTag（amiibo 配置读取）
int  nfctool_init_raw(void);   // 初始化为原始 NFC 模式（读 UID / 读页 / 写页）
int  nfctool_init_amiibo(void);// 初始化为 NFC Tag 模式（读 amiibo 配置）
void nfctool_exit(void);

// ---- 扫描（阻塞，按 B 取消）----
// 等待标签进入感应区；成功后 info 被填充
int  nfctool_wait_tag(NFCTagInfo *info);

// ---- 数据访问（需要先 init_raw 且 wait_tag 成功）----
// 读取整卡（FAST_READ 逐段），data 至少 NFCTAG_MAX_SIZE 字节
int  nfctool_read_full(u8 *data, int datalen, void (*progress)(int cur, int total));
// 读单个块（READ 命令一次 4 页 = 16 字节）
int  nfctool_read_block(int page, u8 *out16);
// 写单个页（WRITE 命令，data 为 4 字节）；成功返回 0
int  nfctool_write_page(int page, const u8 data[4]);

// ---- amiibo 配置（需要先 init_amiibo 且 wait_tag 成功）----
// 读取已加载 amiibo 的配置（角色 ID / 系列 / 类型 / 写入次数等）
int  nfctool_read_amiibo_config(NFC_AmiiboConfig *cfg);
// 读取 amiibo 设置（昵称 / 主人 Mii / 国家码）
int  nfctool_read_amiibo_settings(NFC_AmiiboSettings *set);

// ---- 工具函数 ----
void nfctool_uid_to_string(const u8 *uid, int len, char *out);   // "04 A2 3C ..." 形式
const char *nfctool_amiibo_type(u8 type);                        // figure/card/plush
const char *nfctool_amiibo_series(u8 series);                    // 系列名称
