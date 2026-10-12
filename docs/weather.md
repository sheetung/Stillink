# Stillink 天气服务

服务获取 Open-Meteo 天气并使用服务器中文字体生成四色图。ESP 下载二进制帧，不存字库，也不处理 PNG/JPEG。浏览器预览与二进制图片来自同一渲染器。

## 在设备中使用

1. 先部署下面的天气服务，或取得可以访问的服务地址。
2. 打开设备网页，依次选择「拍立得 → 便签 → 天气」。
3. 在「设备设置 → 额外服务支持」填写并保存共用服务根地址，例如 `http://192.168.31.12:8787`，不要填写 `/v1/weather.bin`。
4. 搜索城市或区县，从结果中选中位置，开启「启用天气卡片」，点击「保存天气设置」。
5. 保存后立即刷新一次，默认所在地时间 08:00、12:00、18:00 自动刷新；可修改时间或点击「立即刷新」。

设备需要连接路由器 Wi-Fi，并能访问天气服务；服务器需要能访问互联网。关闭天气后停止自动更新，已保存的地址和位置会保留。网络或服务失败时屏幕保留原画面，设备约 5 分钟后重试。服务器天气数据缓存 15 分钟，立即刷新不会强制绕过缓存。

## Linux / uv 部署

需要 Python 3.11+、[uv](https://docs.astral.sh/uv/) 和中文字体，服务要能访问 Open-Meteo。已有 uv 时直接使用，无需 Docker。以下通过 SSH 下载仓库，需先配置 GitHub SSH 公钥；示例安装在当前用户的 `~/Stillink`，也可选择其他目录。

```bash
sudo apt install git fonts-noto-cjk
git clone git@github.com:sheetung/Stillink.git ~/Stillink
cd ~/Stillink/weather-service
uv sync --frozen
uv run --frozen python test_app.py
uv run --frozen app.py --host 0.0.0.0 --port 8787
```

其他字体通过 `STILLINK_FONT=/绝对路径/font.ttf` 或 `--font` 指定。首次启动会检查字体，缺失时明确报错。系统字体不随仓库分发。

[服务目录](../weather-service/) 随附当前用户的 systemd 服务模板 `stillink-weather.service`。模板中的 `%h` 代表当前用户的主目录，默认使用 `~/Stillink/weather-service` 和 `~/.local/bin/uv`。如果安装目录或 `command -v uv` 返回的路径不同，请先修改模板的 `WorkingDirectory` 和 `ExecStart`。

```bash
mkdir -p ~/.config/systemd/user
cp stillink-weather.service ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user enable --now stillink-weather
sudo loginctl enable-linger "$(id -un)"
```

日志：`journalctl --user -u stillink-weather -f`。更新服务：

```bash
cd ~/Stillink/weather-service
git pull --ff-only
uv sync --frozen
systemctl --user restart stillink-weather
```

在设备的「设备设置 → 额外服务支持」保存共用服务根地址，例如 `http://192.168.31.12:8787`，随后在「天气」页选择位置、启用并保存。默认当地时间 08:00、12:00、18:00 下载并刷新。服务必须持续运行且设备能够访问该地址；公网部署建议通过反向代理提供 HTTPS，ESP 使用可信证书验证。HTTPS 控制页预览也需使用 HTTPS 服务地址。

## 接口

- `GET /health`：健康检查。
- `GET /v1/weather.bin?latitude=30.29365&longitude=120.16142&name=杭州`：16 字节帧头 + 105984 字节 2bpp 载荷，包含 CRC16 和 180° 打包；可直接用于设备 `/api/frame`。
- `GET /v1/weather.png`：相同参数，返回同一张图片的 PNG 预览。
- `GET /v1/weather.json`：相同参数，返回当前天气及未来三天的数据。

响应包含 `Content-Length`、`X-Weather-Updated`（天气数据 Unix 时间）和 `X-Weather-UTC-Offset`（所在地时区秒数）。错误参数返回 400，上游或绘制失败返回 502，错误响应不会伪装成图片。天气原始数据按经纬度缓存 15 分钟；手动刷新也使用该缓存。缓存仅在内存中，最多 128 个位置。

二进制几何固定 768×552，兼容现有 A0/A1 接收格式。色码：黑 0、白 1、黄 2、红 3。

数据由 [Open-Meteo](https://open-meteo.com/) 提供，按 [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) 标注来源；其免费托管 API 适用于非商业使用，商业部署需遵守服务方条款。
