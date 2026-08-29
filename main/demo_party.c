// main/demo_party.c —— 派对:随机抽签小游戏合集 + 酒神颁奖。
// 入口说明页(玩法简介 + 最后一行选人数) → 派对子菜单(4游戏 + 酒神颁奖,不含人数项)
// → 轮流制游戏(随机起始 N 号递增) → 颁奖页 lv_image 显示「酒神」结算图。
#include "demo.h"
#include "ui_pixel.h"
#include "font_zh.h"
#include "party_data.h"
#include "party_award_b.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "party";

typedef enum { P_INTRO, P_MENU, P_PLAY, P_AWARD_SEL, P_AWARD_SHOW } page_t;

// 子菜单项:0..3 四个游戏, 4 = 酒神颁奖(人数已在前置页定好,不再重复)
enum { IDX_TRUTH = 0, IDX_KING, IDX_ROULETTE, IDX_FATE, IDX_AWARD, IDX_BACK, IDX_N };

static const char *GAME_NAMES[] = { "真心话大冒险", "国王游戏", "俄罗斯转盘", "命运签" };

static lv_obj_t *s_scr;
static lv_obj_t *s_panel;
static lv_obj_t *s_text;
static lv_obj_t *s_hint;
static lv_obj_t *s_players_label;  // 前置页人数大字
static lv_obj_t *s_cards[IDX_N];
static lv_obj_t *s_rows[IDX_N];
static lv_obj_t *s_mascot;

static page_t s_page = P_INTRO;
static int s_sel = 0;
static int s_players = 4;
static int s_game = 0;
static int s_truth_mode = 0;
static int s_cur = 1;        // 当前玩家号(轮流)
static int s_stage = 0;      // 0=「从N号开始」 1=已抽结果
static int s_award_sel = 1;  // 颁奖选中的玩家号
static char s_buf[160];

// 前向声明(跨页跳转需要)
static void menu_show(void);
static void intro_show(void);
static void play_enter(void);
static void award_sel_show(void);
static void award_show_show(void);

// ---------- 像素块 helper ----------
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

static void clear_scr(void)
{
    if (s_scr) { lv_obj_delete(s_scr); s_scr = NULL; }
    s_panel = s_text = s_hint = s_players_label = s_mascot = NULL;
    for (int i = 0; i < IDX_N; i++) s_cards[i] = s_rows[i] = NULL;
}

static lv_obj_t *content_panel(void)
{
    s_panel = ui_pixel_panel_create(s_scr, 18, 58, 204, 178, UI_PAPER);
    s_text = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_text, &font_zh14, 0);
    lv_obj_set_style_text_color(s_text, lv_color_hex(UI_INK), 0);
    lv_obj_set_style_text_align(s_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_text, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_text, 176);
    lv_obj_align(s_text, LV_ALIGN_CENTER, 0, -10);
    return s_text;
}

static void set_hint(const char *txt)
{
    if (s_hint) lv_label_set_text(s_hint, txt);
}

// ---------- 子菜单 ----------
static void menu_refresh(void)
{
    for (int i = 0; i < IDX_N; i++) {
        const char *t = GAME_NAMES[i];
        if (i == IDX_AWARD) t = "酒神颁奖";
        else if (i == IDX_BACK) t = "返回";
        lv_label_set_text(s_rows[i], t);
        ui_pixel_set_selected(s_cards[i], i == s_sel, true);
    }
}

