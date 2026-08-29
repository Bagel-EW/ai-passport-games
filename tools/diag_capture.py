#!/usr/bin/env python3
# tools/diag_capture.py —— 诊断截屏传输在哪一字节断流
import importlib.util, json, serial, time
from pathlib import Path

PUB = r"C:\Users\Administrator\.workbuddy\skills\folotoy-ai-passport-publisher\scripts\publisher.py"
PORT = "COM5"
OUT = r"E:\TRAE workspace\016_ai passport\ai-passport-games\diag_capture.bin"

spec = importlib.util.spec_from_file_location("pub", PUB)
pub = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pub)

ser = serial.Serial(PORT, 115200, timeout=0.5, write_timeout=5, dsrdtr=False, rtscts=False)
ser.dtr = False
ser.rts = False
print("opened; waiting 5s for boot...")
time.sleep(5)
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
    print("NO HEADER")
    ser.close()
    raise SystemExit(1)

w, h, enc, length = pub.parse_screenshot_header(header)
print(f"header ok: {w}x{h} {enc} expect={length}B")

data = bytearray()
last_got = time.time()
overall_deadline = time.time() + 60
gaps = []
prev_len = 0
t0 = time.time()
while len(data) < length and time.time() < overall_deadline:
    chunk = ser.read(min(4096, length - len(data)))
    if chunk:
        data.extend(chunk)
        if len(data) > prev_len:
            now = time.time()
            if prev_len != 0 and (now - last_got) > 0.4:
                gaps.append((prev_len, round(now - t0, 2), round(now - last_got, 2)))
            last_got = now
            prev_len = len(data)
    else:
        idle = time.time() - last_got
        if idle > 4:
            print(f"IDLE >4s at byte {len(data)} (t={round(time.time()-t0,2)}s)")
            break

elapsed = round(time.time() - t0, 2)
print(f"got {len(data)}/{length} bytes in {elapsed}s")
if gaps:
    print("notable gaps (byte, t_abs, gap_dur):")
    for g in gaps[:20]:
        print("  ", g)
Path(OUT).write_bytes(bytes(data))
print(f"saved partial -> {OUT}")
ser.close()
