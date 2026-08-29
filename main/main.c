// main/main.c —— AI Passport 玩法固件入口。
// 导航:开机动画(赛博终端) → 开机动效 Title → OK → 主选单(多人游戏|单人游戏)
//      → 单人游戏:子菜单(打砖块/极速反应/翻牌配对) → 选中即玩。
// 多人游戏直接进入派对(含说明+人数页)。
// 长按确定(=返回)在当前子菜单/游戏内逐级返回。
#include "bsp_i2c.h"
#include "bsp_display.h"
#include "bsp_button.h"
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_pins.h"
#include "demo.h"
#include "ui_pixel.h"
#include "app_boot.h"
#include "app_bgm.h"
#include "app_settings.h"
#include "font_zh.h"
#include "fap_screenshot.h"
#include "lvgl.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include <string.h>

static const char *TAG = "main";

// 运行时可进入的游戏(供 nav 索引)
enum { GID_PARTY = 0, GID_BREAKOUT, GID_SNAKE, GID_MEMORY, GID_N };

// 派对
static void run_entry(const demo_entry_t *e);

static const demo_entry_t s_games[GID_N] = {
    { "多人游戏", demo_party_enter, demo_party_exit, demo_party_key },
    { "打砖块",   demo_breakout_enter, demo_breakout_exit, demo_breakout_key },
    { "贪吃蛇", demo_snake_enter,   demo_snake_exit,   demo_snake_key },
    { "翻牌配对", demo_memory_enter,  demo_memory_exit,  demo_memory_key },
};

// 主选单项:索引 0=多人 → 直接进游戏;1=单人 → 进子菜单;2=设置 → 进设置页
#define MAIN_COUNT 3
static const char *MAIN_NAMES[MAIN_COUNT] = { "多人游戏", "单人游戏", "设置" };

// 单人子菜单项 → 对应 GID;最后一项是「返回」(回主菜单),不对应游戏
static const int SINGLE_IDS[3] = { GID_BREAKOUT, GID_SNAKE, GID_MEMORY };
#define SINGLE_COUNT 4
static const char *SINGLE_NAMES[SINGLE_COUNT] = { "打砖块", "贪吃蛇", "翻牌配对", "返回" };

static bool s_btn_ok;                    // 按键是否可用

typedef enum { NAV_TITLE, NAV_MAIN, NAV_SINGLE, NAV_SETTINGS, NAV_PLAY } nav_t;
static nav_t s_nav = NAV_TITLE;
static int  s_sel;                       // 当前层选中项
static int  s_active = -1;               // NAV_PLAY 时正在玩的 GID

static lv_obj_t *s_scr;                  // 当前导航屏
static lv_obj_t *s_cards[6];
static lv_obj_t *s_rows[6];
static lv_obj_t *s_mascot;
static lv_timer_t *s_title_tm;
static int  s_title_frame;

// ---------- 像素块 helper ----------
static lv_obj_t *blk(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    return o;
}

static void nav_del_scr(void)
{
    if (s_scr) { lv_obj_delete(s_scr); s_scr = NULL; }
    for (int i = 0; i < 6; i++) s_cards[i] = s_rows[i] = NULL;
    s_mascot = NULL;
}

// ---------- 通用两列卡片菜单 ----------
static void nav_refresh(int count, bool can_fail);

