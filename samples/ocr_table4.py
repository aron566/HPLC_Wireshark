import sys
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
from PIL import Image
from rapidocr_onnxruntime import RapidOCR

src = Image.open(r"C:\Users\work\AppData\Roaming\Hermes\composer-images\image_8408b5.png")
# 3x 放大(保持清晰度)提升小字识别
img = src.resize((src.width*3, src.height*3), Image.LANCZOS)
img.save("table3x.png")
ocr = RapidOCR()
res, _ = ocr("table3x.png")
out = []
for box, text, score in res:
    ys = [p[1] for p in box]; xs = [p[0] for p in box]
    out.append((min(ys)//3, min(xs)//3, text, score))   # 归一回原坐标
out.sort(key=lambda r: (r[0]//14, r[1]))
for y, x, t, s in out:
    print(f"[y={int(y):4d} x={int(x):4d} s={s:.2f}] {t}")
