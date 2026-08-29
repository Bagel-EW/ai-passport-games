// main/app_bgm.h —— 8-bit 芯片音乐 BGM + 游戏音效统一出口 + 开机赛博主题曲。
//
// 设计要点:
//  1) BGM 在【独立 FreeRTOS 任务】里循环合成并写 codec,绝不在按键回调 / LVGL 任务里播
//     (bsp_audio_write 会阻塞到 I2S DMA 完成)。
//  2) BGM 与游戏音效(beep)争用同一个 ES8311,故统一走一把互斥锁;拿不到锁就跳过本次
//     写入(音乐短暂让位),绝不阻塞按键。
//  3) 音效必须调用 app_bgm_beep(),不要各自直接 bsp_audio_write(),否则会出杂音。
//  4) 开关/音量存 SPIFFS(/spiffs/bgm_on、/spiffs/vol),掉电保持。
#pragma once

#include <stdbool.h>
#include <stdint.h>

// 初始化:读开关/音量 → 初始化 codec → 建播放任务。
// 必须在 bsp_audio_init() 之后调用。codec 不可用时内部降级,不崩。
void app_bgm_init(void);

// codec 是否就绪(ES8311 初始化成功)。false 时 BGM 与音效都静默。
bool app_bgm_ready(void);

// BGM 开关(读内存副本,不访问存储)。
bool app_bgm_enabled(void);

// 设置开关:写 SPIFFS + 启停播放任务。
void app_bgm_set_enabled(bool on);

// 开机动画期间播赛博主题曲(区别于常规 BGM);动画结束切回常规 BGM。
// 在 LVGL 上下文调用即可(只改内部状态,不阻塞)。
void app_bgm_play_boot(void);
void app_bgm_play_normal(void);

// 音量 0..100(设置页可调,存 /spiffs/vol)。BGM 按 62%、音效按 95% 派生。
int  app_bgm_volume(void);
void app_bgm_set_volume(int pct);

// 短音效。hz<=0 时按 8kHz 处理;ms 建议 ≤200。与 BGM 互斥,抢占不到锁会直接放弃本次音效。
void app_bgm_beep(int hz, int ms);
