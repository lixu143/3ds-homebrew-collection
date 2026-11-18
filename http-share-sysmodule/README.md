# HTTP Share 3DS — sysmodule 版

把 New 3DS 变成一个**开机后台常驻**的 HTTP 文件服务器：同一 WiFi 下，用电脑浏览器就能给 SD 卡**上传 / 下载 / 删除**文件，**不用进任何游戏、不占屏幕、不影响正常游玩**。

这是比 3GX 插件更高一档的形态——它是一个真正的**系统进程（sysmodule）**，装进 NAND 后由 Launcher 拉起，常驻后台。

---

## ⚠️ 先读：三条必须知道的事实

### 1. 这不是"装了就开机自动跑"的普通软件

3DS 的自制生态里，"sysmodule 开机自启"没有一键方案。真实机制是这样的：

- `Luma` 的 `/luma/sysmodules/` 只能**替换**同名内置模块（`sm.cxi`/`fs.cxi` 等），放个新名字进去不会被加载——所以不能用它来加载我们的服务器；
- 本项目走的是**已被 3Input 验证**的路：把 HTTP 服务器编译成 `Category:Base` 的系统进程 CIA，装进 NAND；再装一个普通 Launcher，由它调用 `NS_LaunchTitle()` 把服务器进程拉起来；
- **所谓"开机自启"= 让 Luma 开机自动进 Launcher → Launcher 自动拉起服务器 → 退回 HOME 菜单**。这一步需要在 Luma 里配置启动项（见后文）。

### 2. 这是本项目难度最高的一档，有变砖（bootloop）风险

- sysmodule 以系统进程身份运行，**写错 RSF 权限或内存配置可能导致开机卡死**；
- 我无法在这台机器上编译或实机测试（本地没装 devkitPro），你需要在自己的机器上编译、刷入、验证；
- **务必先备份 NAND**（GodMode9 → `NAND backup`）再安装。真变砖时用恢复模式 + NAND 备份可救。

### 3. 它只适用于 New 3DS / New 2DS

sysmodule 用了 `MemoryType:Base` 和 N3DS 的额外内存；老 3DS 不在支持范围。

---

## 和另一版（3GX 插件 / 独立 App）的区别

| 版本 | 产物 | 后台能力 | 难度 / 风险 |
|---|---|---|---|
| 独立 App（`http-share-3ds`） | `.3dsx` | 前台，退出即停 | 低，最安全 |
| 3GX 插件（`default.3gx`） | `.3gx` | 进游戏后后台，HOME 菜单不加载 | 中 |
| **本版 sysmodule** | 2 个 `.cia` | **开机常驻，HOME 菜单也在跑** | **高，有 bootloop 风险** |

如果你只是想要"玩游戏时传文件"，3GX 插件版更稳妥。本版适合你明确要"开机就在后台、不依赖任何游戏"的场景。

---

## 目录结构

```
http-share-sysmodule/
├── Makefile              # 顶层：一键编译两个 CIA
├── common/               # 共用的纯 C HTTP 引擎（复用自 3GX 版）
│   ├── httpshare.c
│   └── httpshare.h
├── module/               # 后台常驻的 sysmodule
│   ├── Makefile
│   ├── httpshare.rsf     # 进程属性：Category:Base + SVC/服务权限
│   └── source/
│       ├── _init.c       # 自定义堆分配（sysmodule 必需）
│       ├── main.c        # 入口、服务初始化、退出通知、WiFi 恢复
│       └── wifi.c/.h     # WiFi 保活 + 掉线重连
├── launcher/             # 拉起 sysmodule 的普通应用
│   ├── Makefile
│   ├── httpshare_launcher.rsf
│   └── source/main.c     # NS_LaunchTitle 启动模块 + 显示 IP
└── pc/                   # 电脑端上传脚本
    ├── upload.ps1        # Windows
    └── upload.sh         # Linux / macOS
```

**TID 对应关系（不要改错）**：
- sysmodule `UniqueId = 0x5A17` → TID `0x00040130005A1702`
- launcher `UniqueId = 0x5A18`（普通应用）
- Launcher 源码里的 `MODULE_UNIQUE_ID` 必须和 module 的 RSF `UniqueId` 一致，否则拉起失败。

---

## 一、安装开发环境

### Windows

1. 下载 devkitPro 安装器：https://github.com/devkitPro/installer/releases
2. 运行，只勾选 **3DS 开发**（会自动装 devkitARM + libctru）
3. 打开 **MSYS2**（开始菜单 devkitPro → MSYS2），执行：
   ```bash
   dkp-pacman -Syu
   dkp-pacman -S 3ds-dev --needed
   ```
4. 安装打包工具（两个都要）：
   - `makerom`：https://github.com/3DSGuy/Project_CTR/releases → 下载 `makerom` 放进 MSYS2 的 `usr/local/bin`
   - 用 `which makerom` 确认能找到

### Linux / macOS

按 https://devkitpro.org/wiki/Getting_Started 装 devkitPro + `3ds-dev`，再单独编译安装 `makerom`。

验证环境：
```bash
echo $DEVKITARM        # 应指向 devkitARM
arm-none-eabi-gcc --version
makerom --version
```

---

## 二、编译

在项目根目录：

```bash
make
```

成功后得到两个文件：

```
module/HTTPShare_Module.cia       <- 后台服务器（系统进程）
launcher/HTTPShare_Launcher.cia   <- 启动器
```

