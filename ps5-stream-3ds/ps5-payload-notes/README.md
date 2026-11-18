# PS5 端要做什么（腐化 PS5）

3DS 只是接收端。画面和控制都要靠 PS5 上跑的 **PBS Payload** 提供。

## 前提

- PS5 已破解（有 elfldr / etaHEN 等，elfldr 监听 9021 端口）
- 固件在 PBS 支持的列表内（3.20–13.60 的多数版本；9.00/11.00/14.x 部分不支持）

## 步骤

1. 下载 PBS Payload：`https://github.com/seregonwar/PBS-Payload`（GPL-3.0）
2. 用 payload loader 把 `pbs.elf` 注入 PS5
3. 启动串流（默认 9999 端口）。建议在 3DS 上用低分辨率减轻 WiFi b/g 负担：

   ```
   pbs.elf avserve 720p bitrate=4000
   ```

   **不要加 `nocontroller`** —— 那会关掉我们要用的输入回传。
   **也不要加 `noaudio`** —— 那会关掉 3DS 的喇叭声音。
4. 记下 PS5 的局域网 IP（路由器 DHCP 里看，或 PS5 设置里看）

## PBS 提供给 3DS 的接口（本项目用到）

| 接口 | 用途 |
|---|---|
| `GET /frames` | multipart MJPEG 视频流 |
| `GET /audio` | 音频流：每块 `[u32 长度BE][u32 PTS BE][float32 LE 数据]`，48kHz 立体声 |
| `POST /input/connect` | 申请虚拟手柄，返回 token |
| `POST /input/state` | 发按键状态（CSV） |
| `POST /input/disconnect` | 释放手柄 |

音频响应头是 `HTTP/1.1 200 OK` + `Content-Type: application/octet-stream`，
之后是连续块，无 Content-Length、非 chunked。payload 是 pcm_f32le，
48kHz / 2 声道 / 交错 L,R。3DS 端把它转成 s16 送 NDSP 播放。

输入状态格式（PBS README 原文）：8 个无符号十进制 CSV 字段
`sequence,buttons,lx,ly,rx,ry,l2,r2`，摇杆中位 128、范围 0-255，
sequence 每帧 +1（mod 2^32），至少每 100ms 发一次。

## 验证 PS5 端是否正常

在电脑浏览器打开 `http://<PS5-IP>:9999/`，能看到画面 + "Connect controller"
按钮、点击后能听到声音，就说明服务端 OK。之后再用 3DS 连。

也可直接访问 `http://<PS5-IP>:9999/audio`，若浏览器开始播放声音（或下载到
octet-stream 数据），说明音频端点正常——3DS 连的就是这个地址。

## 其它可选 payload

- **PSCapture**（NV12 over TCP，延迟低，但输出的是原始帧，需要另写解码）：
  `https://github.com/fstat13/PSCapture`
- **PS5 MCP**（自带 MJPEG + 输入，偏自动化）：
  `https://github.com/leolobato/ps5-mcp`

本项目协议是针对 PBS Payload 的 HTTP API 写的，换其它 payload 需要改
`inputctl.c`（输入）、`mjpeg.c`（视频流解析）和 `audio.c`（音频流）的协议部分。

---

合规提示：仅在你自己拥有、已破解的 PS5 与可信局域网内使用。破解主机可能违反
Sony 服务条款、失去保修；不要用于盗版或对外服务。
