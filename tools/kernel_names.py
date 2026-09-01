#!/usr/bin/env python3
"""
The kernel naming grammar: propose it, check it, and say where it does not fit.

WHAT THIS IS FOR. The variant space under 3rd/toridraw is large -- six ISA lanes
of the portable projection ladder, ninety-six template-generated texture
kernels, twenty-four gated span variants, sixteen scanline texture variants, two
sorts times two scene tiers -- and it is navigable only if a filename is a
complete coordinate in that space. Today it is not: the same axis is spelled
`none` in one family and `scalar` in another, `aarch64` in one and `neon` in its
neighbour, and one family writes its axes in a different order from the rest.

Run with no arguments it REPORTS: every file it can place, the name the grammar
gives it, and -- the part worth reading -- every file it cannot place and why.

    python3 tools/kernel_names.py                # the map, plus what does not fit
    python3 tools/kernel_names.py --check        # exit 1 if any name is off-grammar
    python3 tools/kernel_names.py --map out.tsv  # old<TAB>new, for a move script

It moves nothing. The moves are a separate, mechanical step, and they should not
run until this map has been read by a person: a rename that lands with a
behaviour change in the same commit poisons every bisect through the result.

THE ONE RULE THAT NEEDED DECIDING. A file is not always one kernel. The ninety-six
texture files are one kernel each, so the filename can carry the whole
coordinate. The ISA ladder files are not -- `projection16_simd.neon.u.c` defines
eight entry points spanning clip/noclip and tex/notex. Naming every axis in a
file that SPANS several of them produces either a lie or `clipall.texall.yawall`.

So: a filename names the axes the file FIXES. Axes the file spans are carried by
the FUNCTION names inside it, which is where they were always going to have to
be anyway, because they distinguish entry points within one translation unit.
Where a file is one kernel, the two coincide and the filename is the full
coordinate. That is the convention, and it is held for every family here.
"""

import argparse
import os
import re
import sys
from collections import defaultdict

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "3rd", "toridraw")
ROOT = os.path.normpath(ROOT)

# ---- the vocabulary ------------------------------------------------------
#
# One spelling per axis value. The left column is what the tree says today; the
# right is the one spelling the grammar keeps. `none` vs `scalar` and `aarch64`
# vs `neon` are the two splits that actually cost something: they make a
# whole-tree grep for one ISA miss half of it. The NEON entries additionally
# carry the ENCODING WIDTH, for a stronger reason still -- see the block below.

#
# NEON IS TWO VALUES, NOT ONE.
#
# `neon` alone was a lie the tree told itself: ARM NEON in the A32 (armv7)
# encoding and NEON in the A64 (aarch64) encoding are not the same instruction
# set, and several kernels here are written in intrinsics that exist ONLY in
# A64 -- the widening high-half multiplies (vmull_high_*), the 64-bit vector
# compares (vcgtq_s64), the horizontal reductions (vaddvq_*, vminvq_*,
# vmaxvq_*) and the full 16-byte table lookup (vqtbl1q_u8). A32 has none of
# them, and a kernel using them does not run slower on armv7 -- it does not
# COMPILE. That is exactly how the Android armeabi-v7a lane found the facesort
# kernel: an `#if defined(__ARM_NEON)` guard admitted armv7 into an A64-only
# lane, and the build died on five undeclared intrinsics.
#
# So the width is in the name, and a reader can tell from the filename alone
# whether a 32-bit ARM target can use a kernel:
#
#   neon32   the A32 NEON baseline. Runs on armv7 AND on aarch64.
#   neon64   requires aarch64, either for A64-only intrinsics or because the
#            file wraps aarch64 assembly.
#
ISA_CANON = {
    "scalar": "scalar",
    "none": "scalar",          # projection_ortho.none, face_sort.none
    "neon32": "neon32",
    "neon64": "neon64",
    # Bare `neon` is retired: it never said which encoding, which is the whole
    # point of the split above. Mapped to neon32 so an un-migrated name resolves
    # to the PORTABLE lane -- the safe direction, because a kernel wrongly
    # called neon32 fails loudly at compile time on armv7, while one wrongly
    # called neon64 would quietly exclude armv7 from a lane it could have used.
    "neon": "neon32",
    "aarch64": "neon64",       # C files only; .S keeps the arch name
    "sse2": "sse2",
    "sse41": "sse41",
    "sse_float": "sse_float",
    "avx": "avx",
    "i686": "i686",            # .S
    "x64": "x64",              # .S
}

