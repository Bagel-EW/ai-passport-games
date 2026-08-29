// main/demo_avatar.c —— 虚拟形象选择页 + 共享 helper（NEW FILE, Phase 3 草稿）
//
// 契约（与 demo.h 一致）：实现 enter/exit/key 三个函数，由 main.c 的 DEMOS[] 注册。
// 本文件同时提供 home 页复用的 helper：像素预设绘制、选择持久化、自定义形象加载。
//
// 注意：ui_pixel.c 的 block() 是 static 不外暴露，这里复制一份同款 helper 自绘。
#include "demo_avatar.h"
#include "ui_pixel.h"
#include "font_zh.h"
#include "lvgl.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "avatar";

// ---------- 自绘 helper（复制自 ui_pixel.c 的 block，因它是 static）----------
static lv_obj_t *block(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    return obj;
}

// ---------- 像素风预设形象（矢量绘制，零 RAM 占用）----------
// 在 (x,y) 画一个 size×size 的方块头像；用 8×8 虚拟栅格，U = size/8。
static void draw_face(lv_obj_t *p, int x, int y, int size,
                      uint32_t face, uint32_t eye, uint32_t accent, int style)
{
    const int U = size / 8;
    // 头/脸底色
    block(p, x, y, size, size, face);
    // 耳朵 / 触角（依风格）
    if (style == 1) {                       // 猫：顶部两耳
        block(p, x, y, 2 * U, 2 * U, face);
        block(p, x + size - 2 * U, y, 2 * U, 2 * U, face);
    } else if (style == 2) {                // 熊猫：黑耳
        block(p, x, y, 2 * U, 2 * U, UI_INK);
        block(p, x + size - 2 * U, y, 2 * U, 2 * U, UI_INK);
    } else if (style == 0 || style == 3) {  // 机器人/外星：顶部天线灯
        block(p, x + 3 * U, y - U, 2 * U, U, accent);   // 灯（轻微出界无害）
    }
    // 眼睛
    block(p, x + 2 * U, y + 3 * U, U, U, eye);
    block(p, x + 5 * U, y + 3 * U, U, U, eye);
    if (style == 2) {                       // 熊猫：眼斑
        block(p, x + U, y + 2 * U, 2 * U, 2 * U, UI_INK);
        block(p, x + 5 * U, y + 2 * U, 2 * U, 2 * U, UI_INK);
        block(p, x + 2 * U, y + 3 * U, U, U, UI_PAPER);
        block(p, x + 5 * U, y + 3 * U, U, U, UI_PAPER);
    }
    if (style == 3) {                       // 外星：大眼
        block(p, x + U, y + 3 * U, 2 * U, 2 * U, eye);
        block(p, x + 5 * U, y + 3 * U, 2 * U, 2 * U, eye);
    }
    // 嘴 / 鼻
    if (style == 1) {                       // 猫：粉鼻
        block(p, x + 3 * U, y + 5 * U, 2 * U, U, 0xE58AA8);
    } else {
        block(p, x + 2 * U, y + 5 * U, 4 * U, U, eye);
    }
}

void avatar_draw_preset(lv_obj_t *parent, int preset, int x, int y, int size)
{
    switch (preset) {
    case 0: draw_face(parent, x, y, size, 0x4FC3F7, UI_INK, UI_ORANGE, 0); break; // 机器人(蓝)
    case 1: draw_face(parent, x, y, size, 0xFFB23E, UI_INK, UI_INK,     1); break; // 小猫(橙)
    case 2: draw_face(parent, x, y, size, UI_PAPER, UI_INK, UI_INK,     2); break; // 熊猫(白)
    case 3: draw_face(parent, x, y, size, 0x7557D9, UI_INK, 0xB9F3FF,   3); break; // 外星(紫)
    default: draw_face(parent, x, y, size, 0x4FC3F7, UI_INK, UI_ORANGE, 0); break;
    }
}

// ---------- 选择持久化 ----------
#define SEL_PATH "/spiffs/avatar_sel.txt"

int avatar_load_sel(void)
{
    int s = 0;
    FILE *f = fopen(SEL_PATH, "r");
    if (f) { (void)fscanf(f, "sel=%d", &s); fclose(f); }
    if (s < 0 || s > AVATAR_CUSTOM) s = 0;
    return s;
}

void avatar_save_sel(int sel)
{
    FILE *f = fopen(SEL_PATH, "w");
    if (f) { fprintf(f, "sel=%d\n", sel); fclose(f); }
}

