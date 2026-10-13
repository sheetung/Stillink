# 音乐广播与大封面卡片

音乐入口位于「拍立得 → 便签 → 天气 → 音乐」，可预览、启用或关闭、保存配置及立即刷新；共用服务地址在「设备设置 → 额外服务支持」中配置。

## 分工

- Windows：只读取系统 GSMTC 的歌曲、歌手、专辑和原始封面，通过 UDP 广播。不绘图，也不直接操作 ESP。
- 服务器：额外服务接收 UDP，生成大封面四色卡片，提供音乐状态、PNG 预览和二进制图片。
- ESP：保存服务地址，每 5 秒检查歌曲版本，仅歌曲变化时下载并刷新，自动刷新间隔至少 60 秒。

卡片以 512×512 专辑封面为主，右侧仅显示歌名与歌手；缺少封面时显示唱片占位图。

浏览器可以关闭。启用并保存音乐会自动关闭天气和相簿；关闭音乐不会自动重新启用天气。拍立得或便签上传成功后，所有自动刷新都会关闭，已有配置保留。暂停或恢复播放不重复刷新；Windows 停止广播 35 秒后，服务视为暂停，屏幕保留最后画面。

## 服务器

按 [额外服务部署说明](services.md) 启动共用服务，无需阅读天气文档或配置天气。服务接收 UDP 音乐信息，生成 PNG 预览与 ESP 二进制图片。

默认 HTTP 8787、UDP 8790。多台 ESP 可共用同一服务；多台 Windows 同时广播时，显示最近收到的播放会话。封面超过 4 MiB 时仅广播文字，服务器使用占位图。

## Windows 广播工具

需要 Windows 10 / 11 以及接入系统媒体控件的播放器。

1. 从 [Release 下载 Windows 工具包](https://github.com/sheetung/Stillink/releases/latest/download/StillinkMusic-win.zip)，解压后双击 **StillinkMusic.exe**。
2. 工具在后台运行，任务栏右侧通知区域显示音乐图标，不弹出命令行窗口，也无需填写服务器地址。重复双击不会产生多个实例。
3. 开始播放音乐，服务器即可收到歌曲和封面。
4. 右键托盘图标选择「退出」停止工具。图标可能收在隐藏图标中。

电脑与服务器须处于同一局域网。默认广播到 `255.255.255.255:8790`，切歌或播放状态变化时广播，每 10 秒重复一次作为心跳。原始封面和歌曲信息组成 JSON，按 1200 字节分片，附带 SHA256；接收端重组、校验并合并重复广播。

### 从源码构建工具包

安装 [uv](https://docs.astral.sh/uv/) 后，通过 SSH 下载仓库（已有仓库执行 `git pull --ff-only`）：

```powershell
git clone git@github.com:sheetung/Stillink.git
cd Stillink/extra-services/music-client-win
.\build.ps1
```

生成 `dist/StillinkMusic-win.zip`，其中 EXE 可独立运行，不需要用户安装 Python 或 uv。源码调试：

```powershell
uv run --frozen app.py

# 查看系统媒体会话，不广播
uv run --frozen app.py --inspect
```

播放器提供哪些信息就显示哪些；缺少封面时显示占位图。未接入系统媒体控件的播放器无法读取。

### 排查连接

工具日志位于 `%LOCALAPPDATA%\Stillink\music-client-win\client.log`。默认不需要配置；确需指定接收地址或端口时，将工具旁的 `config.example.yml` 复制为 `config.yml`，修改 `target`、`port`（与服务器一致）或 `player`（播放器 ID 中包含的文字），再停止并重新启动。配置只从工具所在目录读取。

## 网页与 ESP

1. 启动服务器和 Windows 广播工具，开始播放音乐。
2. 打开设备网页的「音乐」入口。
3. 首次使用时，在「设备设置 → 额外服务支持」保存服务器的 HTTP 根地址，如 `http://192.168.1.100:8787`。天气、音乐和相簿共用此地址，无需分别填写。
4. 点击「预览卡片」可查看服务器的当前图片。
5. 打开启用开关，点击「保存音乐设置」。ESP 会主动请求并刷新；服务地址与开关在重启和 OTA 后保留。

手动「立即刷新屏幕」可重新显示当前歌曲。下载失败时保留原画面，约 30 秒后重试。图片为 768×552 四色帧，包含 CRC16 和 180° 打包，与已有 A0/A1 上传协议一致。

## 服务接口

- `GET /v1/music.json`：`playing`、`version`、`title`、`artist`、`album`。
- `GET /v1/music.png?v=版本`：PNG 预览。
- `GET /v1/music.bin?v=版本`：ESP 直接使用的二进制帧。

未收到歌曲时图片返回 204；指定版本已变化时返回 409，避免把另一首歌曲的图片当作旧版本。图片只在服务器内存中保存，不写入磁盘。

## 测试

```powershell
# Windows 广播编码测试
cd extra-services/music-client-win
uv run --frozen python test_music.py
```

```bash
# 服务器 UDP 重组、去重、暂停及二进制校验测试
cd extra-services/server
uv run --frozen python test_music.py
```
