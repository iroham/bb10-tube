#!/usr/bin/env python3
"""Pack the native app into an unsigned BB10 archive (.bar) for the root installer.
Usage (from bb10-tools/): python3 native-tube/package.py  -> native-tube/build/<PKG>.bar
Layout: META-INF/MANIFEST.MF, native/tube (launcher + core), native/js/** (solver), native/assets/** (QML, icons),
native/icon.png."""
import base64, hashlib, os, struct, sys, zipfile, zlib
HERE = os.path.dirname(os.path.abspath(__file__))
PKG, NAME, VER = "org.bq.tube", "Tube", "0.3.2.0"

def b64(b): return base64.urlsafe_b64encode(b).decode().rstrip("=")
def ident(letter, text): return "bqnt" + letter + b64(hashlib.sha256(text.encode()).digest())[:22]

def icon(n=144):
    """A plain icon: red rounded square, white play triangle (no third-party artwork)."""
    rows = []
    for y in range(n):
        row = bytearray([0])
        for x in range(n):
            r = n * 0.18
            cx, cy = min(max(x, r), n - 1 - r), min(max(y, r), n - 1 - r)
            inside = (x - cx) ** 2 + (y - cy) ** 2 <= r * r
            tx, ty = (x - n * 0.38) / (n * 0.30), (y - n * 0.5) / (n * 0.22)
            tri = 0 <= tx <= 1 and abs(ty) <= 1 - tx
            row += bytes((255, 255, 255, 255) if inside and tri else (200, 30, 30, 255) if inside else (0, 0, 0, 0))
        rows.append(bytes(row))
    def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", n, n, 8, 6, 0, 0, 0)) + \
        chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b"")

assets = {"native/tube": open(os.path.join(HERE, "build/tube"), "rb").read(), "native/icon.png": icon()}
# the signature solver: QuickJS runner + scripts, started by the core when a video needs it
EXEC = {"native/tube", "native/js/jsrun"}
assets["native/js/jsrun"] = open(os.path.join(HERE, "build/jsrun"), "rb").read()
for src in ("third_party/ejs/yt.solver.lib.min.js", "third_party/ejs/yt.solver.core.js", "js/glue.js", "js/solve.js"):
    assets["native/js/" + os.path.basename(src)] = open(os.path.join(HERE, src), "rb").read()
for root, _, files in os.walk(os.path.join(HERE, "assets")):
    for f in files:
        p = os.path.join(root, f)
        assets["native/assets/" + os.path.relpath(p, os.path.join(HERE, "assets"))] = open(p, "rb").read()

m = f"""Archive-Manifest-Version: 1.5
Archive-Created-By: bq native packager 1.0

Package-Type: application
Package-Author: bq
Package-Author-Id: {ident('A', 'bq')}
Package-Name: {PKG}
Package-Id: {ident('B', PKG)}
Package-Version: {VER}
Package-Version-Id: {ident('C', PKG + VER)}
Package-Architecture: armle-v7

Application-Name: {NAME}
Application-Id: {ident('D', PKG)}
Application-Description: Native video client
Application-Version: {VER}
Application-Version-Id: {ident('E', PKG + VER)}
Application-Requires-System: BlackBerry 10/10.3.0.0
Application-Development-Mode: false

Entry-Point-Name: {NAME}
Entry-Point-Key: e1
Entry-Point: app/native/tube
Entry-Point-Type: Qnx/Elf
Entry-Point-Icon: {{144x144}}native/icon.png
Entry-Point-Orientation: auto
Entry-Point-User-Actions: access_internet,run_when_backgrounded
Entry-Point-System-Actions: run_native

"""
for k, v in assets.items():
    m += f"Archive-Asset-Name: {k}\nArchive-Asset-SHA-512-Digest: {b64(hashlib.sha512(v).digest())}\n"
    if k in EXEC: m += "Archive-Asset-Type: Qnx/Elf\n"         # the installer makes these executable
    m += "\n"
out = os.path.join(HERE, "build", PKG + ".bar")
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("META-INF/MANIFEST.MF", m)
    for k, v in assets.items():
        zi = zipfile.ZipInfo(k)
        zi.external_attr = (0o755 if k in EXEC else 0o644) << 16
        zi.compress_type = zipfile.ZIP_DEFLATED
        z.writestr(zi, v)
print(out, os.path.getsize(out), "bytes;", len(assets), "assets")
