#include <3ds.h>
#include <citro2d.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "ui.h"
#include "nfc_tag.h"
#include "storage.h"

// ---------------- 应用状态 ----------------
typedef enum {
    SCREEN_MENU = 0,
    SCREEN_SCAN,       // 标签探测
    SCREEN_AMIIBO,     // amiibo 档案
    SCREEN_QR,         // UID 二维码
    SCREEN_HISTORY,    // 扫描历史
    SCREEN_DUMP,       // 数据导出 / 原始读写
    SCREEN_ABOUT,
} Screen;

static Screen s_screen = SCREEN_MENU;
static int s_menu_sel = 0;

static const char *MENU[] = {
    "1. Tag scan / info",
    "2. Amiibo profile",
    "3. UID QR code",
    "4. Scan history",
    "5. Dump / raw read-write",
    "6. About & safety",
};
#define MENU_COUNT 6

// 全局：最近一次扫描的标签
static NFCTagInfo s_last_tag;
static bool s_has_tag = false;

// 扫描进度回调
static void progress_cb(int cur, int total)
{
    (void)total;
    // 简单的屏上进度提示
    ui_begin_frame();
    ui_header("Reading tag...");
    ui_text(20, 70, 0.55f, CLR_TEXT, "Keep the tag still on the NFC area");
    ui_text(20, 100, 0.5f, CLR_ACCENT, "Reading page 0x%02X ...", cur);
    ui_end_frame();
}

// 让系统时间设置提示更友好：检查年份是否异常（未设时间通常是 2010 或更早）
static void check_clock(void)
{
    time_t t = time(NULL);
    struct tm *lt = localtime(&t);
    if (lt && (lt->tm_year + 1900) < 2015) {
        ui_begin_frame();
        ui_header("Notice");
        ui_text(16, 70, 0.55f, CLR_WARN, "System clock looks wrong.");
        ui_text(16, 100, 0.5f, CLR_TEXT, "Please set date/time in System");
        ui_text(16, 124, 0.5f, CLR_TEXT, "Settings, or HTTPS/dump time");
        ui_text(16, 148, 0.5f, CLR_TEXT, "may be incorrect.");
        ui_text(16, 200, 0.5f, CLR_MUTED, "Press A to continue");
        ui_end_frame();
        ui_wait_key();
    }
}

// ---------------- 主菜单 ----------------
static void screen_menu(void)
{
    while (aptMainLoop() && s_screen == SCREEN_MENU) {
        hidScanInput();
        u32 k = hidKeysDown();
        if (k & KEY_DOWN) { s_menu_sel = (s_menu_sel + 1) % MENU_COUNT; }
        if (k & KEY_UP)   { s_menu_sel = (s_menu_sel + MENU_COUNT - 1) % MENU_COUNT; }
        if (k & KEY_A) {
            switch (s_menu_sel) {
                case 0: s_screen = SCREEN_SCAN;    break;
                case 1: s_screen = SCREEN_AMIIBO;  break;
                case 2: s_screen = SCREEN_QR;      break;
                case 3: s_screen = SCREEN_HISTORY; break;
                case 4: s_screen = SCREEN_DUMP;    break;
                case 5: s_screen = SCREEN_ABOUT;   break;
            }
        }

        ui_begin_frame();
        ui_header("NFC Toolbox for 3DS  v1.0");
        ui_text(16, 60, 0.5f, CLR_MUTED,  "New 3DS built-in NFC / NTAG215 / amiibo");
        ui_text(16, 84, 0.5f, CLR_MUTED,  "D-pad: move    A: select    B: back");
        ui_text(16, 120, 0.5f, CLR_ACCENT,"Use only with tags you own.");
        ui_text(16, 144, 0.5f, CLR_MUTED, "Backup / inspect your own cards.");
        for (int i = 0; i < MENU_COUNT; i++) {
            ui_menu_item(24 + i * 36, MENU[i], i == s_menu_sel);
        }
        ui_end_frame();
    }
}

