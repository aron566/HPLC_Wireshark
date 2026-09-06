import sys
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
from PIL import Image, ImageOps
from rapidocr_onnxruntime import RapidOCR

img = Image.open(r"C:\Users\work\AppData\Roaming\Hermes\composer-images\image_8408b5.png")
ocr = RapidOCR()
segments = [("A", 490, 700), ("B", 700, 900), ("C", 900, 1160)]
for name, x1, x2 in segments:
    crop = img.crop((x1, 834, x2, 884))
    crop = crop.resize(((x2-x1)*8, 50*8), Image.LANCZOS)
    crop = ImageOps.autocontrast(crop)
    fn = f"seg{name}.png"
    crop.save(fn)
    res, _ = ocr(fn)
    texts = [f"{t}({s:.2f})" for b, t, s in (res or [])]
    print(f"seg {name} x{x1}-{x2}: {' | '.join(texts) if texts else '(空)'}")
