#!/usr/bin/env python3
"""
Generate a 3DS SMDH (application icon + metadata) with pure Python, no deps.

Produces a valid 0x36C0-byte SMDH containing:
  - 16 language entries (short title / long title / publisher) in UTF-16LE
  - Region-free + visible flags
  - A hand-drawn RGB565 icon (48x48 large + 24x24 small), Z-order swizzled

The Z-order tile mapping is verified against devkitPro's tex3ds swizzle table.
"""
import struct, os

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "icon.smdh")

# ---- metadata -------------------------------------------------------------
SHORT_TITLE = {
    "en": "WifiShare 3DS",
    "zh": "无线传文件",
}
LONG_TITLE = {
    "en": "WiFi File Share - send files to 3DS over LAN",
    "zh": "无线传文件 - 通过局域网给3DS传文件",
}
PUBLISHER = {
    "en": "Homebrew",
    "zh": "自制软件",
}

# SMDH language slot order (3dbrew): JP,EN,FR,DE,IT,ES,zh-CN,KR,NL,PT,RU,zh-TW
LANGS = ["jp", "en", "fr", "de", "it", "es", "zh", "kr", "nl", "pt", "ru", "zhtw"]
def text_for(lang, table, fallback):
    return table.get(lang, fallback)

def utf16_field(s, nbytes):
    """UTF-16LE, null-terminated, padded to nbytes."""
    raw = s.encode("utf-16-le") + b"\x00\x00"
    return raw[:nbytes].ljust(nbytes, b"\x00")

# ---- RGB565 icon drawing --------------------------------------------------
def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3)

def new_canvas(n):
    return [[(0, 0, 0, 0) for _ in range(n)] for _ in range(n)]  # (r,g,b,a)

def rect(c, x0, y0, x1, y1, color):
    n = len(c)
    for y in range(max(0, y0), min(n, y1)):
        for x in range(max(0, x0), min(n, x1)):
            c[y][x] = color

def disc(c, cx, cy, r0, r1, color):
    """Annulus (ring) from radius r0..r1."""
    n = len(c)
    for y in range(n):
        for x in range(n):
            d2 = (x - cx) ** 2 + (y - cy) ** 2
            if r0 * r0 <= d2 <= r1 * r1:
                c[y][x] = color

def arc(c, cx, cy, r0, r1, a0, a1, color):
    """Annular sector, angles in radians [a0,a1] measured standard math (y up)."""
    import math
    n = len(c)
    for y in range(n):
        for x in range(n):
            d2 = (x - cx) ** 2 + (y - cy) ** 2
            if r0 * r0 <= d2 <= r1 * r1:
                ang = math.atan2(cy - y, x - cx)  # y-up
                if ang < 0: ang += 2 * math.pi
                if a0 <= ang <= a1:
                    c[y][x] = color

def draw_icon(n):
    """Draw a WiFi-signal + folder glyph, scaled to n x n."""
    c = new_canvas(n)
    # scale helpers from a 48-unit design space
    s = n / 48.0
    def S(v): return int(round(v * s))

    # rounded-ish background: solid rounded square (blue gradient by row)
    for y in range(n):
        t = y / max(1, n - 1)
        bg = rgb565(int(28 + 20 * t), int(96 + 40 * t), int(200 + 30 * t))
        for x in range(n):
            c[y][x] = (bg >> 11 & 0x1F, bg >> 5 & 0x3F, bg & 0x1F, 255)
    # knock off corners for a rounded look
    rad = S(8)
    for y in range(n):
        for x in range(n):
            cx = min(x, n - 1 - x); cy = min(y, n - 1 - y)
            if cx < rad and cy < rad and (rad - cx) ** 2 + (rad - cy) ** 2 > rad * rad:
                c[y][x] = (0, 0, 0, 0)

    white = (31, 63, 31, 255)          # bright white 565
    cyan = (10, 56, 31, 255)           # light cyan
    # ---- WiFi arcs at top (three stacked arcs + dot) ----
    wcx, wcy = n / 2.0, S(20)
    import math
    # outer, mid, inner arcs (opening upward), y-down canvas
    for (r0, r1) in [(S(17), S(20)), (S(11), S(14)), (S(5), S(8))]:
        for y in range(n):
            for x in range(n):
                dy = wcy - y  # y-up relative
                dx = x - wcx
                d2 = dx * dx + dy * dy
                if r0 * r0 <= d2 <= r1 * r1 and dy >= -S(1):
                    c[y][x] = white
    # WiFi dot (bottom of signal)
    dot_y, dot_x = S(24), n // 2
    dr = max(1, S(2))
    for y in range(n):
        for x in range(n):
            if (x - dot_x) ** 2 + (y - dot_y) ** 2 <= dr * dr:
                c[y][x] = white

    # ---- Folder at bottom ----
    fx0, fx1 = S(11), S(37)
    fy0, fy1 = S(30), S(43)
    # folder tab
    rect(c, fx0, fy0, fx0 + S(11), fy0 + S(3), cyan)
    # folder body
    rect(c, fx0, fy0 + S(3), fx1, fy1, white)
    # darker inner line for depth
    rect(c, fx0 + S(1), fy1 - S(2), fx1 - S(1), fy1 - S(1), cyan)
    return c

