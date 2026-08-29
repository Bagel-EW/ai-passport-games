#!/usr/bin/env python3
import json, subprocess, sys, os

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
os.chdir(ROOT)

cfg = json.load(open(os.path.join(HERE, "submit_args.json"), encoding="utf-8"))
VENV = r"E:\WorkBuddyData\.workbuddy\binaries\python\envs\default\Scripts\python.exe"
PUB = r"C:\Users\Administrator\.workbuddy\skills\folotoy-ai-passport-publisher\scripts\publisher.py"

argv = [VENV, PUB, "validate",
        "--firmware", cfg["firmware"],
        "--cover", cfg["cover"],
        "--screen-capture", cfg["screen_capture"]]

r = subprocess.run(argv, capture_output=True, text=True, encoding="utf-8")
sys.stdout.write(r.stdout)
if r.stderr:
    sys.stderr.write(r.stderr)
sys.exit(r.returncode)
