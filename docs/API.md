# Stillink HTTP API

## 访问鉴权

所有 `/api/` 接口都需要请求头 `Authorization: Bearer <设备密钥或登录 token>`，仅 `POST /api/auth/login` 和 CORS `OPTIONS` 预检无需认证。控制页可以公开打开，但读取设备信息、配网、上传图片、修改设置、升级和恢复出厂必须验证；未认证返回 `401`。

连接卡片使用英文标签，无中文字库。首次启动会在墨水屏显示访问地址、地址二维码和随机设备密钥。首次配网成功后显示新的局域网地址；自动重连不覆盖画面。设备设置可主动显示连接信息，也可修改密钥（12–64 位 ASCII 字符，不含空格）。修改后所有旧会话立即失效，旧密钥不能继续调用 API。

直接调用示例（下文其他示例也必须带相同请求头）：

```bash
curl -H "Authorization: Bearer $STILLINK_DEVICE_KEY" http://设备IP/api/info
curl -H "Authorization: Bearer $STILLINK_DEVICE_KEY" -H "Content-Type: application/octet-stream" --data-binary @frame.bin http://设备IP/api/frame
```

登录：`POST /api/auth/login`，JSON 请求体 `{"key":"设备密钥"}`，成功返回 `{"token":"会话token","expires_in":86400}`。后续将 token 放入同一 Bearer 请求头即可。token 最长有效 24 小时，重启、恢复出厂或改密钥后失效。连续 5 次登录失败暂停登录 30 秒，返回 `429`。

- `GET /api/auth/status`：验证当前认证是否有效。
- `POST /api/auth/key`：JSON `{"key":"新设备密钥"}`，成功后重新登录。
- `POST /api/auth/logout`：注销当前登录 token。
- `POST /api/connection/show`：请求在墨水屏显示连接地址和密钥。

短按唤醒按键唤醒设备，连续按住 5 秒（从固件开始检测时计）恢复出厂，松开后重启进入配网。恢复出厂清除设备密钥、Wi-Fi 和服务配置，但不删除服务器照片。正常固件升级保留设备密钥、Wi-Fi 和服务密钥。

### 额外服务密钥

额外服务首次启动生成独立服务密钥，保存在 `data/service.key`。所有服务 HTTP 接口（包括 `/health`、天气、音乐和相簿读写）均要求 `Authorization: Bearer <服务密钥>`，OPTIONS 预检除外。该密钥与设备密钥互不影响；更换服务密钥不会删除照片，但所有设备要更新服务配置。

- `GET /api/service`：返回 `service_url` 与 `key_set`，不返回服务密钥。
- `POST /api/service/check`：JSON `{"service_url":"http://服务IP:8787","key":"服务密钥"}`，检查连接和密钥，成功返回服务状态，不保存配置。
- `POST /api/service`：相同 JSON，验证成功后保存；地址不变时省略 `key` 保持已有服务密钥；更换服务地址需重新提供密钥，空服务地址清除配置。
- `GET/POST/DELETE /api/service/proxy?path=<URL编码的服务路径>`：通过 ESP 转发 `/health` 或 `/v1/` 请求。浏览器仅携带设备认证，ESP 自动附加服务密钥；上传相簿使用二进制帧请求体。

```bash
curl -H "Authorization: Bearer $STILLINK_SERVICE_KEY" http://服务IP:8787/v1/album/photos
```

HTTP 不加密传输，密钥仅提供访问控制。本功能用于可信局域网，避免对公网开放设备和服务端口。


接口与内置网页使用同一套设备服务。热点直连时基础地址为 `http://192.168.4.1`；连接路由器后换成设备的局域网 IP，调用端与设备需在可互通的网络中。接口需要设备密钥或登录 token，设备深睡或断电时不可访问。

连接路由器后不会因 `sleep_s` 超时进入深睡。热点关闭后，设备在空闲时自动轻睡，保持 Wi-Fi 连接并恢复处理网络请求；不需要先发送额外的唤醒请求。响应延迟取决于路由器的 DTIM 周期。未连接路由器时仍按 `sleep_s` 进入深睡，需按键或定时唤醒。

