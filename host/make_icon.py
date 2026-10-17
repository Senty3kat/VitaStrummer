#!/usr/bin/env python3
"""Иконка приложения (пузырь на главном экране Vita): sce_sys/icon0.png, 128x128, 8-бит палитра.

Тот же стиль, что у интерфейса («Sonara»): кремовый корпус, утопленная синяя панель,
клавиши, которые сужаются в нити и расходятся в полосы струн, «жидкость» цветов Retoka
и оранжевая звезда-логотип. Рисуется с запасом (x4) и уменьшается - края гладкие.

    python3 host/make_icon.py        # пишет sce_sys/icon0.png и host/out/icon_preview.png
"""
import os
import numpy as np
from PIL import Image

S = 4                      # суперсэмплинг
N = 128 * S
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")

yy, xx = np.mgrid[0:N, 0:N].astype(np.float32) / S + 0.5 / S   # координаты в пикселях иконки
img = np.zeros((N, N, 3), np.float32)


def col(r, g, b):
    return np.array([r, g, b], np.float32)


def paint(mask, c):
    """mask 0..1 (покрытие), c - цвет или массив цветов"""
    m = np.clip(mask, 0, 1)[..., None]
    img[:] = img * (1 - m) + c * m


def cov(d):
    """покрытие по расстоянию со знаком (в пикселях иконки): гладкий край ~1 px"""
    return np.clip(0.5 - d * 1.0, 0, 1)


def rrect(x0, y0, x1, y1, r):
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    hx, hy = (x1 - x0) / 2 - r, (y1 - y0) / 2 - r
    qx, qy = np.abs(xx - cx) - hx, np.abs(yy - cy) - hy
    return np.hypot(np.maximum(qx, 0), np.maximum(qy, 0)) + np.minimum(np.maximum(qx, qy), 0) - r


def vgrad(c0, c1, y0, y1):
    t = np.clip((yy - y0) / (y1 - y0), 0, 1)[..., None]
    return c0 * (1 - t) + c1 * t


# ---- корпус: кремовый, сверху светлее ----
img[:] = vgrad(col(240, 230, 210), col(220, 205, 180), 0, 128)

# ---- утопленная синяя панель (круглая - под пузырь) ----
CX, CY, R = 64.0, 64.0, 53.0
d_panel = np.hypot(xx - CX, yy - CY) - R
paint(cov(d_panel - 2.5) * 0.35, col(150, 132, 108))              # тень вокруг выемки
paint(cov(d_panel + 0.0), vgrad(col(58, 84, 106), col(74, 102, 126), CY - R, CY + R))
# внутренняя тень сверху: панель утоплена
sh = np.clip(1 - (yy - (CY - R)) / 16, 0, 1) * cov(d_panel)
paint(sh * 0.45, col(28, 44, 60))
# светлая кромка снизу
rim = cov(np.abs(d_panel + 0.8) - 0.8) * np.clip((yy - CY) / R, 0, 1)
paint(rim * 0.55, col(236, 226, 206))

# ---- клавиши и струны ----
CREAM = col(234, 222, 200)
EDGE = col(160, 142, 118)
keys = ["W", "B", "W", "B", "W", "W", "B", "W", "B", "W"]       # снизу вверх: C C# D D# E F F# G G# A
NK = len(keys)
KB_TOP, KB_BOT = 30.0, 98.0                     # клавиатура слева (по высоте)
ST_TOP, ST_BOT = 26.0, 102.0                    # полосы струн справа
nw = keys.count("W")
wh = (KB_BOT - KB_TOP) / nw
centers = []
w = 0
for k in keys:
    if k == "W":
        centers.append(KB_BOT - (w + 0.5) * wh)
        w += 1
    else:
        centers.append(KB_BOT - w * wh)
pitch = (ST_BOT - ST_TOP) / NK
str_y = [ST_BOT - (i + 0.5) * pitch for i in range(NK)]

X_BK0, X_BK1 = 20.0, 37.0        # чёрные (выступают влево)
X_WK0, X_WK1 = 26.0, 45.0        # белые
X_NECK, X_BAR0, X_BAR1 = 52.0, 78.0, 118.0

def smooth(t):
    t = np.clip(t, 0, 1)
    return t * t * (3 - 2 * t)

