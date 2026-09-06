# 生成应用图标 icons/app.ico(多尺寸)与 icons/app.png(窗口图标用)
# 主题:深蓝底 + 青色信号阶梯波形(BPLC 监控)
# ICO 手写组装:16..128 用 32bpp BMP 条目(Explorer/NSIS/资源管理器兼容),
# 256 用 PNG 压缩条目(大图标视图);避免 PIL ICO 多尺寸插件缺陷。
import os
import struct
from PIL import Image, ImageDraw

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "icons")
os.makedirs(OUT, exist_ok=True)
S = 256

def draw(size: int) -> Image.Image:
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    r = max(6, size // 14)
    grad = [(16, 44, 76), (11, 33, 60), (8, 25, 48), (6, 19, 38)]
    img2 = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d2 = ImageDraw.Draw(img2)
    d2.rounded_rectangle([0, 0, size - 1, size - 1], r, fill=grad[0])
    for i, c in enumerate(grad[1:], start=1):
        seg = Image.new("RGBA", (size, size), (0, 0, 0, 0))
        ds = ImageDraw.Draw(seg)
        y0 = i * size // 4
        ds.rectangle([0, y0, size, size], fill=c)
        img2.alpha_composite(seg)
    img = img2
    d = ImageDraw.Draw(img)

    pts = [(0.07, 0.73), (0.23, 0.58), (0.36, 0.66), (0.51, 0.38),
           (0.66, 0.26), (0.80, 0.18), (0.94, 0.10)]
    P = [(int(x * size), int(y * size)) for x, y in pts]
    w = max(3, size // 19)
    d.line(P, fill=(13, 95, 140), width=w + max(2, size // 48), joint="curve")
    d.line(P, fill=(80, 220, 255), width=w, joint="curve")
    rad = max(4, size // 22)
    x, y = P[-1]
    d.ellipse([x - rad, y - rad, x + rad, y + rad], fill=(160, 240, 255))
    d.ellipse([x - rad // 2, y - rad // 2, x + rad // 2, y + rad // 2],
              fill=(255, 255, 255))
    return img

def bmp_entry(im: Image.Image) -> bytes:
    """32bpp BGRA DIB(BITMAPINFOHEADER + XOR + AND),height=2*h,底行在前。"""
    w, h = im.size
    rgba = im.convert("RGBA")
    xor = bytearray()
    for y in range(h - 1, -1, -1):          # 自底向上
        for x in range(w):
            px = rgba.getpixel((x, y))      # 逐像素慢但仅图标,可接受
            xor += bytes((px[2], px[1], px[0], px[3]))
    and_row = (w + 31) // 32 * 4            # AND 掩码行(全 0,32bpp 走 alpha)
    and_mask = b"\x00" * (and_row * h)
    hdr = struct.pack("<IiiHHIIiiII", 40, w, h * 2, 1, 32, 0,
                      len(xor) + len(and_mask), 0, 0, 0, 0)
    return hdr + bytes(xor) + and_mask

def png_entry(im: Image.Image) -> bytes:
    from io import BytesIO
    buf = BytesIO()
    im.save(buf, format="PNG")
    return buf.getvalue()

def write_ico(path: str, images: dict, png_size: int = 256) -> None:
    """images: {size: PIL RGBA};小尺寸 BMP,指定 png_size 用 PNG 条目。"""
    sizes = sorted(images)
    datas, entries, offset = [], [], 6 + 16 * len(sizes)
    for s in sizes:
        if s == png_size:
            data, enc = png_entry(images[s]), 0
        else:
            data, enc = bmp_entry(images[s]), 1
        w = h = 0 if s == 256 else s
        entries.append(struct.pack("<BBBBHHII", w & 0xFF, h & 0xFF, 0, 0,
                                   1, 32, len(data), offset))
        datas.append(data)
        offset += len(data)
    with open(path, "wb") as f:
        f.write(struct.pack("<HHH", 0, 1, len(sizes)))
        for e in entries:
            f.write(e)
        for d in datas:
            f.write(d)

sizes = [16, 24, 32, 48, 64, 128, 256]
imgs = {s: draw(s) for s in sizes}
imgs[256].save(os.path.join(OUT, "app.png"))
write_ico(os.path.join(OUT, "app.ico"), imgs, png_size=256)

raw = open(os.path.join(OUT, "app.ico"), "rb").read()
n = struct.unpack("<H", raw[4:6])[0]
print(f"icons written, ico entries: {n} ->", sorted(os.listdir(OUT)))
for i in range(n):
    off = 6 + 16 * i
    w, h = raw[off], raw[off + 1]
    size = struct.unpack("<I", raw[off + 8:off + 12])[0]
    print(f"  {w or 256}x{h or 256} data={size}B")
