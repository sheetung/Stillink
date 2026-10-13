# Stillink

**把喜欢的瞬间，放在身边。**

基于 ESP32-S3 与四色电子墨水屏的网页相框。打开浏览器，就能把照片、便签、天气和正在听的音乐放到桌面；断电后，画面依然保留。

[快速上手](#快速上手) · [固件构建](#固件构建) · [服务部署](#可选服务部署) · [HTTP API](docs/API.md) · [更新记录](docs/CHANGELOG.md)

## 特色

- **无需 App**：手机、电脑都能操作，支持热点直连与局域网访问。
- **四色墨水屏**：黑、白、黄、红，支持 A0 / A1 屏幕配置。
- **浏览器编辑**：裁剪、旋转、调色、留白和文字排版，本地预览后上传。
- **联网常在线**：连接 Wi-Fi 后空闲轻睡，仍可接收 HTTP 请求；离线支持深睡与按键唤醒。
- **按需扩展**：天气、音乐共用可选服务，部署并配置后才显示入口。
- **方便维护**：网页上传固件或控制页，支持 HTTP API 和电池电压校准。

## 功能展示

以下为新版网页操作截图与卡片渲染预览，墨水屏效果以实物为准。天气、音乐预览统一使用圆角和灰色边框。

### 拍立得

选图、裁剪和旋转，搭配四色风格、留白与文字，把照片变成桌面卡片。

![拍立得：选图裁剪与四色预览](docs/images/photo.png)

### 便签

写下提醒、待办或喜欢的句子。文字可以拖动、旋转，调整字体、大小、颜色与底色。

![便签：文字排版与预览](docs/images/note.png)

### 天气

选择城市或区县，查看当前天气和未来三天预报。默认每天 08:00、12:00、18:00 自动更新，也可自定义时间或手动刷新。[使用说明](docs/weather.md)

![天气：卡片预览、位置与更新计划](docs/images/weather.png)

### 音乐

Windows 通过 UDP 广播播放器的歌曲、歌手、专辑和封面，服务器生成 **512 × 512 圆角大封面**卡片，右侧显示歌名与歌手，设备在切歌时自动更新。支持接入系统媒体控件的 QQ 音乐、Spotify 等播放器；缺少封面时显示唱片占位图。[使用说明](docs/music.md)

![音乐：当前 Windows UDP 广播歌曲的圆角大封面与灰色预览框](docs/images/music.png)

### 配网与局域网访问

点击 Wi-Fi 名称自动扫描，连接后保存配置并关闭设备热点。接入同一路由器，即可通过设备 IP 或 [stillink.local](http://stillink.local/) 操作。

![设备设置：Wi-Fi 配网与局域网地址](docs/images/wifi.png)

### 额外服务支持

天气与音乐只需配置一次共用服务地址。保存后显示两张状态卡片，展开设置时每 15 秒更新：天气显示服务是否就绪，音乐未收到 Windows 广播时显示「等待音乐」，收到歌曲后才显示「可用」。为节省 ESP 的 Flash 和内存，字库与卡片渲染放在服务器，设备直接下载二进制图片。未配置服务时，只显示拍立得和便签。

![设备设置：共用服务地址与天气、音乐状态卡片](docs/images/services.png)

### 屏幕与电源设置

切换 A0 / A1 屏幕，设置离线休眠和定时唤醒，并按实测电压校准电池读数。

![设备设置：电池校准、休眠与屏幕](docs/images/settings.png)

### 网页升级

在设备设置中上传 `.bin` 升级固件，或上传 `.html` 热更新控制页；也可清理残影、恢复内置页面。

![系统维护：固件与控制页升级](docs/images/maintenance.png)

## 快速上手

1. 烧录固件，连接设备热点 `Stillink-XXXX`，打开 `http://192.168.4.1`。
2. 在「拍立得」选图，或在「便签」编辑文字，预览后点击「上传并刷新屏幕」。
3. 如需局域网访问，在「设备设置 → Wi-Fi 配网」选择家中的 2.4 GHz 网络并连接，之后可用 `http://stillink.local/` 访问。
4. 如需天气或音乐，部署下方服务，在「设备设置 → 额外服务支持」保存 HTTP 根地址，再打开对应功能。

`stillink.local` 使用局域网 mDNS，设备响应当前 IP，无需公网域名；设备和客户端需在同一网络，客户端须支持 mDNS。

## 硬件与接线

**ESP32-S3 · 4 MB Flash · 768 × 552 四色墨水屏**。固件不依赖 PSRAM，通过原生 USB Serial/JTAG 下载。

| 屏幕信号 | ESP32-S3 |
| --- | --- |
| SCK | GPIO4 |
| MOSI | GPIO6 |
| CS | GPIO7 |
| DC | GPIO1 |
| RST | GPIO3 |
| BUSY | GPIO10 |
| 唤醒按键 | GPIO5，按下接 GND |
| 电池采样 | GPIO2，470kΩ : 100kΩ 分压 |

唤醒键需单独连接；电池需经合适的稳压及充电电路供电。在设备设置中选择 A0 / A1，并按万用表读数校准电池电压。

## 固件构建

安装并激活 **ESP-IDF 6.1**，通过 SSH 获取工程：

```powershell
git clone git@github.com:sheetung/Stillink.git
cd Stillink
idf.py set-target esp32s3
idf.py build
idf.py -p COM13 flash monitor
```

将 `COM13` 换成实际串口。生成 USB 整包与 OTA 应用镜像：

```powershell
python tools/package_bins.py V1.2.0
```

| 文件 | 用途 |
| --- | --- |
| `stillink_V1.2.0_merged.bin` | 首次 USB 烧录，写入地址 `0x0` |
| `stillink_V1.2.0_app.bin` | 已有相同分区表的 S3 设备，通过网页 OTA 升级 |
| `page/index.html` | 单独热更新控制页 |

固件 OTA 会重置屏幕与休眠等设备设置；Wi-Fi、天气、音乐和电池校准配置保留。升级后确认屏幕设置。

## 可选服务部署

天气和音乐共用 `extra-services/server` 中的 Python 服务，需要 **Python 3.11+、uv 和中文字体**。Linux 示例：

```bash
sudo apt install git fonts-noto-cjk
git clone git@github.com:sheetung/Stillink.git
cd Stillink/extra-services/server
uv sync --frozen
uv run --frozen app.py --host 0.0.0.0 --port 8787
```

在「设备设置 → 额外服务支持」填写 `http://服务器IP:8787` 并保存。服务默认使用 HTTP `8787` 和音乐 UDP `8790`；常驻运行与 systemd 配置见 [总部署说明](docs/services.md)。天气数据来自 [Open-Meteo](https://open-meteo.com/)，服务需要能访问互联网。

音乐还需在 Windows 解压工具包并双击 **StillinkMusic.exe**，即可在后台广播当前歌曲，无需安装 Python 或填写地址。双击「停止音乐广播.vbs」可停止。Windows 与服务器需处于同一局域网；工具使用与构建见 [音乐说明](docs/music.md)。启用音乐会暂停自动天气，关闭音乐后恢复天气计划。

## 文档

- [HTTP API 与二进制帧格式](docs/API.md)
- [额外服务总部署说明](docs/services.md)
- [天气卡片使用](docs/weather.md)
- [Windows 音乐广播与专辑卡片](docs/music.md)
- [版本更新记录](docs/CHANGELOG.md)

## 许可

沿用 MoInk 上游的版权与许可限制；上游尚未提供明确的开源协议。Cropper.js 等第三方代码保留各自的许可声明。

## 致谢

感谢 [MoInk](https://github.com/atunverse/moink) 提供原始工程、网页编辑器与墨水屏实现，也感谢 Cropper.js、InkSight 等相关项目的作者。
