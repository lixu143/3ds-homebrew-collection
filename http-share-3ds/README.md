# WifiShare 3DS（无线传文件）

New 3DS 上的 HTTP 文件服务器：在同一 WiFi 下，用电脑浏览器就能给 3DS 的 SD 卡**上传 / 下载 / 删除**文件。核心是纯 C 的 HTTP 引擎（`common/`），提供两种外壳。

> **v2.1 起**：CIA 已带自制图标（WiFi + 文件夹，蓝底白图）和正式名字 —— 英文 `WifiShare 3DS`、中文 `无线传文件`。图标由 `app/tools/make_smdh.py` 纯 Python 生成（`app/icon.smdh`），打包时通过 makerom `-icon` 嵌入 NCCH 与 CIA Meta。
>
> **v2.1 同时修复了"SD 卡被拔出"启动崩溃**：根因是 exheader 缺 `DirectSdmcWrite`（服务器要写 SD，只读权限不够）以及服务名 `apt:u` 大小写错误（libctru 实际请求 `APT:U`），再加上 CIA 原先完全没有 icon 文件。已按通过验证的 homebrew CIA 配置补齐权限、恢复 libctru 默认初始化。界面顶部会显示 `WifiShare 3DS  v2.1`，装完请以此确认刷的是新版。

| 形态 | 产物 | 后台能力 | 适合 |
|---|---|---|---|
| **A. 独立 App** | `.3dsx` / `.cia` | 前台运行，退出即停 | 立即可用、好调试 |
| **B. 3GX 插件** | `.3gx` → 更名 `default.3gx` | **进任何游戏自动后台常驻**，SELECT 呼出面板 | 你要的"不挂游戏、自动后台" |

> 你的需求（不用每次挂到某个游戏、开机进游戏就自动后台传文件）对应的是 **形态 B 的全局部署**：编译一次得到 `.3gx`，改名 `default.3gx` 放进 `luma/plugins/`，之后**所有游戏通用，不用按 TitleID 逐个挂**。形态 A 用于先跑通验证传输。

## 关于"开机自动运行"的真实边界（务必先看）

3DS **没有** Windows 那种"开机后台服务进程"。无论哪种自制方案，都逃不开下面这条边界：

| 方式 | 开机自动？ | 后台？ | 停在 HOME 菜单时？ |
|---|---|---|---|
| 独立 App 自启 | ✅ 但要进 hbmenu 点一下 | ❌ 占屏幕，玩不了游戏 | 不适用 |
| **`default.3gx` 全局插件（本项目）** | 进游戏即自动加载 | ✅ 不占屏，游戏照常玩 | ❌ HOME 菜单不加载插件 |
| sysmodule 系统模块 | ✅ 真正开机自启 | ✅ 真后台 | ✅ 常驻（难度最高，有 bootloop 风险） |

**结论**：`default.3gx` 已经覆盖你 99% 的使用场景——你日常传文件本来就是在玩游戏时顺手传，而不是停在 HOME 菜单传。它一次部署、所有游戏通用、进游戏自动后台，体验和"开机自动"几乎无差别，且**零 bootloop 风险**。真要"连 HOME 菜单都常驻"才需要 sysmodule，那是 3DS 自制最高难度的一档，本项目暂不提供。

---

## 功能

- `GET /` — 网页文件列表，带上传表单
- `POST /upload` — multipart 上传（流式写 SD，不占大内存），支持 `path` 子目录
- `GET /download?file=xxx` — 下载文件到电脑
- `GET /delete?file=xxx` — 删除文件（有二次确认）
- 路径沙箱：所有操作限制在 `sdmc:/3ds/httpshare/`，拒绝 `..`、绝对路径、反斜杠穿越
- 单连接、非阻塞 accept + 主动让出 CPU（后台不卡游戏）

上传的文件默认落在 `sdmc:/3ds/httpshare/`，同时会自动创建 `Downloads/` 目录。

---

## 前置条件（两种形态都需要）

1. New 3DS / New 2DS（老 3DS 也能跑 App，但插件生态主要针对 N3DS）
2. 已破解：**boot9strap + Luma3DS**（插件形态需要 Luma 的 3GX 插件加载器，Luma 13.x 自带；本 README 按 v13.5 写）
3. devkitPro 工具链（安装见文末"附录 A"）
4. WiFi 已连上，且电脑和 3DS 在**同一个局域网**（访客网络隔离会连不上）

