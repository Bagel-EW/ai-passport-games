// main/demo_weather.c —— 今日天气:读取 /spiffs/weather.txt(SPIFFS,PC 推送覆盖),
// 无文件时显示内置示例。v1 走 PC 推送(与形象生成同一 USB 管道),后续可换设备直连天气 API。
#include "demo.h"
#include "ui_pixel.h"
#include "font_zh.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "weather";

typedef struct {
    char city[24];
    char cond[16];
    int  temp;
    int  humi;
    char wind[12];
    char date[12];
    bool pushed;     // true=来自 SPIFFS 推送,false=内置示例
} weather_t;

static lv_obj_t *s_scr;
static lv_obj_t *s_panel;
static lv_obj_t *s_disc;
static lv_obj_t *s_hint;

static void set_hint(const char *t) { if (s_hint) lv_label_set_text(s_hint, t); }

static uint32_t cond_color(const char *cond)
{
    if (strstr(cond, "雨")) return UI_SKY;
    if (strstr(cond, "雪")) return UI_MUTED;
    if (strstr(cond, "云")) return UI_MUTED;
    if (strstr(cond, "雷")) return UI_SKY_DARK;
    return UI_YELLOW;   // 晴 / 默认
}

static void load(weather_t *w)
{
    // 内置示例
    snprintf(w->city, sizeof(w->city), "上海");
    snprintf(w->cond, sizeof(w->cond), "晴");
    w->temp = 26; w->humi = 60;
    snprintf(w->wind, sizeof(w->wind), "3级");
    snprintf(w->date, sizeof(w->date), "08-26");
    w->pushed = false;

    FILE *f = fopen("/spiffs/weather.txt", "r");
    if (!f) return;
    char line[64];
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "city=%23s", w->city) == 1) {}
        else if (sscanf(line, "cond=%15s", w->cond) == 1) {}
        else if (sscanf(line, "temp=%d", &w->temp) == 1) {}
        else if (sscanf(line, "humi=%d", &w->humi) == 1) {}
        else if (sscanf(line, "wind=%11s", w->wind) == 1) {}
        else if (sscanf(line, "date=%11s", w->date) == 1) {}
    }
    fclose(f);
    w->pushed = true;
}

static void build(void)
{
    weather_t w;
    load(&w);

    s_scr = ui_pixel_screen_create("今日天气");
    s_panel = ui_pixel_panel_create(s_scr, 18, 56, 204, 180, UI_PAPER);

    // 城市
    lv_obj_t *city = lv_label_create(s_panel);
    lv_obj_set_style_text_font(city, &font_zh14, 0);   // 用 zh14 全集,防 PC 推送任意城市名缺字
    lv_obj_set_style_text_color(city, lv_color_hex(UI_INK), 0);
    lv_label_set_text(city, w.city);
    lv_obj_align(city, LV_ALIGN_CENTER, 0, -54);

    // 天气图标(圆盘,按天气着色)
    s_disc = lv_obj_create(s_panel);
    lv_obj_remove_flag(s_disc, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_disc, 56, 56);
    lv_obj_align(s_disc, LV_ALIGN_CENTER, 0, -10);
    lv_obj_set_style_radius(s_disc, 28, 0);
    lv_obj_set_style_border_width(s_disc, 0, 0);
    lv_obj_set_style_bg_color(s_disc, lv_color_hex(cond_color(w.cond)), 0);
    lv_obj_set_style_pad_all(s_disc, 0, 0);

    // 温度(大字)
    lv_obj_t *temp = lv_label_create(s_panel);
    lv_obj_set_style_text_font(temp, &font_game, 0);
    lv_obj_set_style_text_color(temp, lv_color_hex(UI_RED), 0);
    lv_obj_align(temp, LV_ALIGN_CENTER, 0, 26);

    // 天气状况
    lv_obj_t *cond = lv_label_create(s_panel);
    lv_obj_set_style_text_font(cond, &font_zh14, 0);
    lv_obj_set_style_text_color(cond, lv_color_hex(UI_INK), 0);
    lv_obj_align(cond, LV_ALIGN_CENTER, 0, 58);

    // 湿度/风力/日期
    lv_obj_t *meta = lv_label_create(s_panel);
    lv_obj_set_style_text_font(meta, &font_zh14, 0);
    lv_obj_set_style_text_color(meta, lv_color_hex(UI_INK), 0);
    lv_obj_align(meta, LV_ALIGN_CENTER, 0, 80);

    char buf[64];
    lv_label_set_text_fmt(temp, "%d°C", w.temp);
    lv_label_set_text(cond, w.cond);
    snprintf(buf, sizeof(buf), "湿度 %d%%  风力 %s", w.humi, w.wind);
    lv_label_set_text(meta, buf);

    s_hint = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_hint, &font_zh14, 0);
    lv_obj_set_style_text_color(s_hint, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(s_hint, LV_ALIGN_BOTTOM_MID, 0, 70);
    set_hint(w.pushed ? "数据:PC 推送 · 长按返回" : "示例数据 · 长按返回");

    lv_screen_load(s_scr);
}

void demo_weather_enter(void)
{
    build();
}

void demo_weather_exit(void)
{
    if (s_scr) { lv_obj_delete(s_scr); s_scr = NULL; }
    s_panel = s_disc = s_hint = NULL;
}

void demo_weather_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    (void)btn;
    (void)ev;
    // 纯展示页,无按键交互(长按返回由 main 处理)
}
