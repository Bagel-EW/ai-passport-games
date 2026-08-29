// main/demo_breakout.c —— 像素打砖块:街机外壳内新增的单机主菜。
// 三键:上/下=挡板步进 · OK=发球/重开 · OK长按=main 统一返回菜单。
// 砖/球/挡板用无边框纯色块绘制(自己可控尺寸),得分用 font_game 大数字。
#include "demo.h"
#include "ui_pixel.h"
#include "font_zh.h"
#include "app_bgm.h"
#include "lvgl.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static const char *TAG = "breakout";

#define SCR_W  240
#define SCR_H  320
#define AREA_TOP 62          // 砖墙可放置区域顶(标题栏+信息行之下)
#define AREA_BOT 284         // 落底线(接近草地装饰)
#define BRICK_W   22
#define BRICK_H   8
#define BRICK_GX  3
#define BRICK_GY  3
#define BRICK_COLS 8
#define ROWS_MAX    8
#define ROWS_INIT   5
#define BRICK_X0   21       // (240 - (8*22+7*3))/2
#define BRICK_Y0   72
#define PADDLE_W   42
#define PADDLE_H   8
#define PADDLE_Y   252
#define PADDLE_STEP 13
#define BALL       8
#define BALL_PX    3.0f     // 基准每帧位移(约50fps)

typedef enum { ST_READY, ST_PLAY, ST_OVER } st_t;

static lv_obj_t *s_scr;
static lv_obj_t *s_score;   // font_game 大数字
static lv_obj_t *s_life;
static lv_obj_t *s_combo;
static lv_obj_t *s_msg;
static lv_obj_t *s_lvl;        // 顶栏"第 X 关"
static lv_obj_t *s_go_title;   // 结算:游戏结束
static lv_obj_t *s_go_line;    // 结算:本次分数/关卡
static lv_obj_t *s_go_top[3];  // 结算:TOP3 三行
static lv_obj_t *s_paddle;
static lv_obj_t *s_ball;
static lv_obj_t *s_brick[ROWS_MAX][BRICK_COLS];
static int  s_val[ROWS_MAX][BRICK_COLS];
static bool s_gold[ROWS_MAX][BRICK_COLS];
static bool s_last_hint;    // 只剩一砖时亮提示

static lv_timer_t *s_tick;

static st_t s_st;
static int  s_level = 1, s_rows = ROWS_INIT;
static int  s_life_cnt = 3;
static int  s_score_v = 0;
static int  s_combo_v = 0;
static int  s_alive = 0;        // 本关剩余砖数
static float s_px = 0, s_py = 0, s_vx = 0, s_vy = 0, s_paddle_x = 0;

// ---- 无边框纯色块(复刻 ui_pixel 内部 block,避免面板的 border/pad) ----
static lv_obj_t *bk_block(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color)
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

// ---- 短蜂鸣(16k 16bit 方波,直接写 codec,逐事件即时) ----
// 走 app_bgm_beep:与 BGM 共用一把 codec 互斥锁,否则会和背景音乐抢 ES8311 出杂音。
static void beep(int hz, int ms)
{
    app_bgm_beep(hz, ms);
}

// ---- 本地 TOP3 榜单(独立模块:/spiffs/brk_top) ----
static const char *title_for(int level)
{
    if (level <= 2) return "新手砖匠";
    if (level <= 4) return "砖墙破坏者";
    if (level <= 6) return "金砖猎手";
    return "传说瓦匠";
}

typedef struct { int score; int level; } bk_rank_t;
static bk_rank_t s_top[3] = {0};
static int s_new_rank = -1;      // 本次排名(0..2),-1=未入榜