---

## 形态 A：独立 App（先跑通这个）

```bash
cd app
make
```

生成 `HTTPShare3DS.3dsx`。放进 SD 卡 `/3ds/` 目录，从 Homebrew Launcher 启动。

同一个 `make` 还会产出 **`HTTPShare3DS.cia`**（需要 `makerom` 在 PATH 里，缺失时自动跳过、不影响 `.3dsx`）。这是**普通应用 CIA**（`Category: Application`，TID `00040000005A1902`），和 sysmodule 版完全不同：

- ✅ **低风险**：装 SD 卡即可，普通应用权限，没有 bootloop / 变砖风险，不需要备份 NAND；
- ✅ 装好后出现在 HOME 菜单，点开即用，按 START 退出；
- ✅ 卸载随时可在 FBI → Titles 里删。

用 FBI 安装 `.cia`（装 SD 或 NAND 都行，建议 SD），或从 GitHub Actions 的 `httpshare-app-cia` artifact 下载现成的。功能与 `.3dsx` 完全一样，只是多了一个 HOME 菜单图标入口。

> 注意：App 版是**前台**的——退出就停止服务，玩游戏时不后台。要后台传文件还是得形态 B（3GX 插件）或 sysmodule 版。

操作：
1. 按 **A** 启动服务（屏幕显示 `http://192.168.x.x:8080/`）
2. 电脑浏览器打开这个地址，就能看到文件列表和上传表单
3. 按 **A** 停止，按 **START** 退出

---

## 形态 B：3GX 后台插件（核心交付）

### 额外依赖

CTRPF 插件库不在官方源里，需要加 ThePixellizerOSS 的 pacman 源（与 Gen6CTRPluginFramework 等插件项目相同）：

```bash
# 打开 MSYS2 (C:/devkitPro/msys2/msys2_shell.bat) 后执行：
if ! grep -Fxq "[thepixellizeross-lib]" /etc/pacman.conf; then
  echo -e "\n[thepixellizeross-lib]\nServer = https://thepixellizeross.gitlab.io/packages/any\nSigLevel = Optional" | tee -a /etc/pacman.conf >/dev/null
fi
if ! grep -Fxq "[thepixellizeross-win]" /etc/pacman.conf; then
  echo -e "\n[thepixellizeross-win]\nServer = https://thepixellizeross.gitlab.io/packages/x86_64/win\nSigLevel = Optional" | tee -a /etc/pacman.conf >/dev/null
fi
pacman -Sy
pacman -S libctrpf          # 若包名不同，用 pacman -Ss ctrpf 查找
```

`3gxtool.exe`（打包 .3gx 用）需要从 CTRPF 模板发布页获取，放到 PATH 里或 plugin 目录。

### 编译

```bash
cd plugin
make
```

生成 `HTTPShare.3gx`。

### 安装到 SD 卡（全局部署，推荐）

把编译出的 `HTTPShare.3gx` **改名为 `default.3gx`**，放到：

```
SD:/luma/plugins/default.3gx
```

就这样——**不用建 TitleID 目录，不用管你玩的是哪款游戏**。`default.3gx` 是 Luma 官方支持的"全局插件"机制：任何游戏/应用启动时都会自动加载它。

> 备用：若只想对某个游戏生效，才放 `SD:/luma/plugins/<游戏TitleID>/HTTPShare.3gx`（TitleID 在 FBI → Titles 查，如 `0004000000190E00`）。全局部署不需要这个。

### 使用

1. 开机后按 **L + 下 + SELECT** 打开 Rosalina 菜单 → **Plugin loader** 设为 **Enabled** → 按 B 返回
   - 老 3DS 该选项默认开启；New 3DS 需手动开一次（Rosalina 里设为 Enabled 后，部分版本重启仍保持，掉设置就再开一次）
2. 之后**每次进任何游戏，插件都会自动加载**（屏幕闪一下色即成功），服务随即在后台启动
3. 电脑浏览器打开 `http://<3DS的IP>:8080/` 传文件，**游戏照常玩，无需退出**
4. 游戏中按 **SELECT** 呼出小面板，可看 IP/URL、传输统计、重启服务；不呼出时它完全隐形

