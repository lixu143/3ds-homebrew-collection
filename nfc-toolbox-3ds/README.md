# NFC Toolbox for New 3DS

一个跑在 New 3DS 上的 NFC 工具箱自制程序。用 libctru 的 `nfc:u` 服务直接操作
机身内置的 NFC 读头，实现标签探测、amiibo 信息查看、UID 二维码、扫描历史、
整卡备份与原始页读写。纯本地运行，不联网、不需要百度/任天堂账号。

## 功能

1. **标签探测**：读 NTAG213/215/216 的 7 字节 UID，识别标签容量。
2. **amiibo 档案**：读取 amiibo 的系列、类型、角色 ID、写入次数、昵称等
   （只读你的主机本来就会显示的信息）。
3. **UID 二维码**：把标签 UID 生成二维码，手机扫码即可录入表格/台账。
4. **扫描历史**：扫描记录追加到 SD 卡 `history.txt`，可翻页回看、清空。
5. **数据导出 / 原始读写**：
   - FAST_READ 整卡备份（NTAG215 为 540 字节）到 `dumps/` 目录；
   - 读单个数据块（16 字节）；
   - 写单个页（4 字节）——带二次确认，仅用于你自己的空白标签。
6. **关于与安全说明**。

## 硬件要求

- **New 3DS / New 3DS XL / New 2DS XL**（内置 NFC 读头，2014 年底后的机型）。
  - 老 3DS 没有内置 NFC，本程序不适用。
- 系统版本 ≥ 9.3.0（New 3DS 出厂即满足）。
- 已破解并安装自定义固件（Luma3DS 等），能从 Homebrew Launcher 运行 `.3dsx`。
- 一张 microSD 卡。

## 开发环境

- devkitPro（含 `3ds-dev`、`citro2d`、`citro3d`、`libctru`）
- `dkp-pacman -S 3ds-dev 3ds-citro2d 3ds-citro3d`（若用 pacman 安装）
- 可选：`bannertool` + `makerom`（仅打 CIA 时需要）

## 编译

```bash
export DEVKITPRO=/opt/devkitpro      # 按你的实际安装路径
export DEVKITARM=$DEVKITPRO/devkitARM
cd nfc-toolbox-3ds
make
```

产物：`build/nfc-toolbox-3ds.3dsx`

打 CIA（可选，装到 HOME 菜单）：
```bash
make cia
# 需要自行提供 .rsf / 图标 / banner；否则用 FBI 的"从 SD 安装 3dsx"更省事
```

## 安装与使用

1. 把 `build/nfc-toolbox-3ds.3dsx` 拷到 SD 卡 `/3ds/` 目录。
2. 把主机连上 WiFi（首次会校时；时间不对时程序会提示去系统设置里调）。
3. 从 Homebrew Launcher 启动。
4. 主菜单用 ↑↓ 选择、A 进入、B 返回。
5. 扫描时把卡片/ amiibo 贴到**机身 NFC 感应区**（下屏下方、New 3DS 徽标附近），
   保持不动直到读取完成；按 B 取消。

程序生成的文件都在 SD 卡：
```
/NFC-Toolbox/
├── history.txt        # 扫描历史（制表符分隔，可用表格软件打开）
└── dumps/
    └── <UID>_01.bin    # 整卡备份
```

## 技术说明

- **扫描**：raw 模式用 `nfcStartOtherTagScanning`，amiibo 模式用 `nfcStartScanning`，
  轮询 `nfcGetTagState` 直到 `InRange`。
- **整卡读取**：FAST_READ 命令 `{0x3A, start, end}`，一次 15 页循环读完
  NTAG215 的 0x00–0x86 页。
- **写页**：WRITE 命令 `{0xA2, page, d0,d1,d2,d3}`，返回 `0x0A` 为 ACK；
  写前调用 `nfcCmd21()` 让读头保持连续模式（参考开源项目 Thenaya 的实机验证实现）。
- **二维码**：使用 [Nayuki QR Code generator](https://www.nayuki.io/page/qr-code-generator-library)
  （MIT 许可）的 C 版本，ECC-M、自动版本，手机相机可直接识别。
- NTAG 命令时序与 UID 提取方式参考 MIT 许可项目 Thenaya。

## 法律与安全声明

- **仅用于你合法拥有的标签**：备份自己的卡片、管理自己的 amiibo、读写自己的空白标签。
- **不要**用本工具复制商业 amiibo 进行出售或分发，也不要破解门禁卡、交通卡等
  涉密或他人财产——这可能违法并违反服务条款。
- 写页操作（尤其锁定位）**不可逆**，写错可能让标签报废，请谨慎。
- "写入数据"功能仅向 NTAG 的用户数据区写 4 字节示例，不涉及任何加密密钥。

## 已知限制

- 仅支持 New 3DS 系列；老 3DS 无内置 NFC。
- amiibo 档案页是只读展示，不含解密密钥，不提供 amiibo 克隆功能。
- CIA 打包需要额外工具，默认只产出 `.3dsx`。
- 本程序在 Citra 模拟器里无法测试 NFC 硬件（模拟器未实现 nfc:u），需实机运行。

## 许可

本项目代码采用 MIT 许可。第三方组件：Nayuki qrcodegen（MIT）。
参考实现：Thenaya（MIT）。