// ---------------- 公共：扫描一次标签 ----------------
// raw=true 用原始模式（可读写页），raw=false 用 amiibo 模式
static bool do_scan(bool raw)
{
    int rc = raw ? nfctool_init_raw() : nfctool_init_amiibo();
    if (rc != NFCTOOL_OK) return false;

    NFCTagInfo info;
    rc = nfctool_wait_tag(&info);

    bool ok = (rc == NFCTOOL_OK);
    if (ok) {
        s_last_tag = info;
        s_has_tag = true;
    }
    nfctool_exit();
    return ok;
}

// ---------------- 功能 1：标签探测 ----------------
static void screen_scan(void)
{
    while (aptMainLoop() && s_screen == SCREEN_SCAN) {
        // 执行扫描
        bool ok = do_scan(true);

        ui_begin_frame();
        ui_header("Tag scan / info");
        if (ok) {
            char uid[32];
            nfctool_uid_to_string(s_last_tag.uid, s_last_tag.uid_len, uid);
            ui_text(16, 64, 0.6f, CLR_OK, "Tag detected!");
            ui_text(16, 100, 0.55f, CLR_TEXT, "Type   : %s", s_last_tag.name);
            ui_text(16, 130, 0.55f, CLR_TEXT, "UID    : %s", uid);
            ui_text(16, 160, 0.55f, CLR_TEXT, "UID len: %d bytes", s_last_tag.uid_len);
            ui_text(16, 196, 0.5f, CLR_MUTED, "UID is the tag's unique ID.");
        } else {
            ui_text(16, 70, 0.55f, CLR_ERR, "No tag / cancelled.");
            ui_text(16, 104, 0.5f, CLR_TEXT, "Place tag on the NFC area");
            ui_text(16, 128, 0.5f, CLR_TEXT, "(below the screens) and hold.");
            ui_text(16, 168, 0.5f, CLR_MUTED, "B cancels. A retries.");
        }
        ui_text(16, 300, 0.45f, CLR_MUTED, "A: scan again   B: back to menu");
        ui_end_frame();

        u32 k = ui_wait_key();
        if (k & KEY_B) s_screen = SCREEN_MENU;
        // A -> 循环重新扫描
    }
}

// ---------------- 功能 2：amiibo 档案 ----------------
static void screen_amiibo(void)
{
    while (aptMainLoop() && s_screen == SCREEN_AMIIBO) {
        bool ok = do_scan(false);
        bool got_cfg = false;
        NFC_AmiiboConfig cfg;
        NFC_AmiiboSettings set;
        memset(&cfg, 0, sizeof(cfg));
        memset(&set, 0, sizeof(set));

        if (ok) {
            got_cfg = (nfctool_read_amiibo_config(&cfg) == NFCTOOL_OK);
            nfctool_read_amiibo_settings(&set);
        }

        ui_begin_frame();
        ui_header("Amiibo profile");
        if (ok && got_cfg) {
            char uid[32];
            nfctool_uid_to_string(s_last_tag.uid, s_last_tag.uid_len, uid);
            ui_text(16, 60, 0.55f, CLR_OK, "Amiibo data loaded");
            ui_text(16, 92, 0.5f, CLR_TEXT, "Series : %s", nfctool_amiibo_series(cfg.series));
            ui_text(16, 116, 0.5f, CLR_TEXT, "Type   : %s", nfctool_amiibo_type(cfg.type));
            ui_text(16, 140, 0.5f, CLR_TEXT, "Char ID: %02X%02X%02X",
                    cfg.characterID[0], cfg.characterID[1], cfg.characterID[2]);
            ui_text(16, 164, 0.5f, CLR_TEXT, "AmiiboID: 0x%04X", cfg.amiiboID);
            ui_text(16, 188, 0.5f, CLR_TEXT, "Writes : %u", (unsigned)cfg.write_counter);
            ui_text(16, 212, 0.5f, CLR_TEXT, "Last   : %04d-%02d-%02d",
                    cfg.lastwritedate_year, cfg.lastwritedate_month, cfg.lastwritedate_day);
            ui_text(16, 236, 0.5f, CLR_TEXT, "UID    : %s", uid);

            // UTF-16BE 昵称转简单 ASCII 显示（仅取可用 ASCII 字符）
            char nick[32];
            int ni = 0;
            for (int i = 0; i < 11 && ni < 31; i++) {
                u16 c = (u16)((set.nickname[i * 2] << 8) | set.nickname[i * 2 + 1]);
                if (c == 0) break;
                if (c >= 0x20 && c < 0x7F) nick[ni++] = (char)c;
            }
            nick[ni] = '\0';
            if (ni > 0) ui_text(16, 264, 0.5f, CLR_ACCENT, "Nick   : %s", nick);
        } else {
            ui_text(16, 70, 0.55f, CLR_ERR, "No amiibo / not readable.");
            ui_text(16, 104, 0.5f, CLR_TEXT, "Place an amiibo on the NFC area.");
            ui_text(16, 132, 0.5f, CLR_MUTED, "Only reads info your console shows.");
            ui_text(16, 156, 0.5f, CLR_MUTED, "No keys, no cloning, no writes.");
        }
        ui_text(16, 300, 0.45f, CLR_MUTED, "A: scan again   B: back");
        ui_end_frame();

        u32 k = ui_wait_key();
        if (k & KEY_B) s_screen = SCREEN_MENU;
    }
}

