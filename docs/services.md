# 额外服务部署

天气、音乐、相簿和待办共用一个服务器，只需部署一次。设备上三项自动卡片互斥，启用并保存一项会关闭其他项，配置各自保留。天气用户无需运行 Windows 工具，音乐用户无需配置天气。

## 目录与分工

```text
extra-services/
├── server/          HTTP 卡片服务、天气模块、UDP 音乐接收器与相簿存储
└── music-client-win/    Windows 音乐信息与原始封面广播
```

服务器负责字库与卡片渲染，ESP 保存配置并请求二进制图片。部署完成后，功能操作分别见 [天气](weather.md)、[音乐](music.md) 和 [相簿](album.md)。

## 服务器部署

### Windows 免安装 ZIP

从 [Release 下载 StillinkServices-vX.Y.Z-win.zip](https://github.com/sheetung/Stillink/releases/latest)，完整解压并双击 `StillinkServices.exe`，无需 Python、uv 或额外字库。启动窗口显示局域网服务地址，分别点击「复制地址」「复制密钥」，填入设备的「额外服务支持」并保存。

关闭窗口后留在托盘运行，右键可显示地址或退出。首次运行允许 Windows 防火墙的专用网络访问；照片保存在程序旁的 `data` 文件夹，升级时保留它与照片数据库。音乐仍需运行独立的 `StillinkMusic.exe` 广播工具。详细步骤见 [Windows 使用说明](../extra-services/server/WINDOWS.md)。

建议在保持开机的设备上运行；Windows 电脑休眠或关机后服务暂停，设备保留当前画面。服务仅供局域网使用。

### Linux

需要 Python 3.11+、[uv](https://docs.astral.sh/uv/)、Git 和中文字体。以下为 Debian / Ubuntu 示例，Git 下载使用 SSH，需已配置 GitHub SSH 公钥。

```bash
sudo apt install git fonts-noto-cjk
git clone git@github.com:sheetung/Stillink.git ~/Stillink
cd ~/Stillink/extra-services/server
uv sync --frozen
uv run --frozen app.py --host 0.0.0.0 --port 8787
```

首次启动生成服务密钥，保存在 `data/service.key`，可用 `cat data/service.key` 查看。更换密钥时先停止服务，执行 `uv run --frozen app.py --rotate-service-key`，再启动；设备需要重新填写密钥，照片保留。

默认 HTTP `8787`、音乐 UDP `8790`。服务器应允许所需端口的入站连接。仅音乐接收与渲染不访问天气 API；天气功能需要能访问 Open-Meteo。

检查服务：

```bash
curl -H "Authorization: Bearer $(cat data/service.key)" http://127.0.0.1:8787/health
```

正常返回 `status: "ok"`。未收到 Windows 音乐广播时，设备显示「等待音乐」。

相簿默认持久保存到服务工作目录的 `data/album.sqlite3`。可通过 `--data-dir /持久存储目录` 指定路径，升级服务时保留该目录；备份时先停止服务再复制数据库。

## 更新服务

```bash
cd ~/Stillink
git pull --ff-only
cd extra-services/server
uv sync --frozen
systemctl --user restart stillink-services
```

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

查看运行日志：

```bash
journalctl --user -u stillink-services -f
```

## 设备配置

1. 让 ESP 连接路由器 Wi-Fi。
2. 打开「设备设置 → 额外服务支持」，填写服务器 HTTP 根地址和独立的服务密钥，如 `http://服务器IP:8787`。
3. 点击「测试连接」查看服务状态；保存时会先检测连接，检测失败时不修改已有配置。保存后显示天气、音乐、相簿和待办四张状态卡片，并开启对应功能入口。展开服务设置时每 15 秒更新状态，Windows 未广播不会阻止保存地址。
4. 按需启用天气、音乐或相簿，各功能无需重复填写服务地址。

服务与 ESP 在同一局域网内使用，服务需持续运行。清空地址并保存会关闭相关功能并隐藏入口。

## 音乐：Windows 广播工具

仅使用音乐时，需要额外运行 Windows 10 / 11 工具。它读取接入系统媒体控件的播放器，广播歌曲信息和原始封面；服务器接收并绘图。

下载并解压 [Windows 工具包](https://github.com/sheetung/Stillink/releases/latest)，双击 **StillinkMusic.exe** 后即在后台运行，无需填写配置。电脑与服务器连接同一局域网，默认通过 UDP `8790` 广播。右键任务栏通知区域的音乐图标，选择「退出」停止。

工具构建、播放器支持和卡片使用见 [音乐说明](music.md)。

## 接口

服务接口见 [HTTP API](API.md) 及各功能使用说明。

待办支持共享增删与完成状态，手动上传屏幕。使用说明见 [待办](todo.md)。升级服务时保留 `data`，其中 `todo.sqlite3` 保存清单。