# у каждой струны - своя форма: клавиша -> нить (S-кривая) -> полоса
shapes = []
for i, k in enumerate(keys):
    c, sy = centers[i], str_y[i]
    kh = (wh - 1.6) / 2 if k == "W" else 2.2       # полувысота клавиши
    x0 = X_BK0 if k == "B" else X_WK0
    x1 = X_BK1 if k == "B" else X_WK1
    t = smooth((xx - x1) / (X_BAR0 - x1))           # 0 у клавиши, 1 у полосы
    ycen = c + (sy - c) * t
    half = np.where(xx < x1, kh,
                    np.where(xx < X_NECK, kh + (0.55 - kh) * smooth((xx - x1) / (X_NECK - x1)),
                             0.55 + (pitch / 2 - 0.7 - 0.55) * smooth((xx - X_NECK) / (X_BAR0 + 6 - X_NECK))))
    d = np.maximum(np.abs(yy - ycen) - half, np.maximum(x0 - xx, xx - X_BAR1))
    # скругления торцов
    d = np.maximum(d, d_panel + 4.0)                # всё внутри круглой панели, с полем
    shapes.append(d)

for i, d in enumerate(shapes):                    # сначала белые, чёрные - поверх (тень)
    if keys[i] == "B":
        continue
    paint(cov(d - 0.6) * 0.5, EDGE)
    paint(cov(d), CREAM)
for i, d in enumerate(shapes):
    if keys[i] != "B":
        continue
    paint(cov(d - 0.9) * 0.7, col(60, 70, 84))
    paint(cov(d), CREAM)

# ---- жидкость: несколько струн горят, цвет растекается к клавишам ----
PAL = [(0.00, (96, 182, 220)), (0.16, (196, 222, 206)), (0.30, (250, 226, 160)), (0.44, (250, 176, 70)),
       (0.57, (243, 112, 34)), (0.70, (214, 54, 40)), (0.82, (150, 34, 62)), (0.92, (60, 48, 112)),
       (1.00, (26, 62, 128))]
def palette(v):
    out = np.zeros(v.shape + (3,), np.float32)
    for (t0, c0), (t1, c1) in zip(PAL[:-1], PAL[1:]):
        m = (v >= t0) & (v <= t1)
        tt = ((v - t0) / (t1 - t0))[..., None]
        out[m] = (np.array(c0) * (1 - tt) + np.array(c1) * tt)[m]
    return out

lit = {2: 0.78, 4: 0.92, 5: 0.70, 7: 0.84, 9: 0.6}       # струна -> сила (аккорд: C E F G A... условно)
for i, amt in lit.items():
    d = shapes[i]
    inside = cov(d + 0.4)
    # капля: ядро на струне справа, хвост к клавише
    core_x = 92.0 - 6.0 * (i % 3)
    g = np.exp(-((xx - core_x) / 15.0) ** 2) + 0.55 * np.exp(-((xx - (core_x - 34)) / 20.0) ** 2)
    v = np.clip(g * amt, 0, 1) * 0.86
    a = smooth((v - 0.05) / 0.3)
    paint(inside * a, palette(v))

# ---- звезда-логотип (оранжевая) сверху ----
def star(cx, cy, r_out, r_in, n=4):
    ang = np.arctan2(yy - cy, xx - cx)
    rr = np.hypot(xx - cx, yy - cy)
    k = (np.cos(n * ang) + 1) / 2                      # 1 на лучах
    rad = r_in + (r_out - r_in) * k ** 3
    return rr - rad
ds = star(64.0, 16.5, 8.0, 3.4)
paint(cov(ds - 1.2) * 0.35, col(120, 80, 40))
paint(cov(ds), col(226, 124, 52))
paint(cov(star(63.2, 15.7, 3.4, 1.4)) * 0.55, col(250, 196, 140))

# ---- уменьшение и запись ----
im = Image.fromarray(np.clip(img, 0, 255).astype(np.uint8), "RGB").resize((128, 128), Image.LANCZOS)
os.makedirs(os.path.join(ROOT, "sce_sys"), exist_ok=True)
os.makedirs(os.path.join(ROOT, "host", "out"), exist_ok=True)
im.resize((384, 384), Image.NEAREST).save(os.path.join(ROOT, "host", "out", "icon_preview.png"))
# Vita требует icon0.png 128x128 в 8-битной палитре
im.quantize(colors=256, method=Image.MEDIANCUT, dither=Image.NONE).save(os.path.join(ROOT, "sce_sys", "icon0.png"), optimize=True)
print("sce_sys/icon0.png")
