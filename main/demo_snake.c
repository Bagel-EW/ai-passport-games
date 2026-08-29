// main/demo_snake.c —— 像素贪吃蛇(替代极速反应)。
// 上=左转下=右转(相对当前头方向),OK=开始/暂停/重开。
// 持续操作控制,不依赖"卡按键时机",三键手感顺滑。撞墙/撞身结束,进 TOP3.
#include "demo.h"
#include "ui_pixel.h"
#include "font_zh.h"
#include "app_bgm.h"
#include "lvgl.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "snake";

#define SCR_W  240
#define GRID_L 16            // 网格列
#define GRID_R 18            // 网格行
#define CELL   12
#define LEFT   ((SCR_W - GRID_L * CELL) / 2)   // 居中
#define TOP    64
#define MAXLEN (GRID_L * GRID_R)

typedef enum { ST_READY, ST_PLAY, ST_OVER } st_t;

static lv_obj_t *s_scr;
static lv_obj_t *s_hud_score;    // 顶栏小字 分数
static lv_obj_t *s_score;        // 右上大数字(font_game)
static lv_obj_t *s_msg;
static lv_obj_t *s_go_title, *s_go_line, *s_go_top[3];
static lv_obj_t *s_food;
static lv_obj_t *s_seg[MAXLEN];
static int s_obj_n;

static lv_timer_t *s_tick;
static st_t  s_st;
static bool  s_paused;
static int   s_dir;          // 0右 1下 2左 3上
static int   s_len;
static int   s_bx[MAXLEN], s_by[MAXLEN];
static int   s_fx, s_fy;
static int   s_score_v;
static int   s_acc;
static int   s_step;         // 移动间隔(ms),随长度加快
static int   s_eat;          // 本局累计吃食(用于调速)

static const char *title_for_len(int len)
{
    if (len <= 12) return "小蛇出洞";
    if (len <= 20) return "铁线游走";
    if (len <= 30) return "敏捷缠斗";
    return "吞天巨蟒";
}

typedef struct { int score; int len; } sn_rank_t;
static sn_rank_t s_top[3];
static int s_new_rank = -1;

static void read_top(void)
{
    for (int i = 0; i < 3; i++) { s_top[i].score = 0; s_top[i].len = 0; }
    FILE *f = fopen("/spiffs/sn_top", "r");
    if (f) {
        char b[24]; int n = 0;
        while (n < 3 && fgets(b, sizeof b, f)) {
            int s = 0, l = 0;
            if (sscanf(b, "%d %d", &s, &l) == 2) { s_top[n].score = s; s_top[n].len = l; n++; }
        }
        fclose(f);
    }
}
static void write_top(void)
{
    FILE *f = fopen("/spiffs/sn_top", "w");
    if (f) {
        for (int i = 0; i < 3; i++)
            fprintf(f, "%d %d\n", s_top[i].score, s_top[i].len);
        fclose(f);
    }
}
static int insert_top(int score, int len)
{
    int idx = -1;
    if (score <= 0) return -1;
    sn_rank_t in = { score, len }, tmp;
    for (int i = 0; i < 3; i++) {
        if (score > s_top[i].score || (score == s_top[i].score && len > s_top[i].len)) {
            idx = i;
            for (int j = i; j < 3; j++) { tmp = s_top[j]; s_top[j] = in; in = tmp; }
            break;
        }
    }
    if (idx >= 0) write_top();
    return idx;
}

static void game_hit(void);
static void show_score(void);

static lv_obj_t *blk(lv_obj_t *parent, int x, int y, int w, int h, uint32_t c)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, 2, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(c), 0);
    return o;
}

// 走 app_bgm_beep:与 BGM 共用一把 codec 互斥锁,否则会和背景音乐抢 ES8311 出杂音。
static void beep(int hz, int ms)
{
    app_bgm_beep(hz, ms);
}

static void show_score(void) { lv_label_set_text_fmt(s_score, "%d", s_score_v);
                               lv_label_set_text_fmt(s_hud_score, "长度 %d", s_len); }

static int cell_on_food(void)
{
    for (int i = 0; i < s_len; i++)
        if (s_fx == s_bx[i] && s_fy == s_by[i]) return 1;
    return 0;
}

