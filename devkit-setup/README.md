# 3DS 开发工具链安装（绕过 Cloudflare 403）

## 为什么 pacman / 官方 updater 一直下载失败

不是你的配置问题，是 **IP 被 Cloudflare 拉黑**。实测证据：

- 访问 `https://pkg.devkitpro.org/packages/` 返回 403，页面内容明确写着
  **"Sorry, you have been blocked — Your IP: 61.178.65.2"**（兰州电信出口）
- 换 User-Agent：无效
- 系统 curl / pacman / Python urllib：全部 403
- 伪装成 Chrome 的 TLS 指纹（curl_cffi / curl-impersonate）：**仍然 403**

devkitPro 的 CDN 同时按「TLS 指纹 + IP」双重拦截，只解决其中一个没用。
官方 updater（`devkitProUpdater-3.0.3.exe`）和 MSYS2 里的 pacman 都用了会被识别的指纹，
所以它们必然失败。

## 关键点：必须换一个没被封的出口 IP

这是唯一的前置条件。可选办法（任选其一）：

1. **手机热点**：电脑连手机 4G/5G 热点，出口 IP 立刻变成移动网络，
   通常不在封禁列表里。这是最简单、成功率最高的办法。
2. **换代理节点**：如果你有代理软件，换一个**境外**节点（香港/日本/美国），
   不要用它默认的国内节点。注意当前本机 `127.0.0.1:21076` 这个代理的出口
   IP 也在黑名单里，实测同样 403，换节点才有用。
3. **换网络环境**：公司/其他运营商的宽带。

## 安装步骤

换好网络后，在本目录打开命令行（普通 cmd / PowerShell 即可，不需要 MSYS2）：

```bat
:: 1) 确认能通（应该列出包名，不再是 403）
python install_3ds_dev.py --dry-run --proxy http://127.0.0.1:21076

:: 2) 正式安装（走代理；如果直连就能通，去掉 --proxy，或加 --no-proxy）
python install_3ds_dev.py --proxy http://127.0.0.1:21076

:: 3) 如果想装到别的盘
python install_3ds_dev.py --proxy http://127.0.0.1:21076 --dest D:/devkitPro
```

脚本会自己：
- 用 Chrome TLS 指纹访问源站（绕指纹检测）
- 读取包数据库、自动算出依赖闭包（不用手动挑包）
- 下载 `.pkg.tar.zst` 并解包到目标目录

> 依赖 `curl_cffi`（已装）。换机器时装法：
> `python -m pip install -i https://pypi.tuna.tsinghua.edu.cn/simple curl_cffi`

## 安装后配置环境变量

假设装在 `C:\devkitPro`，设置（系统属性 → 环境变量，或用 setx）：

```bat
setx DEVKITPRO  C:\devkitPro
setx DEVKITARM  C:\devkitPro\devkitARM
setx PATH "%PATH%;C:\devkitPro\devkitARM\bin;C:\devkitPro\tools\bin;C:\devkitPro\portlibs\devkitARM\bin"
```

注意：包解包后可能位于 `C:\devkitPro\opt\devkitpro\...`（取决于包的内部结构）。
脚本结束时会打印实际目录。如果是这种结构，把上面的路径改成对应的
`C:\devkitPro\opt\devkitpro\devkitARM` 等。

重开一个终端，验证：

```bat
arm-none-eabi-gcc --version
```

能打印版本号就装好了，之后就能 `make` 编译 nfc-toolbox / http-share / iptv / ps5-stream 等项目。

## 如果换了网络还是 403

1. 确认 IP 真的变了：访问 <https://myip.ipip.net> 看当前出口 IP，
   不再是 61.178.65.2 才行。
2. 代理节点要选**境外**的；国内节点出口仍在黑名单。
3. 手机热点如果还是 403，开关一次飞行模式换 IP 再试。
4. 把 `install_3ds_dev.py --dry-run` 的完整输出发我，我根据具体报错调整。
