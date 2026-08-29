#!/usr/bin/env python3
# tools/capture_games.py —— 抓屏辅助脚本(绕过 publisher.py 的开端口即发命令导致的复位丢包)
#
# 背景:ai-passport 设备用 USB_SERIAL_JTAG 做控制台,打开 PC 端 COM 口会复位 ESP32。
# folotoy publisher.py 的 capture-screen 一开端口就立即发 FAP_SCREENSHOT_V1 命令,
# 此时设备还在启动,fap 监听器尚未就绪,命令被丢弃 -> "device did not answer"。
# 本脚本开端口后先等设备启动完毕(20s),再发命令,即可稳定抓屏。
# ai-passport-games 开机到主菜单耗时较长,10s 仍可能落在开机动画尾段。
# 为保持与社区发布器完全兼容,直接复用 publisher.py 的 rgb565_to_png / write_screenshot_receipt,
# 产出的 PNG + 相邻 .fap-capture.json 回执可被 publisher.py validate / submit 直接消费。
import importlib.util
import json
import serial
import sys
import time
from pathlib import Path

PUB = r"C:\Users\Administrator\.workbuddy\skills\folotoy-ai-passport-publisher\scripts\publisher.py"
PORT = "COM5"
OUT = r"E:\TRAE workspace\016_ai passport\ai-passport-games\capture_screen.png"
SETTLE_SECONDS = 20


def read_tolerant(ser, length: int, overall: float = 60.0, idle_break: float = 10.0) -> bytes:
    """容错读取:整体限时 + 长空闲才放弃,容忍条带渲染产生的短暂空隙(read_exact 任何空隙都报错)。"""
    payload = bytearray()
    last = time.time()
    deadline = time.time() + overall
    while len(payload) < length and time.time() < deadline:
        chunk = ser.read(min(65536, length - len(payload)))
        if chunk:
            payload.extend(chunk)
            last = time.time()
        elif time.time() - last > idle_break:
            raise RuntimeError(
                f"device stopped sending after {len(payload)}/{length} bytes (idle {idle_break}s)")
    if len(payload) != length:
        raise RuntimeError(
            f"captured only {len(payload)}/{length} bytes before timeout")
    return bytes(payload)


def main() -> int:
    spec = importlib.util.spec_from_file_location("pub", PUB)
    pub = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(pub)

    ser = serial.Serial(PORT, baudrate=115200, timeout=2, write_timeout=5,
                        dsrdtr=False, rtscts=False)
    ser.dtr = False
    ser.rts = False
    print(f"port {PORT} opened; waiting {SETTLE_SECONDS}s for device to boot "
          f"(avoids reset-on-open dropping the command)...")
    time.sleep(SETTLE_SECONDS)

    ser.reset_input_buffer()
    ser.write(pub.SCREENSHOT_COMMAND)
    ser.flush()

    deadline = time.time() + 25
    header = None
    while time.time() < deadline:
        line = ser.readline()
        if line.startswith(pub.SCREENSHOT_HEADER.encode("ascii") + b" "):
            header = line
            break
    if header is None:
        print(json.dumps({"ok": False, "error": "device did not answer FAP_SCREENSHOT_V1"}))
        ser.close()
        return 1

    w, h, enc, length = pub.parse_screenshot_header(header)
    print(f"header: {header.decode('utf-8', 'replace').strip()}  -> {w}x{h} {enc} {length}B")
    payload = read_tolerant(ser, length)
    png = pub.rgb565_to_png(w, h, payload)

    outp = Path(OUT)
    outp.parent.mkdir(parents=True, exist_ok=True)
    outp.write_bytes(png)
    receipt = pub.write_screenshot_receipt(outp, w, h, PORT)

    # 粗粒度 ASCII 预览:无需图片预览面板也能核对是否抓到了正确画面(蓝色主菜单等)。
    try:
        from PIL import Image
        ascii_preview(str(outp), cols=72, rows=40)
    except Exception as e:  # noqa
        print(f"[ascii preview skipped: {e}]")

    print(json.dumps({
        "ok": True,
        "path": str(outp.resolve()),
        "receipt": str(receipt.resolve()),
        "width": w,
        "height": h,
        "source": "serial-framebuffer",
        "pngBytes": len(png),
    }, ensure_ascii=False, indent=2))
    ser.close()
    return 0


def ascii_preview(png_path: str, cols: int = 72, rows: int = 40) -> None:
    """把截屏下采样成 ASCII:用亮度+色相粗略着色,蓝色背景应明显可见。"""
    from PIL import Image
    img = Image.open(png_path).convert("RGB")
    W, H = img.size
    px = img.load()
    # 字符按亮度从暗到亮
    ramp = " .:-=+*#%@"
    out = []
    for ry in range(rows):
        line = []
        for rx in range(cols):
            x = int(rx * W / cols)
            y = int(ry * H / rows)
            r, g, b = px[x, y]
            lum = (r * 299 + g * 587 + b * 114) // 1000
            ch = ramp[min(len(ramp) - 1, lum * len(ramp) // 256)]
            # 用 ANSI 近似色:蓝背景标蓝,绿标绿,红标红,其余默认
            if b > 120 and b >= r and b >= g:
                line.append(f"\033[34m{ch}\033[0m")       # 蓝
            elif g > 120 and g >= r and g >= b:
                line.append(f"\033[32m{ch}\033[0m")       # 绿
            elif r > 120 and r >= g and r >= b:
                line.append(f"\033[31m{ch}\033[0m")       # 红
            else:
                line.append(ch)
        out.append("".join(line))
    print("\n===== ASCII 预览 (蓝=背景 绿=标题/图标 红=强调) =====")
    print("\n".join(out))
    print("=============================================")


if __name__ == "__main__":
    raise SystemExit(main())