def morton_index(x, y):
    """Z-order index within an 8x8 tile for coordinates 0..7."""
    z = 0
    for b in range(3):
        z |= ((x >> b) & 1) << (2 * b)
        z |= ((y >> b) & 1) << (2 * b + 1)
    return z

def to_rgb565_raw(canvas):
    """Row-major RGB565 bytes. canvas pixels hold 5/6/5 channel values."""
    n = len(canvas)
    out = bytearray()
    for y in range(n):
        for x in range(n):
            r, g, b, a = canvas[y][x]
            # r/g/b are already 5/6/5 bit channel values - pack directly
            out += struct.pack("<H", ((r & 0x1F) << 11) | ((g & 0x3F) << 5) | (b & 0x1F))
    return bytes(out)

def swizzle_image(canvas):
    """Z-order swizzle: output[k] where k = tile_base + morton(local_x,local_y)."""
    n = len(canvas)
    raw = to_rgb565_raw(canvas)
    out = bytearray(len(raw))
    for ty in range(0, n, 8):
        for tx in range(0, n, 8):
            base = (ty // 8) * (n // 8) * 64 + (tx // 8) * 64
            for ly in range(8):
                for lx in range(8):
                    k = morton_index(lx, ly)
                    src_i = ((ty + ly) * n + (tx + lx)) * 2
                    dst_i = (base + k) * 2
                    out[dst_i:dst_i + 2] = raw[src_i:src_i + 2]
    return bytes(out)

# ---- SMDH assembly --------------------------------------------------------
def build():
    smdh = bytearray()
    smdh += b"SMDH"                                  # 0x00 magic
    smdh += struct.pack("<H", 2)                     # 0x04 version
    smdh += struct.pack("<H", 0)                     # 0x06 reserved

    # 0x08: 16 x 0x200 title structs (12 languages + 4 reserved slots)
    title_slots = list(LANGS) + ["", "", "", ""]  # pad to 16
    for lang in title_slots:
        st = text_for(lang, SHORT_TITLE, SHORT_TITLE["en"])
        lt = text_for(lang, LONG_TITLE, LONG_TITLE["en"])
        pb = text_for(lang, PUBLISHER, PUBLISHER["en"])
        smdh += utf16_field(st, 0x80)
        smdh += utf16_field(lt, 0x100)
        smdh += utf16_field(pb, 0x80)

    # 0x2008: application settings (0x30 bytes)
    ratings = bytearray(0x10)                        # all ratings inactive
    smdh += ratings
    smdh += struct.pack("<I", 0x7FFFFFFF)            # 0x2018 region lockout: region-free
    smdh += struct.pack("<I", 0)                     # 0x201C match maker id
    smdh += struct.pack("<Q", 0)                     # 0x2020 match maker bit id
    smdh += struct.pack("<I", 0x00000001)            # 0x2028 flags: visible on Home Menu
    smdh += struct.pack("<H", 0)                     # 0x202C EULA minor
    smdh += struct.pack("<H", 0)                     # 0x202E EULA major
    smdh += struct.pack("<I", 0)                     # 0x2030 optimal animation frame
    smdh += struct.pack("<I", 0)                     # 0x2034 CEC/StreetPass id

    # 0x2038: reserved 8 bytes
    smdh += b"\x00" * 8

    # 0x2040: icon graphics
    #  small: 24x24 RGB565 = 0x480 bytes
    small = draw_icon(24)
    smdh += swizzle_image(small)
    smdh += b"\x00" * (0x480 - 24 * 24 * 2)
    #  large: 48x48 RGB565 = 0x1200 bytes
    large = draw_icon(48)
    smdh += swizzle_image(large)
    smdh += b"\x00" * (0x1200 - 48 * 48 * 2)

    assert len(smdh) == 0x36C0, "bad SMDH size: 0x%X" % len(smdh)
    with open(OUT, "wb") as f:
        f.write(smdh)
    print("wrote %s (%d bytes = 0x%X)" % (OUT, len(smdh), len(smdh)))

if __name__ == "__main__":
    build()
