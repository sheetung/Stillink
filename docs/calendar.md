# 日历与纪念日

更新额外服务至 V1.6.0，在设备设置中测试服务连接后，即可看到「日历」和「纪念日」。[服务部署](services.md)

## 日历

- 切换月份、选择年月或回到本月；点击日期查看当天日程。
- 添加、编辑、删除日程，时间留空表示全天；有日程的日期带红色标记。
- 可选显示纪念日、农历与节气、天气，并突出今天。
- 天气使用设备天气功能中保存的位置，获取失败时显示「天气暂不可用」。
- 预览由服务生成，点击「上传并刷新屏幕」才更新设备，成功上传会停止天气、音乐和相簿自动更新。

## 纪念日

添加名称、日期，可选择每年重复。选择一条记录查看倒数或累计天数卡片；使用「编辑当前纪念日」修改记录。2 月 29 日按年重复时，平年按 2 月最后一天计算。

## 数据保存与历法

纪念日和日程保存在服务 `data/calendar.sqlite3`，多台手机或电脑共用。升级服务保留整个 `data/` 目录；设备重启、升级或更换登录密钥不会删除服务数据。

农历、节气与节日由 `lunar-python` 计算。支持 1901–2099 年，日期按浏览器本地日期显示。周末标红和节日名称**不代表法定放假或调休**，当前未提供年度调休表。卡片目前手动更新，不会每天自动刷新。

## HTTP 接口

以下接口位于额外服务，均需 `Authorization: Bearer <服务密钥>`。网页通过设备已鉴权的 `/api/service/proxy?path=...` 调用，无需向浏览器额外提供服务密钥。直接刷新 ESP 使用设备登录令牌，而非服务密钥。

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/v1/calendar/month?month=2026-10` | 当月农历、节气、节日、全部纪念日和日程 |
| GET / POST | `/v1/calendar/anniversaries` | 列出／新增纪念日 |
| POST / DELETE | `/v1/calendar/anniversaries/{id}` | 修改／删除纪念日 |
| GET / POST | `/v1/calendar/schedules` | 列出／新增日程 |
| POST / DELETE | `/v1/calendar/schedules/{id}` | 修改／删除日程 |
| GET | `/v1/calendar/calendar.png` / `calendar.bin` | 日历预览／二进制帧 |
| GET | `/v1/calendar/anniversary.png` / `anniversary.bin` | 单个纪念日预览／二进制帧 |

纪念日 JSON：`{"title":"生日","date":"2026-11-15","yearly":true}`。
日程 JSON：`{"title":"开会","date":"2026-10-18","time":"09:00"}`。

卡片参数：`date=YYYY-MM-DD`、`month=YYYY-MM`、`width=768&height=552&panel=0`；纪念日需 `id`。日历开关：`anniversaries=1&lunar=1&today=1&weather=1`，天气同时提供 `latitude`、`longitude`、`name`。A1 原生尺寸可传 `width=800&height=600&panel=1`。二进制为现有 16 字节头＋2bpp 帧，上传至设备 `POST /api/frame`。