## 接口列表

| 方法 | 路径 | 请求与响应 |
| --- | --- | --- |
| GET | `/api/info` | 版本、屏幕配置、堆内存、电池毫伏值、休眠时间等 JSON |
| GET | `/api/settings` | 当前设置 JSON |
| POST | `/api/settings` | URL 编码表单，成功返回 `OK` |
| POST | `/api/frame` | 四色二进制帧，接收成功返回 `OK`，后台开始刷新 |
| POST | `/api/clear` | 空请求体，执行两次黑白残影清理，完成返回 `OK` |
| GET | `/api/wifi/status` | `status`、`ssid`、`ip`、`saved`、`reason` JSON |
| GET | `/api/wifi/scan` | `networks` 数组，每项含 `ssid`、`rssi`、`secured` |
| POST | `/api/wifi/connect` | 表单 `ssid`、`password`，接受后返回 HTTP 202 |
| POST | `/api/wifi/forget` | 清除路由器凭据，恢复热点 |
| POST | `/api/upload` | 应用固件 BIN 或控制页 HTML，按内容自动识别 |
| POST | `/api/web/clear` | 清除上传的控制页，恢复内嵌页面 |
| GET | `/api/weather` | 天气配置、状态和最近获取的数据 |
| POST | `/api/weather` | JSON，保存天气配置 |
| POST | `/api/weather/refresh` | 无请求体，启用且联网时返回 HTTP 202 |
| GET / POST | `/api/music` | 音乐配置和同步状态，保存使用 JSON |
| POST | `/api/music/refresh` | 请求重新下载当前音乐卡片 |
| POST | `/api/factory` | 恢复出厂设置并重启 |

`/api/weather` 和 `/api/music` 使用 JSON；其他 POST 设置和配网使用 `application/x-www-form-urlencoded`；省略设置字段表示保持原值。跨域调用支持 `GET`、`POST`、`OPTIONS` 和 `Content-Type` 请求头。

## 查询与设置

Windows PowerShell 使用 `curl.exe`：

```powershell
curl.exe -H "Authorization: Bearer $env:STILLINK_DEVICE_KEY" http://192.168.4.1/api/info
curl.exe -H "Authorization: Bearer $env:STILLINK_DEVICE_KEY" http://192.168.4.1/api/settings
curl.exe -H "Authorization: Bearer $env:STILLINK_DEVICE_KEY" -X POST http://192.168.4.1/api/settings --data "sleep_s=180"
```

常用设置字段：`panel`（0=A0，1=A1）、`hflip`（0/1）、`a1_mode`（1/2，仅 A1）、`sleep_s`（空闲休眠秒数，0=关闭）、`wake_s`（定时唤醒秒数，0=关闭）、`bat_cal_ppm`（电池校准系数，默认 1000000）。热点配置使用 `ssid`、`pass`；路由器配网使用独立的 Wi-Fi 接口。屏幕参数在空闲时修改，刷新中不要切换型号。

JavaScript 示例：

```javascript
const base = 'http://192.168.4.1'; // 或局域网 IP
const headers = { Authorization: 'Bearer ' + deviceKey }; // deviceKey 为设备密钥或登录 token
const response = await fetch(`${base}/api/info`, { headers });
if (!response.ok) throw new Error(await response.text());
console.log(await response.json());

const saved = await fetch(`${base}/api/settings`, {
  method: 'POST',
  headers,
  body: new URLSearchParams({ sleep_s: '180' }),
});
if (!saved.ok) throw new Error(await saved.text());
```

浏览器自身的 HTTPS 混合内容、局域网访问权限限制仍适用；命令行与 Python 客户端不受浏览器跨域规则影响。

## 上传图像

`POST /api/frame` 接收 **16 字节帧头 + 2bpp 载荷**，Content-Type 为 `application/octet-stream`。JPG/PNG 必须先缩放、转换为黑白黄红四色并按协议打包。A0 使用 768×552，载荷 105984 字节；A1 可接收 768×552 或原生 800×600（载荷 120000 字节），实际显示模式由设备设置决定。

帧头中的多字节数值使用大端序：

