// main/spiffs_uart.h —— 文件推送监听器声明（NEW FILE, Phase 3/4 配套）
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// 启动 PC→设备 文件推送监听器（在 SPIFFS 挂载之后调用）。
// 具体 UART 端口/引脚/波特见 spiffs_uart.c 顶部配置。
void spiffs_uart_start(void);

#ifdef __cplusplus
}
#endif
