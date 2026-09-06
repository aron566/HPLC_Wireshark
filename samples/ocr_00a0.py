import sys
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
from PIL import Image
from rapidocr_onnxruntime import RapidOCR

img = Image.open(r"C:\Users\work\AppData\Roaming\Hermes\composer-images\image_8408b5.png")
ocr = RapidOCR()
# 0x00A0 整行(端口列 x930..1100,行 y915..955)
for name, box in {"0x00A0行": (900, 915, 1150, 960)}.items():
    crop = img.crop(box).resize(((box[2]-box[0])*6, (box[3]-box[1])*6), Image.LANCZOS)
    crop.save("c5.png")
    res, _ = ocr("c5.png")
    print(f"=== {name} ===")
    for b, t, s in (res or []):
        print(f"  [{s:.2f}] {t}")
