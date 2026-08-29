// main/fap_screenshot.h —— FAP_SCREENSHOT_V1 串口截屏协议(固件侧)声明
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// 启动 FAP_SCREENSHOT_V1 监听器(在 LVGL 初始化之后调用)。
// 监听控制台(stdin / USB-CDC)收到的 "FAP_SCREENSHOT_V1\n",
// 抓当前全屏 RGB565 帧缓冲并回传。纯观测性,不重启/不擦写/不输出凭证。
void fap_screenshot_start(void);

#ifdef __cplusplus
}
#endif