# Assembly keeps the architecture in its name: a .S file IS an architecture, and
# `neon` names an instruction set that aarch64 assembly does not exclusively use.
ASM_ISA = {"aarch64", "i686", "x64", "xtensa"}

# A kernel that writes ONE framebuffer format names it, between the family and
# the architecture: tri.flat.rgb565.xtensa.S. Absent, the file is XRGB8888 --
# which is what every kernel was when there was only one format, so the token
# appears exactly where a second one did. Renaming the incumbents to carry
# `.xrgb8888.` would be churn that tells a reader nothing they did not know.
FORMAT_TOKEN = {"xrgb8888", "argb8888", "rgba8888", "abgr8888", "bgra8888",
                "rgb565", "argb1555"}

STAGE_PROJ, STAGE_SORT, STAGE_RASTER = "projection", "facesort", "raster"


class Placement:
    def __init__(self, old, new, stage, note=""):
        self.old, self.new, self.stage, self.note = old, new, stage, note


class Unplaced:
    def __init__(self, old, why):
        self.old, self.why = old, why


def _isa(token):
    """Canonical ISA spelling, or None if this token is not an ISA."""
    return ISA_CANON.get(token)


def _ext(name):
    for e in (".u.c", ".inc", ".h", ".c", ".S"):
        if name.endswith(e):
            return e
    return None


# ---- stage 1: projection -------------------------------------------------

def place_projection(rel, base):
    """
    graphics/projection*  ->  impl/projection/

    Families, and what each file fixes:

      projection16_simd.<isa>       the portable perspective ladder. Fixes mode
                                    and camera; SPANS clip, tex and rotation
                                    shape, which the eight contract entry points
                                    inside carry.
      projection_ortho.<isa>        the parallel family, same shape.
      projection_prepared.<isa>     the prepared-camera hooks: mode, camera and
                                    tex fixed, clip spanned by two hooks.
      projection_bound.<isa>        the screen-box fold. Not a projection
                                    kernel at all -- it reads what one wrote --
                                    so it keeps its own name under projection/.
      projection16.aarch64.S        the hand-written prepared kernel.
      projection_zdiv_simd.<isa>    compiled, no production caller.
    """
    ext = _ext(base)
    if ext is None:
        return None
    stem = base[: -len(ext)]

    # The dispatcher/aggregator of a family: no ISA token.
    parts = stem.split(".")
    isa = _isa(parts[-1]) if len(parts) > 1 else None
    fam = parts[0]

    def out(name, note=""):
        return Placement(rel, "impl/projection/" + name + ext, STAGE_PROJ, note)

    if fam == "projection16_simd":
        if isa:
            return out("projection.perspective.plain.%s" % isa,
                       "spans clip/tex/shape; the eight contract entries carry them")
        return out("projection.perspective.plain.dispatch")
    if fam == "projection_ortho":
        if isa:
            return out("projection.parallel.plain.%s" % isa)
        return out("projection.parallel.plain.dispatch")
    if fam == "projection_prepared":
        if isa:
            return out("projection.perspective.prepared.%s" % isa)
        return out("projection.perspective.prepared.dispatch")
    if fam == "projection16_prepared" and isa:
        return out("projection.perspective.prepared.%s.impl" % isa,
                   "header carrying the SSE2 prepared core")
    if fam == "projection16_fast" and isa:
        return Placement(rel, "bench/projection.perspective.fast.%s%s" % (isa, ext), STAGE_PROJ,
                         "BENCH ONLY: sole includer is toridraw_proj_bench.c")
    if fam == "projection_bound":
        return out("projection.bound." + isa if isa else "projection.bound.dispatch")
    if fam == "projection_zdiv_simd":
        return Placement(rel, "impl/projection/zdiv/projection.zdiv.%s%s" % (isa or "dispatch", ext),
                         STAGE_PROJ, "NO PRODUCTION CALLER: delete or move to bench/")
    if fam == "projection16" and parts[-1] in ASM_ISA:
        return out("projection.perspective.prepared.%s" % parts[-1], "hand-written kernel")
    if fam == "projection":
        return out("projection.scalar_reference")
    return None


