#include "nfc_tag.h"

#include <string.h>
#include <stdio.h>

// FAST_READ(start, end)：一次读 end-start+1 页，最多 15 页
#define CMD_FAST_READ(start, count) { 0x3A, (u8)(start), (u8)((start) + (count) - 1) }
#define CMD_READ(page)              { 0x30, (u8)(page) }
#define CMD_WRITE(page, d)          { 0xA2, (u8)(page), (d)[0], (d)[1], (d)[2], (d)[3] }

static int s_mode = 0;   // 1 = raw, 2 = amiibo
static int s_write_ready = 0;

int nfctool_init_raw(void)
{
    nfctool_exit();
    Result rc = nfcInit(NFC_OpType_RawNFC);
    if (R_FAILED(rc)) return NFCTOOL_ERR_INIT;
    s_mode = 1;
    s_write_ready = 0;
    return NFCTOOL_OK;
}

int nfctool_init_amiibo(void)
{
    nfctool_exit();
    Result rc = nfcInit(NFC_OpType_NFCTag);
    if (R_FAILED(rc)) return NFCTOOL_ERR_INIT;
    s_mode = 2;
    return NFCTOOL_OK;
}

void nfctool_exit(void)
{
    if (s_mode) nfcExit();
    s_mode = 0;
    s_write_ready = 0;
}

// 等待标签进入感应区。raw 模式用 StartOtherTagScanning，amiibo 模式用 StartScanning
int nfctool_wait_tag(NFCTagInfo *info)
{
    if (!s_mode) return NFCTOOL_ERR_INIT;
    if (!info) return NFCTOOL_ERR_STATE;

    Result rc;
    if (s_mode == 1)
        rc = nfcStartOtherTagScanning(NFC_STARTSCAN_DEFAULTINPUT, 0x01);
    else
        rc = nfcStartScanning(NFC_STARTSCAN_DEFAULTINPUT);
    if (R_FAILED(rc)) return NFCTOOL_ERR_STATE;

    NFC_TagState prev = NFC_TagState_Uninitialized;
    NFC_TagState cur  = NFC_TagState_Uninitialized;
    int result = NFCTOOL_ERR_CANCEL;

    while (aptMainLoop()) {
        gspWaitForVBlank();
        hidScanInput();
        if (hidKeysDown() & KEY_B) { result = NFCTOOL_ERR_CANCEL; break; }

        if (R_FAILED(nfcGetTagState(&cur))) { result = NFCTOOL_ERR_STATE; break; }
        if (cur != prev) {
            prev = cur;
            if (cur == NFC_TagState_InRange) { result = NFCTOOL_OK; break; }
        }
    }

    if (result != NFCTOOL_OK) { nfcStopScanning(); return result; }

    memset(info, 0, sizeof(*info));
    info->kind = TAGKIND_UNKNOWN;
    strcpy(info->name, "Unknown tag");

    // raw 模式：读第 0 页（16 字节）取得 7 字节 UID，再读第 3 页判断容量
    if (s_mode == 1) {
        u8 block[16] = {0};
        size_t got = 0;
        u8 cmd[] = CMD_READ(0);
        rc = nfcSendTagCommand(cmd, sizeof(cmd), block, sizeof(block), &got, NFC_TIMEOUT_NS);
        if (R_SUCCEEDED(rc) && got >= 8) {
            // NTAG21x 7 字节 UID 在 READ 结果中的分布（跳过第 4 字节 BCC0）
            u8 uid7[7] = { block[0], block[1], block[2], block[4], block[5], block[6], block[7] };
            memcpy(info->uid, uid7, 7);
            info->uid_len = 7;

            u8 ccblock[16] = {0};
            size_t ccgot = 0;
            u8 cmdcc[] = CMD_READ(3);
            if (R_SUCCEEDED(nfcSendTagCommand(cmdcc, sizeof(cmdcc), ccblock, sizeof(ccblock), &ccgot, NFC_TIMEOUT_NS))
                && ccgot >= 4) {
                u8 cap = ccblock[2];
                if (cap == 0x12) {
                    info->kind = TAGKIND_NTAG213;
                    strcpy(info->name, "NTAG213 (180B)");
                } else if (cap == 0x3F) {
                    info->kind = TAGKIND_NTAG215;
                    strcpy(info->name, "NTAG215 (540B)");
                } else if (cap == 0x6D) {
                    info->kind = TAGKIND_NTAG216;
                    strcpy(info->name, "NTAG216 (924B)");
                } else if (cap == 0xFF) {
                    // amiibo 的 CC 被改成 F1 10 FF EE
                    info->kind = TAGKIND_AMIIBO;
                    strcpy(info->name, "amiibo / NTAG215");
                }
            }
        } else {
            // 读不到页，仍尝试从 nfcGetTagInfo 取 UID
            NFC_TagInfo ti;
            if (R_SUCCEEDED(nfcGetTagInfo(&ti))) {
                int n = (ti.id_offset_size > 0 && ti.id_offset_size <= 8) ? ti.id_offset_size : 7;
                memcpy(info->uid, ti.id, n);
                info->uid_len = n;
            }
        }
    } else {
        NFC_TagInfo ti;
        if (R_SUCCEEDED(nfcGetTagInfo(&ti))) {
            int n = (ti.id_offset_size > 0 && ti.id_offset_size <= 8) ? ti.id_offset_size : 7;
            memcpy(info->uid, ti.id, n);
            info->uid_len = n;
        }
        info->kind = TAGKIND_AMIIBO;
        strcpy(info->name, "amiibo tag");
    }

    return NFCTOOL_OK;
}

