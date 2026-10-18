# Stillink 额外服务

天气、音乐、相簿和待办的可选扩展，统一部署、按需使用。字库与图片渲染放在服务器，ESP 直接下载二进制图片。

| 目录 | 作用 |
| --- | --- |
| `server/` | 共用 HTTP 服务、天气与音乐卡片、相簿存储 |
| `music-client-win/` | Windows 系统音乐信息与原始封面 UDP 广播工具 |

从 [总部署说明](../docs/services.md) 开始。仅使用音乐无需配置天气；仅使用天气无需运行 Windows 工具。

部署后分别查看 [天气使用说明](../docs/weather.md) 、[音乐使用说明](../docs/music.md) 或 [相簿使用说明](../docs/album.md)。

待办支持共享增删与完成状态，手动上传屏幕。使用说明见 [待办](../docs/todo.md)。升级服务时保留 `data`，其中 `todo.sqlite3` 保存清单。

日历、共享日程与纪念日功能需 V1.6.0 及以上服务，使用方法见 [日历与纪念日](../docs/calendar.md)。升级保留 data 目录。
