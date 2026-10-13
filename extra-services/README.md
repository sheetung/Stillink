# Stillink 额外服务

天气、音乐和相簿的可选扩展，统一部署、按需使用。字库与图片渲染放在服务器，ESP 直接下载二进制图片。

| 目录 | 作用 |
| --- | --- |
| `server/` | 共用 HTTP 服务、天气与音乐卡片、相簿存储 |
| `music-client-win/` | Windows 系统音乐信息与原始封面 UDP 广播工具 |

从 [总部署说明](../docs/services.md) 开始。仅使用音乐无需配置天气；仅使用天气无需运行 Windows 工具。

部署后分别查看 [天气使用说明](../docs/weather.md) 、[音乐使用说明](../docs/music.md) 或 [相簿使用说明](../docs/album.md)。
