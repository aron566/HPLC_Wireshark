import sys
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
from PIL import Image
from rapidocr_onnxruntime import RapidOCR

img = Image.open(r"C:\Users\work\AppData\Roaming\Hermes\composer-images\image_ba94e0.png")
print("Size:", img.size, img.mode)
ocr = RapidOCR()
res, _ = ocr(img)
rows = []
for box, text, score in res:
    ys = [p[1] for p in box]; xs = [p[0] for p in box]
    rows.append((min(ys), min(xs), text, score))
rows.sort(key=lambda r: (r[0] // 20, r[1]))
for y, x, t, s in rows:
    print(f"[y={int(y):4d} x={int(x):4d} s={s:.2f}] {t}")
