# 在线烧录

[打开 Stillink 在线烧录页](https://blog.moontung.top/Stillink/)

使用电脑 Chrome 或 Edge，通过 USB 数据线连接设备。

1. 选择开发板对应的芯片：ESP32-S3 或 ESP32-C3。
2. 新设备选择「首次烧录 · 完整包」；已安装 Stillink 的设备选择「更新固件 · OTA 应用」。
3. 点击「连接并选择设备」，在弹出的列表中选择设备串口。
4. 点击烧录，等待完成，期间不要拔掉 USB。

烧录后连接 `Stillink-XXXX` 热点，在浏览器打开 `http://192.168.4.1`。

## 连接失败

确认 USB 线支持数据传输，并关闭占用串口的软件。仍无法连接时，按住 BOOT、轻按 RESET，再松开 BOOT 后重试。

## 更新已有设备

设备联网后，可在「设备设置 → 系统维护与升级」检查新版本并下载升级，也可从 [Release](https://github.com/sheetung/Stillink/releases) 下载对应芯片的 `-ota.bin`，通过设备网页上传。

新设备的 USB 首次烧录使用完整 `.bin`，不要将完整包上传到设备网页。升级后请确认屏幕与休眠设置。
