#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
install_3ds_dev.py — 绕过 Cloudflare,直接安装 3DS 开发工具链 (devkitARM + libctru + citro3d/2d)

背景:
    pkg.devkitpro.org 用 Cloudflare 同时按「TLS 指纹 + IP」拦截。
    - 普通 curl / pacman / wget     -> 403 (指纹被识别)
    - 只换 UA / 改 pacman 源         -> 无效
    - 只伪装指纹但 IP 在黑名单       -> 仍然 403
    脚本用 curl_cffi 伪装 Chrome 的 TLS 指纹 (JA3/JA4),再叠加一个「未被封的代理」,
    自己解析 pacman 数据库、算依赖闭包、下载 .pkg.tar.zst、解包到目标目录。

 prerequisite:
    python -m pip install curl_cffi
    (本机已装好;换到别的机器用清华源: -i https://pypi.tuna.tsinghua.edu.cn/simple)

用法:
    1) 先确认出口 IP 没被封。当前网络若被封,会看到 403 "Sorry, you have been blocked"。
       换手机热点 / 换代理节点 / 换网络后重试。
    2) 直连(出口干净时):
         python install_3ds_dev.py
    3) 走代理(推荐,用你本机现成的 127.0.0.1:21076 或一个更干净的节点):
         python install_3ds_dev.py --proxy http://127.0.0.1:21076
    4) 只想看会装哪些包、不下载:
         python install_3ds_dev.py --dry-run

安装位置:
    默认 C:/devkitPro  (与官方 devkitPro shell 约定一致)
    可用 --dest 改,例如 --dest D:/devkitPro
