# 固件

构建后运行 `python tools/package_bins.py v1.2.1`，固件自动生成到本目录。

- `stillink-v1.2.1-esp32s3.bin`：完整烧录包，从 `0x0` 写入。
- `stillink-v1.2.1-esp32s3-ota.bin`：设备网页上传的 OTA 应用镜像。

BIN 不提交到 Git；推送 `vX.Y.Z` 标签后，GitHub Actions 自动构建并上传到该版本的 Release。在线烧录页面使用完整烧录包。