// ---------------- 功能 3：UID 二维码 ----------------
static void screen_qr(void)
{
    char content[128] = {0};
    bool has_qr = false;

    while (aptMainLoop() && s_screen == SCREEN_QR) {
        ui_begin_frame();
        ui_header("UID QR code");
        if (has_qr) {
            ui_qrcode(content);
            ui_text(16, 232, 0.45f, CLR_MUTED, "Scan with your phone camera");
        } else {
            ui_text(16, 70, 0.55f, CLR_TEXT, "Generate a QR code from a tag UID.");
            ui_text(16, 100, 0.5f, CLR_MUTED, "Useful for logging your own tags");
            ui_text(16, 124, 0.5f, CLR_MUTED, "into a spreadsheet or inventory.");
        }
        ui_text(16, 320, 0.45f, CLR_MUTED, "A: scan tag & make QR   B: back");
        ui_end_frame();

        u32 k = ui_wait_key();
        if (k & KEY_B) { s_screen = SCREEN_MENU; }
        if (k & KEY_A) {
            if (do_scan(true)) {
                char uid[32];
                nfctool_uid_to_string(s_last_tag.uid, s_last_tag.uid_len, uid);
                snprintf(content, sizeof(content), "NFC-UID:%s", uid);
                has_qr = true;
            }
        }
    }
}

// ---------------- 功能 4：扫描历史 ----------------
static void screen_history(void)
{
    static HistoryEntry list[32];
    static int total = 0;
    static int cursor = 0;
    static bool loaded = false;
    if (!loaded) {
        total = storage_load_history(list, 32);
        loaded = true;
    }

    while (aptMainLoop() && s_screen == SCREEN_HISTORY) {
        hidScanInput();
        u32 k = hidKeysDown();
        if (k & KEY_DOWN && total > 0) cursor = (cursor + 1) % total;
        if (k & KEY_UP   && total > 0) cursor = (cursor + total - 1) % total;
        if (k & KEY_X) { // 清空历史
            storage_clear_history();
            total = 0; cursor = 0;
        }

        ui_begin_frame();
        ui_header("Scan history");
        if (total > 0) {
            HistoryEntry *e = &list[cursor];
            ui_text(16, 62, 0.55f, CLR_ACCENT, "Entry %d / %d", cursor + 1, total);
            ui_text(16, 96, 0.5f, CLR_TEXT, "UID  : %s", e->uid);
            ui_text(16, 122, 0.5f, CLR_TEXT, "Type : %s", e->name);
            ui_text(16, 148, 0.5f, CLR_TEXT, "Time : %s", e->time);
            ui_text(16, 174, 0.5f, CLR_TEXT, "Note : %s", e->note);
            ui_text(16, 210, 0.45f, CLR_MUTED, "Stored in sdmc:/NFC-Toolbox/history.txt");
        } else {
            ui_text(16, 70, 0.55f, CLR_MUTED, "No history yet.");
            ui_text(16, 100, 0.5f, CLR_TEXT, "Scan a tag in the Dump screen");
            ui_text(16, 124, 0.5f, CLR_TEXT, "and choose 'Save to history'.");
        }
        ui_text(16, 300, 0.45f, CLR_MUTED, "Up/Down: browse   X: clear   B: back");
        for (int i = 0; i < total && i < 6; i++) {
            ui_menu_item(24 + i * 34, list[i].uid, i == cursor);
        }
        ui_end_frame();
    }
    loaded = false;
}