"""

import argparse
import io
import os
import re
import sys
import tarfile
from pathlib import Path

try:
    from curl_cffi import requests as cfrequests
except ImportError:
    sys.exit("缺少 curl_cffi,请先运行: python -m pip install curl_cffi")

# 3DS 编译真正需要的最小包集合(会自动展开依赖)
ROOTS = [
    "devkitARM",              # arm-none-eabi 编译器 (含 libstdc++)
    "libctru",                # 3DS 系统库
    "citro3d",                # 3D 渲染
    "citro2d",                # 2D 渲染
    "3ds-tools",              # 3dxtool / makerom / bannertool
    "3ds-libjpeg-turbo",      # JPEG 解码 (mjpeg 串流用)
    "3ds-zlib",
    "3ds-freetype",
    "3ds-libpng",
    "3ds-mbedtls",            # TLS (3ds-curl 依赖)
    "3ds-curl",               # HTTP 客户端 (IPTV 用)
    "3ds-pkg-config",         # Makefile 解析依赖
    "dkp-toolchain-vars",     # 工具链环境变量脚本
]

BASE = "https://pkg.devkitpro.org/packages"
# 数据库与包文件路径。devkitPro 把「所有平台的库」放在 dkp-libs,
# Windows 宿主工具单独放。3DS 的库/工具大部分在 dkp-libs 与 dkp-windows。
REPOS = [
    ("dkp-libs",   f"{BASE}/dkp-libs.db",   BASE),
    ("dkp-windows", f"{BASE}/windows/x86_64/dkp-windows.db", f"{BASE}/windows/x86_64"),
]

IMPERSONATE = "chrome136"


def make_session(proxy):
    kw = {"impersonate": IMPERSONATE, "timeout": 60}
    if proxy:
        kw["proxies"] = {"http": proxy, "https": proxy}
    return cfrequests.Session(**kw)


def fetch_bytes(session, url):
    r = session.get(url)
    if r.status_code != 200:
        ctype = r.headers.get("content-type", "")
        snippet = ""
        if "text" in ctype or "html" in ctype:
            snippet = re.sub(r"\s+", " ", re.sub(r"<[^>]+>", " ", r.text))[:200]
        raise RuntimeError(
            f"HTTP {r.status_code} {url}\n"
            f"  content-type: {ctype}\n"
            f"  {snippet}\n"
            f"  若是 'Sorry, you have been blocked' -> 出口 IP 被 Cloudflare 封锁,"
            f"换网络/换代理节点后重试。"
        )
    return r.content


def parse_desc(text):
    """解析 pacman 的 desc 文件: %KEY%\\n值... 的重复块。"""
    fields, key = {}, None
    for line in text.splitlines():
        if line.startswith("%") and line.endswith("%"):
            key = line.strip("%")
            fields.setdefault(key, [])
        elif key is not None:
            fields[key].append(line)
    return fields


def load_db(session, name, db_url, base_url, index, cache_dir):
    data = fetch_bytes(session, db_url)
    db_path = cache_dir / f"{name}.db"
    db_path.write_bytes(data)
    count = 0
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as tar:
        for m in tar.getmembers():
            if not m.name.endswith("/desc"):
                continue
            f = tar.extractfile(m)
            if not f:
                continue
            desc = parse_desc(f.read().decode("utf-8", "replace"))
            nm = desc.get("NAME", [""])[0].strip()
            fn = desc.get("FILENAME", [""])[0].strip()
            if not nm or not fn:
                continue
            deps = []
            for d in desc.get("DEPENDS", []):
                d = d.strip()
                if not d:
                    continue
                # 去掉版本约束: foo>=1.2 / foo=1.2
                deps.append(re.split(r"[<>=]", d)[0].strip())
            index[nm] = {
                "filename": fn,
                "repo": name,
                "base": base_url,
                "depends": deps,
                "version": desc.get("VERSION", [""])[0].strip(),
            }
            count += 1
    return count


def main():
    ap = argparse.ArgumentParser(description="绕过 Cloudflare 安装 3DS devkitPro 工具链")
    ap.add_argument("--proxy", default=os.environ.get("HTTPS_PROXY") or os.environ.get("https_proxy"),
                    help="代理地址,如 http://127.0.0.1:21076;留空则直连")
    ap.add_argument("--dest", default="C:/devkitPro", help="安装目录,默认 C:/devkitPro")
    ap.add_argument("--dry-run", action="store_true", help="只列出依赖,不下载")
    ap.add_argument("--no-proxy", action="store_true", help="强制直连(忽略环境变量里的代理)")
    args = ap.parse_args()

    proxy = None if args.no_proxy else args.proxy
    print(f"代理      : {proxy or '(直连)'}")
    print(f"TLS 指纹  : {IMPERSONATE}")
    print(f"安装目录  : {args.dest}")

    session = make_session(proxy)
    dest = Path(args.dest)
    cache = dest / "_download_cache"
    root = dest
    cache.mkdir(parents=True, exist_ok=True)
    root.mkdir(parents=True, exist_ok=True)

    # ---- 1. 拉数据库 ----
    index = {}
    print("\n[1/3] 读取包数据库 ...")
    for name, db_url, base_url in REPOS:
        try:
            n = load_db(session, name, db_url, base_url, index, cache)
            print(f"  {name}: {n} 个包")
        except Exception as e:
            print(f"  ✗ {name}: {e}", file=sys.stderr)
            return 1
    if not index:
        print("数据库为空,放弃。", file=sys.stderr)
        return 1

    # ---- 2. 依赖闭包 ----
    want, queue, missing = set(), list(ROOTS), []
    while queue:
        name = queue.pop()
        if name in want:
            continue
        if name not in index:
            missing.append(name)
            continue
        want.add(name)
        queue.extend(index[name]["depends"])

    print(f"\n[2/3] 依赖闭包: {len(want)} 个包")
    for name in sorted(want):
        info = index[name]
        print(f"  {name:26s} {info['version']:14s} [{info['repo']}]")
    if missing:
        print(f"\n  提示: 源里没有这些(可能由其他包提供,已忽略): {', '.join(sorted(set(missing)))}")

    if args.dry_run:
        print("\n--dry-run 结束,未下载。")
        return 0

    # ---- 3. 下载并解包 ----
    print(f"\n[3/3] 下载并解包到 {root} ...")
    failed = []
    for i, name in enumerate(sorted(want), 1):
        info = index[name]
        url = f"{info['base']}/{info['filename']}"
        pkg = cache / info["filename"]
        print(f"  [{i}/{len(want)}] {name} ... ", end="", flush=True)
        try:
            if not (pkg.exists() and pkg.stat().st_size > 0):
                data = fetch_bytes(session, url)
                if data[:1] == b"<":
                    print("上游返回 HTML(被拦截)")
                    failed.append(name)
                    continue
                pkg.write_bytes(data)
            # .pkg.tar.zst 内是相对路径,直接解到 dest
            with tarfile.open(pkg, mode="r:zst") as tar:
                for member in tar.getmembers():
                    # 防目录穿越
                    target = (root / member.name).resolve()
                    if not str(target).startswith(str(root.resolve())):
                        continue
                tar.extractall(root, filter="data")
            print("ok")
        except Exception as e:
            print(f"失败: {type(e).__name__}: {str(e)[:80]}")
            failed.append(name)

    if failed:
        print(f"\n✗ 以下包失败: {', '.join(failed)}", file=sys.stderr)
        return 1

    # devkitPro 包通常解成 dest/opt/devkitpro/... 或 dest/devkitARM/...
    print(f"\n✅ 完成。检查目录:")
    for d in ["devkitARM", "libctru", "citro3d", "citro2d", "tools", "portlibs"]:
        p = root / d
        p2 = root / "opt" / "devkitpro" / d
        print(f"  {'OK ' if p.exists() or p2.exists() else '-- '} {d}")
    print(f"\n下一步: 把 {root} 设为 DEVKITPRO,并把 devkitARM/bin 加入 PATH。")
    print("  详见同目录 README.md")
    return 0


if __name__ == "__main__":
    sys.exit(main())
