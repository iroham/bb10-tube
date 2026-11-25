#!/usr/bin/env python3
"""Draw the app's action/tab icons (white on transparent, 81x81) into assets/icons/. Own simple shapes, no
third-party artwork.  python3 tools/icons.py"""
import math, os, struct, zlib
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "assets", "icons")
N, SS = 96, 4

def poly(pts):
    def f(x, y):
        inside = False
        j = len(pts) - 1
        for i in range(len(pts)):
            xi, yi = pts[i]; xj, yj = pts[j]
            if (yi > y) != (yj > y) and x < (xj - xi) * (y - yi) / (yj - yi) + xi: inside = not inside
            j = i
        return inside
    return f
def circle(cx, cy, r): return lambda x, y: (x - cx) ** 2 + (y - cy) ** 2 <= r * r
def ring(cx, cy, r, w): return lambda x, y: (r - w) ** 2 <= (x - cx) ** 2 + (y - cy) ** 2 <= r * r
def rect(x0, y0, x1, y1): return lambda x, y: x0 <= x <= x1 and y0 <= y <= y1
def rrect(x0, y0, x1, y1, r):
    def f(x, y):
        if not (x0 <= x <= x1 and y0 <= y <= y1): return False
        cx = min(max(x, x0 + r), x1 - r); cy = min(max(y, y0 + r), y1 - r)
        return (x - cx) ** 2 + (y - cy) ** 2 <= r * r
    return f
def line(x0, y0, x1, y1, w):
    def f(x, y):
        dx, dy = x1 - x0, y1 - y0
        t = max(0, min(1, ((x - x0) * dx + (y - y0) * dy) / (dx * dx + dy * dy)))
        return (x - x0 - t * dx) ** 2 + (y - y0 - t * dy) ** 2 <= (w / 2) ** 2
    return f
def any_(*fs): return lambda x, y: any(f(x, y) for f in fs)
def sub(a, b): return lambda x, y: a(x, y) and not b(x, y)
def flip_v(f): return lambda x, y: f(x, 1 - y)
def flip_h(f): return lambda x, y: f(1 - x, y)
def arc(cx, cy, r, w, a0, a1):
    rg = ring(cx, cy, r, w)
    def f(x, y):
        if not rg(x, y): return False
        a = math.degrees(math.atan2(y - cy, x - cx)) % 360
        return a0 <= a <= a1 if a0 <= a1 else (a >= a0 or a <= a1)
    return f

play = poly([(0.30, 0.20), (0.30, 0.80), (0.80, 0.50)])
thumb = any_(rrect(0.16, 0.44, 0.30, 0.82, 0.03),
             poly([(0.35, 0.82), (0.35, 0.46), (0.50, 0.16), (0.58, 0.16), (0.62, 0.22), (0.58, 0.42), (0.82, 0.42), (0.86, 0.50), (0.78, 0.80), (0.72, 0.82)]))
