// main/app_bgm.c —— 8-bit 芯片音乐 BGM + 音效 + 开机音乐(WAV)。
//
// BGM 是【实时合成】的(方波主旋律 + 三角波贝斯,120 BPM 循环);开机音乐是
// 【内嵌 WAV】(tools/gen_boot_wav.py 生成 boot_music.h,16kHz/16bit/单声道,
// 约 2.4s),开机动画期间播一次,播完自动落回常规 BGM。全程整数运算。
//
// 音量(设置页可调,0..100 存 /spiffs/vol):BGM 按 62%、音效按 95% 派生。
#include "app_bgm.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsp_audio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "boot_music.h"      // 开机音乐 PCM(自动生成,勿手改)

static const char *TAG = "bgm";

#define SR      16000            // 采样率(与 bsp_audio_set_format 一致)
#define CHUNK   256              // 每块 16ms
#define TICK    (SR * 60 / (120 * 4))   // 常规 BGM:16 分音符采样数(120 BPM)

// 音量派生比例:BGM 压低、音效突出(基础音量见设置页,存 /spiffs/vol)
#define VOL_RATIO_BGM 62
#define VOL_RATIO_SFX 95
#define VOL_DEFAULT   75

// ---------------------------------------------------------------------------
// 曲谱:MIDI 音高(60=C4),-1 = 休止
// ---------------------------------------------------------------------------
// 主旋律:每个音占 2 个 16 分音符(= 8 分音符),共 16 个音 / 32 步
static const int8_t k_mel[] = {
    72, 76, 79, 76, 81, 79, 76, 74,   // C5 E5 G5 E5 | A5 G5 E5 D5
    77, 81, 84, 81, 79, 76, 72, 72,   // F5 A5 C6 A5 | G5 E5 C5 (延音)
};
#define MEL_N       ((int)(sizeof(k_mel) / sizeof(k_mel[0])))
#define MEL_STEPS   2                    // 每个音持续的 16 分音符数

// 贝斯:每个音占 8 个 16 分音符(= 二分音符),共 4 个音 / 32 步
static const int8_t k_bass[] = { 48, 45, 53, 43 };   // C3 A2 F3 G2
#define BASS_N      ((int)(sizeof(k_bass) / sizeof(k_bass[0])))
#define BASS_STEPS  8

// ---------------------------------------------------------------------------
// 声部
// ---------------------------------------------------------------------------
typedef struct {
    uint32_t ph;     // 相位累加器(0..2^32 为一周期)
    uint32_t inc;    // 每采样相位增量;0 = 休止
    uint32_t t;      // 当前音符已渲染采样数
    uint32_t dur;    // 当前音符总采样数
    int      step;   // 曲谱下标
    int      amp;    // 幅度
    bool     tri;    // true=三角波(贝斯),false=25% 方波(主旋律)
} voice_t;

static uint16_t s_hz[128];              // MIDI 音高 → 频率表(启动时算一次)
static voice_t  s_v[2];

static TaskHandle_t     s_task;
static SemaphoreHandle_t s_mux;         // 保护 codec(BGM vs 音效)
static volatile bool    s_on;           // 用户开关
static bool             s_ready;        // codec 就绪
static int              s_vol = VOL_DEFAULT;   // 用户音量 0..100(设置页可调)
static volatile bool    s_wav_playing;  // 正在播开机音乐(WAV)
static uint32_t         s_wav_pos;      // 已播样本数

// 开关/音量状态存 SPIFFS(与游戏 TOP3 榜单一处,省掉 nvs_flash 依赖)
#define FLAG_PATH "/spiffs/bgm_on"
#define VOL_PATH  "/spiffs/vol"

// ---------- 曲谱/波形 ----------
static void build_hz_table(void)
{
    for (int m = 0; m < 128; m++) s_hz[m] = (uint16_t)(440.0f * powf(2.0f, (m - 69) / 12.0f));
}

static void voice_next(voice_t *v, const int8_t *seq, int seq_n, int steps)
{
    int note = seq[v->step % seq_n];
    v->step++;
    v->t = 0;
    v->dur = (uint32_t)steps * TICK;
    v->inc = (note > 0 && note < 128) ? (uint32_t)(((uint64_t)s_hz[note] << 31) / SR) : 0;
}