// ---------------- 功能 5：数据导出 / 原始读写 ----------------
static void screen_dump(void)
{
    static int sub = 0;      // 当前子菜单
    static bool has_dump = false;
    static u8 dump[NFCTAG_MAX_SIZE];
    static char dump_path[256];

    const char *OPTS[] = {
        "Scan & read full tag (backup)",
        "Save last dump to SD card",
        "Append last tag to history",
        "Read one block (16 bytes)",
        "Write one page (4 bytes)",
    };
    const int OPT_COUNT = 5;

    while (aptMainLoop() && s_screen == SCREEN_DUMP) {
        hidScanInput();
        u32 k = hidKeysDown();
        if (k & KEY_DOWN) sub = (sub + 1) % OPT_COUNT;
        if (k & KEY_UP)   sub = (sub + OPT_COUNT - 1) % OPT_COUNT;

        if (k & KEY_A) {
            switch (sub) {
                case 0: { // 读全卡
                    if (do_scan(true)) {
                        nfctool_init_raw();
                        // 重新等待标签（do_scan 里已 exit，这里重来）
                        NFCTagInfo info;
                        if (nfctool_wait_tag(&info) == NFCTOOL_OK) {
                            if (nfctool_read_full(dump, sizeof(dump), progress_cb) == NFCTOOL_OK) {
                                has_dump = true;
                                s_last_tag = info;
                                s_has_tag = true;
                            }
                        }
                        nfctool_exit();
                    }
                    break;
                }
                case 1: { // 保存 dump
                    if (has_dump) {
                        char uid[32];
                        nfctool_uid_to_string(s_last_tag.uid, s_last_tag.uid_len, uid);
                        if (storage_save_dump(dump, sizeof(dump), uid, dump_path, sizeof(dump_path)) == 0) {
                            // 成功
                        } else {
                            snprintf(dump_path, sizeof(dump_path), "SAVE FAILED");
                        }
                    } else {
                        snprintf(dump_path, sizeof(dump_path), "No dump yet - read first");
                    }
                    break;
                }
                case 2: { // 存历史
                    if (s_has_tag) {
                        HistoryEntry e;
                        memset(&e, 0, sizeof(e));
                        nfctool_uid_to_string(s_last_tag.uid, s_last_tag.uid_len, e.uid);
                        snprintf(e.name, sizeof(e.name), "%s", s_last_tag.name);
                        storage_now_string(e.time, sizeof(e.time));
                        snprintf(e.note, sizeof(e.note), "saved from toolbox");
                        storage_append_history(&e);
                    }
                    break;
                }
                case 3: { // 读单块
                    if (do_scan(true)) {
                        nfctool_init_raw();
                        NFCTagInfo info;
                        u8 block[16] = {0};
                        char result[128];
                        if (nfctool_wait_tag(&info) == NFCTOOL_OK &&
                            nfctool_read_block(4, block) == NFCTOOL_OK) {
                            snprintf(result, sizeof(result),
                                     "P04: %02X %02X %02X %02X | %02X %02X %02X %02X",
                                     block[0],block[1],block[2],block[3],
                                     block[4],block[5],block[6],block[7]);
                        } else {
                            snprintf(result, sizeof(result), "Read failed");
                        }
                        nfctool_exit();
                        // 显示结果
                        ui_begin_frame();
                        ui_header("Block read");
                        ui_text(16, 70, 0.5f, CLR_TEXT, "%s", result);
                        ui_text(16, 110, 0.5f, CLR_MUTED, "Press A/B to continue");
                        ui_end_frame();
                        ui_wait_key();
                    }
                    break;
                }
                case 4: { // 写单页（危险操作，带确认）
                    ui_begin_frame();
                    ui_header("Write page - WARNING");
                    ui_text(16, 60, 0.5f, CLR_ERR, "Writing can permanently lock");
                    ui_text(16, 84, 0.5f, CLR_ERR, "a tag. Only on your own blank tags.");
                    ui_text(16, 120, 0.5f, CLR_WARN, "This demo writes 4 zero bytes");
                    ui_text(16, 144, 0.5f, CLR_WARN, "to page 0x10 (user area).");
                    ui_text(16, 184, 0.5f, CLR_TEXT, "A = proceed   B = cancel");
                    ui_end_frame();
                    u32 c = ui_wait_key();
                    if (c & KEY_A) {
                        u8 zeros[4] = {0,0,0,0};
                        nfctool_init_raw();
                        NFCTagInfo info;
                        int wrc = -1;
                        if (nfctool_wait_tag(&info) == NFCTOOL_OK)
                            wrc = nfctool_write_page(0x10, zeros);
                        nfctool_exit();
                        ui_begin_frame();
                        ui_header("Write result");
                        ui_text(16, 70, 0.55f, wrc == 0 ? CLR_OK : CLR_ERR,
                                wrc == 0 ? "Write OK (ACK 0x0A)" : "Write failed / NAK");
                        ui_text(16, 108, 0.5f, CLR_MUTED, "Press A/B to continue");
                        ui_end_frame();
                        ui_wait_key();
                    }
                    break;
                }
            }
        }

        ui_begin_frame();
        ui_header("Dump / raw read-write");
        ui_text(16, 56, 0.5f, s_has_tag ? CLR_OK : CLR_MUTED,
                s_has_tag ? "Tag: %s" : "No tag scanned yet",
                s_has_tag ? s_last_tag.name : "");
        ui_text(16, 82, 0.45f, CLR_MUTED, "Dump: %s", has_dump ? "540 bytes ready" : "none");
        ui_text(16, 104, 0.45f, CLR_ACCENT, "Last file: %s", dump_path[0] ? dump_path : "-");
        ui_text(16, 300, 0.45f, CLR_MUTED, "Up/Down: select   A: run   B: back");
        for (int i = 0; i < OPT_COUNT; i++) {
            ui_menu_item(20 + i * 34, OPTS[i], i == sub);
        }
        ui_end_frame();

        if (k & KEY_B) s_screen = SCREEN_MENU;
    }
}

