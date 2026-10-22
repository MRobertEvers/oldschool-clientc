#!/usr/bin/env python3
"""Build and run the Java oracle for the rszbuf rasterizer; write its goldens.

The rszbuf rasterizer (3rd/toridraw/impl/raster/rszbuf/) mirrors the rev-241
client's depth-buffered triangle setups. This proves it against the client
itself: the setups' method bodies are copied VERBATIM out of a Deobfuscator
checkout (class162, class153.method5155, class106.method3896) into a
standalone Java program, only the classes around them are stubs, and
Oracle.java drives them over the same deterministic stream of triangles that
toridraw_rszbuf_test.c replays in C.

Nothing of the deob is checked in here: the bodies are read at run time from
--deob, and only the hashes they produce are written into the tree.

  python3 tools/rszbuf_oracle/make_oracle.py \\
      --deob ~/Documents/git_repos/Deobfuscator/src_osrs241_rl1_13_3/deob \\
      --out 3rd/toridraw/toridraw_rszbuf_golden.h

Needs javac and java on PATH.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))

# The live setups and spans, and the decoys the live code names (reached only
# through `var0 == null`), kept so the class compiles.
CLASS162_METHODS = [
    "method5387", "method5342", "method5379", "method5343", "method5377", "method5351",
    "method5346", "method5355", "method5353", "method5347", "method5375", "method5165",
    "method5350", "method5169", "method5352", "method5170", "method5354", "method5171",
    "method5405", "method5402", "method5403", "method5401", "method5345", "method5382",
]

# Cases per golden group: the test folds each group's per-case hashes into one,
# so a mismatch names a range of 64 cases rather than one number for all.
GROUP = 64


def extract(src, name):
    m = re.search(r"\n\t[^\n]*\b" + name + r"\(([^)]*)\)\s*\{", src)
    if not m:
        sys.exit("make_oracle: %s not found -- is --deob the rev-241 deob?" % name)
    start = m.start() + 1
    i = src.index("{", m.end() - 1)
    depth = 0
    for j in range(i, len(src)):
        if src[j] == "{":
            depth += 1
        elif src[j] == "}":
            depth -= 1
            if depth == 0:
                return src[start: j + 1]
    sys.exit("make_oracle: unbalanced body for " + name)


def clean(body):
    body = re.sub(r"\t@ObfuscatedName\([^)]*\)\n", "", body)
    return body.replace("\t@Override\n", "")


def write_sources(deob, out):
    def read(name):
        return open(os.path.join(deob, name + ".java")).read()

    c162, c153, c106 = read("class162"), read("class153"), read("class106")
    methods = "\n\n".join(clean(extract(c162, n)) for n in CLASS162_METHODS)
    files = {
        "class162.java": "public class class162 {\n"
        "\tclass175 field2157 = new class175();\n"
        "\tint[] field2156;\n"
        "\tboolean field2155;\n"
        "\tstatic final int method5155(int a, int b) { return class153.method5155(a, b); }\n\n"
        + methods + "\n}\n",
        "class153.java": "public class class153 {\n" + clean(extract(c153, "method5155")) + "\n}\n",
        "class106.java": "public class class106 {\n" + clean(extract(c106, "method3896")) + "\n}\n",
        # The rasterizer's state [class175] and texture source [class171],
        # reduced to the fields the setups read.
        "class175.java": """public class class175 {
	int field2458, field2447, field2466 = 2, field2457, field2446, field2456, field2436,
	    field2454, field2449, field2455, field2442 = -1, field2465 = -1, field2452 = 512;
	boolean field2459, field2450 = true;
	int[] field2462 = new int[1024];
	class171 field2453 = new class171();
}
""",
        "class171.java": """public class class171 {
	int[] texels;
	boolean opaque;
	int average;
	int[] method4393(int id, byte b) { return texels; }
	int method4394(int id, int b) { return average; }
	boolean method4396(int id, byte b) { return opaque; }
}
""",
        "Statics.java": """public class Statics {
	public static int[] field7152;
	public static int field7153;
	public static float[] field7156;
}
""",
    }
    for name, text in files.items():
        with open(os.path.join(out, name), "w") as f:
            f.write(text)
    shutil.copy(os.path.join(HERE, "Oracle.java"), out)


def fold(h, v):
    return ((h ^ v) * 16777619) & 0xFFFFFFFF


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--deob", required=True, help="the rev-241 deob source directory")
    ap.add_argument("--out", required=True, help="the golden header to write")
    ap.add_argument("--cases", type=int, default=6000)
    args = ap.parse_args()

    with tempfile.TemporaryDirectory() as work:
        write_sources(os.path.expanduser(args.deob), work)
        subprocess.check_call(["javac", "-nowarn", "-d", work] +
                              [os.path.join(work, n) for n in os.listdir(work) if n.endswith(".java")])
        lines = subprocess.check_output(["java", "-cp", work, "Oracle", str(args.cases)], text=True).split("\n")

    groups = []
    h = 0x811C9DC5
    for i, line in enumerate(l for l in lines if l):
        _, _, pixels, depths = line.split()
        h = fold(fold(h, int(pixels, 16)), int(depths, 16))
        if i % GROUP == GROUP - 1 or i == args.cases - 1:
            groups.append(h)
            h = 0x811C9DC5

    with open(args.out, "w") as f:
        f.write("/* Generated by tools/rszbuf_oracle/make_oracle.py from the rev-241 deob's\n"
                " * class162. Do not edit; regenerate. One hash per %d cases of the stream\n"
                " * toridraw_rszbuf_test.c replays. */\n" % GROUP)
        f.write("#define RSZBUF_GOLDEN_CASES %d\n" % args.cases)
        f.write("#define RSZBUF_GOLDEN_GROUP %d\n" % GROUP)
        f.write("static const unsigned int g_rszbuf_golden[] = {\n")
        for i in range(0, len(groups), 6):
            f.write("    " + ", ".join("0x%08xu" % g for g in groups[i:i + 6]) + ",\n")
        f.write("};\n")
    print("make_oracle: %d cases, %d groups -> %s" % (args.cases, len(groups), args.out))


if __name__ == "__main__":
    main()
