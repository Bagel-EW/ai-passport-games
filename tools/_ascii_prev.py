from PIL import Image

path = r"E:\TRAE workspace\016_ai passport\ai-passport-games\capture_screen.png"
OUT = r"E:\TRAE workspace\016_ai passport\ai-passport-games\tools\_ascii_prev.txt"
img = Image.open(path).convert("RGB")
W, H = img.size
px = img.load()

# 采样若干"应当为背景"的内部点,确认深蓝
samp = [(120, 140), (60, 160), (180, 130), (120, 220), (40, 200)]
samp_lines = ["sampled bg-ish points:"] + [f"  ({x},{y}) -> {px[x,y]}" for (x, y) in samp]

ramp = " .:-=+*#%@"
out = []
COLS, ROWS = 80, 48
for ry in range(ROWS):
    line = []
    for rx in range(COLS):
        x = int(rx * W / COLS)
        y = int(ry * H / ROWS)
        r, g, b = px[x, y]
        lum = (r * 299 + g * 587 + b * 114) // 1000
        ch = ramp[min(len(ramp) - 1, lum * len(ramp) // 256)] if lum > 6 else " "
        if b > r and b > g and b >= 25:
            line.append("\033[34m" + ch + "\033[0m")          # 深蓝/蓝背景
        elif g > r and g > b and g >= 30:
            line.append("\033[32m" + ch + "\033[0m")          # 绿
        elif r > g and r > b and r >= 40:
            line.append("\033[31m" + ch + "\033[0m")          # 红
        elif r > 110 and g > 110 and b > 110:
            line.append(ch)                                    # 白/亮灰
        else:
            line.append(ch)
    out.append("".join(line))

with open(OUT, "w", encoding="utf-8") as f:
    f.write("\n".join(samp_lines) + "\n\n" + "\n".join(out))
