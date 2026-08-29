// main/demo_shengbei.c —— 掷圣杯:两枚筊杯随机掷出,判定 圣杯/笑杯/阴杯。
// OK 掷杯(带翻滚动画),再次 OK 重掷;长按确定由 main 统一返回菜单。
#include "demo.h"
#include "ui_pixel.h"
#include "font_zh.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static const char *TAG = "shengbei";

static lv_obj_t *s_scr;
static lv_obj_t *s_panel;
static lv_obj_t *s_result;
static lv_obj_t *s_flavor;
static lv_obj_t *s_hint;
static lv_obj_t *s_block[2];
static lv_obj_t *s_tag[2];
static int s_by[2];
static int s_face[2];      // 0=阴(平面朝上) 1=阳(弧面朝上)
static bool s_tossing;

static void set_hint(const char *t) { if (s_hint) lv_label_set_text(s_hint, t); }

static void reveal(lv_timer_t *timer)
{
    (void)timer;
    s_tossing = false;
    for (int i = 0; i < 2; i++) {
        uint32_t c = s_face[i] ? UI_SKY : UI_ORANGE;
        lv_obj_set_style_bg_color(s_block[i], lv_color_hex(c), 0);
        lv_obj_set_style_transform_angle(s_block[i], 0, 0);
        lv_obj_set_y(s_block[i], s_by[i]);
        lv_label_set_text(s_tag[i], s_face[i] ? "阳" : "阴");
        lv_obj_set_style_text_color(s_tag[i], lv_color_hex(c), 0);
    }

    const char *name, *flavor;
    uint32_t rc;
    if (s_face[0] != s_face[1]) {            // 一阴一阳 = 圣杯
        name = "圣杯 · 神明应允"; flavor = "所求皆如愿"; rc = UI_GRASS;
    } else if (s_face[0] == 1) {             // 双阳 = 笑杯
        name = "笑杯 · 神明含笑"; flavor = "再掷以问分明"; rc = UI_YELLOW;
    } else {                                 // 双阴 = 阴杯
        name = "阴杯 · 神明未允"; flavor = "且缓，再思量"; rc = UI_RED;
    }
    lv_label_set_text(s_result, name);
    lv_obj_set_style_text_color(s_result, lv_color_hex(rc), 0);
    lv_label_set_text(s_flavor, flavor);
    lv_obj_set_style_text_color(s_flavor, lv_color_hex(rc), 0);
    set_hint("OK 再掷 · 长按返回");
}

static void toss_cb0(void *var, int32_t v)
{
    (void)var;
    int y = s_by[0] - (v < 500 ? (500 - v) : (v - 500)) * 28 / 500;
    lv_obj_set_y(s_block[0], y);
    lv_obj_set_style_transform_angle(s_block[0], (int)(v * 720 / 1000), 0);
}

static void toss_cb1(void *var, int32_t v)
{
    (void)var;
    int y = s_by[1] - (v < 500 ? (500 - v) : (v - 500)) * 28 / 500;
    lv_obj_set_y(s_block[1], y);
    lv_obj_set_style_transform_angle(s_block[1], (int)(v * 720 / 1000), 0);
}

static void roll(void)
{
    s_tossing = true;
    s_face[0] = rand() & 1;
    s_face[1] = rand() & 1;
    // 掷出前复位为中性色,隐藏判定
    for (int i = 0; i < 2; i++) {
        lv_obj_set_style_bg_color(s_block[i], lv_color_hex(UI_PAPER), 0);
        lv_label_set_text(s_tag[i], "?");
        lv_obj_set_style_text_color(s_tag[i], lv_color_hex(UI_INK), 0);
    }
    lv_label_set_text(s_result, "掷杯中…");
    lv_obj_set_style_text_color(s_result, lv_color_hex(UI_INK), 0);
    lv_label_set_text(s_flavor, "");
    set_hint("");

    for (int i = 0; i < 2; i++) {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, (void *)(intptr_t)i);
        lv_anim_set_exec_cb(&a, i == 0 ? toss_cb0 : toss_cb1);
        lv_anim_set_values(&a, 0, 1000);
        lv_anim_set_duration(&a, 700);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_start(&a);
    }
    lv_timer_create(reveal, 760, NULL);
}

static void build(void)
{
    s_scr = ui_pixel_screen_create("掷圣杯");
    s_panel = ui_pixel_panel_create(s_scr, 18, 56, 204, 180, UI_PAPER);

    int cx = 120;
    int block_w = 44, block_h = 60;
    int rest_y = 150;
    int xs[2] = { cx - 52, cx + 52 };
    for (int i = 0; i < 2; i++) {
        s_by[i] = rest_y;
        lv_obj_t *b = lv_obj_create(s_panel);
        lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(b, block_w, block_h);
        lv_obj_align(b, LV_ALIGN_CENTER, xs[i] - cx, rest_y - 118);
        lv_obj_set_style_radius(b, 10, 0);
        lv_obj_set_style_border_width(b, 2, 0);
        lv_obj_set_style_border_color(b, lv_color_hex(UI_INK), 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(UI_PAPER), 0);
        lv_obj_set_style_pad_all(b, 0, 0);
        s_block[i] = b;

        lv_obj_t *t = lv_label_create(s_panel);
        lv_obj_set_style_text_font(t, &font_zh14, 0);
        lv_obj_set_style_text_color(t, lv_color_hex(UI_INK), 0);
        lv_label_set_text(t, "?");
        lv_obj_align(t, LV_ALIGN_CENTER, xs[i] - cx, rest_y - 118 - 40);
        s_tag[i] = t;
    }

    s_result = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_result, &font_zh14, 0);
    lv_obj_set_style_text_color(s_result, lv_color_hex(UI_INK), 0);
    lv_obj_set_style_text_align(s_result, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_result, 180);
    lv_obj_align(s_result, LV_ALIGN_CENTER, 0, 18);

    s_flavor = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_flavor, &font_zh14, 0);
    lv_obj_set_style_text_color(s_flavor, lv_color_hex(UI_INK), 0);
    lv_obj_set_style_text_align(s_flavor, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_flavor, 180);
    lv_obj_align(s_flavor, LV_ALIGN_CENTER, 0, 42);

    s_hint = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_hint, &font_zh14, 0);
    lv_obj_set_style_text_color(s_hint, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(s_hint, LV_ALIGN_BOTTOM_MID, 0, 70);
    set_hint("OK 掷杯 · 长按返回");

    lv_screen_load(s_scr);
}

void demo_shengbei_enter(void)
{
    s_tossing = false;
    s_face[0] = s_face[1] = 0;
    build();
}

void demo_shengbei_exit(void)
{
    if (s_scr) { lv_obj_delete(s_scr); s_scr = NULL; }
    s_panel = s_result = s_flavor = s_hint = NULL;
    for (int i = 0; i < 2; i++) s_block[i] = s_tag[i] = NULL;
}

void demo_shengbei_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (ev != BSP_BTN_CLICK) return;
    if (s_tossing) return;
    if (btn == BSP_BTN_OK) roll();
}
