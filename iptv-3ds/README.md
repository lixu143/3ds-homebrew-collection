# IPTV-3DS

在 New 3DS 上直接收看公网公开免费频道的自制程序。**不需要电脑中转**——3DS 自己连 WiFi 拉流、自己解码播放。

## 先说清楚三件重要的事

1. **不是所有频道都能看。** New 3DS 的硬件解码只支持 H.264、最高 800×240（上屏一整屏）、码率别太高。现在网上大部分公开源是 1080p 或 H.265，本机硬解不了。这个 App 会自动把不兼容的频道过滤掉（列表里按 **X** 可以显示全部并看到原因）。要获得可用频道，你需要自己收集**低码率 H.264 源**。
2. **需要已经破解的 3DS（Luma3DS）**，并且装好了 devkitPro 才能编译。本项目只提供源码，不提供编译好的 .3dsx/.cia。
3. **只接公开、合法、免费的频道。** 本项目不包含任何频道地址，也不破解加密频道。你填入的源请自行确认有权收看。

## 工作原理

```
M3U 列表(SD卡/在线URL)
   -> 解析频道 + 按 3DS 解码能力过滤
   -> 选中频道, 如果是 HLS 主列表则自动选最低码率变体
   -> 3ds-curl 通过 HTTPS 拉流 (CA 证书来自 SD 卡)
   -> FFmpeg 解封装 + 解码 (MVD 硬解, 需专用 FFmpeg 构建)
   -> citro2d 画到上屏
```

## 依赖

用 devkitPro 的 pacman 安装：

```
pacman -S 3ds-curl 3ds-mbedtls 3ds-dev
```

还需要 **3DS 版的 FFmpeg 静态库**（带 MVD 硬件解码）。社区现成的来源：

- ThirdTube 仓库内附带的 `library/FFmpeg`（3ds 分支，已启用 MVD）— 推荐
- `Video player for 3DS` (Core-2-Extreme) 里带的 FFmpeg 构建

把 FFmpeg 的 `include/` 和 `lib/` 放到项目的 `library/FFmpeg/` 下，或修改 Makefile 里的 `FFMPEG_DIR`。链接 MVD 版时，取消 Makefile 里 `-DIPTV_FFMPEG_MVD` 的注释。

## 编译

```
make
```

产物 `iptv-3ds.3dsx`。如果链接报 undefined reference，多半是 FFmpeg 库路径或 `LIBS` 顺序问题——把实际报错发给我，我帮你调。

## 安装到 3DS

1. 把 `iptv-3ds.3dsx` 拷到 SD 卡根目录的 `3ds/` 文件夹。
2. 建立目录 `sdmc:/3ds/iptv-3ds/`。
3. 把你的频道列表存成 `sdmc:/3ds/iptv-3ds/channels.m3u`（格式见 `config/channels.m3u`）。
4. （可选）把列表的在线 URL 写进 `sdmc:/3ds/iptv-3ds/source.txt`，App 启动会自动下载更新。
5. **CA 证书**：确认 `sdmc:/config/ssl/cacert.pem` 存在（Luma3DS / Universal-Updater 会带）。没有它，HTTPS 源会连接失败。
6. **系统时间要正确**（Rosalina 菜单 `SELECT` → `Miscellaneous` → `Set clock`）。时间差太大会导致证书校验失败。

## 操作

| 按键 | 功能 |
|---|---|
| ↑ / ↓ | 选择频道 |
| ← / → | 翻页（每次 12 个） |
| A | 打开 / 播放选中频道 |
| B | 从播放器返回 |
| X | 切换"仅显示可播放频道" |
| Y | 重新加载列表 |
| START | 退出 |

## 播放失败的常见原因

- `Not playable: resolution above 800x240` — 源分辨率太高，找 240p/低码率源
- `Not playable: not H.264` — 源是 H.265/VP9，本机解不了
- `Cannot open stream` — URL 失效、证书缺失、系统时间错误，或 WiFi 没连
- 画面卡顿 — 码率超过 2.5Mbps，或 3DS 距离路由器太远

## 合规

本项目是播放器框架，不内置、不推荐任何具体频道地址。请只收看公开免费且你有权访问的频道，不要用于破解加密频道或传播盗版内容。
