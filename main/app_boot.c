// main/app_boot.c —— 开机动画:赛博终端启动(CRT 扫描 + 网格 + 逐字 boot 日志 + 霓虹加载条)
// 结束后回调交给主流程(主菜单)。零新增图片资源,纯 LVGL 图元 + 补间。
//
// ⚠ 性能约束(踩过坑):
//   - 面板 shadow_width 过大会拖慢 LVGL 软件渲染循环(曾让 14ms 定时器实际 40ms 才触发),
//     故阴影宽度收敛在 6、opa 30。
//   - 半透明大对象(扫描光带/全屏闪光)每帧都要 blend,尺寸刻意压小(240x22 / 一次性 320ms)。
//   - 对象总数控制在 ~28 个以内。
#include "app_boot.h"

#include <string.h>

#include "app_bgm.h"
#include "esp_log.h"
#include "font_zh.h"
#include "lvgl.h"

#define BG      0x04070A
#define PANEL   0x06120F
#define BORDER  0x0E4A43
#define NEON    0x19E3C0
#define ACCENT  0xFF4D8D
#define DIM     0x2E6B62

// boot 日志(行宽控制在 ~22 字符内,适配 14px 字体与面板宽度)
static const char *k_lines[5] = {
    "> boot ai-passport v1.0",
    "> load identity .... ok",
    "> mount enclave .... ok",
    "> link neural ...... ok",
    "> exec gen_passport()",
};

typedef struct {
    lv_obj_t *scr;
    lv_obj_t *log;
    lv_obj_t *exec;
    lv_obj_t *bar;
    lv_obj_t *track;
    lv_obj_t *pct;
    lv_obj_t *ready;
    lv_obj_t *cursor;
    lv_obj_t *flash;
    lv_obj_t *led[3];
    lv_timer_t *typ;
    lv_timer_t *fx;
    lv_timer_t *fin;
    void (*done)(void);
    int li;          // 当前日志行(0..3 走 log,4 走 exec)
    int ch;
    int pause;
    int led_i;
    char lines[4][32];   // 已打完的日志行
    char cur[32];        // 正在打的一行
    char disp[220];      // 拼好的显示串(含 recolor 标记)
    char execbuf[48];
} boot_ctx_t;

static boot_ctx_t s_ctx;

// ---------- 动效回调 ----------
static void scan_cb(void *obj, int32_t v)      { lv_obj_set_y((lv_obj_t *)obj, v); }
static void border_cb(void *obj, int32_t v)    { lv_obj_set_style_border_opa(obj, (lv_opa_t)v, 0); }
static void flash_cb(void *obj, int32_t v)     { lv_obj_set_style_bg_opa(obj, (lv_opa_t)v, 0); }

static void flash_done_cb(lv_anim_t *a)
{
    lv_obj_delete((lv_obj_t *)a->var);
    s_ctx.flash = NULL;
}

static void real_finish(lv_timer_t *t)
{
    lv_timer_delete(t);
    void (*done)(void) = s_ctx.done;
    lv_obj_t *scr = s_ctx.scr;
    if (s_ctx.typ) lv_timer_delete(s_ctx.typ);
    if (s_ctx.fx)  lv_timer_delete(s_ctx.fx);
    app_bgm_play_normal();          // 开机赛博主题曲结束,切回常规 BGM
    ESP_LOGI("app_boot", "terminal done -> menu");
    memset(&s_ctx, 0, sizeof(s_ctx));
    if (done) done();
    if (scr) lv_obj_delete(scr);
}

static void bar_anim_cb(void *obj, int32_t v)
{
    lv_obj_set_width((lv_obj_t *)obj, v);
    // 扫描头是 track 的子对象,坐标相对 track 内容区(track 有 pad 1)
    if (s_ctx.cursor) lv_obj_set_x(s_ctx.cursor, v - 4);
    if (s_ctx.pct) lv_label_set_text_fmt(s_ctx.pct, "%d%%", (int)(v * 100 / 144));
}

