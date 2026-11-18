#pragma once

#include <3ds.h>
#include <citro2d.h>

// 通用 RGBA 配色
#define CLR_BG       C2D_Color32(0x1E, 0x24, 0x30, 0xFF)  // 深蓝灰背景
#define CLR_PANEL    C2D_Color32(0x2B, 0x33, 0x43, 0xFF)
#define CLR_ACCENT   C2D_Color32(0x4F, 0xA8, 0xFF, 0xFF)  // 亮蓝
#define CLR_TEXT     C2D_Color32(0xEC, 0xF0, 0xF5, 0xFF)
#define CLR_MUTED    C2D_Color32(0x9A, 0xA6, 0xB8, 0xFF)
#define CLR_OK       C2D_Color32(0x5C, 0xD9, 0x8A, 0xFF)
#define CLR_WARN     C2D_Color32(0xFF, 0xC0, 0x5C, 0xFF)
#define CLR_ERR      C2D_Color32(0xFF, 0x6B, 0x6B, 0xFF)

// 初始化 / 结束图形系统
bool ui_init(void);
void ui_exit(void);

// 每帧开始：清屏两个屏幕
void ui_begin_frame(void);
// 每帧结束：提交渲染
void ui_end_frame(void);

// 绘制一行文字（默认系统字体，支持 UTF-8）
void ui_text(float x, float y, float scale, u32 color, const char *fmt, ...);
// 居中的标题栏（固定绘制在上屏）
void ui_header(const char *title);
// 画一个实心矩形
void ui_rect(float x, float y, float w, float h, u32 color);

// 菜单项渲染：返回当前高亮项索引由外部管理
void ui_menu_item(float y, const char *label, bool selected);

// 在上屏绘制二维码（content 为要编码的文本），返回是否成功
bool ui_qrcode(const char *content);

// 等待按键（阻塞到任意键按下），返回按下的键位
u32  ui_wait_key(void);