static void place_food(void)
{
    int tries = 0;
    do {
        s_fx = rand() % GRID_L;
        s_fy = rand() % GRID_R;
        tries++;
    } while (cell_on_food() && tries < 2000);
    lv_obj_set_pos(s_food, LEFT + s_fx * CELL + 3, TOP + s_fy * CELL + 3);
    lv_obj_clear_flag(s_food, LV_OBJ_FLAG_HIDDEN);
}

static void move_one(void)
{
    static const int DX[4] = { 1, 0, -1, 0 };
    static const int DY[4] = { 0, 1, 0, -1 };
    int nx = s_bx[0] + DX[s_dir];
    int ny = s_by[0] + DY[s_dir];

    if (nx < 0 || nx >= GRID_L || ny < 0 || ny >= GRID_R) { game_hit(); return; }

    int grow = (nx == s_fx && ny == s_fy);
    if (grow) {
        s_len++;
        if (s_len >= MAXLEN) s_len = MAXLEN - 1;
        s_eat++;
        s_score_v += 10;
        show_score();
        beep(660, 50); beep(880, 50);
        place_food();
        // 提速
        if (s_eat % 6 == 0 && s_step > 46) s_step -= 6;
    }
    for (int i = s_len - 1; i > 0; i--) { s_bx[i] = s_bx[i - 1]; s_by[i] = s_by[i - 1]; }
    s_bx[0] = nx; s_by[0] = ny;

    // 撞自身(不含刚让出的尾)
    for (int i = 1; i < s_len; i++)
        if (nx == s_bx[i] && ny == s_by[i]) { game_hit(); return; }
}

