// main/spiffs_uart.c —— PC→设备 文件推送监听器（NEW FILE, Phase 3/4 配套固件）
//
// 作用：在 UART 上监听 PC 侧 gen_avatar.py / push_weather.py 发来的「写 SPIFFS 文件」帧，
//       收到后把 DATA 写到指定路径（如 /spiffs/avatar.bin、/spiffs/weather.txt）。
//
// ⚠️ 重要：COM5 是 ESP32-C3 的「控制台/烧录」UART（ESP_LOG 与 idf.py monitor 共用），
//    不要在此占用它，否则日志会与帧互相打断。本监听器默认跑在 UART_NUM_1，
//    请用一块 USB-TTL 接到板子的空闲 GPIO（见下方 PUSH_UART_*_GPIO，按需修改），
//    然后 PC 侧用对应出来的 COM 口推送（gen_avatar.py --port COMx）。
//    若板子没有第二路 UART，可改用 SPIFFS 分区镜像烧录方式（见 tools/PHASE3_INTEGRATION.md）。
//
// 帧格式（小端）：
//   MAGIC    4B  0x41 0x56 0x50 0x01  ("AVP\x01")
//   CMD      1B  0x01=写文件  0x02=删文件
//   PATHLEN  1B  路径长度(≤32)
//   PATH     N B  ASCII 路径, 如 "/spiffs/avatar.bin"
//   DATALEN  4B   LE, DATA 字节数
//   DATA     M B  文件内容
//   CRC16    2B   CRC-16/CCITT(0x1021, init 0xFFFF) 覆盖 [CMD .. DATA]
// 收到并执行后回 1 字节：0x06=ACK(成功) | 0x15=NAK(失败)
//
// 接入方式（由主 agent 后续执行）：
//   - main/CMakeLists.txt 的 SRCS 加入 "spiffs_uart.c"
//   - main.c 的 app_main() 里 SPIFFS 挂载之后调用 spiffs_uart_start();
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "spiffs_uart";

// ---------- 配置（按你的板子修改）----------
#define PUSH_UART_NUM     UART_NUM_1
#define PUSH_UART_BAUD    115200
#define PUSH_UART_TX_GPIO 4     // TODO: 改成板子实际接到 USB-TTL 的 TX
#define PUSH_UART_RX_GPIO 5     // TODO: 改成板子实际接到 USB-TTL 的 RX
#define PUSH_UART_BUF     4096
#define PUSH_TASK_STACK   4096
#define PUSH_MAX_PATH     32
#define PUSH_MAX_DATA     (240 * 320 * 2 + 16)   // 最大一帧 payload（含头像头余量）

// ---------- 帧常量 ----------
static const uint8_t MAGIC[4] = {0x41, 0x56, 0x50, 0x01};
enum { CMD_WRITE = 0x01, CMD_DELETE = 0x02 };