int nfctool_read_full(u8 *data, int datalen, void (*progress)(int cur, int total))
{
    if (!data || datalen < NTAG_PAGE_SIZE * 4) return NFCTOOL_ERR_SIZE;
    memset(data, 0, datalen);

    for (int page = 0; page <= NTAG215_LAST_PAGE; page += NTAG_FAST_READ_PAGES) {
        if (progress) progress(page, NTAG215_LAST_PAGE);
        u8 cmd[] = CMD_FAST_READ(page, NTAG_FAST_READ_PAGES);
        u8 buf[NTAG_FAST_READ_PAGES * NTAG_PAGE_SIZE];
        memset(buf, 0, sizeof(buf));
        size_t got = 0;
        Result rc = nfcSendTagCommand(cmd, sizeof(cmd), buf, sizeof(buf), &got, NFC_TIMEOUT_NS);
        if (R_FAILED(rc)) return NFCTOOL_ERR_CMD;
        size_t expect = NTAG_FAST_READ_PAGES * NTAG_PAGE_SIZE;
        if (got < expect) return NFCTOOL_ERR_SIZE;

        size_t off = (size_t)page * NTAG_PAGE_SIZE;
        size_t copy = sizeof(buf);
        if (off + copy > (size_t)datalen) copy = (size_t)datalen - off;
        if (copy) memcpy(data + off, buf, copy);
    }
    if (progress) progress(NTAG215_LAST_PAGE, NTAG215_LAST_PAGE);
    return NFCTOOL_OK;
}

int nfctool_read_block(int page, u8 *out16)
{
    if (!out16 || page < 0 || page > NTAG215_LAST_PAGE) return NFCTOOL_ERR_SIZE;
    u8 cmd[] = CMD_READ(page);
    size_t got = 0;
    Result rc = nfcSendTagCommand(cmd, sizeof(cmd), out16, 16, &got, NFC_TIMEOUT_NS);
    if (R_FAILED(rc)) return NFCTOOL_ERR_CMD;
    if (got < 16) return NFCTOOL_ERR_SIZE;
    return NFCTOOL_OK;
}

int nfctool_write_page(int page, const u8 data[4])
{
    if (!data || page < 0 || page > NTAG215_LAST_PAGE) return NFCTOOL_ERR_SIZE;
    // 首次写入前调用 nfcCmd21，让读头保持连续模式，避免每页掉电（Thenaya 验证过）
    if (!s_write_ready) {
        nfcCmd21();
        s_write_ready = 1;
    }
    u8 cmd[] = CMD_WRITE(page, data);
    u8 resp[16] = {0};
    size_t got = 0;
    Result rc = nfcSendTagCommand(cmd, sizeof(cmd), resp, sizeof(resp), &got, NFC_TIMEOUT_NS);
    if (R_FAILED(rc)) return NFCTOOL_ERR_CMD;
    if (got >= 1 && resp[0] != 0x0A) return NFCTOOL_ERR_CMD;  // NAK
    return NFCTOOL_OK;
}

int nfctool_read_amiibo_config(NFC_AmiiboConfig *cfg)
{
    if (!cfg) return NFCTOOL_ERR_SIZE;
    NFC_TagState st;
    if (R_FAILED(nfcGetTagState(&st))) return NFCTOOL_ERR_STATE;
    if (st != NFC_TagState_DataReady && st != NFC_TagState_InRange) {
        Result rc = nfcLoadAmiiboData();
        if (R_FAILED(rc)) return NFCTOOL_ERR_CMD;
    }
    return R_SUCCEEDED(nfcGetAmiiboConfig(cfg)) ? NFCTOOL_OK : NFCTOOL_ERR_CMD;
}

int nfctool_read_amiibo_settings(NFC_AmiiboSettings *set)
{
    if (!set) return NFCTOOL_ERR_SIZE;
    return R_SUCCEEDED(nfcGetAmiiboSettings(set)) ? NFCTOOL_OK : NFCTOOL_ERR_CMD;
}

void nfctool_uid_to_string(const u8 *uid, int len, char *out)
{
    static const char hex[] = "0123456789ABCDEF";
    for (int i = 0; i < len; i++) {
        out[i * 3]     = hex[(uid[i] >> 4) & 0xF];
        out[i * 3 + 1] = hex[uid[i] & 0xF];
        out[i * 3 + 2] = ' ';
    }
    if (len > 0) out[len * 3 - 1] = '\0';
    else out[0] = '\0';
}

const char *nfctool_amiibo_type(u8 type)
{
    switch (type) {
        case 0: return "Figure";
        case 1: return "Card";
        case 2: return "Plush / Yarn";
        default: return "Unknown";
    }
}

const char *nfctool_amiibo_series(u8 series)
{
    // amiibo series ID 的常见映射
    switch (series) {
        case 0x00: return "Super Mario";
        case 0x01: return "Mario Party";
        case 0x02: return "The Legend of Zelda";
        case 0x03: return "Fire Emblem";
        case 0x04: return "Animal Crossing";
        case 0x05: return "Super Smash Bros.";
        case 0x06: return "Splatoon";
        case 0x07: return "Kirby";
        case 0x08: return "Pokemon";
        case 0x09: return "Mario Kart 8";
        case 0x0A: return "Pikmin";
        case 0x0B: return "Captain Toad";
        case 0x0C: return "Super Mario Party";
        case 0x0D: return "Luigi's Mansion 3";
        case 0x0F: return "Minecraft";
        default:   return "Other / Unknown";
    }
}