static void bar_complete_cb(lv_anim_t *a)
{
    (void)a;
    // 全屏霓虹闪一下再落 READY
    if (s_ctx.flash) {
        lv_anim_t f;
        lv_anim_init(&f);
        lv_anim_set_var(&f, s_ctx.flash);
        lv_anim_set_exec_cb(&f, flash_cb);
        lv_anim_set_values(&f, 150, 0);
        lv_anim_set_duration(&f, 320);
        lv_anim_set_path_cb(&f, lv_anim_path_ease_out);
        lv_anim_set_completed_cb(&f, flash_done_cb);
        lv_anim_start(&f);
    }
    lv_obj_set_style_opa(s_ctx.ready, LV_OPA_COVER, 0);
    app_bgm_beep(784, 70);
    app_bgm_beep(1047, 110);
    s_ctx.fin = lv_timer_create(real_finish, 700, NULL);
}

static void start_bar(void)
{
    lv_obj_set_style_opa(s_ctx.ready, LV_OPA_MIN, 0);
    // 光标从日志区挪进加载条,变成跟着进度跑的扫描头
    if (s_ctx.cursor && s_ctx.track) {
        lv_obj_set_parent(s_ctx.cursor, s_ctx.track);
        lv_obj_set_size(s_ctx.cursor, 5, 10);
        lv_obj_set_pos(s_ctx.cursor, -4, 0);
        lv_obj_set_style_bg_color(s_ctx.cursor, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_opa(s_ctx.cursor, LV_OPA_COVER, 0);
    }

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_ctx.bar);
    lv_anim_set_exec_cb(&a, bar_anim_cb);
    lv_anim_set_values(&a, 0, 144);
    lv_anim_set_duration(&a, 750);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_completed_cb(&a, bar_complete_cb);
    lv_anim_start(&a);
}

// 光标闪烁 + 状态灯轮询(250ms)
static void fx_cb(lv_timer_t *t)
{
    (void)t;
    static bool on = false;
    on = !on;
    // li>=5 之后光标已经变成加载条的扫描头,不再闪
    if (s_ctx.cursor && s_ctx.li < 5)
        lv_obj_set_style_opa(s_ctx.cursor, on ? LV_OPA_COVER : LV_OPA_MIN, 0);

    s_ctx.led_i = (s_ctx.led_i + 1) % 3;
    for (int i = 0; i < 3; i++) {
        if (s_ctx.led[i])
            lv_obj_set_style_bg_opa(s_ctx.led[i], i == s_ctx.led_i ? LV_OPA_COVER : LV_OPA_30, 0);
    }
}

// 把"已完成的最后一行"高亮成白色(LVGL 的 recolor:#RRGGBB 文本#)
static void log_render(void)
{
    boot_ctx_t *c = &s_ctx;
    c->disp[0] = 0;
    for (int j = 0; j < c->li && j < 4; j++) {
        if (j == c->li - 1) strcat(c->disp, "#FFFFFF");
        strcat(c->disp, c->lines[j]);
        if (j == c->li - 1) strcat(c->disp, "#");
        strcat(c->disp, "\n");
    }
    strcat(c->disp, c->cur);
    lv_label_set_text(c->log, c->disp);
}

static void type_one(void)
{
    boot_ctx_t *c = &s_ctx;
    if (c->li >= 5) return;
    if (c->pause > 0) { c->pause -= 14; return; }

    const char *line = k_lines[c->li];
    char cc = line[c->ch];

    if (c->li < 4) {
        if (cc) {
            size_t n = strlen(c->cur);
            if (n + 1 < sizeof(c->cur)) { c->cur[n] = cc; c->cur[n + 1] = 0; }
        }
        c->ch++;
        if (line[c->ch] == 0) {
            strncpy(c->lines[c->li], c->cur, sizeof(c->lines[0]) - 1);
            c->cur[0] = 0;
            c->li++;
            c->ch = 0;
            c->pause = 90;
            app_bgm_beep(1500, 8);          // 每行打完"嗒"一下
        }
        log_render();
    } else {
        if (cc) {
            size_t n = strlen(c->execbuf);
            if (n + 1 < sizeof(c->execbuf)) { c->execbuf[n] = cc; c->execbuf[n + 1] = 0; }
        }
        c->ch++;
        lv_label_set_text(c->exec, c->execbuf);
        if (line[c->ch] == 0) {
            c->li = 5;
            start_bar();
        }
    }
}