# ---- stage 2: face cull + sort -------------------------------------------

def place_facesort(rel, base):
    """
    toridraw_face_sort_bitonic_radix.*  ->  impl/facesort/

    The bitonic+radix sort's lane hooks. The bucket sort has no files of its
    own yet --
    it lives inside toridraw_render.u.c, and extracting it is a move of its own,
    which is why it is reported as a gap rather than given a name here.
    """
    ext = _ext(base)
    if ext is None:
        return None
    stem = base[: -len(ext)]
    parts = stem.split(".")
    isa = _isa(parts[-1]) if len(parts) > 1 else None
    if parts[0] != "toridraw_face_sort_bitonic_radix":
        return None
    if isa:
        return Placement(rel, "impl/facesort/facesort.bitonic_radix.small.%s%s" % (isa, ext),
                         STAGE_SORT)
    return Placement(rel, "impl/facesort/facesort.bitonic_radix.small.dispatch" + ext, STAGE_SORT)


# ---- stage 3: raster -----------------------------------------------------

# The texture families are already the most systematic names in the tree. What
# they are missing is the two axes encoded by ABSENCE -- a file with no
# `facealpha` segment means "no face alpha", and one with no `zbuf` means
# "painter". Absence is not a value: it cannot be grepped for, and it makes the
# axis order load-bearing for reading rather than just for sorting.
TEX_MAPPING = ("texplane", "texpmn", "texcylinder", "texcube", "texsphere",
               "texshadeflat", "texshadeblend")
TEX_GATE = ("texopaque", "textrans", "texalpha")


def place_raster_texture(rel, base):
    ext = _ext(base)
    if ext is None:
        return None
    stem = base[: -len(ext)]
    parts = stem.split(".")
    if not parts or parts[0] not in TEX_MAPPING:
        return None

    mapping = parts[0]
    rest = parts[1:]
    if not rest:
        return None

    interp = rest[0] if rest and rest[0] in ("persp", "affine") else None
    interp = "perspective" if interp == "persp" else interp
    if interp is None:
        return None
    rest = rest[1:]

    gate = rest[0] if rest and rest[0] in TEX_GATE else None
    if gate is None:
        return None
    rest = rest[1:]

    facealpha = "facealpha" if "facealpha" in rest else "nofacealpha"
    modulate = "modulate" if "modulate" in rest else "nomodulate"
    depth = "zbuf" if "zbuf" in rest else "painter"
    traversal = next((t for t in rest if t in ("branching", "scanline", "sort", "ordered")), None)
    step = next((t for t in rest if t.startswith("lerp8") or t in ("s1", "s4", "s8")), None)

    if traversal is None or step is None:
        return Unplaced(rel, "no traversal or step segment in %r" % (rest,))

    # texshadeflat.persp.texopaque.ordered.lerp8.scanline puts the traversal
    # LAST and calls the pre-sorted entry `ordered`. Everywhere else `ordered`
    # is a suffix on the FUNCTION and the file's traversal slot says branching /
    # sort / scanline. One spelling: the traversal is a traversal.
    note = ""
    if traversal == "ordered":
        traversal = "scanline" if "scanline" in rest else "sort"
        note = "axis order was reversed; `ordered` was in the traversal slot"

    name = ".".join(["raster", mapping, interp, gate, facealpha, modulate, depth,
                     traversal, step, "scalar"])
    return Placement(rel, "impl/raster/tex/" + name + ext, STAGE_RASTER, note)


SOLID_SHADING = {"flat": "flat", "gouraudhsllightness": "gouraudhsllightness",
                 "gouraudrgb": "gouraudrgb", "zbuf": "zbuf", "scanline": None}


