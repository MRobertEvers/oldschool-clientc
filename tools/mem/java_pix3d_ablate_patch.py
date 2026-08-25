"""Ablate the Java client's span fills, so its rasterisation cost can be measured.

The mirror of TORIRS_ABL_NORASTER on our side: the three span functions return at
entry, so triangle setup, edge stepping and the whole scene walk still run and
only the pixel writing disappears. Baseline minus this is what rasterising costs
the Java client -- a number that has so far only been estimated, and the target
we have to beat.

Ablation by deletion rather than by timing the functions: 5,100 triangles a frame
means ~50,000 span calls, and two System.nanoTime() calls each would cost more
than the work being measured.
"""
import re
import sys

SRC = sys.argv[1]
text = open(SRC, encoding="utf-8").read()

for name in ("gouraudRaster", "flatRaster", "textureRaster"):
    m = re.search(r"public static void " + name + r"\([^)]*\)\s*\{", text)
    assert m, "not found: " + name
    text = text[:m.end()] + "\n\t\tif (ablateRaster) { return; }" + text[m.end():]

m = re.search(r"(public final class Pix3D[^\n]*\{\n|public class Pix3D[^\n]*\{\n)", text)
assert m, "class declaration not found"
FIELD = ('\n\tpublic static final boolean ablateRaster ='
         ' System.getenv("JAVA_ABL_NORASTER") != null;\n')
text = text[:m.end()] + FIELD + text[m.end():]

open(SRC, "w", encoding="utf-8").write(text)
print("ablation patched into " + SRC)
