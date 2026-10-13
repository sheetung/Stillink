# Stillink Windows 音乐广播

读取系统媒体信息和原始封面，通过 UDP 广播，不绘图、不直接上传 ESP。

## 使用

解压 Windows 工具包后，双击 **StillinkMusic.exe**，工具在后台运行，不显示命令行窗口。无需安装 Python 或 uv，也无需填写地址。重复双击不会启动多个实例。

电脑与接收服务器连接同一局域网，打开支持 Windows 系统媒体控件的播放器即可。双击 **停止音乐广播.vbs** 停止后台工具。

先按 [总部署说明](../../docs/services.md) 启动接收服务器，再按 [音乐使用说明](../../docs/music.md) 启用设备音乐卡片。不需要配置天气功能。

## 源码构建

Windows 10 / 11 安装 uv 后，在本目录执行：

```powershell
.\build.ps1
```

生成 `dist/StillinkMusic-win.zip`。源码调试可用 `uv run --frozen app.py`，按 Ctrl+C 退出。

默认使用 UDP `255.255.255.255:8790`。确需改变默认值时，将 `config.example.yml` 复制为同目录的 `config.yml`，修改后停止并重新启动。EXE 读取其所在目录的配置，源码读取本目录的配置。

运行日志位于 `%LOCALAPPDATA%\Stillink\music-client-win\client.log`。
