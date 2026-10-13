# Stillink 共用服务

提供天气、音乐和相簿的 HTTP JSON、PNG 与二进制图片接口，接收 Windows UDP 音乐广播。

- `app.py`：HTTP 服务入口与路由。
- `weather.py`：天气获取、缓存与卡片渲染。
- `music_receiver.py` / `music_card.py`：UDP 接收与专辑卡片渲染。
- `album.py`：处理后的照片、预览和轮播列表，持久保存在 `data/album.sqlite3`。

部署与 systemd 常驻运行见 [额外服务总部署说明](../../docs/services.md)，相簿操作见 [相簿说明](../../docs/album.md)。各功能可单独使用。

Windows 用户可直接下载 [免安装服务 ZIP](https://github.com/sheetung/Stillink/releases/latest/download/StillinkServices-win.zip)，使用与升级步骤见 [Windows 说明](WINDOWS.md)。
