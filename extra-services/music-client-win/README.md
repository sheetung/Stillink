# Stillink Windows 音乐广播

读取系统媒体信息和原始封面，通过 UDP 广播，不绘图、不直接上传 ESP。

## 使用

从 [Release 下载 Windows 工具包](https://github.com/sheetung/Stillink/releases/latest/download/StillinkMusic-win.zip)，解压后双击 **StillinkMusic.exe**。工具在后台运行，任务栏右侧通知区域显示音乐图标，不弹出命令行窗口。无需安装 Python 或 uv，也无需填写地址。重复双击不会启动多个实例。

电脑与接收服务器连接同一局域网，打开支持 Windows 系统媒体控件的播放器即可。右键托盘图标选择「退出」停止广播；图标可能在任务栏右侧的隐藏图标中。

先按 [总部署说明](../../docs/services.md) 启动接收服务器，再按 [音乐使用说明](../../docs/music.md) 启用设备音乐卡片。不需要配置天气功能。

## 源码构建

Windows 10 / 11 安装 uv 后，在本目录执行：

```powershell
.\build.ps1
```

生成 `dist/StillinkMusic-win.zip`。源码调试可用 `uv run --frozen app.py`，按 Ctrl+C 退出。

默认使用 UDP `255.255.255.255:8790`。确需改变默认值时，将 `config.example.yml` 复制为同目录的 `config.yml`，修改后停止并重新启动。EXE 读取其所在目录的配置，源码读取本目录的配置。

运行日志位于 `%LOCALAPPDATA%\Stillink\music-client-win\client.log`。