// ---------- 自定义形象（PC 推送的 RGB565）----------
// 文件格式：8 字节头 "AV01" + uint16 LE 宽 + uint16 LE 高，其后为 w*h 个 RGB565 像素。
bool avatar_load_custom(uint8_t **out_buf, int *out_w, int *out_h)
{
    FILE *f = fopen("/spiffs/avatar.bin", "rb");
    if (!f) return false;
    uint8_t hdr[8];
    if (fread(hdr, 1, 8, f) != 8) { fclose(f); return false; }
    if (memcmp(hdr, "AV01", 4) != 0) { fclose(f); return false; }
    int w = (int)hdr[4] | ((int)hdr[5] << 8);
    int h = (int)hdr[6] | ((int)hdr[7] << 8);
    if (w <= 0 || h <= 0 || w > 320 || h > 320) { fclose(f); return false; }
    size_t need = (size_t)w * (size_t)h * 2;
    uint8_t *buf = (uint8_t *)malloc(need);
    if (!buf) { fclose(f); return false; }
    if (fread(buf, 1, need, f) != need) { free(buf); fclose(f); return false; }
    fclose(f);
    *out_buf = buf; *out_w = w; *out_h = h;
    return true;
}

void avatar_free(uint8_t *buf) { if (buf) free(buf); }

// ---------- 选择页 UI ----------
static lv_obj_t *s_scr;
static lv_obj_t *s_cards[AVATAR_PRESETS + 1];   // 预设 + 「我的形象」(自定义)
static lv_obj_t *s_rows[AVATAR_PRESETS + 1];
static lv_obj_t *s_hint;
static int s_sel;

static void refresh(void)
{
    int n = AVATAR_PRESETS + 1;
    int cur = avatar_load_sel();
    for (int i = 0; i < n; i++) {
        const char *name = (i == AVATAR_CUSTOM) ? "我的形象" : "预设形象";
        lv_label_set_text_fmt(s_rows[i], "%d. %s", i + 1, name);
        ui_pixel_set_selected(s_cards[i], i == s_sel, true);
    }
    if (s_hint) {
        lv_label_set_text_fmt(s_hint, "当前: %s",
                              cur == AVATAR_CUSTOM ? "我的形象(PC推送)" : "预设形象");
    }
}

static void build(void)
{
    int n = AVATAR_PRESETS + 1;
    s_sel = avatar_load_sel();
    s_scr = ui_pixel_screen_create("虚拟形象");

    // 上半部：每个形象一个预览卡片（2 列布局）
    for (int i = 0; i < n; i++) {
        int cx = 18 + (i % 2) * 110;
        int cy = 52 + (i / 2) * 86;
        s_cards[i] = ui_pixel_panel_create(s_scr, cx, cy, 100, 78, UI_PAPER);
        // 预览区
        if (i == AVATAR_CUSTOM) {
            // 占位方块（实际是否有文件在 key 时校验）
            block(s_cards[i], 34, 8, 32, 32, UI_MUTED);
        } else {
            avatar_draw_preset(s_cards[i], i, 34, 8, 32);
        }
        s_rows[i] = lv_label_create(s_cards[i]);
        lv_obj_set_style_text_font(s_rows[i], &font_zh14, 0);
        lv_obj_set_style_text_align(s_rows[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(s_rows[i], LV_ALIGN_BOTTOM_MID, 0, -4);
    }

    s_hint = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_hint, &font_zh14, 0);
    lv_obj_set_style_text_color(s_hint, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(s_hint, LV_ALIGN_BOTTOM_MID, 0, 70);
    refresh();
    lv_screen_load(s_scr);
}

void demo_avatar_enter(void) { build(); }

void demo_avatar_exit(void)
{
    if (s_scr) { lv_obj_delete(s_scr); s_scr = NULL; }
    for (int i = 0; i < AVATAR_PRESETS + 1; i++) s_cards[i] = s_rows[i] = NULL;
    s_hint = NULL;
}

void demo_avatar_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    int n = AVATAR_PRESETS + 1;
    if (ev != BSP_BTN_CLICK) return;
    if (btn == BSP_BTN_UP)   { s_sel = (s_sel + n - 1) % n; refresh(); }
    else if (btn == BSP_BTN_DOWN) { s_sel = (s_sel + 1) % n; refresh(); }
    else if (btn == BSP_BTN_OK) {
        if (s_sel == AVATAR_CUSTOM) {
            uint8_t *b; int w, h;
            if (!avatar_load_custom(&b, &w, &h)) {
                if (s_hint) lv_label_set_text(s_hint, "未找到 PC 推送形象,先用预设");
                return;
            }
            avatar_free(b);
        }
        avatar_save_sel(s_sel);
        if (s_hint) lv_label_set_text(s_hint, "已设为当前形象 ✓");
    }
}
