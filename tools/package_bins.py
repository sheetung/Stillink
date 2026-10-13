#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Generate build/dist/stillink-vX.Y.Z-<chip>.bin and -ota.bin from an ESP-IDF build."""
import os
import sys
import shutil
import subprocess
import argparse
import json
import re

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BUILD = os.path.join(ROOT, "build")


def esptool(args):
    """用当前解释器的 esptool 模块跑命令（venv 里已装）。"""
    cmd = [sys.executable, "-m", "esptool"] + args
    subprocess.run(cmd, check=True)


def main():
    global BUILD
    parser = argparse.ArgumentParser()
    parser.add_argument("tag", nargs="?", default=None)
    parser.add_argument("--build-dir", default=BUILD)
    parser.add_argument("--output-dir", default=os.path.join(ROOT, "build", "dist"))
    args = parser.parse_args()
    tag = args.tag or re.search(r'#define\s+EPAPER_VERSION\s+"([^"]+)"', open(os.path.join(ROOT, "main", "version.h"), encoding="utf-8").read()).group(1)
    tag = tag.lower()
    if not tag.startswith("v"): tag = "v" + tag
    if not re.fullmatch(r"v\d+\.\d+\.\d+(?:-[a-z0-9.-]+)?", tag):
        parser.error("tag must be vX.Y.Z")
    BUILD = os.path.abspath(args.build_dir)
    with open(os.path.join(BUILD, "project_description.json"), encoding="utf-8") as f:
        chip = json.load(f)["target"]

    boot = os.path.join(BUILD, "bootloader", "bootloader.bin")
    part = os.path.join(BUILD, "partition_table", "partition-table.bin")
    ota_data = os.path.join(BUILD, "ota_data_initial.bin")
    app = os.path.join(BUILD, "stillink.bin")

    for p in (boot, part, ota_data, app):
        if not os.path.exists(p):
            print("missing build artifact: %s" % p)
            return 1

    os.makedirs(args.output_dir, exist_ok=True)
    app_out = os.path.join(args.output_dir, "stillink-%s-%s-ota.bin" % (tag, chip))
    merged = os.path.join(args.output_dir, "stillink-%s-%s.bin" % (tag, chip))

    shutil.copyfile(app, app_out)
    print("app     -> %s (%d B)" % (app_out, os.path.getsize(app_out)))

    esptool([
        "--chip", chip, "merge-bin",
        "-o", merged,
        "--flash-mode", "dio",
        "--flash-freq", "40m",
        "--flash-size", "4MB",
        "0x0", boot,
        "0x8000", part,
        "0xd000", ota_data,
        "0x10000", app,
    ])
    print("merged  -> %s (%d B)" % (merged, os.path.getsize(merged)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
