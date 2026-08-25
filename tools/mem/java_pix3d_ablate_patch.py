"""Per-kernel ablation flags for the Java client's rasterisers.

Each of Pix3D's three span kernels, plus Pix2D.cls, gets its own switch so their
costs can be measured one at a time by deletion.

The flags are `static volatile boolean`, and that is load-bearing. A previous
attempt used `static final boolean`, which HotSpot folds to a constant: the
method body became dead, inlined to nothing, and the JIT then dead-code
eliminated the CALLERS' argument computation too -- the whole edge walk, not
just the fill. Java appeared to collapse from 50.3% to 4.2% of a core, which is
not a rasterisation measurement, it is a measurement of most of the renderer
disappearing. A volatile read cannot be folded, so the arguments are still
computed at every call site and only the fill is removed.
"""
import re
import sys

SRC = sys.argv[1]
text = open(SRC, encoding="utf-8").read()

FIELDS = '''
	/* Per-kernel ablation. volatile: see the note in the patch script -- a
	 * foldable constant lets the JIT delete the callers as well as the body. */
	public static volatile boolean ablGouraud = System.getenv("JAVA_ABL_GOURAUD") != null;
	public static volatile boolean ablTexture = System.getenv("JAVA_ABL_TEXTURE") != null;
	public static volatile boolean ablFlat    = System.getenv("JAVA_ABL_FLAT") != null;
'''

m = re.search(r"(public final class Pix3D[^\n]*\{\n|public class Pix3D[^\n]*\{\n)", text)
assert m, "Pix3D class declaration not found"
text = text[:m.end()] + FIELDS + text[m.end():]

for name, flag in (("gouraudRaster", "ablGouraud"),
                   ("textureRaster", "ablTexture"),
                   ("flatRaster", "ablFlat")):
    mm = re.search(r"public static void " + name + r"\([^)]*\)\s*\{", text)
    assert mm, "not found: " + name
    text = text[:mm.end()] + "\n\t\tif (" + flag + ") { return; }" + text[mm.end():]

open(SRC, "w", encoding="utf-8").write(text)
print("per-kernel ablation patched into Pix3D")
