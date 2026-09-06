import sys
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
from PIL import Image
from rapidocr_onnxruntime import RapidOCR

img = Image.open(r"C:\Users\work\AppData\Roaming\Hermes\composer-images\image_8408b5.png")
ocr = RapidOCR()
# 0x0040 行含义文字区域(参考 0x0041 含义 x~495,y~893;0x0040 在 y~850)
for name, box in {
    "0x0040含义": (300, 836, 1000, 880),
    "0x0041含义全": (200, 880, 1000, 920),
}.items():
    crop = img.crop(box).resize(((box[2]-box[0])*4, (box[3]-box[1])*4), Image.LANCZOS)
    crop.save(f"c2_{name}.png")
    res, _ = ocr(crop)
    print(f"=== {name} ===")
    for b, t, s in (res or []):
        print(f"  [{s:.2f}] {t}")