static void menu_show(void)
{
    clear_scr();
    s_page = P_MENU;
    s_scr = ui_pixel_screen_create("派对");

    for (int i = 0; i < IDX_N; i++) {
        int x = 11 + (i % 2) * 112;
        int y = 52 + (i / 2) * 64;
        s_cards[i] = ui_pixel_panel_create(s_scr, x, y, 102, 54, UI_PAPER);
        s_rows[i] = lv_label_create(s_cards[i]);
        lv_obj_set_style_text_font(s_rows[i], &font_zh14, 0);
        lv_obj_set_style_text_align(s_rows[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(s_rows[i]);
    }
    s_mascot = ui_pixel_mascot_create(s_scr, 101, 246);
    menu_refresh();
    lv_screen_load(s_scr);
}

static void menu_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (ev != BSP_BTN_CLICK) return;
    if (btn == BSP_BTN_UP)   { s_sel = (s_sel + IDX_N - 1) % IDX_N; menu_refresh(); }
    else if (btn == BSP_BTN_DOWN) { s_sel = (s_sel + 1) % IDX_N; menu_refresh(); }
    else if (btn == BSP_BTN_OK) {
        if (s_sel == IDX_BACK)        { main_request_back(); return; }   // 回主菜单
        if (s_sel == IDX_AWARD)       award_sel_show();
        else { s_game = s_sel; play_enter(); }
    }
}

// ---------- 前置说明 + 人数 ----------
static void intro_show(void)
{
    clear_scr();
    s_page = P_INTRO;
    s_scr = ui_pixel_screen_create("多人派对");
    s_panel = ui_pixel_panel_create(s_scr, 18, 58, 204, 178, UI_PAPER);

    // 玩法简介
    lv_obj_t *desc = lv_label_create(s_panel);
    lv_obj_set_style_text_font(desc, &font_zh14, 0);
    lv_obj_set_style_text_color(desc, lv_color_hex(UI_INK), 0);
    lv_obj_set_style_text_align(desc, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(desc, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(desc, 176);
    lv_label_set_text(desc, "真心话·国王·转盘·命运签\n轮流抽签,随机起始\n酒神给今晚干杯收场");
    lv_obj_align(desc, LV_ALIGN_TOP_MID, 0, 12);

    // 最后一行:参与人数
    lv_obj_t *lbl = lv_label_create(s_panel);
    lv_obj_set_style_text_font(lbl, &font_zh14, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(UI_INK), 0);
    lv_label_set_text(lbl, "参与人数");
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 92);

    s_players_label = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_players_label, &font_game, 0);
    lv_obj_set_style_text_color(s_players_label, lv_color_hex(UI_RED), 0);
    lv_label_set_text_fmt(s_players_label, "%d 人", s_players);
    lv_obj_align(s_players_label, LV_ALIGN_TOP_MID, 0, 116);

    s_hint = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_hint, &font_zh14, 0);
    lv_obj_set_style_text_color(s_hint, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_hint, "UP/DOWN 人数 · OK 开始");
    lv_obj_align(s_hint, LV_ALIGN_BOTTOM_MID, 0, -12);

    s_mascot = ui_pixel_mascot_create(s_scr, 101, 246);
    lv_screen_load(s_scr);
}

static void intro_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (ev != BSP_BTN_CLICK) return;
    if (btn == BSP_BTN_UP)   { if (s_players < 8) s_players++; }
    else if (btn == BSP_BTN_DOWN) { if (s_players > 2) s_players--; }
    else if (btn == BSP_BTN_OK) { menu_show(); return; }
    else return;
    lv_label_set_text_fmt(s_players_label, "%d 人", s_players);
}

// ---------- 游戏(轮流制) ----------
static void play_intro_text(void)
{
    switch (s_game) {
    case IDX_TRUTH:
        snprintf(s_buf, sizeof(s_buf), "从 %d 号开始\n%s\n\nOK 抽题",
                 s_cur, s_truth_mode ? "大冒险" : "真心话");
        break;
    case IDX_KING:
        snprintf(s_buf, sizeof(s_buf), "国王是 %d 号\n\nOK 抽国王令", s_cur);
        break;
    case IDX_ROULETTE:
        snprintf(s_buf, sizeof(s_buf), "从 %d 号开始\n\nOK 扣扳机", s_cur);
        break;
    case IDX_FATE:
        snprintf(s_buf, sizeof(s_buf), "从 %d 号开始\n\nOK 抽签", s_cur);
        break;
    }
    lv_label_set_text(s_text, s_buf);
}

static void play_result_text(void)
{
    switch (s_game) {
    case IDX_TRUTH:
        snprintf(s_buf, sizeof(s_buf), "玩家 %d\n%s\n\n%s", s_cur,
                 s_truth_mode ? "大冒险" : "真心话",
                 s_truth_mode ? party_pick_dare() : party_pick_truth());
        break;
    case IDX_KING: {
        char cmd[128];
        party_king_text(cmd, sizeof(cmd), s_players);
        snprintf(s_buf, sizeof(s_buf), "国王 %d 号\n\n%s", s_cur, cmd);
        break;
    }
    case IDX_ROULETTE:
        snprintf(s_buf, sizeof(s_buf), "玩家 %d\n\n%s", s_cur,
                 party_roulette_shot() ? "中弹！罚一杯" : "空枪，安全");
        break;
    case IDX_FATE:
        snprintf(s_buf, sizeof(s_buf), "玩家 %d\n\n%s", s_cur, party_pick_fate());
        break;
    }
    lv_label_set_text(s_text, s_buf);
}

