# Stillink 共用服务

提供天气、音乐、相簿和待办的 HTTP JSON、PNG 与二进制图片接口，接收 Windows UDP 音乐广播。

- `app.py`：HTTP 服务入口与路由。
- `weather.py`：天气获取、缓存与卡片渲染。
- `music_receiver.py` / `music_card.py`：UDP 接收与专辑卡片渲染。
- `album.py`：处理后的照片、预览和轮播列表，持久保存在 `data/album.sqlite3`。

部署与 systemd 常驻运行见 [额外服务总部署说明](../../docs/services.md)，相簿操作见 [相簿说明](../../docs/album.md)。各功能可单独使用。

Windows 用户可直接下载 [免安装服务 ZIP](https://github.com/sheetung/Stillink/releases/latest)，使用与升级步骤见 [Windows 说明](WINDOWS.md)。

待办支持共享增删与完成状态，手动上传屏幕。使用说明见 [待办](../../docs/todo.md)。升级服务时保留 `data`，其中 `todo.sqlite3` 保存清单。
