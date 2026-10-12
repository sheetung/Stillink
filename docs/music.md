# Windows 音乐专辑卡片

音乐工具运行在播放音乐的 Windows 10 / 11 电脑上，使用系统 GSMTC 媒体会话接口读取歌曲、歌手、专辑名和封面。卡片在电脑本地生成，通过设备现有的 `/api/frame` 接口上传，无需天气服务器，也无需更改或重新烧录 ESP 固件。

## 安装与使用

需要 [uv](https://docs.astral.sh/uv/) 和接入系统媒体控件的播放器。播放器提供哪些字段，卡片就能显示哪些；缺少封面时显示占位图，缺少专辑信息时显示提示。未接入系统媒体控件的播放器无法读取。

通过 SSH 下载仓库（已下载则在现有仓库执行 `git pull --ff-only`）：

```powershell
git clone git@github.com:sheetung/Stillink.git
cd Stillink/music-client
uv sync --frozen
```

先开始播放音乐，只读检查当前系统媒体信息：

```powershell
uv run --frozen app.py --inspect
```

连接与设备相同的网络，在设备网页关闭自动天气，然后启动同步。以下 IP 请替换为你的设备地址：

```powershell
uv run --frozen app.py --device http://192.168.31.13
```

也可以明确指定关闭天气后启动：

```powershell
uv run --frozen app.py --device http://192.168.31.13 --disable-weather
```

自动天气开启时，工具默认提示并停止，避免定时天气覆盖音乐画面。`--disable-weather` 会将天气开关保存为关闭；退出音乐工具后，可在网页重新开启天气。

工具每 3 秒检查一次当前播放会话，歌曲稳定至少 3 秒后上传。切歌才更新，暂停、恢复、播放进度变化都不触发更新；最小刷新间隔默认 60 秒，快速连续切歌会合并为最后一首。上传失败约 30 秒后重试。按 `Ctrl+C` 退出，屏幕保留最后的卡片。

## 可选参数

```powershell
# 仅上传当前正在播放的歌曲，随后退出
uv run --frozen app.py --device 192.168.31.13 --once

# 选择播放器；匹配 --inspect 返回的 player ID，不区分大小写
uv run --frozen app.py --device 192.168.31.13 --player Spotify

# 每次上传时保存同一张卡片的 PNG 预览
uv run --frozen app.py --device 192.168.31.13 --preview music.png

# 调整最小刷新间隔，不能小于 30 秒
uv run --frozen app.py --device 192.168.31.13 --interval 120
```

默认选择当前正在播放的媒体会话；多个播放器同时播放时，可用 `--player` 指定来源。默认字体为 Windows 微软雅黑，也可用 `--font` 指定字体文件。

图片固定 768×552，兼容现有 A0 / A1 接收格式，带 CRC16 校验和 180° 打包。封面转换为黑、白、黄、红四色并抖动，文字单独绘制保持清晰。HTTP 成功表示设备已接收图片，实际墨水屏刷新还需要时间。

## 测试

```powershell
uv run --frozen python test_music.py
```

检查切歌合并、暂停恢复、刷新间隔、失败重试、天气开关冲突、图片色码和二进制帧方向。
