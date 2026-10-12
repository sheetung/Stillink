# Stillink 共用服务

提供天气与音乐卡片的 HTTP JSON、PNG 与二进制图片接口，接收 Windows UDP 音乐广播。

- `app.py`：HTTP 服务入口与路由。
- `weather.py`：天气获取、缓存与卡片渲染。
- `music_receiver.py` / `music_card.py`：UDP 接收与专辑卡片渲染。

部署与 systemd 常驻运行见 [额外服务总部署说明](../../docs/services.md)。天气和音乐可独立使用。