| 偏移 | 字节数 | 内容 |
| --- | --- | --- |
| 0 | 2 | 魔数 `A5 5A` |
| 2 | 1 | 帧格式：`1` 为 768×552，`2` 为 800×600 |
| 3 | 1 | 面板标记：`0` 为 A0，`1` 为 A1；不会改变设备的面板设置 |
| 4 | 2 | 宽度 |
| 6 | 2 | 高度 |
| 8 | 4 | 载荷长度，即 `宽 × 高 ÷ 4` |
| 12 | 2 | 仅针对载荷计算的 CRC16 |
| 14 | 2 | 保留，填 `0` |

每字节包含 4 个像素，按高位到低位排列；黑、白、黄、红对应 `00`、`01`、`10`、`11`。因此纯色字节分别为 `0x00`、`0x55`、`0xAA`、`0xFF`。浏览器对最终图像旋转 180° 后按此顺序打包；自制客户端需保持相同方向，具体实现可参考 `page/index.html` 的 `packIdx()` 和 `tools/test_frame_post.py`。

CRC 为 CRC-16/CCITT-FALSE：多项式 `0x1021`，初值 `0xFFFF`，输入和输出不反射，最终不异或。校验向量 `123456789` 的结果为 `0x29B1`。

已有打包帧文件时：

```powershell
curl.exe -H "Authorization: Bearer $env:STILLINK_DEVICE_KEY" -X POST http://192.168.4.1/api/frame -H "Content-Type: application/octet-stream" --data-binary "@frame.bin"
```

仓库已提供无需第三方依赖的 Python 示例，会生成并上传黑、白、黄、红四色条带：

```powershell
python tools/test_frame_post.py 192.168.4.1
```

这个示例会实际更换屏幕画面。HTTP 200 `OK` 仅表示帧已接收并通过 CRC，不代表物理刷新成功；目前没有查询刷新结果的独立接口。错误包括 HTTP 400（帧格式、尺寸或 CRC 不正确）和 HTTP 500 `busy`（上一轮刷新仍占用缓冲）。等待刷新结束后再传下一帧。

## 路由器配网

```powershell
curl.exe -H "Authorization: Bearer $env:STILLINK_DEVICE_KEY" http://192.168.4.1/api/wifi/scan
curl.exe -H "Authorization: Bearer $env:STILLINK_DEVICE_KEY" -X POST http://192.168.4.1/api/wifi/connect --data-urlencode "ssid=你的WiFi" --data-urlencode "password=你的密码"
curl.exe -H "Authorization: Bearer $env:STILLINK_DEVICE_KEY" http://192.168.4.1/api/wifi/status
```

`status` 为 `idle`、`connecting`、`connected` 或 `failed`。获得 IP 后，热点约 10 秒后关闭，后续请求换用返回的局域网 IP。扫描期间可能短暂影响热点通信；正在连接时扫描返回 HTTP 409，需要稍后重试。

## 天气 API

`GET /api/weather` 返回 `enabled`、`service_url`（天气服务根地址）、`name`、`latitude`、`longitude`、`times`（当天分钟数，默认 `[480,720,1080]`）、`status`、`error`、`source`；成功刷新后附带 `data.updated` 和 `data.utc_offset`。状态包括 `disabled`、`waiting_wifi`、`waiting_time`、`waiting`、`loading`、`ready`、`error`。完整天气数据和 PNG 预览由天气服务提供，ESP 不保存字库。

`POST /api/weather` 使用 `application/json`。示例：

```json
{"enabled":true,"service_url":"http://192.168.1.100:8787","name":"杭州","latitude":30.29365,"longitude":120.16142,"times":[480,720,1080]}
```

`enabled` 必须是布尔值；完整位置配置须含名称、经纬度和三个递增的当天分钟数。服务地址支持 HTTP / HTTPS，最长 191 字节，不含查询参数或 fragment。首次启用需填写服务地址和位置。只关闭可发送 `{"enabled":false}`，其他配置保留；地址也可以单独修改。保存成功返回 `{"saved":true}`，错误配置返回 400，保存失败不会更新当前配置。

