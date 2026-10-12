# 音乐广播与专辑卡片

音乐入口位于「拍立得 → 便签 → 天气 → 音乐」，可预览、启用或关闭、保存配置及立即刷新；共用服务地址在「设备设置 → 额外服务支持」中配置。

## 分工

- Windows：只读取系统 GSMTC 的歌曲、歌手、专辑和原始封面，通过 UDP 广播。不绘图，也不直接操作 ESP。
- 服务器：现有天气服务接收 UDP，生成四色专辑卡片，提供音乐状态、PNG 预览和二进制图片。
- ESP：保存服务地址，每 5 秒检查歌曲版本，仅歌曲变化时下载并刷新，自动刷新间隔至少 60 秒。

浏览器可以关闭。启用音乐时暂停自动天气，关闭音乐后按原计划恢复天气，天气配置不会被清除。暂停或恢复播放不重复刷新；Windows 停止广播 35 秒后，服务视为暂停，屏幕保留最后画面。

## 服务器

先按 [天气服务部署说明](weather.md) 使用 uv 部署服务，更新到包含音乐功能的版本：

```bash
cd ~/Stillink/weather-service
git pull --ff-only
uv sync --frozen
systemctl --user restart stillink-weather
```

同一个服务默认监听 HTTP 8787 和 UDP 8790。服务器与 Windows 在同一局域网，服务器应允许 UDP 8790 入站；跨子网或广播无法到达时，Windows 可指定服务器 IP 发送 UDP。手动启动时可用 `--music-port` 修改接收端口。

服务器使用自己的中文字体绘制。Windows 不需要安装 Pillow，封面超过 4 MiB 时仅广播文字，服务器使用占位图。多个 ESP 可共用同一个音乐服务；多台 Windows 同时向该服务广播时，显示最近收到的播放会话。

## Windows 广播工具

需要 Windows 10 / 11、[uv](https://docs.astral.sh/uv/) 以及接入系统媒体控件的播放器。通过 SSH 下载仓库（已有仓库执行 `git pull --ff-only`）：

```powershell
git clone git@github.com:sheetung/Stillink.git
cd Stillink/music-client
uv sync --frozen
uv run --frozen app.py
```

默认广播到 `255.255.255.255:8790`，切歌或播放状态变化时广播，每 10 秒重复一次作为心跳。按 `Ctrl+C` 停止。原始封面和歌曲信息组成 JSON，按 1200 字节分片，附带 SHA256；接收端重组、校验并合并重复广播。

可选参数：

```powershell
# 只读查看当前 Windows 媒体会话，不广播
uv run --frozen app.py --inspect

# 指定接收服务器；仍使用 UDP，不上传 ESP
uv run --frozen app.py --target 192.168.31.12

# 指定播放器（匹配 --inspect 返回的 player ID）
uv run --frozen app.py --player Spotify

# 自定义 UDP 端口，需与服务器一致
uv run --frozen app.py --port 8790
```

播放器提供哪些信息就显示哪些；缺少封面时显示占位图。未接入系统媒体控件的播放器无法读取。

## 网页与 ESP

1. 启动服务器和 Windows 广播工具，开始播放音乐。
2. 打开设备网页的「音乐」入口。
3. 首次使用时，在「设备设置 → 额外服务支持」保存服务器的 HTTP 根地址，如 `http://192.168.31.12:8787`。天气与音乐共用此地址，无需分别填写。
4. 点击「预览卡片」可查看服务器的当前图片。
5. 打开启用开关，点击「保存音乐设置」。ESP 会主动请求并刷新；服务地址与开关在重启和 OTA 后保留。

手动「立即刷新屏幕」可重新显示当前歌曲。下载失败时保留原画面，约 30 秒后重试。图片为 768×552 四色帧，包含 CRC16 和 180° 打包，与已有 A0/A1 上传协议一致。

## 服务接口

- `GET /v1/music.json`：`playing`、`version`、`title`、`artist`、`album`。
- `GET /v1/music.png?v=版本`：PNG 预览。
- `GET /v1/music.bin?v=版本`：ESP 直接使用的二进制帧。

未收到歌曲时图片返回 204；指定版本已变化时返回 409，避免把另一首歌曲的图片当作旧版本。图片只在服务器内存中保存，不写入磁盘。

## 测试

```powershell
# Windows 广播编码测试
cd music-client
uv run --frozen python test_music.py
```

```bash
# 服务器 UDP 重组、去重、暂停及二进制校验测试
cd weather-service
uv run --frozen python test_music.py
```