static void typing_cb(lv_timer_t *t)
{
    (void)t;
    type_one();
    type_one();
}

// ---------- 小工具 ----------
static lv_obj_t *bar_obj(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color)
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

static void anim_repeat(lv_anim_t *a, void *var, lv_anim_exec_xcb_t cb,
                        int32_t from, int32_t to, uint32_t dur, uint32_t play)
{
    lv_anim_init(a);
    lv_anim_set_var(a, var);
    lv_anim_set_exec_cb(a, cb);
    lv_anim_set_values(a, from, to);
    lv_anim_set_duration(a, dur);
    lv_anim_set_path_cb(a, lv_anim_path_linear);
    lv_anim_set_repeat_count(a, LV_ANIM_REPEAT_INFINITE);
    if (play) lv_anim_set_playback_time(a, play);
    lv_anim_start(a);
}

void app_boot_show(void (*done)(void))
{
    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.done = done;

    s_ctx.scr = lv_obj_create(NULL);
    lv_obj_remove_flag(s_ctx.scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_ctx.scr, lv_color_hex(BG), 0);
    lv_obj_set_style_border_width(s_ctx.scr, 0, 0);
    lv_obj_set_style_pad_all(s_ctx.scr, 0, 0);

    // ---- 背景网格(8 个细长对象,低不透明) ----
    static const int gx[4] = { 40, 90, 140, 200 };
    static const int gy[4] = { 60, 120, 180, 240 };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *v = bar_obj(s_ctx.scr, gx[i], 0, 1, 320, NEON);
        lv_obj_set_style_bg_opa(v, 24, 0);
        lv_obj_t *h = bar_obj(s_ctx.scr, 0, gy[i], 240, 1, NEON);
        lv_obj_set_style_bg_opa(h, 24, 0);
    }

    lv_anim_t a;

    // ---- 终端面板 ----
    lv_obj_t *panel = lv_obj_create(s_ctx.scr);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(panel, 208, 196);
    lv_obj_align(panel, LV_ALIGN_TOP_LEFT, 16, 40);
    lv_obj_set_style_radius(panel, 6, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(PANEL), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(BORDER), 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_set_style_shadow_color(panel, lv_color_hex(NEON), 0);
    lv_obj_set_style_shadow_width(panel, 6, 0);
    lv_obj_set_style_shadow_opa(panel, 30, 0);
    lv_obj_set_style_shadow_spread(panel, 0, 0);
    // 边框呼吸(青色明暗循环)
    anim_repeat(&a, panel, border_cb, 40, 210, 900, 900);

    // 标题栏
    lv_obj_t *hb = lv_label_create(panel);
    lv_obj_set_style_text_font(hb, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(hb, lv_color_hex(DIM), 0);
    lv_label_set_text(hb, "AI-PASSPORT // BOOT");
    lv_obj_align(hb, LV_ALIGN_TOP_LEFT, 8, 6);

    // 状态灯(轮询闪)
    for (int i = 0; i < 3; i++) {
        s_ctx.led[i] = bar_obj(panel, 168 + i * 12, 10, 7, 7,
                               i == 0 ? NEON : (i == 1 ? ACCENT : 0xFFD928));
        lv_obj_set_style_radius(s_ctx.led[i], 3, 0);
        lv_obj_set_style_bg_opa(s_ctx.led[i], LV_OPA_30, 0);
    }

    // 内容区(flex 纵向,自动堆叠)
    lv_obj_t *content = lv_obj_create(panel);
    lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(content, 188, 150);
    lv_obj_align(content, LV_ALIGN_TOP_LEFT, 6, 26);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(content, 2, 0);
    lv_obj_set_style_bg_color(content, lv_color_hex(PANEL), 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 4, 0);

    s_ctx.log = lv_label_create(content);
    lv_obj_set_style_text_font(s_ctx.log, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(s_ctx.log, lv_color_hex(NEON), 0);
    lv_obj_set_width(s_ctx.log, 180);
    lv_label_set_long_mode(s_ctx.log, LV_LABEL_LONG_CLIP);
    lv_label_set_recolor(s_ctx.log, true);
    lv_label_set_text(s_ctx.log, "");

    s_ctx.exec = lv_label_create(content);
    lv_obj_set_style_text_font(s_ctx.exec, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(s_ctx.exec, lv_color_hex(ACCENT), 0);
    lv_obj_set_width(s_ctx.exec, 180);
    lv_label_set_long_mode(s_ctx.exec, LV_LABEL_LONG_CLIP);
    lv_label_set_text(s_ctx.exec, "");

    // 光标块(打完日志后会被挪到加载条上当扫描头)
    s_ctx.cursor = bar_obj(content, 0, 0, 7, 12, NEON);

    // ---- 加载条 + 百分比 ----
    lv_obj_t *track = lv_obj_create(s_ctx.scr);
    s_ctx.track = track;
    lv_obj_remove_flag(track, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(track, 148, 12);
    lv_obj_align(track, LV_ALIGN_TOP_LEFT, 46, 256);
    lv_obj_set_style_radius(track, 3, 0);
    lv_obj_set_style_border_width(track, 1, 0);
    lv_obj_set_style_border_color(track, lv_color_hex(BORDER), 0);
    lv_obj_set_style_bg_color(track, lv_color_hex(BG), 0);
    lv_obj_set_style_pad_all(track, 1, 0);

    s_ctx.bar = bar_obj(track, 0, 0, 0, 10, NEON);
    lv_obj_set_style_shadow_color(s_ctx.bar, lv_color_hex(NEON), 0);
    lv_obj_set_style_shadow_width(s_ctx.bar, 8, 0);
    lv_obj_set_style_shadow_opa(s_ctx.bar, 80, 0);

    s_ctx.pct = lv_label_create(s_ctx.scr);
    lv_obj_set_style_text_font(s_ctx.pct, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(s_ctx.pct, lv_color_hex(DIM), 0);
    lv_label_set_text(s_ctx.pct, "0%");
    lv_obj_align(s_ctx.pct, LV_ALIGN_TOP_LEFT, 200, 256);

    s_ctx.ready = lv_label_create(s_ctx.scr);
    lv_obj_set_style_text_font(s_ctx.ready, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(s_ctx.ready, lv_color_hex(NEON), 0);
    lv_label_set_text(s_ctx.ready, "READY");
    lv_obj_align(s_ctx.ready, LV_ALIGN_TOP_LEFT, 0, 284);
    lv_obj_set_style_opa(s_ctx.ready, LV_OPA_MIN, 0);
    lv_obj_set_style_text_letter_space(s_ctx.ready, 3, 0);

    // ---- CRT 扫描光带(最后建 = 盖在终端面板之上,才像扫描线) ----
    lv_obj_t *scan = bar_obj(s_ctx.scr, 0, -22, 240, 22, NEON);
    lv_obj_set_style_bg_opa(scan, 32, 0);
    anim_repeat(&a, scan, scan_cb, -22, 320, 2200, 0);

    // ---- 全屏闪光(READY 前闪一下,默认全透明,最顶层) ----
    s_ctx.flash = bar_obj(s_ctx.scr, 0, 0, 240, 320, NEON);
    lv_obj_set_style_bg_opa(s_ctx.flash, 0, 0);

    lv_screen_load(s_ctx.scr);

    app_bgm_play_boot();                 // 开机动画配赛博主题曲(区别于常规 BGM)

    ESP_LOGI("app_boot", "terminal boot start");
    s_ctx.typ = lv_timer_create(typing_cb, 14, NULL);
    s_ctx.fx  = lv_timer_create(fx_cb, 250, NULL);
}
