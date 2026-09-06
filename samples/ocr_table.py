"""OCR 表格图片(中文),输出按行排序的识别文本。"""
import sys
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
from rapidocr_onnxruntime import RapidOCR

img = r"C:\Users\work\AppData\Roaming\Hermes\composer-images\image_8408b5.png"
ocr = RapidOCR()
res, _ = ocr(img)
if not res:
    print("NO RESULT")
    sys.exit(0)
# res: [box, text, score] 按 y 中位(行)排序
rows = []
for box, text, score in res:
    ys = [p[1] for p in box]
    xs = [p[0] for p in box]
    rows.append((min(ys), min(xs), text, score))
rows.sort(key=lambda r: (round(r[0] / 18), r[1]))
for y, x, t, s in rows:
    print(f"[y={int(y):4d} x={int(x):4d} s={s:.2f}] {t}")
