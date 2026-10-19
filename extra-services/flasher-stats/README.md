# 在线烧录统计（Cloudflare Worker + D1）

统计访问量、估算独立访客、串口连接结果、烧录成功/失败、固件与芯片分布。无需在 ESP 或额外服务服务器安装组件。

## 1. 初始化数据库

在 Cloudflare 创建 D1 数据库，在数据库的「控制台 / Console」执行 [schema.sql](schema.sql) 的全部内容。可以重复执行，不会清除数据。

## 2. 部署 Worker

1. 创建 Worker，在「绑定」中将 D1 绑定为 **`DB`**。
2. 点击「编辑代码」，用 [worker.mjs](worker.mjs) 的全部内容替换默认 Hello World，然后部署。无需构建或安装 npm 包。
3. 在 Worker「设置 → 变量和机密」中添加 **Secret / 密钥**：`ADMIN_KEY`，值使用自己生成的随机管理密钥，建议至少 32 个字符。不要放进源码、GitHub 或烧录页。
4. 访问 Worker 根地址，输入 `ADMIN_KEY` 查看汇总。管理密钥只保存在当前页面内存，刷新后需重新输入；点击退出会清除当前页面展示的数据。

Worker 默认允许 `https://092399.xyz` 上报。若烧录页使用其他域名，在 Worker 添加文本变量 `ALLOWED_ORIGINS`，值为允许的完整 Origin，多个用英文逗号分隔，例如 `https://example.com,https://another.example.com`。不要填写路径或结尾斜线。不要给整个 Worker 启用 Cloudflare Access，否则公开烧录页无法上报。

## 3. 连接 GitHub Pages 烧录页

在 GitHub 仓库 **Settings → Secrets and variables → Actions → Variables → New repository variable** 创建：

| 名称 | 值 |
| --- | --- |
| `FLASHER_STATS_URL` | `https://你的Worker域名/events` |

推送烧录页代码后，Pages 工作流会读取该变量并内嵌到单页中。只修改变量时，需要重新运行 **Online flasher** 工作流才会生效。不配置此变量就不发送统计。

本地构建时通过同名环境变量传入；本地预览需要在 `ALLOWED_ORIGINS` 中明确加入对应的 localhost Origin。

## 数据与统计口径

- 浏览器生成匿名随机访客编号，保存在当前站点的 localStorage。不同设备、无痕窗口或清除缓存会产生新编号，因此访客数是估算值。
- 页面访问、连接尝试、烧录尝试都有独立随机编号；重复结果不会重复计数，同一次尝试只接受第一个终态。串口选择取消单独统计。
- 成功代表固件已经写入并通过校验；后续自动复位失败仍算写入成功，并保留页面上的重新通电提示。
- 成功率为成功次数 ÷（成功次数 + 失败次数）。没有收到终态的尝试显示为「未完成」，不直接视为失败。
- 看板按北京时间汇总，提供最近 7 / 30 / 90 天。跨统计周期的进行中尝试可能使每日结果与期间尝试数存在差别。
- 只接收白名单字段：匿名编号、事件类型、芯片类别、Release 版本、烧录方式、耗时、错误分类。不会上传固件内容、本地文件名、串口序列号、设备密钥、照片或原始错误日志；Worker 代码不读取或保存 IP。
- 遵守浏览器 Do Not Track；上报不等待响应、不自动重试，上报被拦截或断网不会影响连接和烧录。静态页没有保存可靠终态的能力，数据只用于参考。
- 公共上报端点限制请求来源、内容大小、字段格式及每匿名编号的频率；前端不包含管理密钥。来源和随机编号可被伪造，这不是防作弊或计费系统。

## 接口

- `POST /events`：公开事件上报，JSON，限制 1024 字节；允许的 Origin 才返回 CORS 响应。
- `GET /stats?days=30`：汇总，需 `Authorization: Bearer <ADMIN_KEY>`，不返回访客编号和原始记录。
- `GET /public-stats`：公开累计访客数 `visitors` 和成功烧录次数 `success`，供烧录页底部展示，不需要管理密钥。页面加载与成功上报后刷新，读取失败显示横线或保留上次结果。
- `GET /`：无外部依赖的管理看板。

## 验证

使用 Node.js 22.13+，在 `extra-services/web-flasher` 目录运行 `npm test`。测试覆盖烧录流程、网络失败隔离、匿名编号、事件去重、权限、CORS、输入限制和真实 SQLite 汇总查询。
