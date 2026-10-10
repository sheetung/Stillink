# Stillink

把喜欢的瞬间，放在身边。

Stillink 是基于 ESP32-C6 与华为拆机四色电子墨水屏的网页相框。手机或电脑在浏览器中完成选图、裁剪、风格化与文字排版，再上传到屏幕。图像处理在浏览器本地完成，设备负责接收与刷新；断电后屏幕仍能保留画面。

## 功能

- 内置网页与热点，无需 App，支持自动弹出控制页和离线客户端导出。
- 图片裁剪、四色抖动、留白与文字编辑，支持 A0 / A1 屏幕配置。
- 扫描 2.4 GHz Wi-Fi，连接并保存配网信息，通过局域网 IP 访问。
- 上传画面、网页热更新与 OTA 固件升级。
- 画面与设备设置保存，定时唤醒和空闲休眠。

## 硬件与接线

默认目标为 **ESP32-C6FH4，4 MB Flash**，使用原生 USB Serial/JTAG 下载。屏幕可见区为 **768 × 552**，颜色为黑、白、黄、红。不同拆机批次需在设备设置中选择对应的屏幕配置。

| 信号 | ESP32-C6 引脚 |
| --- | --- |
| SCK | GPIO4 |
| MOSI | GPIO6 |
| CS | GPIO7 |
| DC | GPIO1 |
| RST | GPIO3 |
| BUSY | GPIO10 |
| 唤醒按键 | GPIO5，按下接 GND |
| 电池采样 | GPIO0 / ADC1_CH0，470kΩ : 100kΩ 分压 |

唤醒按键需单独连接，板载 BOOT（GPIO9）不是此功能按键。电池电压需按实际电路校准。接线定义位于 `main/epd_drv.h`。

## 编译与 USB 烧录

安装并激活 ESP-IDF 6.1 环境后，通过 SSH 获取工程：

```powershell
git clone git@github.com:sheetung/Stillink.git
cd Stillink
idf.py set-target esp32c6
idf.py build
idf.py -p COM11 flash monitor
```

`COM11` 是当前测试板端口，请按实际设备修改。构建自动从 `page/index.html` 生成内嵌页面，并检查页面与固件版本是否一致。

生成整包及 OTA 镜像：

```powershell
python tools/package_bins.py R1.3.3
python tools/flash_device.py --port COM11 --bin stillink_R1.3.3_merged.bin
```

- `build/stillink.bin` / `stillink_R1.3.3_app.bin`：应用镜像，供 OTA 使用。
- `stillink_R1.3.3_merged.bin`：含启动程序、分区表和应用，USB 烧录地址为 `0x0`。
- `flash_kit/`：预编译 R1.3.3 镜像、网页和 Windows 一键刷机脚本。

旧版 R1.2.1 的 OTA 校验固定为 ESP32-C3 芯片标识；当前 C6 板首次升级到 R1.3.3 请用 USB。R1.3.3 已按实际构建目标校验镜像。

## 热点直连与 Wi-Fi 配网

1. 上电后连接默认开放热点 `Stillink-XXXX`，打开自动弹出的控制页，或访问 `http://192.168.4.1`。
2. 选择图片、裁剪、调整风格与文字，预览后上传画面。
3. 在设备设置的「Wi-Fi 配网 · 局域网访问」中点击「Wi-Fi 名称」，自动扫描后选择 2.4 GHz 网络并输入密码，点击「连接并保存」。
4. 获得 IP 后，页面显示局域网链接。手机或电脑连接同一路由器，即可通过该 IP 使用控制页。

获得局域网 IP 后，设备保留热点约 10 秒供页面显示地址，随后自动关闭热点，仅通过路由器访问。路由器断开或忘记 Wi-Fi 时自动恢复热点。局域网 IP 由路由器分配，可能变化，也可在路由器客户端列表中查看。

成功连接后才保存凭据；重启自动连接，固件 OTA 后仍保留。连接失败可从热点重新配网。「忘记 Wi-Fi」和恢复出厂会清除配网信息。状态接口不返回密码。

连接局域网或正在连接时设备不自动休眠，断网后会尝试重连；忘记 Wi-Fi 后恢复原休眠策略。深睡期间网页不可访问，可通过 GPIO5 按键或配置的定时唤醒恢复使用。

直接以 `file://` 打开网页可进行本地编辑与预览；要操作设备，请访问热点 / 局域网控制页，或使用从设备页面导出的离线客户端。

## 开发与验证

```powershell
node tools/smoke_page.js
node tools/test_wifi_page.js
python tools/check_frame.py
idf.py build
```

页面测试覆盖帧打包、页面结构和配网交互，包括扫描结果、凭据编码、连接状态、局域网链接与忘记网络。R1.3.3 已完成 ESP32-C6 编译和页面测试；此前 R1.2.1 已在 COM11 完成 USB 烧录与启动检查，R1.3.3 配网仍需真机连接路由器验证。

内部 NVS 命名空间与网页版本标记保留 `epaper` 标识，以保持已有设置和页面升级格式兼容。

| 路径 | 内容 |
| --- | --- |
| `main/` | ESP-IDF 固件、屏幕驱动、网络、存储与电源管理 |
| `page/index.html` | 当前网页源文件 |
| `tools/` | 页面生成、测试、打包与刷机工具 |
| `flash_kit/` | Windows 刷机包 |
| `sdkconfig.defaults` / `partitions.csv` | 默认构建配置与 OTA 分区 |
| `legacy_ref/` | 历史参考代码与算法材料 |

接口与帧格式见 [传输与驱动契约](docs/CONTRACT.md)，详细操作见 [入门教程](docs/入门教程.md)，历史记录见 [CHANGELOG](docs/CHANGELOG.md)。

## 许可说明

截至 2026-10-10，MoInk 上游提交 `17f2f3b76e1400b685db2b3a108ed1aec2cb17b4` 未提供 LICENSE，README 的许可说明为：

> 私有项目，版权保留。如需开源请先补充 LICENSE 并检查 `legacy_ref/` 中第三方素材的授权情况。

Stillink 保留上游的版权与许可限制，当前不声明 MIT、Apache 或其他开源许可。仓库公开不代表已获得开源授权；采用与上游相同的开源协议，需先由上游明确协议或取得授权。

内嵌 Cropper.js 保留作者 Chen Fengyuan 的 MIT 许可声明；其他第三方代码与 `legacy_ref/` 材料以各自原始授权为准。

## 致谢

感谢 [MoInk](https://github.com/atunverse/moink) 提供原始工程、网页编辑器与墨水屏实现，Stillink 在其基础上完成 ESP32-C6 适配与局域网配网扩展。感谢 Cropper.js、InkSight 等相关项目的作者；原有第三方版权及来源说明予以保留。
