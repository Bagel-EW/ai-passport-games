// main/app_settings.h —— 主菜单「设置」页:BGM 开关 / 亮度 / 电量 / 返回。
//
// 它不是 demo_entry_t(那套接口只支持"选中即玩",没法做"行内调节 + 返回"),
// 由 main.c 的 NAV_SETTINGS 状态直接驱动。
#pragma once

#include "bsp_button.h"
#include <stdbool.h>

// 建屏并载入。进入前 main.c 必须持有 LVGL 锁。
void app_settings_enter(void);

// 删屏、停定时器。
void app_settings_exit(void);

// 处理按键;返回 true 表示用户请求退出(选中「返回」),main.c 应接着调 exit + nav_back。
bool app_settings_key(bsp_btn_t btn, bsp_btn_ev_t ev);
