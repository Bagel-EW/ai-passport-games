// main/app_settings.c —— 设置页:背景音乐 / 屏幕亮度 / 电池电量 / 返回。
//
// 上/下 移动选中项,OK 对可编辑项循环切换(立即生效并落盘),选「返回」或长按 OK 退出。
// 电量行是只读的,但每 3 秒自动刷新一次(也允许按 OK 手动刷新)。
#include "app_settings.h"

#include "app_bgm.h"
#include "bsp_battery.h"
#include "bsp_display.h"
#include "esp_log.h"
#include "font_zh.h"
#include "lvgl.h"
#include "ui_pixel.h"

#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "settings";

#define ROW_N   5
#define ROW_X   18
#define ROW_W   204
#define ROW_H   38
#define ROW_Y0  54
#define ROW_GAP 8

enum { ROW_BGM = 0, ROW_VOL, ROW_BL, ROW_BATT, ROW_BACK };

static const char *k_names[ROW_N] = { "背景音乐", "音量", "屏幕亮度", "电池电量", "返回" };

// 背光档位(与 demo_display.c 的档位表一致)
static const uint8_t BL_LEVELS[] = { 100, 60, 30, 10 };
#define BL_COUNT ((int)(sizeof(BL_LEVELS) / sizeof(BL_LEVELS[0])))

// 亮度档位存 SPIFFS(不引 nvs_flash 依赖)
#define BL_PATH "/spiffs/bl_idx"

// 音量档位(0..100,OK 循环切换,立即生效并保存)
static const uint8_t VOL_LEVELS[] = { 30, 45, 60, 75, 90 };
#define VOL_COUNT ((int)(sizeof(VOL_LEVELS) / sizeof(VOL_LEVELS[0])))

static lv_obj_t *s_scr;
static lv_obj_t *s_row[ROW_N];
static lv_obj_t *s_name[ROW_N];
static lv_obj_t *s_val[ROW_N];
static lv_timer_t *s_poll;
static int s_sel;
static int s_bl_idx;
static int s_vol_idx;
static bool s_batt_ok;

// ---------- 亮度 ----------
static int load_bl(void)
{
    int v = 0;
    FILE *f = fopen(BL_PATH, "r");
    if (f) {
        char b[8] = {0};
        if (fread(b, 1, sizeof(b) - 1, f) > 0) v = atoi(b);
        fclose(f);
    }
    if (v < 0 || v >= BL_COUNT) v = 0;
    return v;
}

static void save_bl(int idx)
{
    FILE *f = fopen(BL_PATH, "w");
    if (f) { fprintf(f, "%d", idx); fclose(f); }
}

