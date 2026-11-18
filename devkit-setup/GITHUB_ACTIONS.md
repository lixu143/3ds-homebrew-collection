# 用 GitHub Actions 云端编译 3DS 自制程序

本机不用装 devkitPro、不用管 Cloudflare 403。把源码推到 GitHub，云端自动编译，直接下载 `.3dsx` / `.cia` / `.3gx` 成品。

## 为什么这样做

- 编译环境跑在 GitHub 服务器上的 Docker 容器里（`devkitpro/devkitarm` 官方镜像，工具链全预装）
- 完全不碰 `pkg.devkitpro.org`，所以你兰州电信 IP 被封这件事跟它无关
- GitHub 你之前测过能正常访问

## 两种推送方式（选一种）

### 方式 A：每个项目单独一个仓库（推荐）

每个项目目录里已经放好了 `.github/workflows/build.yml`，推哪个仓库就自动编哪个。

以 `ps5-stream-3ds` 为例，在项目目录下执行：

```bat
cd C:\Users\Administrator\WorkBuddy\2026-11-17-10-00-18\ps5-stream-3ds
git init
git add .
git commit -m "initial"
git branch -M main
git remote add origin https://github.com/你的用户名/ps5-stream-3ds.git
git push -u origin main
```

仓库要在 GitHub 网页上先创建好（空的即可）。推送后点仓库的 **Actions** 标签就能看到构建。

### 方式 B：整个工作区当一个仓库

根目录 `.github/workflows/build-all.yml` 会一次编译全部 4 个标准项目（NFC / PS5 Stream / HTTP Share app / sysmodule）。在工作区根目录执行同样的 git 命令即可。

## 下载成品

构建完成后：

1. 打开仓库 → **Actions** → 点进最近一次成功的工作流
2. 页面底部 **Artifacts** 里，点对应名字下载（zip 包）：
   - `nfc-toolbox-3ds` → `build/nfc-toolbox-3ds.3dsx` ✅ 已验证出包
   - `ps5-stream-3ds` → `ps5-stream-3ds.3dsx` ✅ 已验证出包
   - `httpshare-app-3dsx` → `HTTPShare3DS.3dsx` ✅ 已验证出包
3. `.3dsx` 放进 SD 卡 `/3ds/` 从 Homebrew Launcher 启动；`.cia` 用 FBI 安装。

Artifacts 默认保留 90 天，需要更长可在 workflow 里加 `retention-days`。

## 两个特例（不会让整体构建失败）

| 项目 | 缺什么 | 怎么办 |
|---|---|---|
| `iptv-3ds` | 3DS 版 FFmpeg（放 `library/FFmpeg`，官方源没有） | 需自行提供 ThirdTube/Video player 分支的 FFmpeg 移植，或改用 PC 转码方案 |
| `http-share-3ds/plugin` | `libctrpf`（第三方 pacman 源 ThePixellizerOSS） | workflow 已尝试安装，装不上就自动跳过；3GX 插件才需要 |

这两个的 workflow 用了 `continue-on-error`，即使跳过也不影响其他项目出包。

**sysmodule 的 CIA 已经能自动产出了**（`httpshare-cia` artifact，含 `HTTPShare_Module.cia` + `HTTPShare_Launcher.cia`）。关键背景：

- `makerom` **不属于 devkitPro**（devkitPro 的 `3dstools` 只做 `.3dsx`），它属于独立项目 [3DSGuy/Project_CTR](https://github.com/3DSGuy/Project_CTR)，所以官方 devkitarm 镜像里根本没有它。
- workflow 现在会在构建时从 Project_CTR 的 GitHub Releases 下载预编译 `makerom`，存到 `$MAKEROM_BIN` 供 Makefile 调用。GitHub Actions 服务器访问 Releases 是内网直连，不受本地网络/Cloudflare 影响。
- 新版 `makerom v0.19.0` 需要 glibc 2.38，devkitarm 镜像的 glibc 太旧会报 `GLIBC_2.38 not found`；workflow 会从新到旧自动尝试，**最终选用兼容的 `v0.18.4`**。

**本地打 CIA**：自己在电脑上装 makerom 后，进入 `http-share-sysmodule/module` 和 `launcher` 直接 `make` 即可。预编译包在这里：
- Windows: `https://github.com/3DSGuy/Project_CTR/releases/download/makerom-v0.19.0/makerom-v0.19.0-win_x86_64.zip`
- Linux x64: `.../makerom-v0.19.0-ubuntu_x86_64.zip`（旧系统用 v0.18.4）
- macOS: `.../makerom-v0.19.0-macos_arm64.zip` / `macos_x86_64.zip`

其他 CIA 打包工具（可选）：`CIAToolsN`（Rust 写的 TUI，内置 makerom/bannertool，跨平台）、`3DS Packaging Studio`（GUI）、`ctrtool`/`3dstool`（提取/重建）。但对本项目的 `make` 流程，`makerom` 是直接对口的工具。

## 常见构建报错

- **`arm-none-eabi-gcc: command not found`** —— 镜像环境变量没生效。在 run 前加 `export PATH=$DEVKITARM/bin:$PATH`。
- **`make: *** No rule to make target`** —— 源码目录结构变了，检查 Makefile 里的 `SOURCES` / `INCLUDES` 路径。
- **`library not found -lxxx`** —— 缺对应 portlib，在 workflow 的 pacman 安装步骤里加上包名（如 `3ds-libjpeg`）。
- **CIA 打包失败 `GLIBC_2.38 not found`** —— makerom 版本太新，用旧版（workflow 已自动降级到 v0.18.4）。
- **CIA 打包失败（makerom 报错）** —— 确认 `$MAKEROM_BIN` 已指向可执行文件，`.rsf` 文件在 Makefile 指定位置。

把报错原文发我，我改 workflow 或 Makefile。
