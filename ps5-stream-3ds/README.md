# PS5-Stream-3DS

在 New 3DS 上看 PS5 游戏画面，并用 3DS 的按键/触摸屏操作 PS5——
一个跑在 3DS 上的"串流接收端 + 虚拟手柄"。

```
PS5 (PBS Payload)  --MJPEG 画面-->  New 3DS 上屏
                   --PCM 音频-->  New 3DS 喇叭
PS5 (虚拟手柄)     <--按键 CSV--  New 3DS 按键/触屏
```

画面、声音、操作三样全走 WiFi，不插 AUX 线。

全程局域网、通话不经过电脑。

## 两端分工（"免安装"只说 3DS 端，别误解）

- **PS5 端：必须加载一个 elf 文件** —— 第三方开源项目 PBS Payload 的
  `pbs.elf`。它是**运行时注入的载荷**（通过 elfldr/etaHEN 这类加载器打进内存），
  不是"安装到系统"，PS5 重启后会失效、要重新注入。这个 elf 需要你自己去
  GitHub 下载，**不在本项目的交付包里**。详见下方"使用前提"和
  `ps5-payload-notes/README.md`。
- **3DS 端**：只需装本项目的一个自制程序（.3dsx/.cia），无额外驱动或插件。

## 它是怎么做到的

难点从来不在 3DS，而在"PS5 怎么把画面吐出来、怎么接收按键"。
这两件事 **PBS Payload**（2026-11 开源的 PS5 破解插件）已经解决了：

- 用 PS5 自带硬件编码器抓画面，通过 `GET /frames` 发 MJPEG 流
- 通过 `GET /audio` 发 48kHz 立体声 PCM 音频（块头 = 大端长度+PTS，payload = float32 LE）
- 通过 `POST /input/*` 在 PS5 上创建一个虚拟 DualSense，接收按键

所以本项目只写 3DS 端：

| 模块 | 作用 |
|---|---|
| `mjpeg.c` | socket 连接 PS5，解析 multipart MJPEG，libjpeg 缩放解码成 RGBA |
| `audio.c` | socket 连 `/audio`，解析块头，float32→s16，NDSP 48kHz 立体声播放 |
| `renderer.c` | C3D 纹理上传 + citro2d 画到上屏（复用 IPTV-3DS 的渲染写法）|
| `inputctl.c` | `/input/connect` 取 token，定时 `POST /input/state` 发按键 |
| `config.c` | SD 卡存 IP/端口/路径 |
| `main.c` | 菜单、按键映射、虚拟摇杆、音量、主循环 |

音频不依赖额外解码库：PBS 输出的就是裸 PCM，3DS 的 NDSP 硬件直接吃 48kHz PCM16，
只需把 float32 转成 s16。声音和画面是两条独立 socket 连接。

## 操作方式

**菜单**
- 上/下：选字段（host / port / path / fit / volume）
- 左/右：改端口、切换 fit 模式、调音量（每次 10%）
- X：弹出软件键盘编辑 host / path
- A：保存并连接　START：退出

连接后菜单会显示 `Video + audio + controller connected` 表示三路都通；
若某路失败会单独提示（例如浏览器要手势才出声，但 3DS 直连 `/audio` 无此限制）。

**串流中**（3DS 当手柄用）
| 3DS | PS5 |
|---|---|
| A / B / X / Y | ✕ / ○ / □ / △ |
| 十字键 | 十字键 |
| L / R | L1 / R1 |
| ZL / ZR | L2 / R2 |
| START / SELECT | Options / Create |
| 下屏触摸拖动 | 虚拟摇杆移动 |
| X | 切换左/右摇杆 |
| B | 断开连接 |

## 编译

需要 devkitPro + `3ds-dev` + libjpeg：

```bash
pacman -S 3ds-dev 3ds-libjpeg
make
```

生成 `ps5-stream-3ds.3dsx`（可选放一个 `icon.png` 生成 .smdh）。
把 .3dsx 放进 SD 卡的 `/3ds/`，从 Homebrew Launcher 启动；
或用 FBI 装 .cia（需要自行用 3dsxtool/makerom 打包）。

## 使用前提（重要）

1. **PS5 必须已破解**（elfldr / etaHEN 等加载器），并**每次开机后注入一次
   `pbs.elf`** 来启动串流服务，且**不能加 `nocontroller` 参数**。
   `pbs.elf` 由用户自行下载，本项目不包含。详见 `ps5-payload-notes/README.md`。
2. PS5 和 3DS 在同一局域网，建议 PS5 用网线、路由器给 PS5 固定 IP。
3. 建议 PBS 用 **720p + 低码率**（3DS 只有 2.4G WiFi b/g，带宽和解码都有限）。
   本项目会把解码结果缩放到约 480×320 以内再显示。
4. **音频走独立连接** `/audio`：PBS 默认就带音频，除非启动时加了 `noaudio` 参数。
   浏览器端要点击才出声（自动播放策略），但 3DS 直连没有这个限制。
5. 输入协议按 PBS 当前 HTTP API 实现；换其它 payload（PSCapture/PS5 MCP）需改协议代码。

## 现实预期

- 画质/帧率受限于 3DS 的 WiFi 和 JPEG 软解，属于"能用、有情怀"，
  别指望达到手机串流的流畅度。动作游戏可能延迟明显。
- 适合：菜单操作、回合制/慢节奏游戏、远程开关机/看画面。
- 按键位映射按标准手柄布局，若你的 PBS 构建位序不同，改 `inputctl.h` 的
  `INPUT_BTN_*` 定义即可。

## 合规

仅在你自己拥有、已破解的 PS5 与可信局域网内使用。破解主机可能违反 Sony
服务条款、失去保修。本项目不含任何盗版内容，仅是一个串流协议客户端。
不要暴露这些无认证的服务到公网。

## 状态

源码骨架已交付，**未做实机编译**（本地无 devkitPro）。编译报错请把原文发来，
逐个修到能出 .3dsx。