// 增量 CRC-16/CCITT（poly 0x1021, init 0xFFFF）：可承接上一段的 crc 继续计算
static uint16_t crc16_step(uint16_t crc, const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

static uint16_t crc16_ccitt(const uint8_t *data, size_t len)
{
    return crc16_step(0xFFFF, data, len);
}

// 从 uart 读 len 字节，带超时（毫秒）；返回实际读到字节数
static size_t uart_read_exact(uart_port_t port, uint8_t *buf, size_t len, int timeout_ms)
{
    size_t got = 0;
    int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    while (got < len) {
        int n = uart_read_bytes(port, buf + got, len - got, 20 / portTICK_PERIOD_MS);
        if (n > 0) got += (size_t)n;
        if (esp_timer_get_time() > deadline) break;
    }
    return got;
}

static void push_task(void *arg)
{
    (void)arg;
    uint8_t *data = (uint8_t *)malloc(PUSH_MAX_DATA);
    uint8_t hdr[6];          // CMD, PATHLEN, DATALEN(4)
    uint8_t foot[2];         // CRC16
    uint8_t ack;

    if (!data) { ESP_LOGE(TAG, "malloc fail"); vTaskDelete(NULL); return; }
    ESP_LOGI(TAG, "listener ready on UART%d @ %d", PUSH_UART_NUM, PUSH_UART_BAUD);

    while (1) {
        // 1) 同步 MAGIC（3s 无数据就重找，避免卡死）
        uint8_t b; int matched = 0;
        while (matched < 4) {
            if (uart_read_bytes(PUSH_UART_NUM, &b, 1, 30 / portTICK_PERIOD_MS) != 1) {
                matched = 0; continue;
            }
            matched = (b == MAGIC[matched]) ? matched + 1 : (b == MAGIC[0] ? 1 : 0);
        }

        // 2) 头部：CMD(1) + PATHLEN(1) + DATALEN(4)
        if (uart_read_exact(PUSH_UART_NUM, hdr, 6, 2000) != 6) { ack = 0x15; uart_write_bytes(PUSH_UART_NUM, (char *)&ack, 1); continue; }
        uint8_t  cmd     = hdr[0];
        uint8_t  pathlen = hdr[1];
        uint32_t datalen = (uint32_t)hdr[2] | ((uint32_t)hdr[3] << 8) | ((uint32_t)hdr[4] << 16) | ((uint32_t)hdr[5] << 24);
        if (pathlen == 0 || pathlen > PUSH_MAX_PATH || datalen > PUSH_MAX_DATA) {
            ack = 0x15; uart_write_bytes(PUSH_UART_NUM, (char *)&ack, 1); continue;
        }

        char path[PUSH_MAX_PATH + 1];
        if (uart_read_exact(PUSH_UART_NUM, (uint8_t *)path, pathlen, 2000) != pathlen) { ack = 0x15; uart_write_bytes(PUSH_UART_NUM, (char *)&ack, 1); continue; }
        path[pathlen] = 0;

        if (cmd == CMD_DELETE) {
            bool ok = (remove(path) == 0);
            ack = ok ? 0x06 : 0x15;
            uart_write_bytes(PUSH_UART_NUM, (char *)&ack, 1);
            ESP_LOGI(TAG, "del %s -> %s", path, ok ? "ACK" : "NAK");
            continue;
        }

        if (cmd != CMD_WRITE) { ack = 0x15; uart_write_bytes(PUSH_UART_NUM, (char *)&ack, 1); continue; }

        if (uart_read_exact(PUSH_UART_NUM, data, datalen, 5000) != datalen) { ack = 0x15; uart_write_bytes(PUSH_UART_NUM, (char *)&ack, 1); continue; }
        if (uart_read_exact(PUSH_UART_NUM, foot, 2, 2000) != 2) { ack = 0x15; uart_write_bytes(PUSH_UART_NUM, (char *)&ack, 1); continue; }

        // CRC 覆盖 CMD + PATHLEN + PATH + DATALEN + DATA
        uint16_t crc = 0xFFFF;
        crc = crc16_step(crc, hdr, 2);             // CMD + PATHLEN
        crc = crc16_step(crc, (const uint8_t *)path, pathlen);
        crc = crc16_step(crc, hdr + 2, 4);          // DATALEN(4)
        crc = crc16_step(crc, data, datalen);
        uint16_t want = (uint16_t)foot[0] | ((uint16_t)foot[1] << 8);
        if (crc != want) { ESP_LOGW(TAG, "CRC mismatch"); ack = 0x15; uart_write_bytes(PUSH_UART_NUM, (char *)&ack, 1); continue; }

        FILE *f = fopen(path, "wb");
        bool ok = false;
        if (f) { size_t w = fwrite(data, 1, datalen, f); fclose(f); ok = (w == datalen); }

        ack = ok ? 0x06 : 0x15;
        uart_write_bytes(PUSH_UART_NUM, (char *)&ack, 1);
        ESP_LOGI(TAG, "write %s (%u bytes) -> %s", path, datalen, ok ? "ACK" : "NAK");
    }
    free(data);
    vTaskDelete(NULL);
}

void spiffs_uart_start(void)
{
    uart_config_t cfg = {
        .baud_rate = PUSH_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWDISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    if (uart_param_config(PUSH_UART_NUM, &cfg) != ESP_OK) { ESP_LOGE(TAG, "param config fail"); return; }
    if (uart_set_pin(PUSH_UART_NUM, PUSH_UART_TX_GPIO, PUSH_UART_RX_GPIO,
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
        ESP_LOGE(TAG, "set pin fail"); return;
    }
    if (uart_driver_install(PUSH_UART_NUM, PUSH_UART_BUF, 0, 0, NULL, 0) != ESP_OK) {
        ESP_LOGE(TAG, "driver install fail"); return;
    }
    xTaskCreate(push_task, "spiffs_uart", PUSH_TASK_STACK, NULL, 10, NULL);
}