// 取一个采样并推进声部状态(休止也要推进 t,否则音符永不结束)
static inline int32_t voice_sample(voice_t *v)
{
    uint32_t atk = v->dur / 16;                       // 6% 起音,避免爆音
    uint32_t e = (v->t < atk) ? (v->t * 256u / atk)
                              : (256u - ((v->t - atk) * 110u) / (v->dur - atk));
    int32_t s = 0;
    if (v->inc) {
        uint16_t w = (uint16_t)(v->ph >> 16);         // 波形查表下标 0..65535
        if (v->tri) {
            int32_t t = (w < 32768) ? w : (int32_t)(65535 - w);
            s = t * 2 - 32767;
        } else {
            s = (w < 16384) ? 32767 : -32767;         // 25% 占空比方波
        }
        v->ph += v->inc;
    }
    v->t++;
    return (int32_t)((int64_t)s * (int32_t)e * v->amp >> 16);
}

static void voices_reset(void)
{
    memset(s_v, 0, sizeof(s_v));
    s_v[0].amp = 90;  s_v[0].tri = false;   // 主旋律
    s_v[1].amp = 64;  s_v[1].tri = true;    // 贝斯
    s_v[0].dur = s_v[1].dur = 1;            // 让首帧触发 voice_next(避免除零)
}

// ---------- 播放任务 ----------
static void bgm_task(void *arg)
{
    (void)arg;
    static int16_t buf[CHUNK];              // 静态:bsp 栈只有几 KB
    int64_t last_us = 0;
    int err = 0;

    for (;;) {
        if (!s_on || !s_ready) { vTaskDelay(pdMS_TO_TICKS(200)); last_us = 0; continue; }

        // 抢不到锁说明音效在播,让位(音乐会有一个小停顿,但不卡按键)
        if (xSemaphoreTake(s_mux, pdMS_TO_TICKS(50)) != pdTRUE) continue;

        // 开机音乐(WAV)优先:播一次,播完自动落回常规合成 BGM
        if (s_wav_playing) {
            const uint32_t total = BOOT_MUSIC_LEN / 2;
            for (int i = 0; i < CHUNK; i++) {
                int32_t s = 0;
                if (s_wav_pos < total) {
                    const uint8_t *p = boot_music_pcm + s_wav_pos * 2;
                    s = (int16_t)(p[0] | (p[1] << 8));
                    s_wav_pos++;
                    // 16ms 淡入淡出,防首尾爆音
                    uint32_t fade = (s_wav_pos < (uint32_t)CHUNK) ? s_wav_pos
                                  : (total - s_wav_pos < (uint32_t)CHUNK) ? (total - s_wav_pos)
                                  : CHUNK;
                    s = (int32_t)(s * (int64_t)fade / CHUNK);
                }
                buf[i] = (int16_t)s;
            }
            if (s_wav_pos >= total) {
                s_wav_playing = false;
                s_wav_pos = 0;
                ESP_LOGI(TAG, "开机音乐播完 -> 常规 BGM");
            }
        } else {
            for (int i = 0; i < CHUNK; i++) {
                if (s_v[0].t >= s_v[0].dur)
                    voice_next(&s_v[0], k_mel,  MEL_N,  MEL_STEPS);
                if (s_v[1].t >= s_v[1].dur)
                    voice_next(&s_v[1], k_bass, BASS_N, BASS_STEPS);
                int32_t a = voice_sample(&s_v[0]) + voice_sample(&s_v[1]);
                if (a >  32000) a =  32000;
                if (a < -32000) a = -32000;
                buf[i] = (int16_t)a;
            }
        }
        esp_err_t e = bsp_audio_write(buf, CHUNK * 2);
        xSemaphoreGive(s_mux);

        // ⚠ 防空转保命:正常情况下这条 write 会阻塞到 DMA 消化完(≈16ms/块)。
        //   但若 codec 没打开 / I2S 没跑起来,write 会立刻报错返回,任务就会全速
        //   打转把 IDLE 饿死(task watchdog 报警)。这里强制至少让出 1 tick。
        int64_t now = esp_timer_get_time();
        if (e != ESP_OK) {
            if (++err > 30) { s_ready = false; ESP_LOGE(TAG, "连续写失败,BGM 停用"); }
            vTaskDelay(pdMS_TO_TICKS(40));
            last_us = 0;
        } else {
            err = 0;
            if (now - last_us < 8000) vTaskDelay(1);
            last_us = esp_timer_get_time();
        }
    }
}

// 打开/复用 codec。必须在持有锁的情况下调,否则会和播放任务抢同一块硬件。
static bool codec_open(void)
{
    if (!s_mux || xSemaphoreTake(s_mux, pdMS_TO_TICKS(500)) != pdTRUE) return false;
    esp_err_t e = bsp_audio_set_format(SR, 16, 1);
    xSemaphoreGive(s_mux);
    return e == ESP_OK;
}

