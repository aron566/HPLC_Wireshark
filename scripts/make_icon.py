# 生成应用图标 icons/app.ico(16..256)与 icons/app.png(窗口图标用)
# 主题:深蓝底 + 青色信号阶梯波形(BPLC 监控)
import os
from PIL import Image, ImageDraw

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "icons")
os.makedirs(OUT, exist_ok=True)
S = 256

def draw(size: int) -> Image.Image:
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    r = max(6, size // 14)                     # 圆角半径
    # 深蓝渐变底(纵向 4 段叠色近似)
    grad = [(16, 44, 76), (11, 33, 60), (8, 25, 48), (6, 19, 38)]
    for i, c in enumerate(grad):
        y0 = i * size // 4
        y1 = (i + 1) * size // 4 + 1
        d.rounded_rectangle([0, 0, size - 1, size - 1], r, fill=c)
        # 以同色覆盖下半段会产生平边,故改为逐段裁剪:直接全圆角后再画段较难,
        # 简化:整体填最亮色再叠加暗色下半段(每段用圆角矩形同半径覆盖)
    # 上面循环里每段都带圆角会互相侵蚀;重画:底=最亮,再由下向上叠暗段
    img2 = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d2 = ImageDraw.Draw(img2)
    d2.rounded_rectangle([0, 0, size - 1, size - 1], r, fill=grad[0])
    for i, c in enumerate(grad[1:], start=1):
        seg = Image.new("RGBA", (size, size), (0, 0, 0, 0))
        ds = ImageDraw.Draw(seg)
        y0 = i * size // 4
        ds.rectangle([0, y0, size, size], fill=c)
        seg = seg.crop((0, 0, size, size))
        img2.alpha_composite(seg)
    img = img2
    d = ImageDraw.Draw(img)

    # 波形:信号采样点(按比例)
    pts = [(0.07, 0.73), (0.23, 0.58), (0.36, 0.66), (0.51, 0.38),
           (0.66, 0.26), (0.80, 0.18), (0.94, 0.10)]
    P = [(int(x * size), int(y * size)) for x, y in pts]
    w = max(3, size // 19)
    # 外层暗青描边 → 内层亮青,显得有立体感
    d.line(P, fill=(13, 95, 140), width=w + max(2, size // 48), joint="curve")
    d.line(P, fill=(80, 220, 255), width=w, joint="curve")
    # 末点亮点
    rad = max(4, size // 22)
    x, y = P[-1]
    d.ellipse([x - rad, y - rad, x + rad, y + rad], fill=(160, 240, 255))
    d.ellipse([x - rad // 2, y - rad // 2, x + rad // 2, y + rad // 2],
              fill=(255, 255, 255))
    return img

base = draw(S)
sizes = [16, 24, 32, 48, 64, 128, 256]
base.save(os.path.join(OUT, "app.png"))
base.save(os.path.join(OUT, "app.ico"),
          sizes=[(s, s) for s in sizes], append_images=[])
print("icons written:", os.listdir(OUT))