def place_raster_solid(rel, base):
    ext = _ext(base)
    if ext is None:
        return None
    stem = base[: -len(ext)]
    parts = stem.split(".")
    if not parts or parts[0] not in SOLID_SHADING:
        return None
    shading = SOLID_SHADING[parts[0]]
    if shading is None:
        return None
    rest = parts[1:]
    if not rest or rest[0] != "screen":
        return None
    rest = rest[1:]

    alpha = "alpha" if "alpha" in rest else "opaque"
    traversal = next((t for t in rest if t in ("branching", "scanline", "sort")), None)
    step = next((t for t in rest if t in ("s1", "s4", "s8")), None)
    if traversal is None and parts[0] == "zbuf":
        # One walker, three shading modes chosen by a runtime enum rather than
        # by four files. That is a real design difference from the painter
        # families, not a naming gap, so the name records the walker.
        return Placement(rel, "impl/raster/zbuf/raster.zbuf.screen.walker" + ext,
                         STAGE_RASTER, "one walker, modes are a runtime enum")
    if traversal is None:
        return Unplaced(rel, "no traversal segment in %r" % (rest,))
    if step is None:
        step = "s4"
    name = ".".join(["raster", shading, alpha, "nofacealpha", "nomodulate", "painter",
                     traversal, step, "scalar"])
    return Placement(rel, "impl/raster/%s/%s" % (parts[0], name) + ext, STAGE_RASTER)


def place_raster_span(rel, base):
    """Span doors: one hook, one ISA, one file."""
    ext = _ext(base)
    if ext is None:
        return None
    stem = base[: -len(ext)]
    parts = stem.split(".")
    isa = _isa(parts[-1]) if len(parts) > 1 else None
    if parts[0] == "tex" and len(parts) > 1 and parts[1] == "span":
        if "gates" in parts:
            # The twenty-four gate x facealpha x modulate x zbuf variants, all
            # stamped from one template. Not the dispatcher.
            return Placement(rel, "impl/raster/span/span.tex.gates" + ext, STAGE_RASTER)
        tail = "span.tex." + (isa or "dispatch")
        return Placement(rel, "impl/raster/span/" + tail + ext, STAGE_RASTER)
    if parts[0] == "gouraudhsllightness" and "span" in parts:
        alpha = "alpha" if "alpha" in parts else "opaque"
        tail = "span.gouraudhsllightness.%s.%s" % (alpha, isa or "dispatch")
        return Placement(rel, "impl/raster/span/" + tail + ext, STAGE_RASTER)
    if parts[0] == "scanline" and "span" in parts:
        return Placement(rel, "impl/raster/span/span.solid.scanline.scalar" + ext,
                         STAGE_RASTER)
    return None


def place_raster_asm(rel, base):
    """
    Hand-written kernels. A .S file IS an architecture, so it keeps the
    architecture name rather than the ISA vocabulary the C files share: `neon`
    names an instruction set that aarch64 assembly does not exclusively use,
    and the two are not interchangeable in a filename that the assembler reads.

    The grammar is `<kind>.<family>[.<format>].<arch>.S`. The format token is
    present only where the kernel writes one framebuffer layout that is not
    the incumbent XRGB8888 -- see FORMAT_TOKEN. Nothing here MAPS onto a
    formatted name, because no legacy file had one: the first such kernel
    (tri.flat.rgb565.xtensa.S) was written under this grammar rather than
    renamed into it.
    """
    m = re.match(r"(flat|gouraud|tex)_(tri|span)_(aarch64|i686|x64|xtensa)\.S$", base)
    if not m:
        return None
    family, kind, arch = m.groups()
    fam = {"flat": "flat", "gouraud": "gouraudhsllightness", "tex": "tex"}[family]
    return Placement(rel, "impl/raster/asm/%s.%s.%s.S" % (kind, fam, arch), STAGE_RASTER)


def place_raster_scanline(rel, base):
    """The scanline family: its own rasteriser, one directory."""
    ext = _ext(base)
    if ext is None:
        return None
    stem = base[: -len(ext)]
    parts = stem.split(".")
    if parts[0] != "scanline":
        return None
    if len(parts) == 1:
        return Placement(rel, "impl/raster/scanline/scanline.dispatch" + ext, STAGE_RASTER)
    fam = parts[1]
    fam = {"gouraudhsllightness": "gouraudhsllightness"}.get(fam, fam)
    return Placement(rel, "impl/raster/scanline/scanline.%s%s" % (fam, ext), STAGE_RASTER)