// 当前音量 → 最近的档位
static int vol_to_idx(int pct)
{
    int best = 0, bd = 999;
    for (int i = 0; i < VOL_COUNT; i++) {
        int d = (pct > VOL_LEVELS[i]) ? (pct - VOL_LEVELS[i]) : (VOL_LEVELS[i] - pct);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

// ---------- 电量 ----------
// CW2017 没烧电池 profile 时 bsp_battery_soc() 会返回 -1,
// 此时用电压线性估算兜底(3000mV=0%, 4200mV=100%),只是粗估不是真 SOC。
static int read_batt(void)
{
    int soc = bsp_battery_soc();
    if (soc >= 0) { s_batt_ok = true; return soc; }
    int mv = bsp_battery_mv();
    if (mv >= 3000 && mv <= 4300) {
        int p = (mv - 3000) * 100 / 1200;
        if (p > 100) p = 100;
        s_batt_ok = true;
        return p;
    }
    s_batt_ok = false;
    return -1;
}

// ---------- 渲染 ----------
static void refresh_rows(void)
{
    for (int i = 0; i < ROW_N; i++) {
        bool enabled = true;
        if (i == ROW_BGM) enabled = app_bgm_ready();
        ui_pixel_set_selected(s_row[i], i == s_sel, enabled);

        uint32_t col = enabled ? UI_INK : 0x7A2020;
        lv_obj_set_style_text_color(s_name[i], lv_color_hex(col), 0);
        lv_obj_set_style_text_color(s_val[i],  lv_color_hex(col), 0);

        switch (i) {
        case ROW_BGM:
            if (!app_bgm_ready())      lv_label_set_text(s_val[i], "不可用");
            else if (app_bgm_enabled()) lv_label_set_text(s_val[i], "开");
            else                        lv_label_set_text(s_val[i], "关");
            break;
        case ROW_VOL:
            lv_label_set_text_fmt(s_val[i], "%d%%", VOL_LEVELS[s_vol_idx]);
            break;
        case ROW_BL:
            lv_label_set_text_fmt(s_val[i], "%d%%", BL_LEVELS[s_bl_idx]);
            break;
        case ROW_BATT: {
            int p = read_batt();
            if (p >= 0) lv_label_set_text_fmt(s_val[i], "%d%%", p);
            else        lv_label_set_text(s_val[i], "无数据");
            break;
        }
        default:
            lv_label_set_text(s_val[i], "");
            break;
        }
    }
}

static void poll_cb(lv_timer_t *tm)
{
    (void)tm;
    if (s_scr) refresh_rows();
}

// ---------- 对外接口 ----------
void app_settings_enter(void)
{
    s_sel = 0;
    s_bl_idx = load_bl();
    s_vol_idx = vol_to_idx(app_bgm_volume());

    s_scr = ui_pixel_screen_create("设置");

    for (int i = 0; i < ROW_N; i++) {
        int y = ROW_Y0 + i * (ROW_H + ROW_GAP);
        s_row[i] = ui_pixel_panel_create(s_scr, ROW_X, y, ROW_W, ROW_H, UI_PAPER);

        s_name[i] = ui_pixel_label(s_row[i], k_names[i], &font_zh14, UI_INK);
        lv_obj_set_pos(s_name[i], 6, 4);

        s_val[i] = ui_pixel_label(s_row[i], "", &font_zh14, UI_INK);
        lv_obj_align(s_val[i], LV_ALIGN_TOP_RIGHT, -6, 4);
    }

    refresh_rows();
    lv_screen_load(s_scr);

    s_poll = lv_timer_create(poll_cb, 3000, NULL);
    ESP_LOGI(TAG, "settings enter bl=%d%% vol=%d%% bgm=%d ready=%d",
             BL_LEVELS[s_bl_idx], VOL_LEVELS[s_vol_idx],
             (int)app_bgm_enabled(), (int)app_bgm_ready());
}

void app_settings_exit(void)
{
    if (s_poll) { lv_timer_delete(s_poll); s_poll = NULL; }
    if (s_scr)  { lv_obj_delete(s_scr);    s_scr  = NULL; }
    for (int i = 0; i < ROW_N; i++) { s_row[i] = s_name[i] = s_val[i] = NULL; }
}

bool app_settings_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (!s_scr) return true;

    // ⚠ 一次物理按键会先后发 PRESS(按下)和 CLICK(松开)两个事件。
    //   方向键只认 PRESS、OK 只认 CLICK,否则按一下会走两步。
    if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
        if (ev != BSP_BTN_PRESS) return false;
        int step = (btn == BSP_BTN_UP) ? -1 : 1;
        s_sel = (s_sel + step + ROW_N) % ROW_N;
        ESP_LOGI(TAG, "sel -> %d (%s)", s_sel, (btn == BSP_BTN_UP) ? "UP" : "DOWN");
        refresh_rows();
        return false;
    }

    if (btn != BSP_BTN_OK || ev != BSP_BTN_CLICK) return false;

    switch (s_sel) {
    case ROW_BGM:
        if (app_bgm_ready()) app_bgm_set_enabled(!app_bgm_enabled());
        refresh_rows();
        return false;
    case ROW_VOL:
        s_vol_idx = (s_vol_idx + 1) % VOL_COUNT;
        app_bgm_set_volume(VOL_LEVELS[s_vol_idx]);
        refresh_rows();
        return false;
    case ROW_BL:
        s_bl_idx = (s_bl_idx + 1) % BL_COUNT;
        bsp_display_backlight(BL_LEVELS[s_bl_idx]);
        save_bl(s_bl_idx);
        refresh_rows();
        return false;
    case ROW_BATT:
        refresh_rows();          // 手动刷新一次
        return false;
    default:
        return true;             // 返回 → 退出设置页
    }
}
