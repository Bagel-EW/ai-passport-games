#!/usr/bin/env python3
# tools/do_submit.py —— 用 submit_args.json 驱动 publisher.py submit(预览或上传)
# 用 Python 构造 argv 调用,规避 PowerShell 对中文/特殊字符的引号转义问题。
import json, subprocess, sys, os

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)            # ai-passport-games/
os.chdir(ROOT)

cfg = json.load(open(os.path.join(HERE, "submit_args.json"), encoding="utf-8"))
VENV = r"E:\WorkBuddyData\.workbuddy\binaries\python\envs\default\Scripts\python.exe"
PUB = r"C:\Users\Administrator\.workbuddy\skills\folotoy-ai-passport-publisher\scripts\publisher.py"

argv = [VENV, PUB, "submit",
        "--title-zh", cfg["title_zh"],
        "--description-zh", cfg["description_zh"],
        "--title-en", cfg["title_en"],
        "--description-en", cfg["description_en"],
        "--source-url", cfg["source_url"],
        "--cover", cfg["cover"],
        "--firmware", cfg["firmware"],
        "--screen-capture", cfg["screen_capture"]]
if cfg.get("confirmed"):
    argv.append("--confirmed")

r = subprocess.run(argv, capture_output=True, text=True, encoding="utf-8")
sys.stdout.write(r.stdout)
if r.stderr:
    sys.stderr.write(r.stderr)
sys.exit(r.returncode)