def place_raster_batch(rel, base):
    """The batched whole-model walk: a traversal, not a pixel kernel."""
    if not base.startswith("raster.batch"):
        return None
    ext = _ext(base)
    return Placement(rel, "impl/raster/walk/walk.batched" + ext, STAGE_RASTER,
                     "a traversal, not a pixel kernel")


def place_triangle(rel, base):
    """triangles/toridraw_triangle_<family>.u.c -- the dispatch layer."""
    m = re.match(r"toridraw_triangle_(.+)\.u\.c$", base)
    if not m:
        return None
    fam = m.group(1)
    return Placement(rel, "impl/raster/dispatch/tri.%s.u.c" % fam, STAGE_RASTER,
                     "legacy toridraw_ prefix; the dispatch layer, not a kernel")


PLACERS = (place_projection, place_facesort, place_raster_asm, place_raster_span,
           place_raster_scanline, place_raster_batch, place_raster_texture,
           place_raster_solid, place_triangle)

# Files that are deliberately not variant files: instrumentation, shared
# headers, the aggregation objects. They keep their names.
KEEP = re.compile(
    r"(census|ablate|_asm\.h$|_asm_support|tex_sampler|texmap_common|scanline_common"
    r"|scanline_select|_steps\.h$|_edges\.h$|zbuf_plane|ysort_order|winding|shade\.h"
    r"|clamp\.h|alpha\.h|pixel_format|int_wrap|dash_|fb_clear|zdepth|sse2_41compat|tori_compat"
    r"|shared_tables|convex_hull|_peer_decl|span_uv|_body\.inc$|_tmpl\.inc$"
    r"|edge_slopes|batch_stats|div3|span_fill)")


def scan():
    placed, unplaced, kept = [], [], []
    for dirpath, _dirnames, filenames in os.walk(ROOT):
        rel_dir = os.path.relpath(dirpath, ROOT)
        if rel_dir.startswith("test") or "/test" in rel_dir:
            continue
        for fn in sorted(filenames):
            if _ext(fn) is None:
                continue
            rel = os.path.normpath(os.path.join(rel_dir, fn))
            # Only the implementation tiers are renamed. kernels/ and families/
            # are the caller-facing tiers and already follow the grammar.
            if not (rel.startswith("graphics") or rel.startswith("triangles")
                    or fn.startswith("toridraw_face_sort_bitonic_radix")):
                continue
            if fn.endswith("_test.c") or fn.endswith("_bench.c"):
                continue
            if KEEP.search(fn):
                kept.append(rel)
                continue
            hit = None
            for placer in PLACERS:
                hit = placer(rel, fn)
                if hit is not None:
                    break
            if hit is None:
                unplaced.append(Unplaced(rel, "no family matched"))
            elif isinstance(hit, Unplaced):
                unplaced.append(hit)
            else:
                placed.append(hit)
    return placed, unplaced, kept


