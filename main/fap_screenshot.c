// main/fap_screenshot.c —— FAP_SCREENSHOT_V1 串口截屏协议(固件侧实现)
//
// 监听控制台(USB-CDC / stdin, fd 0)收到的命令,抓当前全屏 RGB565 帧并回传:
//   请求: ASCII 行 "FAP_SCREENSHOT_V1\n"
//   应答: 文本头 "FAP_SCREENSHOT_V1 <w> <h> RGB565LE <len>\n"
//         + 紧接着 <len> 字节 RGB565(小端,行优先) 原始像素
//
// ── 实现要点(关键坑,已踩过)─────────────────────────────────────────────
// 设备用 LVGL 9.5 PARTIAL 渲染模式 + 20 行单 draw buffer。
//   * esp_lvgl_port 的 flush 回调会在 swap_bytes=true 时,把 draw buffer
//     **原地**交换成大端 RGB565 再发给 ST7789。
//   * PARTIAL 模式下若一次性标脏整屏,LVGL flush 条带的顺序不保证严格
//     自上而下,旧写法用 g_cap_next_y 顺序接收会导致条带错位/花屏。
//   * USB 控制台 fd 1 是非阻塞写,数据先进 ring buffer;若直接发送 color_p
//     并在返回后让 LVGL 渲染下一条带,同一块 draw buffer 可能被覆盖。
//
// 正解:
//   1. 安装 flush 回调包装器,在调用原始 flush 之前,从 color_p 复制小端
//      RGB565 像素到静态缓冲,再发送;复制后即可避免 buffer 复用覆盖。
//   2. 一次只标脏一个条带,等 LVGL flush 并捕获该条带后再进下一条,
//      保证 PC 收到的像素严格自上而下、行优先。
//   3. 捕获期间用 bsp_lvgl_lock() 独占;端口任务用 lvgl_port_lock(0) 非阻塞
//      拿锁,拿不到会自行重试,不会重入 lv_timer_handler,也无看门狗风险。
//
// 全程不改变设备显示状态、不重启、不擦写、不输出任何凭证。纯观测性,符合社区发布。
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lvgl.h"
// 用于读取 disp->flush_cb(原始 flush 回调),以便包装后仍能正确刷新 LCD。
// lvgl 组件把 src/ 加入了 include 路径,故可包含其私有头。
#include "display/lv_display_private.h"
#include "bsp_display.h"
#include "bsp_pins.h"

static const char *TAG = "fap_shot";

#define FAP_W                 BSP_LCD_W
#define FAP_H                 BSP_LCD_H
#define FAP_CMD               "FAP_SCREENSHOT_V1"
#define FAP_HDR               "FAP_SCREENSHOT_V1"
#define FAP_TASK_STACK        (24 * 1024)   // lv_timer_handler() 渲染很吃栈,6KB 会溢出->0xdeadc0de 崩溃
#define FAP_MATCH_MAX         64            // 滑动窗口大小(须 > strlen(FAP_CMD))
#define FAP_BAND_H            20            // draw buffer 行高(由 bsp_display_lvgl.c 决定: BSP_LCD_W*20)

// ── flush 包装器状态 ──────────────────────────────────────────────────────
static lv_display_flush_cb_t g_orig_flush = NULL;  // 原始(esp_lvgl_port)flush 回调
static bool                  g_capturing  = false; // 是否处于截屏 flush 中
static int                   g_cap_expected_y = 0; // 当前期望捕获的条带顶部 y
static volatile bool         g_cap_got_band = false; // 当前条带已捕获完成

// 条带复制缓冲。FAP_W*FAP_BAND_H*2 = 240*20*2 = 9600 bytes,放在静态区避免栈溢出。
static uint8_t               g_band_buf[FAP_W * FAP_BAND_H * 2];

// 非阻塞串口写(控制台 fd 1 是带环形缓冲的非阻塞写:缓冲满时返回 0/负,
// 不能 break,否则只发了一部分;让出 CPU 让 USB 栈把缓冲排空后重试)。
static void fap_serial_write(const uint8_t *data, size_t n)
{
    size_t off = 0;
    uint32_t retry = 0;
    while (off < n) {
        size_t chunk = n - off;
        if (chunk > 1024) chunk = 1024;
        ssize_t w = write(1, data + off, chunk);
        if (w > 0) {
            off += (size_t)w;
        } else {
            if (++retry > 20000) { ESP_LOGE(TAG, "screenshot send stalled"); return; }
            vTaskDelay(pdMS_TO_TICKS(2));
        }
    }
}

