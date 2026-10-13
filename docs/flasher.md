# 在线烧录

基于 Espressif 官方 `esptool-js`，电脑浏览器通过 USB 直接烧录设备。树莓派只提供单页与固件文件，无需将 ESP 接到树莓派。

默认选择最新 Release 的 Stillink ESP32-S3 OTA 应用固件，用于更新已安装 Stillink 且分区表一致的设备。OTA USB 烧录写入 `0x10000`，并重置 `0xd000` 的启动选择数据，使设备启动新应用；保留启动程序、分区表与 NVS 配置。空白设备或分区表不同的设备请选择「首次烧录 · 完整包」。

也支持选择本地 BIN 文件及写入地址，文件不会上传服务器。烧录时显示进度，并使用 MD5 校验写入内容。OTA 模式不可完全擦除；完整包和其他固件可选择擦除全部数据。

## GitHub Pages

在线地址：**https://sheetung.github.io/Stillink/**。使用现有 Stillink 仓库，无需新建仓库。

首次部署需由仓库所有者在 **Settings → Pages → Build and deployment → Source** 选择 **GitHub Actions**。工作流令牌不能替仓库完成这一次启用设置；之后会自动发布。

`pages.yml` 使用 GitHub Actions 发布页面。发布时从 Release 下载最近五个稳定版本的 OTA 和完整包，随页面部署到 Pages，浏览器可以同源下载；Git 仓库不保存 BIN。页面和烧录库均打包为一个 HTML，不依赖 CDN。

推送 `vX.Y.Z` 标签后，`release.yml` 依次完成固件编译、Release 发布、Pages 更新。默认显示最新版本，也可选择历史版本。单独修改烧录页面时，推送 main 即可重新部署；也可手动运行 Online flasher 工作流。

## 构建页面

先用 ESP-IDF 构建固件，再运行：

```bash
cd extra-services/web-flasher
npm ci
npm run build
```

默认从 Release 获取固件，输出到 `public/`。本地测试新固件时，先用 ESP-IDF 构建，并在仓库根目录运行 `python tools/package_bins.py v1.2.1`，再进入本目录运行 `npm run build -- --local`。本地固件放在仓库的 `firmware/`，不会提交到 Git。

## 树莓派部署

将构建好的 `public/`、`pyproject.toml`、`uv.lock` 上传到服务器同一目录，安装 uv 后运行：

```bash
uv sync --frozen --no-dev
uv run --frozen --no-dev python -m http.server 8788 --bind 0.0.0.0 --directory public
```

仓库提供 `stillink-flasher.service`，可按部署目录调整 `WorkingDirectory`，然后安装为用户 systemd 服务。

## 浏览器访问

使用电脑上的 Chrome 或 Edge。Web Serial 需要 **可信 HTTPS 或 localhost**。局域网 `http://服务器IP:8788/` 能查看页面和下载固件，但不能使用串口。

本地部署可在电脑打开 SSH 转发，保持终端运行：

```bash
ssh -N -L 8788:127.0.0.1:8788 用户@服务器IP
```

然后访问 `http://localhost:8788/`，插入 USB 数据线，点击「连接并选择设备」，在浏览器弹出的串口列表中选设备后烧录。GitHub Pages 已提供可信 HTTPS，可直接使用在线地址，无需 SSH 转发。

## 自动发布固件

推送新的 `vX.Y.Z` tag 后，`.github/workflows/release.yml` 用 ESP-IDF 6.1 构建 S3 固件，版本号自动与 tag 一致，Release 包含：

- `stillink-vX.Y.Z-esp32s3.bin`：完整包，从 `0x0` 烧录。
- `stillink-vX.Y.Z-esp32s3-ota.bin`：设备网页 OTA 升级用。

固件发布后自动更新 Pages，无需提交 BIN 或手工部署。已有 tag 不应移动。
