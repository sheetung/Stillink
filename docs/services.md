# 额外服务部署

天气和音乐共用一个服务器，只需部署一次；两项功能可独立启用。天气用户无需运行 Windows 工具，音乐用户无需配置天气。

## 目录与分工

```text
extra-services/
├── server/          HTTP 卡片服务、天气模块、UDP 音乐接收器
└── music-client-win/    Windows 音乐信息与原始封面广播
```

服务器负责字库与卡片渲染，ESP 保存配置并请求二进制图片。部署完成后，功能操作分别见 [天气](weather.md) 和 [音乐](music.md)。

## 服务器部署

需要 Python 3.11+、[uv](https://docs.astral.sh/uv/)、Git 和中文字体。以下为 Debian / Ubuntu 示例，Git 下载使用 SSH，需已配置 GitHub SSH 公钥。

```bash
sudo apt install git fonts-noto-cjk
git clone git@github.com:sheetung/Stillink.git ~/Stillink
cd ~/Stillink/extra-services/server
uv sync --frozen
uv run --frozen app.py --host 0.0.0.0 --port 8787
```

默认 HTTP `8787`、音乐 UDP `8790`。服务器应允许所需端口的入站连接。仅音乐接收与渲染不访问天气 API；天气功能需要能访问 Open-Meteo。

检查服务：

```bash
curl http://127.0.0.1:8787/health
```

正常返回 `status: "ok"`。`services.weather.available` 表示天气服务已就绪（不请求天气上游）；`services.music.receiver_ready` 表示 UDP 接收端已启动，`broadcast_active` 表示最近 35 秒收到有效广播，`available` 表示接收端已启动且有近期广播和歌曲。仅启动服务器而未启动 Windows 广播工具时，音乐卡片显示「等待音乐」。自定义字体使用 `STILLINK_FONT=/绝对路径/font.ttf` 或 `--font`；音乐接收端口使用 `--music-port`。系统字体不随仓库分发。

## 常驻运行

`server/stillink-services.service` 使用当前用户的 systemd 服务，默认路径为 `~/Stillink/extra-services/server`，uv 路径为 `~/.local/bin/uv`。若安装目录或 `command -v uv` 的结果不同，先修改模板的 `WorkingDirectory` 和 `ExecStart`。

```bash
cd ~/Stillink/extra-services/server
mkdir -p ~/.config/systemd/user
cp stillink-services.service ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user enable --now stillink-services
sudo loginctl enable-linger "$(id -un)"
```

日志与更新：

```bash
journalctl --user -u stillink-services -f
# 更新前退出日志查看（Ctrl+C）
cd ~/Stillink
git pull --ff-only
cd extra-services/server
uv sync --frozen
systemctl --user restart stillink-services
```

旧部署若仍使用 `stillink-weather`，先停止旧服务，避免占用相同端口，再安装上述新模板：

```bash
systemctl --user disable --now stillink-weather
```

HTTP 与 UDP 端口、设备地址和接口保持一致，设备无需重新配置。旧安装目录可自行保留；新模板使用上述统一目录。

## 设备配置

1. 让 ESP 连接路由器 Wi-Fi。
2. 打开「设备设置 → 额外服务支持」，填写服务器的 HTTP 根地址，如 `http://服务器IP:8787`。
3. 点击「测试连接」查看服务状态；保存时会先检测连接，检测失败时不修改已有配置。保存后显示天气和音乐两张状态卡片，并开启对应功能入口。展开服务设置时每 15 秒更新状态，Windows 未广播不会阻止保存地址。
4. 按需启用天气或音乐，两个功能页面无需重复填写地址。

服务需持续运行。公网部署可使用反向代理提供 HTTPS，ESP 验证可信证书；HTTPS 控制页预览也需使用 HTTPS 服务地址。清空共用地址并保存会关闭相关功能并隐藏入口。

## 音乐：Windows 广播工具

仅使用音乐时，需要额外运行 Windows 10 / 11 工具。它读取接入系统媒体控件的播放器，广播歌曲信息和原始封面；服务器接收并绘图。

下载并解压 [Windows 工具包](https://github.com/sheetung/Stillink/releases/latest/download/StillinkMusic-win.zip)，双击 **StillinkMusic.exe** 后即在后台运行，无需填写配置。电脑与服务器连接同一局域网，默认通过 UDP `8790` 广播。右键任务栏通知区域的音乐图标，选择「退出」停止。

工具构建、播放器支持和卡片使用见 [音乐说明](music.md)。

## 接口与验证

- `GET /health`：共用服务健康检查。
- `/v1/weather.json`、`.png`、`.bin`：天气数据与图片，参数见 [天气说明](weather.md)。
- `/v1/music.json`、`.png`、`.bin`：音乐状态与图片，参数见 [音乐说明](music.md)。

在服务器目录验证天气接口、帧格式及音乐 UDP 接收：

```bash
cd ~/Stillink/extra-services/server
uv run --frozen python -m unittest test_app test_music
```

Windows 广播协议测试：

```powershell
cd Stillink/extra-services/music-client-win
uv run --frozen python -m unittest test_music
```