// flush 包装器:在 orig flush 把 buffer 原地交换成大端**之前**,
// 把当前条带(小端 RGB565)复制到 g_band_buf 再发送。
// 一次只抓与 g_cap_expected_y 匹配的条带,确保 PC 端自上而下顺序。
static void fap_cap_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *color_p)
{
    if (g_capturing &&
        area->x1 == 0 && area->x2 == FAP_W - 1 &&
        area->y1 == g_cap_expected_y) {
        int h = area->y2 - area->y1 + 1;
        size_t n = (size_t)FAP_W * h * 2;
        if (n <= sizeof(g_band_buf)) {
            memcpy(g_band_buf, color_p, n);     // 复制,避免 USB 未发完前 buffer 被覆盖
            fap_serial_write(g_band_buf, n);
            g_cap_got_band = true;
        }
    }
    // 务必调用原始 flush,否则 LCD 不刷新且 LVGL 认为 flush 未完成会死锁。
    if (g_orig_flush) g_orig_flush(disp, area, color_p);
}

// 在滑动窗口里找命令子串;找到返回 1,并消费掉匹配及其之前的内容(保留之后内容)。
static int fap_find_cmd(uint8_t *win, size_t *len)
{
    size_t l = *len;
    size_t cmdlen = strlen(FAP_CMD);
    if (l < cmdlen) return 0;
    for (size_t i = 0; i + cmdlen <= l; i++) {
        if (memcmp(win + i, FAP_CMD, cmdlen) == 0) {
            size_t consumed = i + cmdlen;
            memmove(win, win + consumed, l - consumed);
            *len = l - consumed;
            return 1;
        }
    }
    // 无完整匹配:丢弃不可能再组成命令的前缀(保留最后 cmdlen-1 字节作下一轮前缀)
    size_t keep = cmdlen - 1;
    if (l > keep) {
        memmove(win, win + (l - keep), keep);
        *len = keep;
    }
    return 0;
}

// 抓屏并回传。安装 flush 包装器,逐条带标脏并捕获,保证像素严格自上而下。
static void fap_capture_and_send(void)
{
    lv_display_t *disp = lv_display_get_default();
    if (!disp) { ESP_LOGE(TAG, "no display"); return; }

    // 独占 LVGL:端口任务用 lvgl_port_lock(0) 非阻塞拿锁,拿不到会自行重试,
    // 不会重入 lv_timer_handler,也无看门狗风险。
    if (!bsp_lvgl_lock(portMAX_DELAY)) { ESP_LOGE(TAG, "lvgl lock failed"); return; }

    g_orig_flush = disp->flush_cb;                 // 保存原始 flush 回调
    lv_display_set_flush_cb(disp, fap_cap_flush_cb);

    // 回传期间关闭日志,避免 ESP_LOG 混入数据流导致 PC 端解析错位
    esp_log_level_t saved = esp_log_level_get("*");
    esp_log_level_set("*", ESP_LOG_NONE);

    uint32_t payload = (uint32_t)FAP_W * FAP_H * 2;
    char hdr[64];
    int hl = snprintf(hdr, sizeof(hdr), "%s %u %u RGB565LE %u\n",
                      FAP_HDR, (unsigned)FAP_W, (unsigned)FAP_H, (unsigned)payload);
    if (hl > 0) fap_serial_write((const uint8_t *)hdr, (size_t)hl);

    g_capturing = true;
    lv_obj_t *scr = lv_screen_active();

    // 逐条带标脏,一次只抓一条,保证顺序。
    for (int y = 0; y < FAP_H; y += FAP_BAND_H) {
        g_cap_expected_y = y;
        g_cap_got_band = false;

        int y2 = y + FAP_BAND_H - 1;
        if (y2 >= FAP_H) y2 = FAP_H - 1;
        lv_area_t band = {0, y, FAP_W - 1, y2};
        lv_obj_invalidate_area(scr, &band);

        // 等待该条带被 flush(通常 1-2 次 lv_timer_handler 就够;动画可能略久)
        for (int i = 0; i < 400 && !g_cap_got_band; i++) {
            lv_timer_handler();
            vTaskDelay(pdMS_TO_TICKS(2));
        }
        if (!g_cap_got_band) {
            ESP_LOGE(TAG, "band y=%d not flushed, abort capture", y);
            break;
        }
    }

    g_capturing = false;
    lv_display_set_flush_cb(disp, g_orig_flush);   // 恢复原始 flush 回调

    esp_log_level_set("*", saved);
    bsp_lvgl_unlock();
    ESP_LOGI(TAG, "screenshot done expected_y=%d", g_cap_expected_y);
}

static void fap_task(void *arg)
{
    (void)arg;
    uint8_t win[FAP_MATCH_MAX];
    size_t len = 0;
    ESP_LOGI(TAG, "FAP_SCREENSHOT_V1 listener ready (console/stdin)");
    while (1) {
        uint8_t b;
        ssize_t n = read(0, &b, 1);
        if (n == 1) {
            if (len < FAP_MATCH_MAX) win[len++] = b;
            if (fap_find_cmd(win, &len)) {
                fap_capture_and_send();
                len = 0;   // 重置窗口,避免回显/重复触发
            }
        } else {
            // 无数据或出错:让出 CPU,稍后重试
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

void fap_screenshot_start(void)
{
    xTaskCreate(fap_task, "fap_shot", FAP_TASK_STACK, NULL, 8, NULL);
}