常见编译问题：
- `Please set DEVKITARM` → 环境没配好，回到第一步；用 MSYS2 终端而不是普通 CMD。
- `makerom: command not found` → makerom 没进 PATH。
- RSF 相关报错 → 大概率是 `makerom` 版本太老，换最新版。
- 出现 `output file too big` / KIP 容量错误 → 说明链接产物过大；确认 `module/Makefile` 里 `-DSOC_BUFFERSIZE=0x50000` 生效。

---

## 三、安装到 3DS（先备份 NAND！）

**备份**：开机进 GodMode9 → `Home` → `NAND backup` → 等它写完，确认 `gm9/out/` 下有 `.bin` 备份文件。

**安装两个 CIA**（二选一）：

- 用 **FBI**：把两个 `.cia` 拷到 SD 卡 → 打开 FBI → `SD` → 找到 CIA → `Install CIA` → 分别安装。
  - `HTTPShare_Module.cia` 装到 **NAND**（系统进程必须装 NAND，不能装 SD）
  - `HTTPShare_Launcher.cia` 装到 NAND 或 SD 都行，建议 NAND

装完后，Launcher 会出现在 HOME 菜单里（图标是默认图标）。先手动测试一次：

1. 连好 WiFi
2. 打开 Launcher → 看屏幕显示的 IP（如 `http://192.168.1.23:8080/`）
3. 按 **A** 启动服务器 → 显示 `Launch module: OK`
4. 电脑浏览器打开那个地址，能看到文件列表 = 成功
5. 关掉 Launcher、打开任意游戏 → 服务器仍在后台跑，浏览器仍能访问

> 如果按 A 显示 `FAILED`：检查两个 CIA 是否都装好、TID 是否匹配（RSF 的 UniqueId 和 Launcher 源码里的 `MODULE_UNIQUE_ID`）。

---

## 四、配置开机自启（可选）

要让开机自动运行，需要让 Luma 开机自动进 Launcher：

1. 拿到 Launcher 的 title ID：在 FBI → `Titles` → 找到 HTTPShare → 记下 ID（形如 `0004000005A18...`）
2. 方法一（推荐，Rosalina）：进游戏或 HOME 菜单按 **L+下+SELECT** 打开 Rosalina → `Miscellaneous options` → 相关的 autoboot 项；但最可靠的是方法二
3. 方法二（config.ini）：用电脑打开 SD 卡 `/luma/config.ini`，在 `[3ds]` 段加入：
   ```ini
   autoboot_3ds_app_titleid = 0004000005A18???
   ```
   （把 `???` 换成你 FBI 里查到的完整 ID 末几位；不同 Luma 版本字段名可能是 `autoboot_app`，按你的版本为准）

⚠️ 各 Luma 版本对 autoboot 普通 CIA 的支持不完全一致。**如果配置后开机没自动进 Launcher，属正常现象**——退回手动点 Launcher 即可，服务器一旦拉起就会常驻，效果和开机自启几乎一样（只是多了点一下）。

---

## 五、使用

### 浏览器

同一 WiFi 下打开（`IP` 以 Launcher 屏幕显示为准）：

```
http://192.168.1.23:8080/
```

- 列出 / 下载 `sdmc:/3ds/httpshare/` 下的文件
- 网页底部有上传表单，选文件即可传到 `Downloads/`
- 支持删除

### 电脑脚本

```powershell
# Windows (PowerShell)
.\pc\upload.ps1 -Ip 192.168.1.23 -File "C:\path\to\game.cia"
```

```bash
# Linux / macOS
bash pc/upload.sh 192.168.1.23 /path/to/game.cia
```

### 停止服务器

- **关机**会自动停止（模块订阅了系统的退出通知）；
- 或打开 Launcher 随时重启它；
- 想彻底卸载：FBI → `Titles` → 卸载两个 CIA → 重启。

---

## 六、工作原理

- **进程身份**：`Category:Base` + `MemoryType:Base` 的系统进程，以较高权限常驻，优先级低于游戏。
- **内存**：`_init.c` 用 `svcControlMemory` 手动分配堆（New 3DS 上 ~3.2MB + 2.7MB 线性堆），socket buffer 压到 320KB 以塞进系统内存区。
- **HTTP**：`common/httpshare.c` 纯 POSIX socket + `socInit`，单连接串行处理，非阻塞 accept；所有路径限制在 `sdmc:/3ds/httpshare/`，拒绝 `..` 穿越。
- **WiFi 保活**：`wifi.c` 持有自己的 `ac` 连接请求，每秒检查链路；掉线时重开无线电并重连，恢复后重建监听 socket（IP 可能变了）。
- **退出**：订阅 `srv` 的 `NOTIF_EXIT`（0x100），系统关机时优雅收尾。

架构参考了真实项目 **3Input**（sysmodule 启动、堆分配、WiFi 保活）与 **rehid**（RSF 权限表）。

---

## 七、合规

这是通用文件传输工具，性质和 ftpd 一样。请只传输你有权持有的文件（自己的备份、自制程序、公开授权资源），不要用来传播盗版游戏或影视。

---

## 八、出问题怎么排查

| 现象 | 可能原因 |
|---|---|
| Launcher 按 A 显示 FAILED | 模块 CIA 没装 / TID 不匹配（检查两个 UniqueId） |
| 编译报 `heap`/`link` 相关错 | `module/Makefile` 的 `-DSOC_BUFFERSIZE` 没生效 |
| 开机卡死 / bootloop | RSF 配错 → 进恢复模式，用 NAND 备份还原 |
| 浏览器打不开 | WiFi 没连、IP 变了（重开 Launcher 看新 IP）、端口被占 |
| 进游戏后掉线 | 游戏关了 WiFi；模块会自动重连，等几秒刷新浏览器 |
| 传文件中途失败 | 确认 SD 卡剩余空间足够；大文件建议用电脑脚本 |