// ---------------- 关于 / 安全说明 ----------------
static void screen_about(void)
{
    while (aptMainLoop() && s_screen == SCREEN_ABOUT) {
        ui_begin_frame();
        ui_header("About & safety");
        ui_text(16, 56, 0.5f, CLR_TEXT, "NFC Toolbox for New 3DS  v1.0");
        ui_text(16, 84, 0.5f, CLR_MUTED, "Built on libctru nfc:u service.");
        ui_text(16, 104, 0.5f, CLR_MUTED, "QR code: Nayuki qrcodegen (MIT).");
        ui_text(16, 132, 0.5f, CLR_ACCENT, "Legal & safety:");
        ui_text(16, 158, 0.45f, CLR_TEXT, "- Use only with tags you own.");
        ui_text(16, 180, 0.45f, CLR_TEXT, "- Back up your own cards for personal use.");
        ui_text(16, 202, 0.45f, CLR_TEXT, "- Do NOT clone commercial amiibo for resale.");
        ui_text(16, 224, 0.45f, CLR_TEXT, "- Writing can permanently lock a tag.");
        ui_text(16, 246, 0.45f, CLR_TEXT, "- Respect local laws and game ToS.");
        ui_text(16, 300, 0.45f, CLR_MUTED, "Press B to go back");
        ui_end_frame();
        u32 k = ui_wait_key();
        if (k & KEY_B) s_screen = SCREEN_MENU;
    }
}

// ---------------- 主入口 ----------------
int main(void)
{
    if (!ui_init()) return 1;
    storage_init();
    check_clock();

    while (aptMainLoop()) {
        switch (s_screen) {
            case SCREEN_MENU:    screen_menu();    break;
            case SCREEN_SCAN:    screen_scan();    break;
            case SCREEN_AMIIBO:  screen_amiibo();  break;
            case SCREEN_QR:      screen_qr();      break;
            case SCREEN_HISTORY: screen_history(); break;
            case SCREEN_DUMP:    screen_dump();    break;
            case SCREEN_ABOUT:   screen_about();   break;
        }
    }

    nfctool_exit();
    ui_exit();
    return 0;
}
