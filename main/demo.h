// main/demo.h —— 每个演示页实现的统一接口。
// 新增一个演示页 = 实现这三个函数 + 在 main.c 的 DEMOS[] 里加一行。
#pragma once

#include "bsp_button.h"

typedef struct {
    const char *name;
    void (*enter)(void);                          // 建自己的屏并载入
    void (*exit)(void);                           // 删屏、停定时器、释放资源
    void (*key)(bsp_btn_t btn, bsp_btn_ev_t ev);  // 收按键(长按确定已被 main 拦截)
} demo_entry_t;

// 由游戏内部主动要求返回上一层(派对菜单的「返回」项等)。
// main.c 统一负责调用当前游戏的 exit() 并重建上级菜单,调用方不要再自己退。
// 必须在 LVGL 锁内调用(即来自按键回调)。
void main_request_back(void);

// 各演示页(定义在各自的 .c 里)
void demo_display_enter(void); void demo_display_exit(void);
void demo_display_key(bsp_btn_t btn, bsp_btn_ev_t ev);

void demo_button_enter(void);  void demo_button_exit(void);
void demo_button_key(bsp_btn_t btn, bsp_btn_ev_t ev);

void demo_audio_enter(void);   void demo_audio_exit(void);
void demo_audio_key(bsp_btn_t btn, bsp_btn_ev_t ev);

void demo_battery_enter(void); void demo_battery_exit(void);
void demo_battery_key(bsp_btn_t btn, bsp_btn_ev_t ev);

void demo_party_enter(void);    void demo_party_exit(void);
void demo_party_key(bsp_btn_t btn, bsp_btn_ev_t ev);

void demo_shengbei_enter(void);  void demo_shengbei_exit(void);
void demo_shengbei_key(bsp_btn_t btn, bsp_btn_ev_t ev);

void demo_breakout_enter(void);  void demo_breakout_exit(void);
void demo_breakout_key(bsp_btn_t btn, bsp_btn_ev_t ev);

void demo_snake_enter(void);  void demo_snake_exit(void);
void demo_snake_key(bsp_btn_t btn, bsp_btn_ev_t ev);

void demo_memory_enter(void);  void demo_memory_exit(void);
void demo_memory_key(bsp_btn_t btn, bsp_btn_ev_t ev);

void demo_weather_enter(void);   void demo_weather_exit(void);
void demo_weather_key(bsp_btn_t btn, bsp_btn_ev_t ev);
