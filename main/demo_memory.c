// main/demo_memory.c —— 翻牌配对:记忆力游戏。
// 3 档难度递进(4/6/8 对),上下移动光标、OK 翻牌,凑对得分解锁下一档,通关进 TOP3。
//
// ⚠⚠ 渲染实现的三条硬约束(都是踩过的坑,别改回去):
//
// 1) 卡片像素【直接写 RGB565 缓冲】,绝对不要用 lv_canvas_set_px()。
//    lv_canvas_set_px() 内部每写一个像素就调一次 lv_obj_invalidate(),而每次
//    invalidate 要走 lv_obj_invalidate_area() → obj_invalidate_area_internal(),
//    后者会对 act_scr / prev_scr / sys_layer / top_layer / bottom_layer 做
//    5 次 lv_obj_tree_walk() 全树遍历(找被模糊/阴影影响的控件)。
//    一屏 16 张牌 × 每张 1280 个符号像素 = 约 2 万次全树遍历 / 每次刷新,
//    单次 refresh_cards() 要跑好几秒 —— 这就是"翻第一张牌后 UI 假死"的真凶
//    (LVGL 主循环其实还活着,heap timer 照常打印,只是事件被彻底拖死)。
//    现在:一张牌画完只 invalidate 一次,16 张牌 = 16 次。
//
// 2) 卡片 lv_obj 【开局一次性建好 16 个】,切关只改坐标/显隐,绝不重建。
//    旧版每次切关 delete+create 卡牌(及其上百个子方块对象),会让父对象的
//    spec_attr->children 指针数组反复 realloc,最终越界写踩坏相邻对象内存头,
//    表现为 Store access fault / LV_ASSERT_NULL 死循环 + 看门狗。
//
// 3) 边框画在像素缓冲里,不用 LVGL border 样式。
//    给 canvas 加 border 会把 44x44 的图像内容挤成 38x38,触发软件缩放绘制
//    (比直接 blit 慢一个量级),且选中/取消选中要反复改样式。
#include "demo.h"
#include "ui_pixel.h"
#include "font_zh.h"
#include "app_bgm.h"
#include "lvgl.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "memory";

#define SCR_W   240
#define CARD_W  44
#define CARD_H  44
#define CARD_MAX 16          // 最多 16 张(8 对),开局一次建满
#define GAP     6
#define COLS    4
#define LEV_MAX 3
#define PAIRS_MAX 8
#define SYM_W   10           // 符号位图宽
#define SYM_H   8            // 符号位图高
#define CELL    3            // 放大倍数 → 30x24

typedef enum { ST_PLAY, ST_OVER } st_t;

// 每档配对数
static const int LEV_PAIRS[LEV_MAX] = { 4, 6, 8 };

// 8 种像素符号(10x08 位图)及其主色
static const char *SYMS[PAIRS_MAX] = {
    // 心形
    "0110000110""1111001111""1111111111""1111111111"
    "0111111110""0011111100""0001111000""0000110000",
    // 星星(八芒)
    "0010010010""0010010010""0011111110""1111111111"
    "1111111111""0011111110""0010010010""0010010010",
    // 菱形
    "0000110000""0001111000""0011111100""0111111110"
    "1111111111""0111111110""0011111100""0001111000",
    // 十字
    "0000110000""0000110000""0000110000""1111111111"
    "1111111111""0000110000""0000110000""0000110000",
    // 圆环
    "0001111000""1111111111""1110000111""1100000011"
    "1100000011""1100000011""1110000111""1111111111",
    // 三角
    "0000110000""0001111000""0011111100""0111111110"
    "1111111111""0110000110""0110000110""0110000110",
    // 花盘
    "0001111000""1111111111""1111111111""1111111111"
    "1111111111""1111111111""1111111111""0001111000",
    // 水滴
    "0000110000""0001111000""0011111100""0111111110"
    "0111111110""0111111110""0011111100""0001111000",
};
static const uint32_t SYM_COL[PAIRS_MAX] = {
    0xE43B2F, 0xFFD928, 0xFF7AC0, 0x3FCE5A,
    0x00C8F0, 0xFF8A2B, 0xB14AE0, 0x3FA0FF,
};

