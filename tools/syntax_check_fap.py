#!/usr/bin/env python3
# 用 build4/compile_commands.json 里 main.c 的编译命令,对 fap_screenshot.c 做 -fsyntax-only 快速校验。
import json, subprocess, sys, os

ROOT = r"E:\TRAE workspace\016_ai passport\ai-passport-games"
CC_JSON = os.path.join(ROOT, "build4", "compile_commands.json")
TARGET = os.path.join(ROOT, "main", "fap_screenshot.c")

def tokenize(s):
    toks, cur, q = [], [], False
    for ch in s:
        if ch == '"':
            q = not q
            cur.append(ch)
        elif ch == ' ' and not q:
            if cur:
                toks.append(''.join(cur)); cur = []
        else:
            cur.append(ch)
    if cur:
        toks.append(''.join(cur))
    return toks

data = json.load(open(CC_JSON, encoding="utf-8"))
entry = None
for e in data:
    if e["file"].replace("\\", "/").endswith("main/main.c"):
        entry = e; break
if not entry:
    print("NO main.c entry found"); sys.exit(2)

cmd = tokenize(entry["command"])
directory = entry["directory"]

# 去掉 -c 与 -o <out>,以及原输入源文件(main.c)
out = []
skip_next = False
for i, t in enumerate(cmd):
    if skip_next:
        skip_next = False; continue
    if t == "-c":
        continue
    if t == "-o":
        skip_next = True; continue
    if t.replace("\\", "/").endswith("main/main.c"):
        continue
    out.append(t)

compiler = out[0]
flags = out[1:]
# 加入 syntax-only 与目标文件
run = [compiler] + flags + ["-fsyntax-only", TARGET]
print("CWD:", directory)
print("CMD:", " ".join(run[:6]), "... (total %d tokens)" % len(run))
r = subprocess.run(run, cwd=directory, capture_output=True, text=True)
print("=== returncode:", r.returncode)
print("=== STDOUT ===")
print(r.stdout)
print("=== STDERR ===")
print(r.stderr)
sys.exit(r.returncode)
