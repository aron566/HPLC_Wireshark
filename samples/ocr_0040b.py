import sys
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
import numpy as np
from PIL import Image
from rapidocr_onnxruntime import RapidOCR

img = Image.open(r"C:\Users\work\AppData\Roaming\Hermes\composer-images\image_8408b5.png")
g = img.convert("L")
arr = np.array(g)
# 该行文字区(0x0040 含义):列 500..1000,行 838..878
sub = arr[838:878, 500:1000]
# 自适应:文字暗于背景;放大6x最近邻保持块状再平滑
from PIL import Image as I
s = I.fromarray(sub).resize((sub.shape[1]*8, sub.shape[0]*8), I.LANCZOS)
sarr = np.array(s)
th = (sarr.min()+sarr.max())//2 + 20
bw = np.where(sarr < th, 0, 255).astype("uint8")
I.fromarray(bw).save("c4_bw.png")
ocr = RapidOCR()
for f in ("c4_bw.png",):
    res, _ = ocr(f)
    print("=== 0x0040 (8x 灰度阈值) ===")
    for b, t, sc in (res or []):
        print(f"  [{sc:.2f}] {t}")