def write_catalog(path, placed, kept):
    """
    The variant catalog, DERIVED from the names rather than maintained beside
    them.

    This is what naming every axis buys. A catalog kept by hand is a second
    copy of the truth, and the copy is wrong within a month: someone adds a
    texture variant and the table that lists them does not move. Here the
    table cannot drift, because it is a projection of the filenames the build
    already reads.

    Written against the PROPOSED names, so it is accurate the moment the moves
    land and readable before them.
    """
    axes = defaultdict(lambda: defaultdict(set))
    counts = defaultdict(int)
    for p in placed:
        stem = os.path.basename(p.new)
        for e in (".u.c", ".inc", ".h", ".c", ".S"):
            if stem.endswith(e):
                stem = stem[: -len(e)]
                break
        parts = stem.split(".")
        if not parts:
            continue
        counts[p.stage] += 1
        # Group by PREFIX AND FAMILY, not prefix alone.
        #
        # A solid raster kernel has no texture gate and a textured one does, so
        # the two carry different axis sets and their segments do not line up
        # positionally. Tabulating them together puts `nofacealpha` and
        # `texopaque` in one column and invites the reader to believe those are
        # alternatives on one axis. They are not: they are position 3 of two
        # different grammars that share a prefix.
        #
        # The axes ARE consistent within a family, which is the property worth
        # showing, so the family is part of the key.
        family = parts[0]
        if len(parts) > 1:
            family = "%s.%s" % (parts[0], parts[1])
        for i, tok in enumerate(parts[2:], start=2):
            axes[(p.stage, family)][i].add(tok)

    with open(path, "w") as fh:
        fh.write("# ToriDraw kernel variant catalog\n\n")
        fh.write("Generated by `tools/kernel_names.py --catalog`. Do not edit: it is\n")
        fh.write("derived from the filenames, which is the point of naming every axis.\n\n")
        fh.write("| stage | files |\n|---|---:|\n")
        for stage in (STAGE_PROJ, STAGE_SORT, STAGE_RASTER):
            if counts[stage]:
                fh.write("| %s | %d |\n" % (stage, counts[stage]))
        fh.write("| (kept: instrumentation, shared headers, templates) | %d |\n\n" % len(kept))

        for stage in (STAGE_PROJ, STAGE_SORT, STAGE_RASTER):
            fams = sorted(f for (st, f) in axes if st == stage)
            if not fams:
                continue
            fh.write("## %s\n\n" % stage)
            for fam in fams:
                slots = axes[(stage, fam)]
                fh.write("### `%s`\n\n" % fam)
                fh.write("| position | values |\n|---|---|\n")
                for i in sorted(slots):
                    vals = sorted(slots[i])
                    fh.write("| %d | %s |\n" % (i, ", ".join("`%s`" % v for v in vals)))
                fh.write("\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true",
                    help="exit 1 if any file cannot be placed or two files collide")
    ap.add_argument("--map", metavar="FILE", help="write old<TAB>new for a move script")
    ap.add_argument("--catalog", metavar="FILE",
                    help="write the variant catalog, derived from the names")
    args = ap.parse_args()

    placed, unplaced, kept = scan()

    collisions = defaultdict(list)
    for p in placed:
        collisions[p.new].append(p.old)
    dupes = {k: v for k, v in collisions.items() if len(v) > 1}

    by_stage = defaultdict(list)
    for p in placed:
        by_stage[p.stage].append(p)

    for stage in (STAGE_PROJ, STAGE_SORT, STAGE_RASTER):
        rows = by_stage.get(stage, [])
        if not rows:
            continue
        print("== %s (%d files)" % (stage, len(rows)))
        for p in sorted(rows, key=lambda r: r.new):
            print("   %-58s -> %s" % (p.old, p.new))
            if p.note:
                print("   %-58s    ~ %s" % ("", p.note))
        print()

    print("== kept as-is (%d): instrumentation, shared headers, templates" % len(kept))
    print()

    if dupes:
        print("== COLLISIONS (%d) -- two files want one name" % len(dupes))
        for new, olds in sorted(dupes.items()):
            print("   %s" % new)
            for o in olds:
                print("       %s" % o)
        print()

    if unplaced:
        print("== NOT PLACED (%d) -- the grammar does not cover these yet" % len(unplaced))
        for u in sorted(unplaced, key=lambda r: r.old):
            print("   %-58s  %s" % (u.old, u.why))
        print()

    print("== GAPS the map cannot close by renaming")
    print("   The bucket sort has no files of its own: its four full-scene and")
    print("   four small-scene loops live inside toridraw_render.u.c. Extracting")
    print("   them is a move of its own and must land as a separate commit --")
    print("   code identical, relocated -- before any rename touches them.")
    print()

    if args.map:
        with open(args.map, "w") as fh:
            for p in sorted(placed, key=lambda r: r.old):
                fh.write("%s\t%s\n" % (p.old, p.new))
        print("wrote %s (%d rows)" % (args.map, len(placed)))

    if args.catalog:
        write_catalog(args.catalog, placed, kept)
        print("wrote %s" % args.catalog)

    if args.check and (dupes or unplaced):
        print("FAIL: %d collision(s), %d unplaced" % (len(dupes), len(unplaced)),
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