static void play_enter(void)
{
    clear_scr();
    s_page = P_PLAY;
    s_stage = 0;
    s_truth_mode = 0;
    s_cur = 1 + party_rand(s_players);   // 随机起始
    s_scr = ui_pixel_screen_create(GAME_NAMES[s_game]);
    content_panel();

    s_hint = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_hint, &font_zh14, 0);
    lv_obj_set_style_text_color(s_hint, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(s_hint, LV_ALIGN_BOTTOM_MID, 0, 70);

    play_intro_text();
    set_hint(s_game == IDX_TRUTH ? "UP/DOWN 切换  OK 抽题" : "OK 继续");
    lv_screen_load(s_scr);
}

static void play_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (ev != BSP_BTN_CLICK) return;

    if (s_game == IDX_TRUTH && s_stage == 0 && btn != BSP_BTN_OK) {
        s_truth_mode = !s_truth_mode;
        play_intro_text();
        return;
    }
    if (btn != BSP_BTN_OK) return;

    if (s_stage == 0) {
        play_result_text();
        s_stage = 1;
        set_hint("OK 下一位");
    } else {
        s_cur = s_cur % s_players + 1;   // 递增轮转
        s_stage = 0;
        play_intro_text();
        set_hint(s_game == IDX_TRUTH ? "UP/DOWN 切换  OK 抽题" : "OK 继续");
    }
}

// ---------- 酒神颁奖 ----------
static void award_sel_show(void)
{
    clear_scr();
    s_page = P_AWARD_SEL;
    s_award_sel = 1;
    s_scr = ui_pixel_screen_create("酒神颁奖");
    s_panel = ui_pixel_panel_create(s_scr, 18, 58, 204, 178, UI_PAPER);

    lv_obj_t *q = lv_label_create(s_panel);
    lv_obj_set_style_text_font(q, &font_zh14, 0);
    lv_obj_set_style_text_color(q, lv_color_hex(UI_INK), 0);
    lv_label_set_text(q, "今晚的酒神是");
    lv_obj_align(q, LV_ALIGN_CENTER, 0, -34);

    s_text = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_text, &font_game, 0);
    lv_obj_set_style_text_color(s_text, lv_color_hex(UI_RED), 0);
    lv_obj_align(s_text, LV_ALIGN_CENTER, 0, 6);

    lv_obj_t *tip = lv_label_create(s_panel);
    lv_obj_set_style_text_font(tip, &font_zh14, 0);
    lv_obj_set_style_text_color(tip, lv_color_hex(UI_INK), 0);
    lv_label_set_text(tip, "UP/DOWN 选人  OK 颁奖");
    lv_obj_align(tip, LV_ALIGN_BOTTOM_MID, 0, -8);

    lv_label_set_text_fmt(s_text, "%d 号", s_award_sel);
    lv_screen_load(s_scr);
}

static void award_sel_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (ev != BSP_BTN_CLICK) return;
    if (btn == BSP_BTN_UP)   { if (s_award_sel < s_players) s_award_sel++; }
    else if (btn == BSP_BTN_DOWN) { if (s_award_sel > 1) s_award_sel--; }
    else if (btn == BSP_BTN_OK) { award_show_show(); return; }
    else return;
    lv_label_set_text_fmt(s_text, "%d 号", s_award_sel);
}

static void award_show_show(void)
{
    clear_scr();
    s_page = P_AWARD_SHOW;
    s_scr = lv_obj_create(NULL);
    lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(0x0C0F18), 0);
    lv_obj_set_style_border_width(s_scr, 0, 0);
    lv_obj_set_style_pad_all(s_scr, 0, 0);

    lv_obj_t *img = lv_image_create(s_scr);
    lv_image_set_src(img, &party_award_b_img);
    lv_obj_center(img);

    lv_screen_load(s_scr);
}

static void award_show_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (ev != BSP_BTN_CLICK) return;
    if (btn == BSP_BTN_OK) award_sel_show();   // 再选下一位
}

// ---------- demo 三函数契约 ----------
void demo_party_enter(void)
{
    s_players = 4;
    s_sel = 0;
    intro_show();
}

void demo_party_exit(void)
{
    clear_scr();
}

void demo_party_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    switch (s_page) {
    case P_INTRO:     intro_key(btn, ev); break;
    case P_MENU:      menu_key(btn, ev); break;
    case P_PLAY:      play_key(btn, ev); break;
    case P_AWARD_SEL: award_sel_key(btn, ev); break;
    case P_AWARD_SHOW: award_show_key(btn, ev); break;
    }
}
