#!/usr/bin/env python3
# tools/diag2.py —— 原始字节持续抓取 90s,判断设备是"崩了重启"还是"卡死不发包"
import importlib.util, serial, time
from pathlib import Path

PUB = r"C:\Users\Administrator\.workbuddy\skills\folotoy-ai-passport-publisher\scripts\publisher.py"
PORT = "COM5"
OUT = r"E:\TRAE workspace\016_ai passport\ai-passport-games\diag2.bin"

spec = importlib.util.spec_from_file_location("pub", PUB)
pub = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pub)

ser = serial.Serial(PORT, 115200, timeout=0.5, write_timeout=5, dsrdtr=False, rtscts=False)
ser.dtr = False
ser.rts = False
print("opened; wait 5s")
time.sleep(5)
ser.reset_input_buffer()
ser.write(pub.SCREENSHOT_COMMAND)
ser.flush()

buf = bytearray()
last = time.time()
overall = time.time() + 90
t0 = time.time()
while time.time() < overall:
    chunk = ser.read(65536)
    if chunk:
        buf.extend(chunk)
        last = time.time()
    else:
        if time.time() - last > 10:
            print(f"idle >10s at byte {len(buf)} (t={round(time.time()-t0,2)}s)")
            break

Path(OUT).write_bytes(bytes(buf))
raw = bytes(buf)
print(f"total={len(buf)} bytes in {round(time.time()-t0,2)}s")
for sig in (b"ets Jun", b"Guru", b"Backtrace", b"reboot", b"abort", b"assert", b"W (", b"E ("):
    idx = raw.find(sig)
    if idx >= 0:
        print(f"  FOUND {sig!r} at offset {idx}")
# show first header line + last 160 bytes
hdr_end = raw.find(b"\n")
print("header:", raw[:hdr_end].decode('ascii','replace') if hdr_end > 0 else raw[:40])
print("tail:", raw[-160:].decode('latin1','replace'))
ser.close()
