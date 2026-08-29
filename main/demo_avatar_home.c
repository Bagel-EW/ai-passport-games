// main/demo_avatar_home.c —— 开机「虚拟形象 Home」页（NEW FILE, Phase 3 草稿）
//
// 展示当前形象（自定义 RGB565 / 内置预设）+ 简短状态（天气可接 weather.txt，电量待接）。
// 提供「更换形象」入口：OK 直接跳到 demo_avatar 选择页。
// 长按 OK 由 main.c 统一拦截回菜单。
#include "demo_avatar.h"
#include "ui_pixel.h"
#include "font_zh.h"
#include "lvgl.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "avatar_home";

static lv_obj_t *s_scr;
static lv_obj_t *s_hint;
static uint8_t  *s_custom_buf;   // 若为自定义形象，动态分配，exit 时释放

// 读 weather.txt（与 demo_weather.c 同字段），仅在存在时回显一行状态
static void fill_status(lv_obj_t *panel)
{
    char city[24] = "—", cond[16] = "—";
    int temp = 0, humi = 0;
    FILE *f = fopen("/spiffs/weather.txt", "r");
    bool has = false;
    if (f) {
        char line[64];
        while (fgets(line, sizeof(line), f)) {
            if (sscanf(line, "city=%23s", city) == 1) has = true;
            else if (sscanf(line, "cond=%15s", cond) == 1) {}
            else if (sscanf(line, "temp=%d", &temp) == 1) {}
            else if (sscanf(line, "humi=%d", &humi) == 1) {}
        }
        fclose(f);
    }
    char buf[64];
    if (has) snprintf(buf, sizeof(buf), "天气 %s %s %d°  湿度%d%%", city, cond, temp, humi);
    else     snprintf(buf, sizeof(buf), "天气 示例数据 · 待 PC 推送");

    lv_obj_t *meta = lv_label_create(panel);
    lv_obj_set_style_text_font(meta, &font_zh14, 0);
    lv_obj_set_style_text_color(meta, lv_color_hex(UI_INK), 0);
    lv_label_set_text(meta, buf);
    lv_obj_align(meta, LV_ALIGN_BOTTOM_MID, 0, 8);
}

static void build(void)
{
    s_custom_buf = NULL;
    s_scr = ui_pixel_screen_create("我的通行证");

    // 形象展示卡片
    lv_obj_t *panel = ui_pixel_panel_create(s_scr, 50, 60, 140, 160, UI_PAPER);
    int sel = avatar_load_sel();

    if (sel == AVATAR_CUSTOM) {
        int w, h;
        if (avatar_load_custom(&s_custom_buf, &w, &h)) {
            static lv_image_dsc_t dsc;   // 仅本屏生命周期内有效
            dsc.header.w = (uint16_t)w;
            dsc.header.h = (uint16_t)h;
            dsc.header.cf = LV_COLOR_FORMAT_NATIVE;
            dsc.data = s_custom_buf;
            dsc.data_size = (uint32_t)((size_t)w * h * 2);
            lv_obj_t *img = lv_image_create(panel);
            lv_image_set_src(img, &dsc);
            lv_obj_center(img);
        } else {
            // 无自定义文件：兜底画 0 号预设
            avatar_draw_preset(panel, 0, 54, 16, 80);
        }
    } else {
        avatar_draw_preset(panel, sel, 54, 16, 80);
    }

    // 昵称 + 状态
    lv_obj_t *name = lv_label_create(s_scr);
    lv_obj_set_style_text_font(name, &font_zh20, 0);
    lv_obj_set_style_text_color(name, lv_color_hex(UI_INK), 0);
    lv_label_set_text(name, "AI 通行证");
    lv_obj_align(name, LV_ALIGN_CENTER, 0, 232);

    fill_status(s_scr);

    s_hint = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_hint, &font_zh14, 0);
    lv_obj_set_style_text_color(s_hint, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(s_hint, LV_ALIGN_BOTTOM_MID, 0, 70);
    lv_label_set_text(s_hint, "OK 更换形象 · 长按返回");

    lv_screen_load(s_scr);
}

void demo_avatar_home_enter(void) { build(); }

void demo_avatar_home_exit(void)
{
    if (s_scr) { lv_obj_delete(s_scr); s_scr = NULL; }
    if (s_custom_buf) { avatar_free(s_custom_buf); s_custom_buf = NULL; }
    s_hint = NULL;
}

void demo_avatar_home_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (ev != BSP_BTN_CLICK) return;
    if (btn == BSP_BTN_OK) {
        // 直接进入形象选择页（先退出本屏，避免双屏叠加）
        demo_avatar_home_exit();
        demo_avatar_enter();
    }
    // 上/下 无操作（如需可在此触发其它快捷）
}