static void game_hit(void)
{
    s_st = ST_OVER;
    lv_obj_add_flag(s_food, LV_OBJ_FLAG_HIDDEN);
    read_top();
    s_new_rank = insert_top(s_score_v, s_len);
    lv_label_set_text_fmt(s_go_line, "本次 %d 分 · 长%d", s_score_v, s_len);
    for (int i = 0; i < 3; i++) {
        uint32_t col = (i == s_new_rank) ? UI_RED : UI_INK;
        lv_obj_set_style_text_color(s_go_top[i], lv_color_hex(col), 0);
        if (s_top[i].score > 0)
            lv_label_set_text_fmt(s_go_top[i], "%d %s %d·长%d", i + 1,
                                  title_for_len(s_top[i].len), s_top[i].score, s_top[i].len);
        else
            lv_label_set_text_fmt(s_go_top[i], "%d 暂无记录", i + 1);
        lv_obj_clear_flag(s_go_top[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_clear_flag(s_go_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_go_line, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_color(s_msg, lv_color_hex(UI_RED), 0);
    lv_label_set_text_fmt(s_msg, "OK 重开 · 长按返回");
    beep(392, 120); beep(311, 140); beep(247, 180);
    ESP_LOGI(TAG, "over score=%d len=%d rank=%d", s_score_v, s_len, s_new_rank);
}

static void draw_all(void)
{
    while (s_obj_n < s_len) {
        s_seg[s_obj_n] = blk(s_scr, LEFT + 1, TOP + 1, CELL - 2, CELL - 2, 0x2EC962);
        lv_obj_add_flag(s_seg[s_obj_n], LV_OBJ_FLAG_HIDDEN);
        s_obj_n++;
    }
    while (s_obj_n > s_len) {
        lv_obj_delete(s_seg[s_obj_n - 1]);
        s_seg[s_obj_n - 1] = NULL;
        s_obj_n--;
    }
    for (int i = 0; i < s_len; i++) {
        uint32_t c = (i == 0) ? 0x8CFF2E
                   : ((i & 1) ? 0x27B554 : 0x41D875);
        lv_obj_t *o = s_seg[i];
        lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(o, LEFT + s_bx[i] * CELL + 1, TOP + s_by[i] * CELL + 1);
        lv_obj_set_style_bg_color(o, lv_color_hex(c), 0);
    }
}

static void reset_run(void)
{
    s_dir = 0;
    s_len = 3;
    s_bx[0] = 5; s_by[0] = GRID_R / 2;
    s_bx[1] = 4; s_by[1] = GRID_R / 2;
    s_bx[2] = 3; s_by[2] = GRID_R / 2;
    s_acc = 0; s_step = 110; s_eat = 0; s_paused = false;
    place_food();
    draw_all();
    lv_obj_set_style_text_color(s_msg, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text_fmt(s_msg, "OK 开始 · 上← 下→");
}

static void tick_cb(lv_timer_t *tm)
{
    (void)tm;
    if (s_st != ST_PLAY || s_paused) return;
    s_acc += 40;
    if (s_acc < s_step) return;
    s_acc -= s_step;
    move_one();
    if (s_st == ST_PLAY) draw_all();
}

static void build(void)
{
    s_scr = ui_pixel_screen_create("");
    s_hud_score = ui_pixel_label(s_scr, "长度 3", &font_zh14, 0x1A4A68);
    lv_obj_set_pos(s_hud_score, 14, 15);
    s_score = ui_pixel_label(s_scr, "0", &font_game, UI_INK);
    lv_obj_align(s_score, LV_ALIGN_TOP_RIGHT, -10, 10);

    // 网格底(深色板)
    blk(s_scr, LEFT - 5, TOP - 5, GRID_L * CELL + 10, GRID_R * CELL + 10, 0x0A2B1C);

    s_food = blk(s_scr, LEFT + 3, TOP + 3, CELL - 6, CELL - 6, 0xFFA300);
    lv_obj_set_style_radius(s_food, 5, 0);
    s_obj_n = 0;

    s_msg = ui_pixel_label(s_scr, "OK 开始", &font_zh14, 0xFFFFFF);
    lv_obj_align(s_msg, LV_ALIGN_BOTTOM_MID, 0, -14);

    s_go_title = ui_pixel_label(s_scr, "游戏结束", &font_zh14, 0xB00000);
    lv_obj_align(s_go_title, LV_ALIGN_TOP_MID, 0, 104); lv_obj_add_flag(s_go_title, LV_OBJ_FLAG_HIDDEN);
    s_go_line = ui_pixel_label(s_scr, "", &font_zh14, 0x1A4A68);
    lv_obj_align(s_go_line, LV_ALIGN_TOP_MID, 0, 128); lv_obj_add_flag(s_go_line, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < 3; i++) {
        s_go_top[i] = ui_pixel_label(s_scr, "", &font_zh14, UI_INK);
        lv_obj_set_pos(s_go_top[i], 22, 158 + i * 26);
        lv_obj_add_flag(s_go_top[i], LV_OBJ_FLAG_HIDDEN);
    }

    lv_screen_load(s_scr);
    s_tick = lv_timer_create(tick_cb, 40, NULL);
}

void demo_snake_enter(void)
{
    s_st = ST_READY;
    s_score_v = 0;
    build();
    reset_run();
    show_score();
}

void demo_snake_exit(void)
{
    if (s_tick) { lv_timer_del(s_tick); s_tick = NULL; }
    if (s_scr)  { lv_obj_delete(s_scr); s_scr = NULL; }
    for (int i = 0; i < MAXLEN; i++) s_seg[i] = NULL;
    s_obj_n = 0;
    s_hud_score = s_score = s_msg = s_food = s_go_title = s_go_line = NULL;
    for (int i = 0; i < 3; i++) s_go_top[i] = NULL;
}

void demo_snake_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (ev != BSP_BTN_PRESS && ev != BSP_BTN_CLICK) return;

    if (s_st == ST_OVER) {
        if (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK) {
            s_st = ST_READY; s_score_v = 0;
            lv_obj_add_flag(s_go_title, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_go_line, LV_OBJ_FLAG_HIDDEN);
            for (int i = 0; i < 3; i++) lv_obj_add_flag(s_go_top[i], LV_OBJ_FLAG_HIDDEN);
            reset_run();
            show_score();
        }
        return;
    }

    if (s_st == ST_READY) {
        if (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK) {
            s_st = ST_PLAY;
            lv_label_set_text(s_msg, "");
            beep(760, 30);
        }
        return;
    }

    // ST_PLAY
    if (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK) {
        s_paused = !s_paused;
        lv_label_set_text(s_msg, s_paused ? "暂停 · OK 继续" : "");
        beep(440, 24);
    } else if ((btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) && (ev == BSP_BTN_PRESS)) {
        // 上=左转(逆时针),下=右转(顺时针)
        s_dir = (s_dir + (btn == BSP_BTN_UP ? 3 : 1)) % 4;
        beep(520, 18);
    }
}