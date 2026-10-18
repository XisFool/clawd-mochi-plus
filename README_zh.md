# Clawd Mochi Plus

**Clawd Mochi 桌面宠物的增强版开源固件** —— 15 种参数化几何表情、运行时可调的几何缩放引擎、仿真 Idle 生命状态机、STA 优先 + AP 配网 WiFi。无位图素材、无私有配置文件，打开即可编译烧录。

本项目源自 [yousifamanuel/clawd-mochi](https://github.com/yousifamanuel/clawd-mochi)（MIT 许可）的独立固件增强分支，在保持原版硬件方案与开箱即用体验的基础上，为现代 AI 编程助手场景补充了一整套表情系统。

<p align="center"><img src="docs/images/expressions-demo.gif" alt="Clawd Mochi Plus 表情演示 —— 全部 15 种表情"></p>

- 🇬🇧 English: [README.md](README.md)
- 🎬 离线预览：任意浏览器打开 `preview.html`，无需硬件

---

## 特性

- **15 种纯数学几何表情** —— 全部基于 `(eyeW, eyeH, eyeGap, eyeOx, eyeOy)` 在运行时用 `Adafruit_GFX` 图元实时绘制。零 ROM 位图、零内存帧缓冲。
- **自适应几何引擎** —— 通过 `/eye` HTTP 接口调节眼睛宽高、间距与偏移，15 种表情自动等比缩放；显式 `save=1` 才写入 NVS 持久化。
- **仿真 Idle 生命感** —— 2.5~6 秒随机眨眼、6~15 秒随机左右环视、每 30~60 秒一次短暂的 `> <` 眯眼、5 分钟无操作自动进入横条 + zZ 小憩。
- **STA 优先 + AP 配网** —— 首次开机启动 `ClaWD-Mochi` 热点（`clawd1234` / `http://192.168.4.1`）；打开 `/wifi` 填写 2.4GHz 家庭网络，凭据存入 NVS，重启后接入局域网并通过 mDNS 提供 `http://clawd-mochi.local`。开机连接失败会保持 AP 并继续以 AP+STA 重试。
- **极简内置网页控制器** —— 设备自带移动端页面：15 个表情按钮、AP/STA 连接状态、背光开关、`WiFi 设置` 入口；`/eye` 几何接口仍保留在 HTTP API 中。
- **离线逐帧预览器** —— `preview.html` 回放固件渲染的全部 15 种表情帧，支持播放/暂停与单帧步进。纯本地运行，无 CDN 依赖。

## 硬件

| 部件 | 说明 |
| --- | --- |
| 主控 | ESP32-C3 Super Mini（160 MHz） |
| 屏幕 | 1.54 寸 ST7789 SPI TFT，240×240，3.3V 逻辑 |
| 接线 | SDA→GPIO 10 (MOSI)，SCL→GPIO 8 (SCK)，RST→GPIO 2，DC→GPIO 1，CS→GPIO 4，BL→GPIO 3，VCC→**3.3V**，GND→GND |

> ⚠️ **电气安全**：屏幕 VCC 只能接 **3.3V**，接 5V 可能烧毁屏幕。
>
> ⚠️ **SPI 引脚约束**：SCK/MOSI 必须用 GPIO 8/10，切勿使用 GPIO 6/7（多数 C3 模组该脚接内部 SPI Flash，接错会导致无法启动）。

3D 打印外壳：请使用原作者在 MakerWorld 的模型 —— [Clawd Mochi physical Claude Code mascot](https://makerworld.com/en/models/2559505-clawd-mochi-physical-claude-code-mascot#profileId-2820000)。本仓库有意**不附带 STL/3MF 文件**：打印原版外壳，刷入本固件即可。

## 编译与烧录

**方式一 —— Arduino IDE 2.x**

1. 安装 `esp32` 开发板包（Boards Manager 添加 Espressif 官方 URL）。
2. 库管理器安装：`Adafruit GFX Library`、`Adafruit ST7735 and ST7789 Library`。
3. 打开 `clawd_mochi/clawd_mochi.ino`，开发板选 **ESP32C3 Dev Module**，参数：
   - CPU Frequency：**160 MHz**
   - USB CDC On Boot：**Enabled**
   - Upload Speed：**921600**
4. 烧录固件即可。

**方式二 —— arduino-cli**

```bash
arduino-cli compile --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc,CPUFreq=160,UploadSpeed=921600 clawd_mochi
arduino-cli upload  --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc,CPUFreq=160,UploadSpeed=921600 -p COM<端口> clawd_mochi
```

默认编译**不需要任何配置文件**，首次启动即为 AP 配网热点。没有 `#error` 拦截，也不需要创建 `wifi_config.h`。

## 表情一览

15 种表情，可通过 HTTP（`/cmd?k=<key>`）或内置网页控制器触发。

<p align="center"><img src="docs/images/expressions-overview.png" alt="全部 15 种表情与名称"></p>

| 序号 | 表情 | 按键 | 别名 | 帧数 | 帧周期 | 行为 | 适用场景 |
| :-: | --- | :-: | :-: | :-: | :-: | --- | --- |
| 1 | 普通 | `w` | `normal` | 1 | — | 常驻视图，受 Idle 眨眼/环视影响 | 空闲待机 |
| 2 | 开心 | `h` | `happy` | 1 | — | 保持后自动恢复 | 回应夸奖 / 完成 |
| 3 | 困倦 | `z` | `sleepy` | 2 | 600 ms | 循环，横条眼 + 上浮 zZ | 深夜 / 小憩 |
| 4 | 惊讶 | `o` | `surprised` | 5 | 60 ms | 单次晃动，双眼放大 | 倾听 / 受惊 |
| 5 | 眨眼 | `k` | `wink` | 1 | — | 保持后自动恢复 | 调皮互动 |
| 6 | 晕眩 | `x` | `dizzy` | 5 | 60 ms | 单次摆动，X X 眼 | 错误 / 卡住 / 绕晕 |
| 7 | 生气 | `g` | `angry` | 1 | — | 保持后自动恢复 | 警告 |
| 8 | 爱心 | `l` | `heart` | 2 | 250 ms | 循环脉动心形眼 | 任务成功 / 赞美 |
| 9 | 思考 | `t` | `think` | 4 | 140 ms | 循环，流动波浪条带 | 分析 / 思考 |
| 10 | 执行中 | `e` | `executing` | 3 | 200 ms | 循环，双眼向内侵蚀 | 代码运行 / 工具调用 |
| 11 | 哭泣 | `c` | `crying` | 4 | 200 ms | 循环，交替下落泪滴 | 任务严重失败 |
| 12 | 震惊 | `b` | `shocked` | 2 | 90 ms | 循环，超大抖动双眼 | 意外结果 |
| 13 | 墨镜 | `v` | `cool` | 2 | 350 ms | 循环，墨镜 + 移动反光 | 编译通过 / 完工 |
| 14 | 害羞 | `m` | `embarrassed` | 2 | 400 ms | 循环，视线偏转 + 腮红 | 请求权限 / 羞涩 |
| 15 | 馋嘴 | `y` | `delicious` | 4 | 200 ms | 循环，嘴角滴水 | 美食 / 奖励 |

**挤压眼（`s`）** —— 经典的 `> <` 视图 —— 是**独立常驻视图**，不计入 15 种表情；切换过去后会一直停留，直到手动切换。

`/cmd?k=` 接受上表的单字母按键与完整单词别名：`normal`、`happy`、`sleepy`、`surprised`、`wink`、`dizzy`、`angry`、`heart`、`think`、`executing`、`crying`、`shocked`、`cool`、`embarrassed`、`delicious`、`squish`；`n` 用于在普通视图静默恢复普通脸，不会退出挤压眼视图。未知或空 key 返回 HTTP **400** `{"error":"unknown key"}`。

临时表情 5 秒后自动恢复（思考为 6 秒）；用 `lease=<ms>`（500~1800000，最长 30 分钟）可延长。普通眼与挤压眼不受 lease 影响。

## REST API

访问地址：`http://192.168.4.1`（AP 模式）或 `http://clawd-mochi.local` / 路由器分配的 IP（STA 模式）。以下接口均返回 JSON（`/wifi` 页面除外）。

| 接口 | 用途 |
| --- | --- |
| `/cmd?k=<key>` | 触发表情按键或别名（见上表）。临时表情可加 `lease=<ms>`（500~1800000）。未知 key 返回 HTTP 400。 |
| `/eye?w=&h=&gap=&ox=&oy=` | 调节眼宽/眼高/眼距/水平偏移/垂直偏移。加 `save=1` 写入 NVS 持久化；`reset=1` 恢复默认。 |
| `/state` | 当前表情、idle/背光状态、眼部几何参数、动画速度、动画背景色（`bg`）、WiFi 模式（`ap`/`sta`）、IP、hostname、RSSI（仅 STA 模式）、剩余堆内存；带 `Cache-Control: no-store`。 |
| `/wifi` | `GET` 返回配网表单；`POST ssid=&pass=` 保存到 NVS 并重启。SSID 为空返回 HTTP 400。 |
| `/health` | `{"ok":true}`，带 `Cache-Control: no-store`。 |
| `/idle?on=1\|0` | 开/关 Idle 生命机能（立即持久化）。 |
| `/speed?v=1\|2\|3` | 动画速度：1 = 慢（默认），2 = 正常，3 = 快。 |
| `/backlight?on=1\|0` | 屏幕背光开/关。 |
| `/redraw?bg=<RRGGBB>` | 设置动画背景色并立即重绘；非法颜色返回 HTTP 400。 |

示例：

```bash
curl "http://192.168.4.1/cmd?k=crying"
curl "http://192.168.4.1/cmd?k=e&lease=1800000"
curl "http://192.168.4.1/eye?w=36&h=72&gap=128&oy=36&save=1"
curl "http://192.168.4.1/state"
curl "http://192.168.4.1/health"
```

## 接入 Claude Code 示例

任何 HTTP 客户端都能驱动表情：

```bash
curl "http://192.168.4.1/cmd?k=e"   # 执行中
curl "http://192.168.4.1/cmd?k=l"   # 爱心 —— 全部完成
```

`examples/clawd-notify.py` 把 Claude Code 的 hook 事件映射为表情。推荐映射（可自行修改）：

| Claude Code hook 事件 | 按键 | Lease (ms) | 表情 |
| --- | :-: | ---: | --- |
| `UserPromptSubmit` | `t` | 600000 | 思考 |
| `PreToolUse` | `e` | 1800000 | 执行中 |
| `PostToolUse` | `t` | 600000 | 思考 |
| `SubagentStart` | `t` | 600000 | 思考 |
| `SubagentStop` | `t` | 600000 | 思考 |
| `PostToolUseFailure` | `x` | — | 晕眩 |
| `PermissionRequest` | `m` | 600000 | 害羞 |
| `Notification` | `m` | 600000 | 害羞 |
| `Stop` | `l` | — | 爱心 |
| `StopFailure` | `c` | — | 哭泣 |
| `SessionEnd` | `n` | — | 恢复普通 |

在 Claude Code 的 `settings.json`（`hooks` 段）中接入该脚本，例如每次工具调用时：

```json
{
  "hooks": {
    "PreToolUse": [
      { "matcher": "*",
        "hooks": [ { "type": "command",
                     "command": "python /path/to/examples/clawd-notify.py", "timeout": 5 } ] }
    ]
  }
}
```

脚本默认访问 `http://clawd-mochi.local`。如果 mDNS 不可用，设置环境变量 `CLAWD_URL` 为设备 IP（例如首次 AP 配网时的 `http://192.168.4.1`）。脚本使用 0.4 秒网络超时；连接错误直接忽略。

## WiFi 模式

**首次开机 / 无凭据 —— 配网热点。** 设备以 AP 模式启动：

- SSID `ClaWD-Mochi`，密码 `clawd1234`，网页地址 `http://192.168.4.1`
- 打开 `http://192.168.4.1/wifi`，填写 2.4GHz SSID 与密码并提交；凭据写入 NVS（`Preferences`，命名空间 `wifi`），随后设备自动重启。

**配网后 —— 家庭局域网（STA）。** 设备以 station 模式启动、通过 DHCP 获取地址，并用 mDNS 广播 `http://clawd-mochi.local`。开机最多等待 15 秒；连接失败会以 AP+STA 模式拉起 AP 热点并继续重试，便于重新配网。

**运行期重连。** 每 10 秒巡检一次，掉线调用 `WiFi.reconnect()`。AP 只在开机阶段拉起；如果运行中路由器密码变更，重启设备即可回到开机 AP 兜底。

说明：仅支持 2.4GHz 网络；不再提供静态 IP 配置，地址由路由器 DHCP 分配；mDNS 为尽力而为，初始化失败不会阻塞启动。

## 离线预览器（`preview.html`）

双击 `preview.html` 即可 —— 单文件 HTML5 播放器，内置全部表情由固件逐帧渲染的结果。支持播放/暂停、单帧步进，并显示每种表情的帧数与周期。完全本地：无 CDN、无构建步骤、无网络请求。

说明：预览帧为预生成 PNG 矩阵，不会响应 `/eye` 几何调整；需要实时调参请直接调用 `/eye` HTTP 接口。

## 项目结构

```text
clawd-mochi-plus/
├── clawd_mochi/
│   └── clawd_mochi.ino        # 完整固件（单文件）
├── examples/
│   └── clawd-notify.py        # Claude Code hook → 表情桥接（示例）
├── docs/images/               # README 配图（由固件渲染帧生成）
├── preview.html               # 离线表情逐帧预览器
├── .github/workflows/compile.yml   # CI：每次推送自动编译固件
├── CHANGELOG.md
├── LICENSE                    # MIT（原作者 + Plus 贡献者）
└── README.md / README_zh.md
```

## 致谢与许可

- 原项目：[yousifamanuel/clawd-mochi](https://github.com/yousifamanuel/clawd-mochi) —— 硬件方案、基础固件、3D 外壳（[MakerWorld](https://makerworld.com/en/models/2559505-clawd-mochi-physical-claude-code-mascot#profileId-2820000)）。
- Clawd Mochi Plus：表情引擎、自适应几何、Idle 状态机、STA 优先 + AP 配网 WiFi、离线预览器。

MIT 许可 —— 见 [LICENSE](LICENSE)。
