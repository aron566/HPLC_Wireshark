"""裁图局部放大再 OCR,补漏/复核:0x0040 行含义、0x00A0 端口。"""
import sys
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
from PIL import Image
from rapidocr_onnxruntime import RapidOCR

img = Image.open(r"C:\Users\work\AppData\Roaming\Hermes\composer-images\image_8408b5.png")
ocr = RapidOCR()
regions = {
    "0x0040行": (100, 830, 1190, 875),     # 含 0x0040 与含义
    "0x00A0端口": (600, 910, 1190, 955),   # 0x00A0 行整行(端口列)
    "0x00A1行": (100, 960, 1190, 1000),
}
for name, (x1, y1, x2, y2) in regions.items():
    crop = img.crop((x1, y1, x2, y2))
    crop = crop.resize((crop.width * 3, crop.height * 3), Image.LANCZOS)
    crop.save(f"crop_{name}.png")
    res, _ = ocr(crop)
    print(f"=== {name} ===")
    if not res:
        print("  (no text)")
    for box, text, score in res:
        print(f"  [{score:.2f}] {text}")