`POST /api/weather/refresh` 将更新排队，HTTP 202 表示已接受，实际完成需轮询天气状态；未启用或未连接路由器返回 HTTP 409。天气配置使用独立 NVS 命名空间，重启与 OTA 不清除，恢复出厂时清除。

ESP 请求 `${service_url}/v1/weather.bin?latitude=...&longitude=...&name=...`，服务返回与 `/api/frame` 相同的 16 字节帧头及 2bpp 数据（固定 768×552），并在 HTTP 响应头提供 `Content-Length`、`X-Weather-Updated` 与 `X-Weather-UTC-Offset`。ESP 流式读取到现有帧缓冲，校验 CRC 后刷新；网络失败、数据截断、CRC 不符均不触发刷新，约 5 分钟后重试。待显示的手动上传优先，天气不会覆盖其帧缓冲。服务端天气缓存 15 分钟。

服务部署见 [额外服务部署说明](services.md)，天气接口见 [天气卡片说明](weather.md)。

## 音乐 API

`POST /api/music` 保存 `{"enabled":true,"service_url":"http://192.168.1.100:8787"}`。只切换开关可以发送 `{"enabled":false}`。地址支持 HTTP / HTTPS，最长 191 字节。配置保存在独立 NVS 命名空间，重启与 OTA 保留，恢复出厂清除。

`GET /api/music` 返回 `enabled`、`service_url`、最后显示的 `version`、`status` 和 `error`。状态包括 `disabled`、`waiting_wifi`、`waiting`、`loading`、`ready`、`error`。`POST /api/music/refresh` 在启用且联网时返回 202，未启用或未联网返回 409。

设备从服务的 `/v1/music.json` 获取当前歌曲版本，版本变化时请求 `/v1/music.bin?v=版本`，校验长度和 CRC 后刷新。自动刷新间隔至少 60 秒，手动刷新可以绕过间隔；失败约 30 秒后重试。启用并保存音乐会关闭天气和相簿；启用并保存天气会关闭音乐和相簿。有效的 `/api/frame` 手动上传会关闭所有自动刷新，并保留配置；关闭状态持久保存，重启后不自动恢复。

Windows UDP 广播和服务器部署见 [音乐功能说明](music.md)。

## 相簿 API

`POST /api/album` 保存 `{"enabled":true,"service_url":"http://服务器:8787","playlist":"服务器返回的32位列表ID","mode":"sequence","interval":1800}`。`mode` 为 `sequence` 或 `random`，`interval` 为 60–86400 秒。只关闭轮播可发送 `{"enabled":false}`。配置重启与 OTA 保留，恢复出厂清除。

`GET /api/album` 返回 `enabled`、`service_url`、`playlist`、`mode`、`interval`、`current`（最后显示的照片 ID）、`status` 和 `error`。状态与音乐 API 相同。`POST /api/album/refresh` 请求下一张，启用且联网时返回 202，否则返回 409。

设备按自身屏幕尺寸从服务器取得下一张照片 ID，再流式下载二进制帧，校验长度和 CRC 后刷新。相簿、天气和音乐互斥；有效的 `/api/frame` 手动上传会关闭三项自动刷新，配置保留。照片与服务器接口见 [相簿说明](album.md)。

## GitHub Release 在线固件升级

设备连接可访问 GitHub 的 Wi-Fi 后使用。设备自动选择最新稳定版中与自身芯片匹配的 `-ota.bin`，HTTPS 下载并通过 ESP-IDF 镜像与芯片校验后切换 OTA 槽重启。

- `GET /api/ota/online`：当前版本、芯片、联网状态、最新版本、升级状态、字节进度和错误；`release_notes` 返回该版本的 GitHub Release 更新说明。
- `POST /api/ota/online/check`：异步检查最新稳定版，返回 202。
- `POST /api/ota/online/install`：下载已检查到的新版本并升级，返回 202；未联网、正在升级或没有新版本时返回 409。

`status` 为 `idle`、`checking`、`ready`、`downloading`、`rebooting` 或 `error`。下载期间可每 1.5 秒读取状态；重启后刷新网页。失败不会切换启动槽，可重新检查或使用文件上传升级。