// 三种牌面的底色 / 牌背花纹色
#define C_BACK_BG   0x1A4A68
#define C_BACK_PAT  0x3E86AC
#define C_FACE_BG   0xEFEFF3
#define C_DONE_BG   0xBFD8E8
#define C_EDGE_SEL  0xFFE14D
#define C_EDGE_NOR  0x08161F

typedef struct { int score; int level; } mm_rank_t;
static mm_rank_t s_top[3];
static int s_new_rank = -1;

static lv_obj_t *s_scr;
static lv_obj_t *s_lvl;         // 顶栏 第N关
static lv_obj_t *s_score;       // 大数字
static lv_obj_t *s_msg;
static lv_obj_t *s_go_title, *s_go_line, *s_go_top[3];
static lv_obj_t *s_card[CARD_MAX];        // 开局建满,常驻不重建
static uint16_t *s_gbuf[CARD_MAX];        // 每卡常驻 RGB565 缓冲(运行时分配)
static int       s_stride;                // 每行 u16 个数(= stride/2)
static lv_timer_t *s_tick;
static lv_timer_t *s_dbg;

static int s_last_refresh_us = 0;         // 最近一次全量刷新的耗时(诊断用)

static st_t  s_st;
static int   s_level = 1;        // 1..3
static int   s_count = 8;        // 本关卡片数
static int   s_pairs = 4;
static int   s_score_v = 0;
static int   s_cursor = 0;
static uint8_t s_sym[CARD_MAX];  // 每张卡符号 id(0..7)
static uint8_t s_face[CARD_MAX]; // 0=扣着 1=翻开对照 2=已配对
static int   s_pick1 = -1, s_pick2 = -1;
static int   s_cmp_acc = 0;      // 对照计时(ms)

static const char *title_for(int level)
{
    if (level <= 1) return "初出茅庐";
    if (level <= 2) return "过目不忘";
    return "记忆大师";
}

// ---- 快捷小函数 ----
// 走 app_bgm_beep:与 BGM 共用一把 codec 互斥锁,否则会和背景音乐抢 ES8311 出杂音。
static void beep(int hz, int ms)
{
    app_bgm_beep(hz, ms);
}

static void show_score(void) { lv_label_set_text_fmt(s_score, "%d", s_score_v); }