> 提示：`default.3gx` 会加载到你启动的每个 3DS 游戏进程，占用少量内存（约 64KB 线程栈 + HTTP 缓冲）。个别**本身重度使用 socket 的在线游戏**可能有端口/服务冲突，若某游戏异常，可把全局 `default.3gx` 换成只针对那个游戏目录的写法排查。普通单机游戏基本无影响。

---

## 电脑端上传脚本

```
pc/upload.ps1 -Ip 192.168.x.x -File "C:\game.cia"        # Windows PowerShell
pc/upload.sh 192.168.x.x game.cia [subdir] [port]         # Linux/macOS/Git Bash
```

也可以直接用浏览器上传（最简单）。

---

## 常见问题

| 现象 | 排查 |
|---|---|
| 浏览器打不开 | IP 对不对、是否同一 WiFi（非访客网）、服务是否 RUNNING、端口 8080 是否被占 |
| 插件加载后游戏卡住 | 该游戏与 socket/CTRPF 冲突；换用独立 App 形态，或只挂单机游戏 |
| `socInit failed` | WiFi 没连；或该游戏已独占 socket 服务 |
| 上传大文件失败 | 确认 SD 卡剩余空间、FAT32 簇大小建议 32–64KB |
| 文件名乱码 | 尽量用英文文件名 |

---

## 合规说明

这是一个**通用的局域网文件传输工具**，本身完全合法——和 ftpd、LocalSend 一类工具性质相同。请只传输你有权持有的文件（自制程序、自己的备份、公开授权素材），不要用它传播盗版游戏或影视。工具不含任何破解、绕过授权的功能。

---

## 源码结构

```
http-share-3ds/
├── common/            # 共用的纯 C HTTP 引擎（socket/解析/上传下载删除/沙箱）
│   ├── httpshare.h
│   └── httpshare.c
├── app/               # 形态 A：citro2d 独立 App
│   ├── source/main.c
│   └── Makefile
├── plugin/            # 形态 B：3GX 后台插件
│   ├── source/main.cpp
│   ├── Includes/      # (ctrpf 由库提供)
│   ├── 3gx.ld, *.plgInfo, Makefile
├── pc/                # 电脑端上传脚本
└── README.md
```

后台 socket server 的架构参考了真实 3GX 插件 **ArticBase**（在游戏进程内起 server 线程 + socket），CTRPF 插件入口与构建参考 **CTRPluginFramework-BlankTemplate**。

---

## 附录 A：从零安装 devkitPro（Windows 11）

你还没装工具链，按这个走一遍就能编译：

### 1. 下载安装器
- 打开 `devkitpro.org` → Download → 下载 **Windows** 的 `devkitProUpdater` (`.exe`)
- 或直接搜 GitHub 的 `devkitPro/devkitPro-installers`  releases 里的 `.exe`

### 2. 安装时勾选组件
运行安装器，**组件选择**页面勾选：
- `3ds-dev`（开发 3DS 的整套工具链，含 `3ds-curl`、`citro2d`、`citro3d`、`libctru`）
- `MSYS2`（安装器默认会带）

安装路径保持默认 `C:\devkitPro`。

### 3. 安装 CTRPF 库（编译插件才需要）
打开 `C:\devkitPro\msys2\msys2_shell.bat`，执行：

```bash
# 加 ThePixellizerOSS 源（CTRPF 库不在官方源）
echo -e "\n[thepixellizeross-lib]\nServer = https://thepixellizeross.gitlab.io/packages/any\nSigLevel = Optional" >> /etc/pacman.conf
pacman -Sy
pacman -S libctrpf
```

如果 `pacman -S libctrpf` 找不到包，用 `pacman -Ss ctrpf` 搜实际包名。

### 4. 获取 3gxtool
从 CTRPluginFramework-BlankTemplate 的 releases 下载 `3gxtool.exe`，放到 `C:\devkitPro\tools\` 或项目 `plugin/` 目录（Makefile 要能找到）。

### 5. 验证环境
在 msys2 shell 里执行：

```bash
echo $DEVKITPRO          # 应显示 /c/devkitPro
arm-none-eabi-gcc --version
```

都能输出即环境就绪，回到项目目录 `make` 即可。

> 提示：本项目的 Makefile 用的是标准 devkitPro 变量（`DEVKITPRO`/`DEVKITARM`），安装器会自动配置，一般不用手动改路径。若 `make` 报找不到头文件或工具，把报错原文发我。
