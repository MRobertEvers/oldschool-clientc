"""Count the Java client's actual frame rate.

Pix2D.cls() is called exactly once per frame from gameDrawMain, so counting it
counts frames. This was assumed rather than measured, and the assumption is
load-bearing: if the client is NOT holding its 50 fps cap it is CPU-saturated,
and then removing any large piece of work lets it reach the cap and idle. Every
ablation then looks enormous, and two of them can each appear to account for
most of the frame -- which is exactly the contradiction the per-kernel arms
produced.
"""
import re
import sys

SRC = sys.argv[1]
text = open(SRC, encoding="utf-8").read()

FIELDS = '''
	/* --- frame census (instrumentation, not client code) -------------- */
	public static long fpsFrames = 0;
	public static long fpsLast = 0;
'''

m = re.search(r"(public final class Pix2D[^\n]*\{\n|public class Pix2D[^\n]*\{\n)", text)
assert m, "Pix2D class declaration not found"
text = text[:m.end()] + FIELDS + text[m.end():]

BODY = '''
		fpsFrames++;
		{
			long now = System.currentTimeMillis();
			if (fpsLast == 0) { fpsLast = now; }
			else if (now - fpsLast >= 2000) {
				System.out.println("[fps] " + (fpsFrames * 1000.0 / (now - fpsLast)));
				System.out.flush();
				fpsFrames = 0; fpsLast = now;
			}
		}
'''
mm = re.search(r"public static void cls\(\)\s*\{", text)
assert mm, "Pix2D.cls not found"
text = text[:mm.end()] + BODY + text[mm.end():]

open(SRC, "w", encoding="utf-8").write(text)
print("fps counter patched into Pix2D.cls")
