#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
package_bins.py [tag] [--build-dir build] —— 把编译产物打包成刷机/OTA 用的 bin。

用受管 venv 的解释器运行（里面装了 esptool 5.4）：
    python tools/package_bins.py r1

产出：
    stillink_<tag>_merged.bin  整包（0x0）：bootloader + partitions + OTA data + app
    stillink_<tag>_app.bin     app 镜像：OTA 上传用（build/stillink.bin 的原样拷贝）

芯片类型从构建目录的 project_description.json 读取；合并使用 dio / 40m / 4MB。
"""
import os
import sys
import shutil
import subprocess
import argparse
import json

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
    parser.add_argument("tag", nargs="?", default="r1")
    parser.add_argument("--build-dir", default=BUILD)
    args = parser.parse_args()
    tag = args.tag
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

    app_out = os.path.join(ROOT, "stillink_%s_app.bin" % tag)
    merged = os.path.join(ROOT, "stillink_%s_merged.bin" % tag)

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
