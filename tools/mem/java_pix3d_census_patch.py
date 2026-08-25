"""Instrument the Java client's Pix3D with draw counters.

The question is geometry, not time: how many triangles and how many pixels does
the Java client actually rasterise per frame, against our 6,682 faces? A profile
cannot answer it and neither can reading the source, because the answer depends
on what the occlusion and LOD passes reject at run time.

Counts only, and only at span granularity -- one add per span, never per pixel.
A per-pixel counter would slow the raster enough to drop the client below its
50 fps cap, which changes the scene being counted.

Reports on a wall clock rather than per frame: Pix3D has no per-frame entry
point, and a time-based report needs no second file patched.
"""
import re
import sys

SRC = sys.argv[1]
text = open(SRC, encoding="utf-8").read()

FIELDS = '''
	/* --- draw census (instrumentation, not client code) --------------- */
	public static long censusTris = 0;
	public static long censusSpans = 0;
	public static long censusPixels = 0;
	public static long censusT0 = 0;
	public static long censusLast = 0;

	public static void censusTick() {
		long now = System.currentTimeMillis();
		if (censusT0 == 0) { censusT0 = now; censusLast = now; return; }
		if (now - censusLast < 2000) { return; }
		double secs = (now - censusLast) / 1000.0;
		System.out.println("[census] tris/s=" + (long)(censusTris / secs)
			+ " spans/s=" + (long)(censusSpans / secs)
			+ " px/s=" + (long)(censusPixels / secs)
			+ " avg_span_px=" + (censusSpans > 0 ? (censusPixels / censusSpans) : 0));
		System.out.flush();
		censusTris = 0; censusSpans = 0; censusPixels = 0; censusLast = now;
	}
'''

# Fields go right after the class opening brace.
m = re.search(r"(public final class Pix3D[^\n]*\{\n|public class Pix3D[^\n]*\{\n)", text)
assert m, "class declaration not found"
text = text[:m.end()] + FIELDS + text[m.end():]

def add_after_sig(sig_re, stmt, label):
    global text
    mm = re.search(sig_re, text)
    assert mm, "signature not found: " + label
    insert = mm.end()
    text = text[:insert] + "\n\t\t" + stmt + text[insert:]

# Triangle entry points: one count each, plus the periodic report.
for name in ("gouraudTriangle", "flatTriangle", "textureTriangle"):
    add_after_sig(r"public static void " + name + r"\([^)]*\)\s*\{",
                  "censusTris++; censusTick();", name)

# Span entry points: count the span and its length before any clipping.
add_after_sig(r"public static void gouraudRaster\([^)]*\)\s*\{",
              "censusSpans++; if (arg3 > arg2) { censusPixels += (arg3 - arg2); }",
              "gouraudRaster")
add_after_sig(r"public static void flatRaster\([^)]*\)\s*\{",
              "censusSpans++; if (arg4 > arg3) { censusPixels += (arg4 - arg3); }",
              "flatRaster")
add_after_sig(r"public static void textureRaster\([^)]*\)\s*\{",
              "censusSpans++; if (arg6 > arg5) { censusPixels += (arg6 - arg5); }",
              "textureRaster")

open(SRC, "w", encoding="utf-8").write(text)
print("patched " + SRC)
