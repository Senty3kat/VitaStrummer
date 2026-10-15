# Накладывает подписи (записанные заглушкой pgf) на кадры и сохраняет PNG.
# Шрифт приблизительный: на Vita будет системный PGF.
import sys, glob, os
from PIL import Image, ImageDraw, ImageFont

FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"

for ppm in sorted(glob.glob("out/*.ppm")):
    base = ppm[:-4]
    img = Image.open(ppm).convert("RGB")
    d = ImageDraw.Draw(img)
    if os.path.exists(base + ".txt"):
        for line in open(base + ".txt", encoding="utf-8"):
            x, y, col, sc, text = line.rstrip("\n").split("\t", 4)
            col = int(col)
            rgb = (col & 255, (col >> 8) & 255, (col >> 16) & 255)
            f = ImageFont.truetype(FONT, max(6, int(round(15 * float(sc)))))
            d.text((int(x), int(y)), text, font=f, fill=rgb, anchor="ls")
    img.save(base + ".png")
    print("wrote", base + ".png")
