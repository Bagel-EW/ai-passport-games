// main/demo_avatar.h —— 虚拟形象 Home / 形象选择 共享声明（NEW FILE, Phase 3 草稿）
//
// 这是 Phase 3 新增文件，独立于 demo.h（不修改它）。后续接入时：
//   - 在 demo.h 追加 #include "demo_avatar.h"（或直接在此声明后由 main.c 引用）
//   - 在 main.c 的 DEMOS[] 加入 demo_avatar_home_enter/exit/key 与 demo_avatar_enter/exit/key
//   - 在 main/CMakeLists.txt 的 SRCS 加入 demo_avatar.c、demo_avatar_home.c
//
// 形象数据模型：
//   sel ∈ [0, AVATAR_PRESETS-1]  → 使用内置像素预设形象（矢量绘制，零 RAM）
//   sel == AVATAR_CUSTOM          → 使用 PC 经串口推送到 SPIFFS 的 /spiffs/avatar.bin
// 当前选择持久化在 /spiffs/avatar_sel.txt（一行 "sel=N"），与 weather.txt 范式一致。
#pragma once

#include "demo.h"   // 引入 bsp_btn_t / bsp_btn_ev_t 及 demo_entry_t 约定

#ifdef __cplusplus
extern "C" {
#endif

// —— 形象选择页（菜单项「虚拟形象」）——
void demo_avatar_enter(void);
void demo_avatar_exit(void);
void demo_avatar_key(bsp_btn_t btn, bsp_btn_ev_t ev);

// —— 开机「虚拟形象 Home」页 ——
void demo_avatar_home_enter(void);
void demo_avatar_home_exit(void);
void demo_avatar_home_key(bsp_btn_t btn, bsp_btn_ev_t ev);

// —— 共享 helper（供 home 与 selection 复用）——
#define AVATAR_PRESETS 4                 // 内置预设数量
#define AVATAR_CUSTOM   AVATAR_PRESETS   // sel 取该值表示使用 PC 推送的自定义形象

// 在 parent 的 (x,y) 处画一个 size×size 的像素风预设形象（preset ∈ [0,AVATAR_PRESETS-1]）
void avatar_draw_preset(lv_obj_t *parent, int preset, int x, int y, int size);

// 读取当前选择：0..AVATAR_PRESETS-1 或 AVATAR_CUSTOM
int  avatar_load_sel(void);
// 写入当前选择
void avatar_save_sel(int sel);

// 从 SPIFFS 读取 PC 推送的自定义形象（RGB565，含 8 字节头）。
// 成功返回 true，*out_buf 为 malloc 的 RGB565 数据（调用方用 avatar_free 释放），
// *out_w/*out_h 为像素尺寸。失败（无文件/头错/内存不足）返回 false。
bool avatar_load_custom(uint8_t **out_buf, int *out_w, int *out_h);
void avatar_free(uint8_t *buf);

#ifdef __cplusplus
}
#endif
