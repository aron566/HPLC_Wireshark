import sys
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
from PIL import Image, ImageOps
from rapidocr_onnxruntime import RapidOCR

img = Image.open(r"C:\Users\work\AppData\Roaming\Hermes\composer-images\image_8408b5.png")
ocr = RapidOCR()
# 0x0040 行文字:x 520..900,y 838..878(行高40px)
box = (470, 836, 1160, 882)
crop = img.crop(box)
crop = crop.resize((crop.width*6, crop.height*6), Image.LANCZOS)
crop = ImageOps.autocontrast(crop)
crop.save("c3_0040.png")
res, _ = ocr("c3_0040.png")
print("=== 0x0040 含义(6x) ===")
for b, t, s in (res or []):
    print(f"  [{s:.2f}] {t}")
# 若空,尝试二值化
import numpy as np
g = crop.convert("L")
arr = np.array(g)
arr = np.where(arr > 140, 255, 0).astype("uint8")
Image.fromarray(arr).save("c3_0040bw.png")
res2, _ = ocr("c3_0040bw.png")
print("=== 0x0040 含义(6x 二值) ===")
for b, t, s in (res2 or []):
    print(f"  [{s:.2f}] {t}")