static void read_top(void)
{
    for (int i = 0; i < 3; i++) { s_top[i].score = 0; s_top[i].level = 0; }
    FILE *f = fopen("/spiffs/brk_top", "r");
    if (f) {
        char b[24]; int n = 0;
        while (n < 3 && fgets(b, sizeof b, f)) {
            int s = 0, l = 0;
            if (sscanf(b, "%d %d", &s, &l) == 2) { s_top[n].score = s; s_top[n].level = l; n++; }
        }
        fclose(f);
    }
}
static void write_top(void)
{
    FILE *f = fopen("/spiffs/brk_top", "w");
    if (f) {
        for (int i = 0; i < 3; i++)
            fprintf(f, "%d %d\n", s_top[i].score, s_top[i].level);
        fclose(f);
    }
}
static int insert_top(int score, int level)
{
    int idx = -1;
    if (score <= 0) return -1;
    bk_rank_t in = { score, level }, tmp;
    for (int i = 0; i < 3; i++) {
        if (score > s_top[i].score || (score == s_top[i].score && level > s_top[i].level)) {
            idx = i;
            for (int j = i; j < 3; j++) { tmp = s_top[j]; s_top[j] = in; in = tmp; }
            break;
        }
    }
    if (idx >= 0) write_top();
    return idx;
}

static void show_score(void)   { lv_label_set_text_fmt(s_score, "%d", s_score_v); }
static void show_life(void)    { lv_label_set_text_fmt(s_life, "生命 ×%d", s_life_cnt); }
static void show_combo(void)   { if (s_combo_v > 1) lv_label_set_text_fmt(s_combo, "连击 ×%d", s_combo_v);
                                 else lv_label_set_text(s_combo, ""); }

// ---- 关卡砖墙重建 ----
static void clear_bricks(void)
{
    for (int i = 0; i < ROWS_MAX; i++)
        for (int j = 0; j < BRICK_COLS; j++) {
            if (s_brick[i][j]) lv_obj_delete(s_brick[i][j]);   // 真删,避免残留旧砖
            s_brick[i][j] = NULL;
        }
    s_alive = 0;
}
static void build_bricks(void)
{
    static const uint32_t row_col[8] = {
        0xFF5CA8, 0x7C5CFF, 0x00FFCE, 0xFFD95C, 0xFFFFFF, 0xFFD95C, 0x00FFCE, 0x7C5CFF };
    static const int row_val[8] = { 50, 40, 30, 20, 10, 20, 30, 40 };
    int rows = s_rows > ROWS_MAX ? ROWS_MAX : s_rows;
    clear_bricks();
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < BRICK_COLS; j++) {
            int x = BRICK_X0 + j * (BRICK_W + BRICK_GX);
            int y = BRICK_Y0 + i * (BRICK_H + BRICK_GY);
            lv_obj_t *o = bk_block(s_scr, x, y, BRICK_W, BRICK_H, row_col[i]);
            lv_obj_set_style_radius(o, 2, 0);              // 简单异形:统一圆角
            if (rand() % 3 == 0)                           // 约每3块一块"尖顶瓦"
                bk_block(o, 6, 0, 10, 3, row_col[i]);      // 顶部中央台阶,视觉凸顶
            s_brick[i][j] = o;
            s_val[i][j] = row_val[i];
            s_gold[i][j] = false;
            s_alive++;
        }
    }
    // 随机一块(任意行)做金砖
    int gi = rows > 0 ? (int)(rand() % rows) : 0;
    int gj = (int)(rand() % BRICK_COLS);
    if (s_brick[gi][gj]) {
        lv_obj_t *b = s_brick[gi][gj];
        s_gold[gi][gj] = true;
        lv_obj_set_style_bg_color(b, lv_color_hex(0xFFA300), 0);
        for (uint32_t k = 0; k < lv_obj_get_child_count(b); k++)   // 金砖隐藏尖顶台阶,保持棱角
            lv_obj_add_flag(lv_obj_get_child(b, k), LV_OBJ_FLAG_HIDDEN);
    }
    s_last_hint = false;
}

static void place_ball_ready(void)
{
    s_px = s_paddle_x + (PADDLE_W - BALL) / 2.0f;
    s_py = PADDLE_Y - BALL;
    lv_obj_set_x(s_ball, (int)s_px);
    lv_obj_set_y(s_ball, (int)s_py);
}

static void reset_round(void)
{
    s_combo_v = 0;
    s_st = ST_READY;
    show_combo();
    place_ball_ready();
    lv_label_set_text_fmt(s_msg, "OK 发球 · 上← 下→");
}

