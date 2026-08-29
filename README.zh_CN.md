# 欢乐游戏厅 · AI Passport 掌机固件

[English](README.md) | 简体中文

「欢乐游戏厅」(Pocket Arcade) 是运行在 **FoloToy AI Passport** 硬件上的掌机游戏固件：一台基于 ESP32-C3 的 240×320 像素小屏掌机，用三个实体按键就能玩派对小游戏和经典单人游戏。本仓库是这套固件的完整源码。

> 硬件平台（板卡、引脚、BSP 驱动）继承自 FoloToy AI Passport；本仓库聚焦于**游戏内容本身**：派对模式、单人游戏、设置页与开机动画。

## 功能一览

| 分类 | 内容 |
| --- | --- |
| 多人派对 | 真心话大冒险、国王游戏、俄罗斯转盘、命运签，外加「酒神颁奖」收场动画 |
| 单人游戏 | 打砖块、贪吃蛇、翻牌配对 |
| 系统 | 赛博终端风格开机动画、设置页（亮度/音效等）、电量显示 |
| 发布 | 内置 `FAP_SCREENSHOT_V1` 截屏协议，配套脚本可一键抓取屏幕、校验并发布到 FoloToy 社区 |

## 操作方式

设备只有三个按键，全部共用一个 ADC 电阻分压（GPIO0）：

- **UP / DOWN**：菜单上下移动、游戏内方向
- **OK 短按**：进入 / 确认
- **OK 长按**：逐级返回（游戏内 → 子菜单 → 主菜单）

主菜单结构：

```text
开机动画（赛博终端）
   └─ 欢乐游戏厅 招牌页（PLAY · PARTY）
        ├─ 多人游戏 → 派对（说明页 → 人数页 → 轮流抽签）
        ├─ 单人游戏 → 打砖块 / 贪吃蛇 / 翻牌配对
        └─ 设置
```

## 硬件平台

| 能力 | 实现 | 说明 |
| --- | --- | --- |
| 主控 | ESP32-C3（RISC-V，无 PSRAM） | 8 MB Flash |
| 显示 | ST7789P3，240 × 320，竖屏 RGB565，SPI | 单 DMA 缓冲，无触摸 |
| 输入 | `UP` / `DOWN` / `OK` 三键，GPIO0 ADC 分压 | 回调运行在按键任务中，不可阻塞 |
| 音频 | ES8311，I2S 全双工 PCM | 播放 / 录音 |
| 电池 | CW2017 SOC 与电压读取 | 可缺省，缺测时安全降级 |
| 烧录/日志 | 原生 USB Serial/JTAG | `COMx`（Windows）/ `/dev/ttyACMx`（Linux） |

引脚、I2C 地址、ADC 阈值等硬件参数只定义在 `components/bsp/include/bsp_pins.h`，应用层不应重复常量。

## 构建与烧录

需要 **ESP-IDF 5.5.x**（已知可用 5.5.3）。

```bash
# 激活 ESP-IDF 环境（按你本机安装路径）
. "$HOME/esp/esp-idf-v5.5.3/export.sh"

idf.py set-target esp32c3     # 新 checkout 或切换过目标后执行一次
idf.py build                  # 首次会经 Component Manager 拉取 LVGL / esp_lvgl_port / button / esp_codec_dev 等依赖
idf.py flash monitor          # 烧录并打开串口监视
```

> 注意：不要直接修改 `managed_components/` 下由组件管理器生成的文件。配置陈旧时用 `idf.py fullclean` 重新配置即可，不要用它清理你的源码改动。

烧录产物为 `passport_games.bin`（factory 分区），通过 USB Serial/JTAG 写入。

## 截屏协议（FAP_SCREENSHOT_V1）

固件内置 `FAP_SCREENSHOT_V1` 串行截屏协议，用于把当前屏幕以 RGB565 帧缓冲形式回传 PC，方便生成社区发布用的截图。`tools/capture_games.py` 已封装好完整流程。

**协议**：

1. PC 向设备串口发送命令 `FAP_SCREENSHOT_V1\n`；
2. 设备回送帧头 `FAP_SCREENSHOT_V1 <w> <h> RGB565LE <len>\n`；
3. 随后发送 `len` 字节的小端 RGB565、按行主序排列的像素（本设备为 240×320，故 `len = 240*320*2 = 153600`）。

**抓取截图**（需设备已 USB 连接）：

```bash
python tools/capture_games.py
# 产出 capture_screen.png 与相邻回执 capture_screen.png.fap-capture.json
```

抓取脚本会先等待设备开机进入主菜单（约 20s），再发命令、接收整帧并解码为 PNG，同时打印一张 ASCII 预览方便快速核对。

## 发布到 FoloToy 社区

`tools/` 下还提供了发布辅助脚本（依赖 `folotoy-ai-passport-publisher` 技能中的 `publisher.py`）：

```bash
python tools/do_validate.py     # 校验 cover / firmware / screen-capture 是否齐全且合规
python tools/do_submit.py       # 预览提交内容（JSON），确认无误后把 submit_args.json 的 confirmed 改为 true 再跑即正式上传
```

提交所需的封面、标题（中英）、简介等均配置在 `tools/submit_args.json`。

## 项目结构

```text
components/bsp/   板级驱动（显示/按键/音频/电池/共享 I2C），应用层只调 bsp_* 与 ui_pixel_*
main/             开机动画、主菜单、各游戏页与派对逻辑
tools/            截屏抓取、社区校验与提交脚本
sdkconfig.defaults  ESP32-C3 / USB console / Flash / LVGL 默认配置
LICENSE            开源许可
```

### 新增一个游戏页

1. 在 `main/` 下新建 `demo_<name>.c`，实现 `enter` / `exit` / `key` 三个接口；
2. 在 `main/demo.h` 增加声明；
3. 在 `main/CMakeLists.txt` 加入源文件；
4. 在 `main/main.c` 的 `s_games[]` / 菜单数组里登记。

## 许可

本项目基于仓库根目录的 `LICENSE` 文件发布。