static void read_top(void)
{
    for (int i = 0; i < 3; i++) { s_top[i].score = 0; s_top[i].level = 0; }
    FILE *f = fopen("/spiffs/mm_top", "r");
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
    FILE *f = fopen("/spiffs/mm_top", "w");
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
    mm_rank_t in = { score, level }, tmp;
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

// ---- 卡片渲染:直接写缓冲,整张牌只 invalidate 一次 ----
static void card_paint(int i)
{
    if (i < 0 || i >= CARD_MAX || !s_gbuf[i] || !s_card[i]) return;

    uint16_t *p = s_gbuf[i];
    const int st = s_stride;
    const bool sel = (i == s_cursor);

    uint32_t bg, fg, pat, edge;
    int bw;
    if (s_face[i] == 1)      { bg = C_FACE_BG; pat = C_FACE_BG; fg = SYM_COL[s_sym[i]]; }
    else if (s_face[i] == 2) { bg = C_DONE_BG; pat = C_DONE_BG; fg = SYM_COL[s_sym[i]]; }
    else                     { bg = C_BACK_BG; pat = C_BACK_PAT; fg = C_BACK_BG; }
    edge = sel ? C_EDGE_SEL : C_EDGE_NOR;
    bw   = sel ? 5 : 3;

    const uint16_t c_bg  = lv_color_to_u16(lv_color_hex(bg));
    const uint16_t c_pat = lv_color_to_u16(lv_color_hex(pat));
    const uint16_t c_fg  = lv_color_to_u16(lv_color_hex(fg));
    const uint16_t c_ed  = lv_color_to_u16(lv_color_hex(edge));

    // 1) 铺底色
    for (int y = 0; y < CARD_H; y++) {
        uint16_t *row = p + (size_t)y * st;
        for (int x = 0; x < CARD_W; x++) row[x] = c_bg;
    }

    // 2) 牌面内容
    if (s_face[i] == 0) {
        // 牌背:中心菱形描边
        const int cx = CARD_W / 2, cy = CARD_H / 2;
        for (int y = 0; y < CARD_H; y++) {
            int dy = (y > cy) ? (y - cy) : (cy - y);
            uint16_t *row = p + (size_t)y * st;
            for (int x = 0; x < CARD_W; x++) {
                int dx = (x > cx) ? (x - cx) : (cx - x);
                if (dx + dy >= 10 && dx + dy <= 12) row[x] = c_pat;
            }
        }
    } else {
        // 翻开/已配对:像素符号
        const char *m = SYMS[s_sym[i]];
        const int ox = (CARD_W - SYM_W * CELL) / 2;
        const int oy = (CARD_H - SYM_H * CELL) / 2;
        for (int sy = 0; sy < SYM_H; sy++) {
            for (int sx = 0; sx < SYM_W; sx++) {
                if (m[sy * SYM_W + sx] != '1') continue;
                uint16_t *row = p + (size_t)(oy + sy * CELL) * st + ox + sx * CELL;
                for (int dy = 0; dy < CELL; dy++) {
                    for (int dx = 0; dx < CELL; dx++) row[dx] = c_fg;
                    row += st;
                }
            }
        }
    }

    // 3) 边框最后画,盖住溢出的内容
    for (int y = 0; y < CARD_H; y++) {
        uint16_t *row = p + (size_t)y * st;
        if (y < bw || y >= CARD_H - bw) {
            for (int x = 0; x < CARD_W; x++) row[x] = c_ed;
        } else {
            for (int x = 0; x < bw; x++) row[x] = c_ed;
            for (int x = CARD_W - bw; x < CARD_W; x++) row[x] = c_ed;
        }
    }

    lv_obj_invalidate(s_card[i]);
}

// 全量重绘(开局 / 切关 / 对照结算后)
static void refresh_cards(void)
{
    int64_t t0 = esp_timer_get_time();
    for (int i = 0; i < s_count; i++) card_paint(i);
    s_last_refresh_us = (int)(esp_timer_get_time() - t0);
}

// 按当前配对数生成配对
// 先选 s_pairs 个两两不同的符号,每个恰好出现 2 次填满再洗牌,
// 保证每一张牌都必然有配对的另一张(避免旧逻辑出现“单张”找不到对)。
static void deal_cards(void)
{
    s_pairs = LEV_PAIRS[s_level - 1];
    s_count = s_pairs * 2;
    if (s_count > CARD_MAX) s_count = CARD_MAX;

    int pool[PAIRS_MAX];
    int np = 0;
    while (np < s_pairs) {                       // 选出互不相同的符号
        int s = rand() % PAIRS_MAX;
        int dup = 0;
        for (int k = 0; k < np; k++) if (pool[k] == s) { dup = 1; break; }
        if (dup) continue;
        pool[np++] = s;
    }
    for (int i = 0; i < s_count; i++) {          // 每个符号放两张
        s_sym[i] = (uint8_t)pool[i / 2];
        s_face[i] = 0;
    }
    for (int i = s_count - 1; i > 0; i--) {      // 洗牌(逐位置与随机位交换)
        int j = rand() % (i + 1);
        uint8_t t = s_sym[i]; s_sym[i] = s_sym[j]; s_sym[j] = t;
    }
    s_pick1 = s_pick2 = -1;
    s_cmp_acc = 0;
    if (s_cursor >= s_count) s_cursor = s_count - 1;

    int cc[PAIRS_MAX] = { 0 };
    for (int i = 0; i < s_count; i++) cc[s_sym[i]]++;
    ESP_LOGI(TAG, "deal level=%d pairs=%d count=%d symcnt[0..7]=%d%d%d%d%d%d%d%d",
             s_level, s_pairs, s_count, cc[0], cc[1], cc[2], cc[3],
             cc[4], cc[5], cc[6], cc[7]);
}

// 排布本关卡片(只挪位置/显隐,不重建对象)
static void build_cards(void)
{
    int rows = (s_count + COLS - 1) / COLS;
    int grid_w = COLS * CARD_W + (COLS - 1) * GAP;
    int grid_h = rows * CARD_H + (rows - 1) * GAP;
    int x0 = (SCR_W - grid_w) / 2;
    int y0 = 66 + (200 - grid_h) / 2;
    if (y0 < 64) y0 = 64;

    for (int i = 0; i < CARD_MAX; i++) {
        if (i >= s_count) { lv_obj_add_flag(s_card[i], LV_OBJ_FLAG_HIDDEN); continue; }
        int r = i / COLS, c = i % COLS;
        lv_obj_set_pos(s_card[i], x0 + c * (CARD_W + GAP), y0 + r * (CARD_H + GAP));
        lv_obj_clear_flag(s_card[i], LV_OBJ_FLAG_HIDDEN);
    }
    refresh_cards();
}

static void game_over_win(void);

// 重开下一关
static void next_level(void)
{
    ESP_LOGI(TAG, "all-matched -> next_level cur=%d", s_level);
    if (s_level < LEV_MAX) {
        s_level++;
        s_score_v += 100 * (s_level - 1);
        beep(659, 90); beep(784, 90); beep(988, 120);
        lv_label_set_text_fmt(s_lvl, "第 %d 关 · %d 对", s_level, LEV_PAIRS[s_level - 1]);
        show_score();
        s_cursor = 0;
        deal_cards();
        build_cards();
        lv_label_set_text_fmt(s_msg, "记住颜色! 找 %d 对", LEV_PAIRS[s_level - 1]);
    } else {
        game_over_win();
    }
}

static void game_over_win(void)
{
    s_st = ST_OVER;
    read_top();
    s_new_rank = insert_top(s_score_v, s_level);
    lv_label_set_text_fmt(s_go_line, "通关! 本次 %d 分", s_score_v);
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
    lv_label_set_text(s_go_title, "通关!");
    lv_obj_clear_flag(s_go_line, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_color(s_msg, lv_color_hex(UI_RED), 0);
    lv_label_set_text_fmt(s_msg, "OK 再玩 · 长按返回");
    beep(659, 90); beep(784, 90); beep(988, 120); beep(1319, 180);
    ESP_LOGI(TAG, "win score=%d new_rank=%d", s_score_v, s_new_rank);
}

static void all_matched(void)
{
    for (int i = 0; i < s_count; i++)
        if (s_face[i] != 2) return;
    next_level();
}

// ---- 自动试玩:仅用于无人值守验证(改 1 重新烧录,设备会自己把牌全部翻完并打通关)----
#define MM_AUTOPILOT 0
#if MM_AUTOPILOT
static lv_timer_t *s_auto;
static int s_auto_steps;
static void auto_cb(lv_timer_t *tm)
{
    (void)tm;
    if (s_auto_steps > 90) {
        ESP_LOGI(TAG, "autopilot 结束 steps=%d", s_auto_steps);
        if (s_auto) { lv_timer_del(s_auto); s_auto = NULL; }
        return;
    }
    if (s_st != ST_PLAY) {                       // 通关界面 → 再来一局
        demo_memory_key(BSP_BTN_OK, BSP_BTN_CLICK);
        s_auto_steps += 20;                      // 快速消耗步数,别无限循环
        return;
    }
    if (s_pick1 >= 0 && s_pick2 >= 0) return;    // 对照中,等结算

    int target = -1;
    // 作弊模式:第二张专挑同符号的牌,保证每轮必中 → 能一路打穿第 3 关并通关,
    // 把"切关重建布局"和"通关结算"这两条旧版崩溃路径全部跑一遍。
    if (s_pick1 < 0) {
        for (int i = 0; i < s_count; i++) if (s_face[i] == 0) { target = i; break; }
    } else {
        for (int i = 0; i < s_count; i++) {
            if (s_face[i] == 0 && i != s_pick1 && s_sym[i] == s_sym[s_pick1]) { target = i; break; }
        }
    }
    if (target < 0) return;

    int64_t t0 = esp_timer_get_time();
    int guard = 0;                                // 用真实的 DOWN 键把光标挪过去
    while (s_cursor != target && guard++ <= s_count) demo_memory_key(BSP_BTN_DOWN, BSP_BTN_PRESS);
    demo_memory_key(BSP_BTN_OK, BSP_BTN_CLICK);
    int us = (int)(esp_timer_get_time() - t0);
    s_auto_steps++;
    ESP_LOGI(TAG, "auto step=%d flip=%d key_cost=%d us (full_refresh=%d us)",
             s_auto_steps, target, us, s_last_refresh_us);
}
#endif

static void dbg_cb(lv_timer_t *tm)
{
    (void)tm;
    ESP_LOGI(TAG, "heap free=%d min=%d | refresh_us=%d p1=%d p2=%d",
             (int)esp_get_free_heap_size(), (int)esp_get_minimum_free_heap_size(),
             s_last_refresh_us, s_pick1, s_pick2);
}

static void tick_cb(lv_timer_t *tm)
{
    (void)tm;
    if (s_st != ST_PLAY || s_pick1 < 0 || s_pick2 < 0) return;
    s_cmp_acc += 40;
    if (s_cmp_acc < 900) return;

    // 对照结果
    int a = s_pick1, b = s_pick2;
    s_pick1 = s_pick2 = -1;
    s_cmp_acc = 0;
    ESP_LOGI(TAG, "cmp idx a=%d sym=%d vs b=%d sym=%d -> %s",
             a, s_sym[a], b, s_sym[b], (s_sym[a] == s_sym[b]) ? "MATCH" : "miss");
    if (s_sym[a] == s_sym[b]) {
        s_face[a] = 2; s_face[b] = 2;
        s_score_v += 50;
        show_score();
        beep(660, 60); beep(880, 70);
        lv_label_set_text(s_msg, "配对成功!");
        refresh_cards();
        all_matched();
    } else {
        s_face[a] = 0; s_face[b] = 0;
        beep(196, 80);
        lv_label_set_text(s_msg, "没对上,记记位置");
        refresh_cards();
    }
}

static void new_game(void)
{
    s_level = 1;
    s_score_v = 0;
    s_cursor = 0;
    lv_obj_set_style_text_color(s_msg, lv_color_hex(0xFFFFFF), 0);
    lv_obj_add_flag(s_go_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_go_line, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < 3; i++) lv_obj_add_flag(s_go_top[i], LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text_fmt(s_lvl, "第 1 关 · %d 对", LEV_PAIRS[0]);
    show_score();
    deal_cards();
    build_cards();
    lv_label_set_text_fmt(s_msg, "上下移动 · OK 翻牌");
}

static void build(void)
{
    s_scr = ui_pixel_screen_create("");
    s_lvl = ui_pixel_label(s_scr, "翻牌配对", &font_zh14, UI_INK);
    lv_obj_set_pos(s_lvl, 12, 15);
    s_score = ui_pixel_label(s_scr, "0", &font_game, UI_INK);
    lv_obj_align(s_score, LV_ALIGN_TOP_RIGHT, -10, 10);

    s_msg = ui_pixel_label(s_scr, "上下移动 · OK 翻牌", &font_zh14, 0xFFFFFF);
    lv_obj_align(s_msg, LV_ALIGN_BOTTOM_MID, 0, -14);

    s_go_title = ui_pixel_label(s_scr, "通关!", &font_zh14, 0xB00000);
    lv_obj_align(s_go_title, LV_ALIGN_TOP_MID, 0, 96); lv_obj_add_flag(s_go_title, LV_OBJ_FLAG_HIDDEN);
    s_go_line = ui_pixel_label(s_scr, "", &font_zh14, 0x1A4A68);
    lv_obj_align(s_go_line, LV_ALIGN_TOP_MID, 0, 120); lv_obj_add_flag(s_go_line, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < 3; i++) {
        s_go_top[i] = ui_pixel_label(s_scr, "", &font_zh14, UI_INK);
        lv_obj_set_pos(s_go_top[i], 22, 152 + i * 26);
        lv_obj_add_flag(s_go_top[i], LV_OBJ_FLAG_HIDDEN);
    }

    // 一次性建满 16 张牌(全程不再 delete/create)
    s_stride = (int)lv_draw_buf_width_to_stride(CARD_W, LV_COLOR_FORMAT_RGB565) / (int)sizeof(uint16_t);
    if (s_stride < CARD_W) s_stride = CARD_W;
    for (int i = 0; i < CARD_MAX; i++) {
        s_gbuf[i] = (uint16_t *)malloc((size_t)s_stride * CARD_H * sizeof(uint16_t));
        if (!s_gbuf[i]) ESP_LOGE(TAG, "gbuf[%d] 分配失败(%d B)", i, s_stride * CARD_H * 2);

        s_card[i] = lv_canvas_create(s_scr);
        lv_obj_remove_flag(s_card[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(s_card[i], CARD_W, CARD_H);
        lv_obj_set_style_pad_all(s_card[i], 0, 0);
        lv_obj_set_style_border_width(s_card[i], 0, 0);   // 边框画在缓冲里
        lv_obj_set_style_radius(s_card[i], 0, 0);
        lv_obj_set_style_bg_opa(s_card[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_outline_width(s_card[i], 0, 0);
        lv_obj_set_style_shadow_width(s_card[i], 0, 0);
        if (s_gbuf[i])
            lv_canvas_set_buffer(s_card[i], s_gbuf[i], CARD_W, CARD_H, LV_COLOR_FORMAT_RGB565);
        lv_obj_add_flag(s_card[i], LV_OBJ_FLAG_HIDDEN);
    }

    lv_screen_load(s_scr);
    s_tick = lv_timer_create(tick_cb, 40, NULL);
    // 诊断心跳:堆 + 最近一次全量刷新耗时。稳定后可整块下架(连同 dbg_cb)。
    s_dbg  = lv_timer_create(dbg_cb, 10000, NULL);
#if MM_AUTOPILOT
    s_auto = lv_timer_create(auto_cb, 700, NULL);
#endif
}

void demo_memory_enter(void) { s_st = ST_PLAY; build(); new_game(); }

void demo_memory_exit(void)
{
    if (s_tick) { lv_timer_del(s_tick); s_tick = NULL; }
    if (s_dbg)  { lv_timer_del(s_dbg);  s_dbg  = NULL; }
    if (s_scr)  { lv_obj_delete(s_scr); s_scr = NULL; }   // 16 张牌是它的子对象,一并销毁
    for (int i = 0; i < CARD_MAX; i++) {
        s_card[i] = NULL;
        free(s_gbuf[i]); s_gbuf[i] = NULL;
    }
    s_lvl = s_score = s_msg = s_go_title = s_go_line = NULL;
    for (int i = 0; i < 3; i++) s_go_top[i] = NULL;
}

void demo_memory_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (ev != BSP_BTN_PRESS && ev != BSP_BTN_CLICK) return;

    if (s_st == ST_OVER) {
        if (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK) new_game();
        return;
    }
    // 对照进行中,暂时锁输入(仅允许继续看)
    if (s_pick2 >= 0 && s_pick1 >= 0) return;

    if ((btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) && ev == BSP_BTN_PRESS) {
        int old = s_cursor;
        int step = (btn == BSP_BTN_UP) ? -1 : 1;
        s_cursor = (s_cursor + step + s_count) % s_count;
        if (s_cursor != old) { card_paint(old); card_paint(s_cursor); }  // 只重画两张
    } else if (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK) {
        int i = s_cursor;
        if (s_face[i] != 0) { beep(240, 30); return; }
        s_face[i] = 1;
        if (s_pick1 < 0) {
            s_pick1 = i;
            lv_label_set_text(s_msg, "再翻一张");
            ESP_LOGI(TAG, "pick1 idx=%d sym=%d", i, s_sym[i]);
        } else if (i != s_pick1) {
            s_pick2 = i;
            lv_label_set_text(s_msg, "…");
            s_cmp_acc = 0;
            ESP_LOGI(TAG, "pick2 idx=%d sym=%d  (p1 idx=%d sym=%d)",
                     i, s_sym[i], s_pick1, s_sym[s_pick1]);
        } else {
            s_face[i] = 0;
            lv_label_set_text(s_msg, "同一张,换一张");
        }
        card_paint(i);
        beep(520, 25);
    }
}