static void next_level(void)
{
    s_level++;
    if (s_rows < ROWS_MAX) s_rows++;
    build_bricks();
    beep(659, 90); beep(784, 90); beep(988, 120);   // 过关小乐句
    reset_round();
    lv_label_set_text_fmt(s_msg, "第 %d 关", s_level);
    lv_label_set_text_fmt(s_lvl, "第 %d 关", s_level);   // 顶栏跟随关卡
}

static void new_game(void)
{
    s_level = 1; s_rows = ROWS_INIT; s_life_cnt = 3; s_score_v = 0;
    lv_label_set_text_fmt(s_lvl, "第 1 关");
    lv_obj_set_style_text_color(s_msg, lv_color_hex(0xFFFFFF), 0);
    lv_obj_add_flag(s_go_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_go_line, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < 3; i++) lv_obj_add_flag(s_go_top[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_ball, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_paddle, LV_OBJ_FLAG_HIDDEN);
    build_bricks();
    show_score(); show_life(); show_combo();
    s_paddle_x = (SCR_W - PADDLE_W) / 2.0f;
    lv_obj_set_x(s_paddle, (int)s_paddle_x);
    reset_round();
}

static void game_over(void)
{
    s_st = ST_OVER;
    // 隐藏球/挡板与剩余砖墙,露出结算面板
    lv_obj_add_flag(s_ball, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_paddle, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < ROWS_MAX; i++)
        for (int j = 0; j < BRICK_COLS; j++)
            if (s_brick[i][j]) lv_obj_add_flag(s_brick[i][j], LV_OBJ_FLAG_HIDDEN);

    read_top();
    s_new_rank = insert_top(s_score_v, s_level);

    lv_label_set_text_fmt(s_go_line, "本次 %d 分 · 第%d关", s_score_v, s_level);
    for (int i = 0; i < 3; i++) {
        uint32_t col = (i == s_new_rank) ? UI_RED : UI_INK;
        lv_obj_set_style_text_color(s_go_top[i], lv_color_hex(col), 0);
        if (s_top[i].score > 0)
            lv_label_set_text_fmt(s_go_top[i], "%d %s %d·L%d", i + 1,
                                  title_for(s_top[i].level), s_top[i].score, s_top[i].level);
        else
            lv_label_set_text_fmt(s_go_top[i], "%d 暂无记录", i + 1);
        lv_obj_clear_flag(s_go_top[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_clear_flag(s_go_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_go_line, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_color(s_msg, lv_color_hex(UI_RED), 0);
    lv_label_set_text_fmt(s_msg, "OK 重开 · 长按返回");

    beep(392, 120); beep(311, 140); beep(247, 180);
    ESP_LOGI(TAG, "game over score=%d new_rank=%d", s_score_v, s_new_rank);
}

// ---- 帧更新(LVGL 任务内) ----
static void tick_cb(lv_timer_t *tm)
{
    (void)tm;
    if (s_st != ST_PLAY) return;

    // 归一化速度模固定(按关卡加速)
    float sp = BALL_PX * (1.0f + 0.06f * (s_level - 1));
    float mag = s_vx * s_vx + s_vy * s_vy;
    if (mag > 0.001f) { float k = sp / mag; s_vx *= k; s_vy *= k; }

    float nx = s_px + s_vx, ny = s_py + s_vy;
    bool bounced = false;

    // 左右/上边界
    if (nx <= 0) { nx = 0; s_vx = -s_vx; }
    if (nx >= SCR_W - BALL) { nx = (float)(SCR_W - BALL); s_vx = -s_vx; }
    if (ny <= AREA_TOP) { ny = (float)AREA_TOP; s_vy = -s_vy; }

    // 挡板反射:命中位置偏→反弹角,但限制水平分量避免近水平滑行
    if (s_vy > 0 && ny + BALL >= PADDLE_Y && ny + BALL <= PADDLE_Y + PADDLE_H + BALL &&
        nx + BALL > s_paddle_x && nx < s_paddle_x + PADDLE_W) {
        float pcx = s_paddle_x + PADDLE_W / 2.0f;
        float bcx = nx + BALL / 2.0f;
        float rel = (bcx - pcx) / (PADDLE_W / 2.0f);
        if (rel > 1) rel = 1;
        if (rel < -1) rel = -1;
        float vx = rel * sp;
        if (vx > 0.7f * sp) vx = 0.7f * sp;
        if (vx < -0.7f * sp) vx = -0.7f * sp;
        s_vx = vx;
        s_vy = -(float)sqrt(fmaxf(sp * sp - vx * vx, 16.0f));
        s_py = PADDLE_Y - BALL;
        bounced = true;
        beep(600, 24);
    }

    s_px = nx;
    if (!bounced) s_py = ny;

    // 砖碰撞
    int hit_row = -1, hit_col = -1;
    for (int i = 0; i < ROWS_MAX && hit_row < 0; i++)
        for (int j = 0; j < BRICK_COLS && hit_row < 0; j++) {
            lv_obj_t *b = s_brick[i][j];
            if (!b) continue;
            if (s_px + BALL > lv_obj_get_x(b) && s_px < lv_obj_get_x(b) + BRICK_W &&
                s_py + BALL > lv_obj_get_y(b) && s_py < lv_obj_get_y(b) + BRICK_H) {
                hit_row = i; hit_col = j;
            }
        }
    if (hit_row >= 0) {
        lv_obj_t *b = s_brick[hit_row][hit_col];
        int bx = lv_obj_get_x(b), by = lv_obj_get_y(b);
        float cx = s_px + BALL / 2.0f, cy = s_py + BALL / 2.0f;
        float rc = bx + BRICK_W / 2.0f, br = by + BRICK_H / 2.0f;
        if (fabsf(cx - rc) > fabsf(cy - br)) {         // 侧面击中:水平反转+推x
            s_vx = -s_vx;
            s_px = (s_vx < 0) ? (float)(bx - BALL) : (float)(bx + BRICK_W);
        } else {                                       // 顶/底击中:竖直反转+推y
            s_vy = -s_vy;
            s_py = (s_vy < 0) ? (float)(by - BALL) : (float)(by + BRICK_H);
        }

        s_combo_v++;
        int gain = s_val[hit_row][hit_col] * s_combo_v;
        if (s_gold[hit_row][hit_col]) { gain += 100; beep(880, 60); beep(1175, 60); }
        else beep(500 + 40 * (s_combo_v > 6 ? 6 : s_combo_v), 22);
        if (s_last_hint) gain *= 3;                 // 最后一砖加成
        s_score_v += gain;
        lv_obj_delete(b);
        s_brick[hit_row][hit_col] = NULL;
        s_alive--;
        show_score(); show_combo();
        if (s_alive == 1) { s_last_hint = true; lv_label_set_text(s_msg, "最后一砖!"); }
        else if (s_alive <= 0) { next_level(); }
    }

    lv_obj_set_x(s_ball, (int)s_px);
    lv_obj_set_y(s_ball, (int)s_py);

    // 落底
    if (s_py >= AREA_BOT) {
        s_combo_v = 0; show_combo();
        s_life_cnt--;   show_life();
        beep(247, 90); beep(196, 120);
        if (s_life_cnt <= 0) { game_over(); }
        else { reset_round(); }
    }
}

// ---- 界面 ----
static void build(void)
{
    s_scr = ui_pixel_screen_create("");     // 留空标题板,由 s_lvl 动态覆盖"第 X 关"

    // 顶栏:第 X 关(盖在标题板上)
    s_lvl = ui_pixel_label(s_scr, "第 1 关", &font_zh14, UI_INK);
    lv_obj_set_pos(s_lvl, 16, 15);

    // 得分:右上角大数字(font_game 32px)
    s_score = ui_pixel_label(s_scr, "0", &font_game, UI_INK);
    lv_obj_align(s_score, LV_ALIGN_TOP_RIGHT, -10, 10);

    // 信息行:生命 / 连击
    s_life = ui_pixel_label(s_scr, "生命 ×3", &font_zh14, 0x1A4A68);
    lv_obj_align(s_life, LV_ALIGN_TOP_LEFT, 12, 45);
    s_combo = ui_pixel_label(s_scr, "", &font_zh14, UI_ORANGE);
    lv_obj_align(s_combo, LV_ALIGN_TOP_LEFT, 150, 45);

    // 结算面板(默认隐藏,Game Over 时显示)
    s_go_title = ui_pixel_label(s_scr, "游戏结束", &font_zh14, 0xB00000);
    lv_obj_align(s_go_title, LV_ALIGN_TOP_MID, 0, 104);
    lv_obj_add_flag(s_go_title, LV_OBJ_FLAG_HIDDEN);
    s_go_line = ui_pixel_label(s_scr, "", &font_zh14, 0x1A4A68);
    lv_obj_align(s_go_line, LV_ALIGN_TOP_MID, 0, 128);
    lv_obj_add_flag(s_go_line, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < 3; i++) {
        s_go_top[i] = ui_pixel_label(s_scr, "", &font_zh14, UI_INK);
        lv_obj_set_pos(s_go_top[i], 22, 158 + i * 26);
        lv_obj_add_flag(s_go_top[i], LV_OBJ_FLAG_HIDDEN);
    }

    // 游戏元素
    for (int i = 0; i < ROWS_MAX; i++)
        for (int j = 0; j < BRICK_COLS; j++) s_brick[i][j] = NULL;
    s_paddle = bk_block(s_scr, (SCR_W - PADDLE_W) / 2, PADDLE_Y, PADDLE_W, PADDLE_H, 0x1A4A68);
    s_ball   = bk_block(s_scr, 0, PADDLE_Y - BALL, BALL, BALL, 0x17202A);

    s_msg = ui_pixel_label(s_scr, "OK 发球", &font_zh14, 0xFFFFFF);
    lv_obj_align(s_msg, LV_ALIGN_BOTTOM_MID, 0, -14);

    lv_screen_load(s_scr);
    s_tick = lv_timer_create(tick_cb, 20, NULL);
}

void demo_breakout_enter(void)
{
    s_st = ST_READY;
    build();
    new_game();
}

void demo_breakout_exit(void)
{
    if (s_tick) { lv_timer_del(s_tick); s_tick = NULL; }
    if (s_scr)  { lv_obj_delete(s_scr); s_scr = NULL; }
    s_paddle = s_ball = s_score = s_life = s_combo = s_msg = s_lvl = NULL;
    s_go_title = s_go_line = NULL;
    for (int i = 0; i < 3; i++) s_go_top[i] = NULL;
    for (int i = 0; i < ROWS_MAX; i++)
        for (int j = 0; j < BRICK_COLS; j++) s_brick[i][j] = NULL;
}

void demo_breakout_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (ev != BSP_BTN_PRESS && ev != BSP_BTN_CLICK) return;

    if (s_st == ST_OVER) {
        if (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK) new_game();
        return;
    }

    if (btn == BSP_BTN_UP && ev == BSP_BTN_PRESS) {
        s_paddle_x -= PADDLE_STEP;
        if (s_paddle_x < 0) s_paddle_x = 0;
        lv_obj_set_x(s_paddle, (int)s_paddle_x);
        if (s_st == ST_READY) place_ball_ready();
    } else if (btn == BSP_BTN_DOWN && ev == BSP_BTN_PRESS) {
        s_paddle_x += PADDLE_STEP;
        if (s_paddle_x > SCR_W - PADDLE_W) s_paddle_x = (float)(SCR_W - PADDLE_W);
        lv_obj_set_x(s_paddle, (int)s_paddle_x);
        if (s_st == ST_READY) place_ball_ready();
    } else if (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK) {
        if (s_st == ST_READY) {
            s_st = ST_PLAY;
            float sp = BALL_PX;
            s_vx = sp * 0.45f;
            s_vy = -(float)sqrt(fmaxf(sp * sp - s_vx * s_vx, 4.0f));
            lv_label_set_text(s_msg, "");
            beep(760, 30);
        }
    }
}