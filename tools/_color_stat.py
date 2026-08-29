from PIL import Image
from collections import Counter

path = r"E:\TRAE workspace\016_ai passport\ai-passport-games\capture_screen.png"
OUT = r"E:\TRAE workspace\016_ai passport\ai-passport-games\tools\_color_stat.txt"
img = Image.open(path).convert("RGB")
W, H = img.size
px = img.load()

lines = []
# 背景 = 四角 + 中心采样
corners = [px[2, 2], px[W - 3, 2], px[2, H - 3], px[W - 3, H - 3], px[W // 2, H // 2]]
lines.append("corners/center: " + str(corners))

# 整体颜色直方图(降采样)
cnt = Counter()
for y in range(0, H, 4):
    for x in range(0, W, 4):
        cnt[px[x, y]] += 1
lines.append("\nTop 12 colors (r,g,b): count")
for c, n in cnt.most_common(12):
    lines.append(f"  {c}: {n}")

# 蓝像素占比 / 红像素占比
blue = red = total = 0
for y in range(0, H, 2):
    for x in range(0, W, 2):
        r, g, b = px[x, y]
        total += 1
        if b > 110 and b >= r and b >= g:
            blue += 1
        if r > 110 and r >= g and r >= b:
            red += 1
lines.append(f"\nblue-ish: {blue}/{total} = {100*blue/total:.1f}%")
lines.append(f"red-ish : {red}/{total} = {100*red/total:.1f}%")

with open(OUT, "w", encoding="utf-8") as f:
    f.write("\n".join(lines))