gear_teeth = any_(*[line(0.5, 0.5, 0.5 + 0.36 * math.cos(math.radians(a)), 0.5 + 0.36 * math.sin(math.radians(a)), 0.13) for a in range(0, 360, 45)])
ICONS = {
    "home": any_(poly([(0.50, 0.14), (0.10, 0.50), (0.22, 0.50), (0.22, 0.84), (0.42, 0.84), (0.42, 0.60), (0.58, 0.60), (0.58, 0.84), (0.78, 0.84), (0.78, 0.50), (0.90, 0.50)])),
    "subs": any_(sub(rrect(0.14, 0.38, 0.86, 0.84, 0.05), poly([(0.42, 0.50), (0.42, 0.74), (0.62, 0.62)])), rrect(0.22, 0.26, 0.78, 0.32, 0.02), rrect(0.30, 0.15, 0.70, 0.21, 0.02)),
    "library": any_(sub(rrect(0.26, 0.14, 0.88, 0.70, 0.05), poly([(0.49, 0.30), (0.49, 0.54), (0.69, 0.42)])),
                    sub(rrect(0.12, 0.30, 0.74, 0.86, 0.05), rrect(0.20, 0.08, 0.92, 0.78, 0.05))),
    "search": any_(ring(0.42, 0.42, 0.27, 0.075), line(0.61, 0.61, 0.84, 0.84, 0.10)),
    "play": play,
    "pause": any_(rrect(0.26, 0.20, 0.42, 0.80, 0.02), rrect(0.58, 0.20, 0.74, 0.80, 0.02)),
    "like": thumb,
    "dislike": flip_v(thumb),
    "refresh": any_(arc(0.5, 0.5, 0.33, 0.085, 40, 330), poly([(0.70, 0.18), (0.92, 0.36), (0.66, 0.44)])),
    "quality": sub(any_(circle(0.5, 0.5, 0.27), gear_teeth), circle(0.5, 0.5, 0.11)),
    "captions": sub(rrect(0.10, 0.22, 0.90, 0.78, 0.08),
                    any_(sub(arc(0.34, 0.5, 0.15, 0.06, 45, 315), rect(0.9, 0.9, 1, 1)), arc(0.66, 0.5, 0.15, 0.06, 45, 315))),
    "speed": any_(poly([(0.14, 0.24), (0.14, 0.76), (0.48, 0.50)]), poly([(0.52, 0.24), (0.52, 0.76), (0.86, 0.50)])),
    "next": any_(poly([(0.20, 0.22), (0.20, 0.78), (0.64, 0.50)]), rrect(0.68, 0.22, 0.80, 0.78, 0.02)),
    "previous": flip_h(any_(poly([(0.20, 0.22), (0.20, 0.78), (0.64, 0.50)]), rrect(0.68, 0.22, 0.80, 0.78, 0.02))),
    "fullscreen": any_(*[poly(p) for p in (
        [(0.14, 0.14), (0.40, 0.14), (0.40, 0.22), (0.22, 0.22), (0.22, 0.40), (0.14, 0.40)],
        [(0.86, 0.14), (0.60, 0.14), (0.60, 0.22), (0.78, 0.22), (0.78, 0.40), (0.86, 0.40)],
        [(0.14, 0.86), (0.40, 0.86), (0.40, 0.78), (0.22, 0.78), (0.22, 0.60), (0.14, 0.60)],
        [(0.86, 0.86), (0.60, 0.86), (0.60, 0.78), (0.78, 0.78), (0.78, 0.60), (0.86, 0.60)])]),
    "later": any_(ring(0.5, 0.5, 0.36, 0.075), line(0.5, 0.5, 0.5, 0.27, 0.075), line(0.5, 0.5, 0.66, 0.60, 0.075)),
    "channel": any_(circle(0.5, 0.34, 0.17), sub(circle(0.5, 0.92, 0.36), rect(0, 0.86, 1, 1))),
    "subscribe": any_(circle(0.40, 0.34, 0.16), sub(circle(0.40, 0.90, 0.33), rect(0, 0.84, 1, 1)), rrect(0.70, 0.22, 0.78, 0.54, 0.02), rrect(0.58, 0.34, 0.90, 0.42, 0.02)),
    "back10": any_(arc(0.5, 0.52, 0.32, 0.08, 300, 220), poly([(0.36, 0.06), (0.36, 0.38), (0.14, 0.22)])),
    "fwd10": flip_h(any_(arc(0.5, 0.52, 0.32, 0.08, 300, 220), poly([(0.36, 0.06), (0.36, 0.38), (0.14, 0.22)]))),
    "account": any_(ring(0.5, 0.5, 0.40, 0.06), circle(0.5, 0.40, 0.13), sub(circle(0.5, 0.86, 0.26), any_(rect(0, 0.80, 1, 1), sub(rect(0, 0, 1, 1), circle(0.5, 0.5, 0.34))))),
    "download": any_(rrect(0.455, 0.12, 0.545, 0.56, 0.02), poly([(0.26, 0.42), (0.74, 0.42), (0.50, 0.68)]), rrect(0.18, 0.76, 0.82, 0.85, 0.03)),
    "done": any_(ring(0.5, 0.5, 0.40, 0.07), line(0.31, 0.52, 0.45, 0.65, 0.085), line(0.45, 0.65, 0.70, 0.37, 0.085)),
    "down": any_(line(0.22, 0.36, 0.50, 0.64, 0.10), line(0.50, 0.64, 0.78, 0.36, 0.10)),
    "back": any_(line(0.60, 0.20, 0.32, 0.50, 0.10), line(0.32, 0.50, 0.60, 0.80, 0.10)),
    "close": any_(line(0.24, 0.24, 0.76, 0.76, 0.10), line(0.76, 0.24, 0.24, 0.76, 0.10)),
    "share": any_(circle(0.72, 0.22, 0.12), circle(0.26, 0.50, 0.12), circle(0.72, 0.78, 0.12), line(0.26, 0.50, 0.72, 0.22, 0.06), line(0.26, 0.50, 0.72, 0.78, 0.06)),
    "delete": any_(rrect(0.20, 0.22, 0.80, 0.30, 0.02), rrect(0.40, 0.12, 0.60, 0.22, 0.02),
                   sub(poly([(0.26, 0.34), (0.74, 0.34), (0.70, 0.88), (0.30, 0.88)]), any_(rect(0.40, 0.44, 0.45, 0.78), rect(0.55, 0.44, 0.60, 0.78)))),
    "history": any_(arc(0.52, 0.5, 0.36, 0.075, 200, 160), line(0.52, 0.5, 0.52, 0.30, 0.07), line(0.52, 0.5, 0.66, 0.58, 0.07), poly([(0.04, 0.40), (0.30, 0.40), (0.17, 0.60)])),
    "playlist": any_(rrect(0.12, 0.22, 0.66, 0.30, 0.02), rrect(0.12, 0.42, 0.66, 0.50, 0.02), rrect(0.12, 0.62, 0.46, 0.70, 0.02), poly([(0.58, 0.58), (0.58, 0.90), (0.88, 0.74)])),
    "videos": sub(rrect(0.10, 0.22, 0.90, 0.78, 0.07), poly([(0.40, 0.36), (0.40, 0.64), (0.64, 0.50)])),
    "exitfull": any_(*[poly(p) for p in (
        [(0.40, 0.14), (0.40, 0.40), (0.14, 0.40), (0.14, 0.32), (0.32, 0.32), (0.32, 0.14)],
        [(0.60, 0.14), (0.60, 0.40), (0.86, 0.40), (0.86, 0.32), (0.68, 0.32), (0.68, 0.14)],
        [(0.40, 0.86), (0.40, 0.60), (0.14, 0.60), (0.14, 0.68), (0.32, 0.68), (0.32, 0.86)],
        [(0.60, 0.86), (0.60, 0.60), (0.86, 0.60), (0.86, 0.68), (0.68, 0.68), (0.68, 0.86)])]),
    "info": any_(ring(0.5, 0.5, 0.38, 0.07), circle(0.5, 0.31, 0.055), rrect(0.455, 0.42, 0.545, 0.72, 0.02)),
}

def png(f):
    rows = []
    for y in range(N):
        row = bytearray([0])
        for x in range(N):
            c = sum(1 for sy in range(SS) for sx in range(SS) if f((x + (sx + 0.5) / SS) / N, (y + (sy + 0.5) / SS) / N))
            row += bytes((255, 255, 255, c * 255 // (SS * SS)))
        rows.append(bytes(row))
    def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", N, N, 8, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b"")

os.makedirs(OUT, exist_ok=True)
for name, f in ICONS.items():
    open(os.path.join(OUT, name + ".png"), "wb").write(png(f))
print(len(ICONS), "icons ->", os.path.normpath(OUT))