// 音量应用:BGM 按比例压低、音效突出
static void vol_bgm(void) { if (s_ready) bsp_audio_set_volume((uint8_t)(s_vol * VOL_RATIO_BGM / 100)); }
static void vol_sfx(void) { if (s_ready) bsp_audio_set_volume((uint8_t)(s_vol * VOL_RATIO_SFX / 100)); }

// ---------- 对外接口 ----------
void app_bgm_init(void)
{
    build_hz_table();
    voices_reset();
    s_mux = xSemaphoreCreateMutex();

    if (bsp_audio_init() != ESP_OK) {
        ESP_LOGW(TAG, "codec 不可用,BGM 与音效全部静默");
        return;
    }
    // ⚠ 必须 open codec,否则 bsp_audio_write() 立刻返回错误 → 播放任务全速空转
    if (!codec_open()) {
        ESP_LOGW(TAG, "codec open 失败,BGM 与音效全部静默");
        return;
    }
    s_ready = true;

    // 读开关(默认开)
    s_on = true;
    FILE *f = fopen(FLAG_PATH, "r");
    if (f) {
        char c = 0;
        if (fread(&c, 1, 1, f) == 1) s_on = (c == '1');
        fclose(f);
    }

    // 读音量(默认 75)
    f = fopen(VOL_PATH, "r");
    if (f) {
        char b[8] = {0};
        if (fread(b, 1, sizeof(b) - 1, f) > 0) s_vol = atoi(b);
        fclose(f);
    }
    if (s_vol < 0 || s_vol > 100) s_vol = VOL_DEFAULT;

    if (s_on) vol_bgm();
    // 与 LVGL 任务同优先级(4):两边都频繁阻塞,时间片轮转即可,不必抢占 UI
    xTaskCreate(bgm_task, "bgm", 3072, NULL, 4, &s_task);
    ESP_LOGI(TAG, "BGM 就绪 enabled=%d vol=%d%%", (int)s_on, s_vol);
}

bool app_bgm_ready(void)   { return s_ready; }
bool app_bgm_enabled(void) { return s_on; }

// 开机动画期间播开机音乐(WAV,播一次);动画结束切回常规 BGM。
// 都在 LVGL 上下文调用,只改状态,不阻塞。
void app_bgm_play_boot(void)
{
    if (!s_ready) return;
    if (BOOT_MUSIC_LEN >= 2) {
        s_wav_pos = 0;
        s_wav_playing = true;
        ESP_LOGI(TAG, "-> 开机音乐(WAV %u 样本 = %.2fs)",
                 (unsigned)(BOOT_MUSIC_LEN / 2),
                 (double)(BOOT_MUSIC_LEN / 2) / SR);
    } else {
        ESP_LOGW(TAG, "无内置开机音乐(重新生成 boot_music.h)");
    }
}

void app_bgm_play_normal(void)
{
    s_wav_playing = false;
    s_wav_pos = 0;
    ESP_LOGI(TAG, "-> 常规 BGM");
}

int  app_bgm_volume(void) { return s_vol; }

void app_bgm_set_volume(int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    s_vol = pct;
    if (s_ready && s_on) vol_bgm();
    FILE *f = fopen(VOL_PATH, "w");
    if (f) { fprintf(f, "%d", pct); fclose(f); }
    ESP_LOGI(TAG, "音量 -> %d%%", s_vol);
}

void app_bgm_set_enabled(bool on)
{
    if (!s_ready) return;
    s_on = on;
    if (on) {
        voices_reset();
        codec_open();                    // 游戏可能改过采样格式,重新对齐
        vol_bgm();
    }
    FILE *f = fopen(FLAG_PATH, "w");
    if (f) { fputc(on ? '1' : '0', f); fclose(f); }
    ESP_LOGI(TAG, "BGM %s", on ? "开" : "关");
}

void app_bgm_beep(int hz, int ms)
{
    if (!s_ready || ms <= 0) return;
    // 抢占不到锁(上一声还没播完)就直接丢弃本次音效 —— 绝不能让按键回调卡住
    if (xSemaphoreTake(s_mux, pdMS_TO_TICKS(60)) != pdTRUE) return;

    if (bsp_audio_set_format(SR, 16, 1) == ESP_OK) {
        vol_sfx();
        static int16_t b[320];
        int n = SR * ms / 1000;
        if (n > 320) n = 320;
        if (n > 0) {
            int f = (hz > 0) ? hz : 8000;
            int period = SR / ((f > 40) ? f : 40);
            if (period < 2) period = 2;
            for (int i = 0; i < n; i++) b[i] = ((i % period) < (period / 2)) ? 9000 : -9000;
            bsp_audio_write(b, (size_t)n * 2);
        }
        if (s_on) vol_bgm();
    }
    xSemaphoreGive(s_mux);
}