static void menu_build(const char *title, const char *names[], int count, bool can_fail)
{
    nav_del_scr();
    s_scr = ui_pixel_screen_create(title);
    // 双列排布,卡片尺寸自动匹配
    int cols = 2;
    int cw = 100, ch = 46, gx = 4, gy = 14;
    int total_w = cols * cw + (cols - 1) * gx;
    int x0 = (240 - total_w) / 2;
    int y0 = 60;
    for (int i = 0; i < count; i++) {
        int x = x0 + (i % cols) * (cw + gx);
        int y = y0 + (i / cols) * (ch + gy);
        s_cards[i] = ui_pixel_panel_create(s_scr, x, y, cw, ch, UI_PAPER);
        s_rows[i] = lv_label_create(s_cards[i]);
        lv_obj_set_style_text_font(s_rows[i], &font_zh14, 0);
        lv_obj_set_style_text_align(s_rows[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(s_rows[i]);
        lv_label_set_text(s_rows[i], names[i]);
    }
    s_mascot = ui_pixel_mascot_create(s_scr, 101, 286);
    lv_screen_load(s_scr);
    nav_refresh(count, can_fail);
}

static void nav_refresh(int count, bool can_fail)
{
    for (int i = 0; i < count; i++) {
        bool ok = can_fail ? s_btn_ok : true;
        ui_pixel_set_selected(s_cards[i], i == s_sel, ok);
        if (ok) {
            lv_obj_set_style_text_color(s_rows[i], lv_color_hex(UI_INK), 0);
        } else {
            lv_obj_set_style_text_color(s_rows[i], lv_color_hex(0x7A2020), 0);
        }
    }
}

// ---------- 主选单(多人|单人) ----------
static void main_build(void)
{
    s_nav = NAV_MAIN;
    s_sel = 0;
    menu_build("AI 通行证", MAIN_NAMES, MAIN_COUNT, false);
    nav_refresh(MAIN_COUNT, false);
}

static void single_build(void)
{
    s_nav = NAV_SINGLE;
    s_sel = 0;
    menu_build("单人游戏", SINGLE_NAMES, SINGLE_COUNT, true);
    nav_refresh(SINGLE_COUNT, true);
}

static void run_entry(const demo_entry_t *e)
{
    s_nav = NAV_PLAY;
    s_active = (int)(e - s_games);
    nav_del_scr();
    e->enter();
}

// ---------- 开机动效 Title ----------
// 像素球跳跃 + "READY? GO!" 字样,OK 进入主选单。
static void title_anim(lv_timer_t *t)
{
    s_title_frame += 2;
    if (s_title_frame >= 2000) s_title_frame = 0;
    int y = 150 + (s_title_frame < 1000 ? s_title_frame / 12 : (2000 - s_title_frame) / 12);
    lv_obj_t *ball = (lv_obj_t *)lv_timer_get_user_data(t);
    if (!ball) return;
    lv_obj_set_y(ball, y);
}

static void title_build(void)
{
    s_nav = NAV_TITLE;
    nav_del_scr();
    s_scr = lv_obj_create(NULL);
    lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(0x0A1E34), 0);   // 夜空深蓝
    lv_obj_set_style_border_width(s_scr, 0, 0);
    lv_obj_set_style_pad_all(s_scr, 0, 0);

    // 顶部霓虹灯串
    static const uint32_t bulb[10] = {
        UI_YELLOW, 0x7C5CFF, 0x00FFCE, UI_ORANGE, 0xFFFFFF,
        UI_YELLOW, 0x7C5CFF, 0x00FFCE, UI_ORANGE, 0xFFFFFF };
    for (int i = 0; i < 10; i++) {
        lv_obj_t *b = blk(s_scr, 8 + i * 30, 12, 10, 10, bulb[i]);
        lv_obj_set_style_radius(b, 5, 0);
    }

    // 星点
    lv_obj_t *s1 = blk(s_scr, 30, 118, 4, 4, 0xFFFFFF);  lv_obj_set_style_radius(s1, 2, 0);
    lv_obj_t *s2 = blk(s_scr, 206, 132, 4, 4, 0xFFFFFF); lv_obj_set_style_radius(s2, 2, 0);
    lv_obj_t *s3 = blk(s_scr, 196, 62, 3, 3, 0xFFFFFF);  lv_obj_set_style_radius(s3, 2, 0);
    lv_obj_t *s4 = blk(s_scr, 40, 196, 3, 3, 0xFFFFFF);  lv_obj_set_style_radius(s4, 2, 0);

    // 霓虹游戏厅招牌
    blk(s_scr, 16, 34, 208, 68, 0x0E3A2E);
    blk(s_scr, 16, 34, 208, 3, UI_YELLOW);
    blk(s_scr, 16, 99, 208, 3, 0x7C5CFF);

    // 大标题
    lv_obj_t *t0 = ui_pixel_label(s_scr, "欢乐游戏厅", &font_zh20, 0xFFFFFF);
    lv_obj_align(t0, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_t *t1 = ui_pixel_label(s_scr, "PLAY · PARTY", &font_zh14, UI_YELLOW);
    lv_obj_align(t1, LV_ALIGN_TOP_MID, 0, 112);

    // 跳跃像素球(动效)
    lv_obj_t *ball = blk(s_scr, 110, 150, 20, 20, UI_RED);
    lv_obj_set_style_radius(ball, 4, 0);
    s_title_tm = lv_timer_create(title_anim, 30, NULL);
    lv_timer_set_user_data(s_title_tm, ball);

    // 草地
    blk(s_scr, 0, 286, 240, 34, UI_GRASS);
    blk(s_scr, 0, 286, 240, 4, 0xA7D93E);
    s_mascot = ui_pixel_mascot_create(s_scr, 101, 236);

    lv_obj_t *tip = ui_pixel_label(s_scr, "OK 开始", &font_zh14, 0xFFFFFF);
    lv_obj_align(tip, LV_ALIGN_BOTTOM_MID, 0, -14);

    lv_screen_load(s_scr);
}

// 回到上一层导航
static void nav_back(void)
{
    if (s_title_tm) { lv_timer_del(s_title_tm); s_title_tm = NULL; }
    if (s_nav == NAV_SINGLE)   { main_build(); }
    else if (s_nav == NAV_SETTINGS) { main_build(); }
    else if (s_nav == NAV_PLAY) {
        // 游戏内长按返回 → 回到其来源层
        int from = (s_active == GID_PARTY) ? -1 : 1;
        s_active = -1;
        if (from < 0) main_build();
        else single_build();
    } else {
        main_build();
    }
}

// ---------- 按键回调 ----------
static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user)
{
    (void)user;
    if (!bsp_lvgl_lock(500)) return;

    // 设置页:自己管屏,长按 OK 或选中「返回」都退回主菜单
    if (s_nav == NAV_SETTINGS) {
        if (btn == BSP_BTN_OK && ev == BSP_BTN_LONG) {
            app_settings_exit();
            nav_back();
        } else if (app_settings_key(btn, ev)) {
            app_settings_exit();
            nav_back();
        }
        bsp_lvgl_unlock();
        return;
    }

    // 游戏运行中:长按OK返回对应层,其余交游戏
    if (s_nav == NAV_PLAY) {
        if (btn == BSP_BTN_OK && ev == BSP_BTN_LONG) {
            s_games[s_active].exit();
            nav_back();
        } else {
            s_games[s_active].key(btn, ev);
        }
        bsp_lvgl_unlock();
        return;
    }

    // 单人游戏子菜单:长按 OK 直接回主菜单(与设置页、游戏内一致)
    if (s_nav == NAV_SINGLE && btn == BSP_BTN_OK && ev == BSP_BTN_LONG) {
        nav_back();
        bsp_lvgl_unlock();
        return;
    }

    if (ev != BSP_BTN_CLICK) { bsp_lvgl_unlock(); return; }

    if (s_nav == NAV_TITLE) {
        if (btn == BSP_BTN_OK) { if (s_title_tm) { lv_timer_del(s_title_tm); s_title_tm = NULL; } main_build(); }
        else ui_pixel_mascot_jump(s_mascot);
        bsp_lvgl_unlock();
        return;
    }

    // 菜单层:上/下移动,OK 进入
    int count = (s_nav == NAV_MAIN) ? MAIN_COUNT : SINGLE_COUNT;
    if (btn == BSP_BTN_UP) {
        s_sel = (s_sel + count - 1) % count;
        nav_refresh(count, s_nav == NAV_MAIN ? false : true);
        ui_pixel_mascot_jump(s_mascot);
    } else if (btn == BSP_BTN_DOWN) {
        s_sel = (s_sel + 1) % count;
        nav_refresh(count, s_nav == NAV_MAIN ? false : true);
        ui_pixel_mascot_jump(s_mascot);
    } else if (btn == BSP_BTN_OK) {
        if (s_nav == NAV_MAIN) {
            if (s_sel == 0)      run_entry(&s_games[GID_PARTY]);
            else if (s_sel == 1) single_build();
            else {
                s_nav = NAV_SETTINGS;
                app_settings_enter();
            }
        } else { // NAV_SINGLE
            if (s_sel == SINGLE_COUNT - 1) { nav_back(); bsp_lvgl_unlock(); return; }   // 返回
            if (!s_btn_ok) { bsp_lvgl_unlock(); return; }
            run_entry(&s_games[SINGLE_IDS[s_sel]]);
        }
    }
    bsp_lvgl_unlock();
}

// ---------- 开机链路 ----------
// 供各游戏/设置页内部主动要求返回上一层(main.c 统一处理退出与重建)。
// 必须在 LVGL 锁内调用(即来自按键回调 / LVGL 定时器)。
void main_request_back(void)
{
    int from = (s_active == GID_PARTY) ? -1 : 1;
    int a = s_active;
    s_active = -1;
    if (a >= 0) s_games[a].exit();
    if (from < 0) main_build();
    else single_build();
}

// 自动导航:仅用于无人值守验证(改 1 重新烧录,设备会自己走一遍全部导航路径,无需按键)。
// 平时必须为 0 —— 为 1 时开机不进菜单,会自己到处点。
#define NAV_AUTOPILOT 0
static bool s_boot_done;
#if NAV_AUTOPILOT
static lv_timer_t *s_nav_auto_tm;
static int s_nav_auto_step;

// 模拟一次完整的物理按键:驱动会先后发 PRESS(按下) 和 CLICK(松开) 两个事件。
static void auto_press(bsp_btn_t btn)
{
    on_key(btn, BSP_BTN_PRESS, NULL);
    on_key(btn, BSP_BTN_CLICK, NULL);
}

static void nav_auto_cb(lv_timer_t *tm)
{
    (void)tm;
    switch (s_nav_auto_step) {
    case 0:
        if (!s_boot_done || s_nav != NAV_TITLE) return;
        ESP_LOGI(TAG, "[auto] T=0 开机完成");
        s_nav_auto_step = 1; break;

    // ---- 主菜单:0=多人 1=单人 2=设置 ----
    case 1:
        auto_press(BSP_BTN_OK);                       // Title → 主菜单
        ESP_LOGI(TAG, "[auto] T=1 OK -> nav=%d (期望 %d=MAIN)", (int)s_nav, (int)NAV_MAIN);
        s_nav_auto_step = 2; break;
    case 2:
        auto_press(BSP_BTN_DOWN);                     // 0 → 1(单人游戏)
        ESP_LOGI(TAG, "[auto] T=2 DOWN -> sel=%d (期望 1)", s_sel);
        s_nav_auto_step = 3; break;
    case 3:
        auto_press(BSP_BTN_OK);                       // 进单人游戏子菜单
        ESP_LOGI(TAG, "[auto] T=3 OK -> nav=%d (期望 %d=SINGLE)", (int)s_nav, (int)NAV_SINGLE);
        s_nav_auto_step = 4; break;

    // ---- 单人游戏子菜单返回项:0..2=游戏 3=返回 ----
    case 4:
        s_sel = 0; nav_refresh(SINGLE_COUNT, true);
        auto_press(BSP_BTN_UP);                       // 0 → 3(返回)
        ESP_LOGI(TAG, "[auto] T=4 UP -> sel=%d (期望 3=返回)", s_sel);
        s_nav_auto_step = 5; break;
    case 5:
        auto_press(BSP_BTN_OK);                       // 触发返回 → 主菜单
        ESP_LOGI(TAG, "[auto] T=5 OK(返回) -> nav=%d (期望 %d=MAIN)", (int)s_nav, (int)NAV_MAIN);
        s_nav_auto_step = 6; break;

    // ---- 进设置页 ----
    case 6:
        s_sel = 0; nav_refresh(MAIN_COUNT, false);
        auto_press(BSP_BTN_DOWN);
        auto_press(BSP_BTN_DOWN);                     // 0 → 2(设置)
        ESP_LOGI(TAG, "[auto] T=6 DOWN x2 -> sel=%d (期望 2=设置)", s_sel);
        s_nav_auto_step = 7; break;
    case 7:
        auto_press(BSP_BTN_OK);
        ESP_LOGI(TAG, "[auto] T=7 OK -> nav=%d (期望 %d=SETTINGS)", (int)s_nav, (int)NAV_SETTINGS);
        s_nav_auto_step = 8; break;

    // ---- 核心回归:一次物理按键只能走一步(下面 sel 应 1 → 2 → 1)----
    case 8:
        ESP_LOGI(TAG, "[auto] T=8 开始按 DOWN(一次物理按键)");
        auto_press(BSP_BTN_DOWN);
        ESP_LOGI(TAG, "[auto] T=8 结果 nav=%d (期望 %d=SETTINGS)", (int)s_nav, (int)NAV_SETTINGS);
        s_nav_auto_step = 9; break;
    case 9:
        ESP_LOGI(TAG, "[auto] T=9 再按 DOWN");
        auto_press(BSP_BTN_DOWN);
        s_nav_auto_step = 10; break;
    case 10:
        ESP_LOGI(TAG, "[auto] T=10 按 UP");
        auto_press(BSP_BTN_UP);
        s_nav_auto_step = 11; break;
    case 11:
        on_key(BSP_BTN_OK, BSP_BTN_LONG, NULL);       // 长按返回
        ESP_LOGI(TAG, "[auto] T=11 长按OK -> nav=%d (期望 %d=MAIN)", (int)s_nav, (int)NAV_MAIN);
        s_nav_auto_step = 12; break;

    // ---- 派对菜单的「返回」项 ----
    case 12:
        s_sel = 0; nav_refresh(MAIN_COUNT, false);
        auto_press(BSP_BTN_OK);                       // 进多人游戏(派对 intro)
        ESP_LOGI(TAG, "[auto] T=12 OK -> nav=%d (期望 %d=PLAY)", (int)s_nav, (int)NAV_PLAY);
        s_nav_auto_step = 13; break;
    case 13:
        auto_press(BSP_BTN_OK);                       // intro → 派对菜单
        s_nav_auto_step = 14; break;
    case 14:
        auto_press(BSP_BTN_UP);                       // 派对菜单 0 → 末项(返回)
        s_nav_auto_step = 15; break;
    case 15:
        auto_press(BSP_BTN_OK);                       // 触发派对返回 → 主菜单
        ESP_LOGI(TAG, "[auto] T=15 派对返回 -> nav=%d (期望 %d=MAIN)", (int)s_nav, (int)NAV_MAIN);
        s_nav_auto_step = 16; break;

    default:
        ESP_LOGI(TAG, "[auto] 导航回归测试结束(最终 nav=%d, heap=%d)",
                 (int)s_nav, (int)esp_get_free_heap_size());
        if (s_nav_auto_tm) { lv_timer_del(s_nav_auto_tm); s_nav_auto_tm = NULL; }
        break;
    }
}
#endif

static void boot_done(void) { title_build(); s_boot_done = true; }

void app_main(void)
{
    ESP_LOGI(TAG, "AI Passport 玩法固件(派对·单人)启动");

    bsp_i2c_init();
    bsp_i2c_scan();

    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "显示/LVGL 初始化失败:SPI MOSI=%d SCLK=%d CS=%d DC=%d BL=%d",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }
    bsp_display_backlight(100);

    // SPIFFS
    {
        esp_vfs_spiffs_conf_t conf = {
            .base_path = "/spiffs",
            .partition_label = "spiffs",
            .max_files = 5,
            .format_if_mount_failed = true,
        };
        esp_err_t r = esp_vfs_spiffs_register(&conf);
        if (r == ESP_OK) {
            size_t total = 0, used = 0;
            esp_spiffs_info("spiffs", &total, &used);
            ESP_LOGI(TAG, "SPIFFS mount total=%uKB used=%uKB",
                     (unsigned)(total / 1024), (unsigned)(used / 1024));
        } else {
            ESP_LOGE(TAG, "SPIFFS mount fail: %s", esp_err_to_name(r));
        }
    }

    s_btn_ok = (bsp_button_init(on_key, NULL) == ESP_OK);
    if (!s_btn_ok) ESP_LOGE(TAG, "按键不可用,单人游戏将不可进入");

    // ⚠ 这两个以前没人调过 —— 结果所有游戏的 beep() 都是空转(一声不响)。
    //    BGM 要能出声,必须先把 codec 与电量计初始化起来。
    if (bsp_battery_init() != ESP_OK) ESP_LOGW(TAG, "CW2017 电量计未应答,设置页电量将显示无数据");
    if (bsp_audio_init()   != ESP_OK) ESP_LOGW(TAG, "ES8311 未就绪,BGM 与音效静默");

    // BGM 必须在 app_boot_show 之前起来,这样开机动画就有音乐
    app_bgm_init();

    // 启动 FAP_SCREENSHOT_V1 截屏监听器(社区发布所需;纯观测性)
    fap_screenshot_start();

    if (bsp_lvgl_lock(1000)) { app_boot_show(boot_done); bsp_lvgl_unlock(); }

#if NAV_AUTOPILOT
    if (bsp_lvgl_lock(1000)) { s_nav_auto_tm = lv_timer_create(nav_auto_cb, 500, NULL); bsp_lvgl_unlock(); }
    ESP_LOGI(TAG, "NAV_AUTOPILOT=1 已启用(验证用)");
#endif

    ESP_LOGI(TAG, "ready Button=%d", s_btn_ok);
}