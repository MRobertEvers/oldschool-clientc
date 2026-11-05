#!/usr/bin/env python3
"""Synthesize smooth vector faces from ToriRSChrome's baked cache-font masks.

APPROACH: "depixelize" -- depixelizing pixel art (Kopf and Lischinski,
SIGGRAPH 2011) adapted to 1-bit glyph masks.  The winner of three candidate
syntheses (depixelize, stemtrace, skeleton) judged on the q = 6 (150 %)
photograph; this is the one the client ships.  It reads only the baked 1x
masks in src/engine/torirs_debug_font_baked.c: no third-party font.

1. Similarity graph.  Every pixel links to its 4-neighbours of the same
   colour.  A diagonal link exists only where it is not redundant, i.e. in a
   checkerboard 2x2 block (ink on one diagonal, background on the other).
   There both diagonals cross and exactly one must go.
2. Crossing resolution.  The paper's three votes decide which diagonal stays:
   curves (length of the valence-2 chain through each diagonal), sparse pixels
   (the smaller connected component inside an 8x8 window wins) and islands
   (a diagonal that keeps a valence-1 pixel attached wins 5).  A 1-bit glyph
   adds one prior: the strokes are ink, so ink gets INK_PRIOR votes (ties go
   to ink).  The prior is set above the largest margin the three votes ever
   give the background in these faces: inside the b12's 2 pixel strokes the
   8x8 sparse window is mostly ink, so the sparse vote misfires and would cut
   the slash of 0, % and the tails of Q and @ (report.json lists each
   crossing the prior decided).
3. Reshaped cell graph.  At each resolved crossing the shared lattice corner
   splits in two (offset a quarter pixel along the incoming and outgoing
   edges), so the kept diagonal's cells meet across a 45 degree edge and the
   cut pixels lose a corner.  With only two colours every cell edge between
   ink and background is visible, so the contours are just the boundaries of
   the reshaped ink region, traced as closed polylines (ink on the right).
4. Curve fitting.  The polyline nodes become the control points of a
   quadratic B-spline -- exactly a TrueType contour of off-curve points.
   Corner detection: a lattice corner with a 90 degree turn stays a sharp
   on-curve point when both runs that meet there are "solid": a straight run
   of 2 or more pixels, or a 1 pixel cap (both ends turn the same way, as on
   a stem end, a terminal or the end of the bar of t and f).  A 1 pixel step
   whose ends turn opposite ways, or any reshaped (chamfered) corner, is a
   staircase and is smoothed.  Every straight run of 2 or more pixels keeps
   an exactly straight section (on-curve points pinned on the pixel edge,
   neighbouring control points constrained to the run's line, so the curve
   leaves the stem tangentially): a stem's two edges stay straight and
   parallel at their authentic positions.
5. Optimisation.  The free control points minimise the paper's energy:
   smoothness (squared second differences of the control polygon) plus a
   positional term that is quadratic near the source node and turns quartic
   beyond POS_SOFT pixels (solved by iteratively reweighted least squares,
   no random walk, so the output is deterministic).  Where the fitted curve
   would cut away more than 60% of a source pixel (the one-pixel fillets
   of the b12's #, M, &), the positional weight around that pixel is raised
   and the contour refitted.  Pin-hole counters of 2 square pixels or less
   keep their reshaped polygon.
6. Check.  Every glyph is rendered back through PIL at 8x and compared with
   the mask: no source pixel may lose 60% of its ink and no ink may land
   farther than 0.75 px from the mask; report.json lists any glyph that
   fails (none do), with per-glyph IoU and ink-area ratios.

Slanted edges (added at integration, from the judges' notes: Menu's x read
as an hourglass and its / bowed into an S).  A staircase of two or more
steps -- the stair runs of /, v, w, N, or a 45 degree run of single-pixel
treads as on the b12's x and k -- is ONE straight line: the least-squares line
through the midpoints of its steps.  Its nodes slide along that line only,
and a sharp terminal at either end moves along its own run to meet it, so /
is straight from terminal to terminal.  A one-pixel slot (concave both ends)
stays square only beside a stem (the top of m's counters); between single
steps it is the waist of a crossing and is smoothed.  A two-pixel notch sunk
one pixel between two convex corners (the V between the arms of x, the top
of M) becomes a V: its floor is one on-curve point and its sides are the
straight inner edges of the two arms.

Metrics are not touched: units per em = 64 x line height, one source pixel is
64 units, advances are the bitmap advances x 64, ascent/descent are the
baseline and line box of the bitmap; OS/2's x-height and cap height are read
off the masks (the tops of x and H).

Outputs (committed; `--check` writes nothing and fails on any difference,
`make -C src chrome-font-vector`):
  src/engine/fonts/ToriRS{Small,Body,Menu}-Vector.ttf
  res/plugin_chrome/font/ToriRS{Small,Body,Menu}-Vector.{ttf,woff,eot}
  src/engine/torirs_chrome_font_vector_baked.{c,h}  (the TTF bytes as static
      const arrays and each face's alignment zones, for the native rasteriser
      in src/ui/torirs_chrome_bake.c)
Review sheets (not committed; --sheets, default src/build_opt/fontsynth/final/):
  sheet_compare_<face>.png, sheet_glyphs_<face>.png, sheet_panel.png,
  report.json
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import torirs_chrome_fonts as base  # noqa: E402

APPROACH = "depixelize"
U = base.FONT_UNITS_PER_PIXEL

# Crossing resolution.
INK_PRIOR = 40.0
ISLAND_VOTE = 5.0
SPARSE_WINDOW = 8

# Reshaped cells: a resolved crossing splits its corner by this much.
CHAMFER = 0.25
# Straight sections start this far in from a smooth end of a run.
STRAIGHT_TRIM = 0.5
# Loops this small (in square pixels) keep their polygon.
SPECK_AREA = 2.0

# Energy weights.
W_SMOOTH = 1.0
W_POS = 0.08
POS_SOFT = 0.45
IRLS_ITERATIONS = 12
# Local stiffening where a curve would cut a source pixel.
HOT_RADIUS = 1.6
HOT_BOOST = 4.0
HOT_ROUNDS = 5

FACE_TTF = {"Small": "ToriRSSmall-Vector.ttf", "Body": "ToriRSBody-Vector.ttf", "Menu": "ToriRSMenu-Vector.ttf"}
FACE_FAMILY = {
    "Small": ("ToriRS Chrome Small Vector", "Regular", 400),
    "Body": ("ToriRS Chrome Vector", "Regular", 400),
    "Menu": ("ToriRS Chrome Vector", "Bold", 700),
}

# ---------------------------------------------------------------- similarity


def _cross(a, b) -> int:
    return a[0] * b[1] - a[1] * b[0]


class Depixelizer:
    """Pixel similarity graph and crossing resolution of one padded mask."""

    PAD = 4

    def __init__(self, mask: np.ndarray):
        p = self.PAD
        h, w = mask.shape
        self.g = np.zeros((h + 2 * p, w + 2 * p), dtype=bool)
        self.g[p : p + h, p : p + w] = mask
        self.H, self.W = self.g.shape
        # checker[y, x] for the 2x2 block whose top-left pixel is (x, y).
        g = self.g
        a, b, c, d = g[:-1, :-1], g[:-1, 1:], g[1:, :-1], g[1:, 1:]
        self.checker = (a == d) & (b == c) & (a != b)
        self.adj = self._graph()
        self.ink_diag: dict[tuple[int, int], bool] = {}
        self.votes: dict[tuple[int, int], tuple[float, float]] = {}
        self.prior_decided: list[tuple[int, int]] = []
        self._resolve()

    def _graph(self):
        adj: dict[tuple[int, int], set] = {}
        g = self.g
        for y in range(self.H):
            for x in range(self.W):
                s = adj.setdefault((x, y), set())
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    nx, ny = x + dx, y + dy
                    if 0 <= nx < self.W and 0 <= ny < self.H and g[ny, nx] == g[y, x]:
                        s.add((nx, ny))
        for y in range(self.H - 1):
            for x in range(self.W - 1):
                if self.checker[y, x]:
                    adj[(x, y)].add((x + 1, y + 1))
                    adj[(x + 1, y + 1)].add((x, y))
                    adj[(x + 1, y)].add((x, y + 1))
                    adj[(x, y + 1)].add((x + 1, y))
        return adj

    def _curve_length(self, p, q) -> int:
        length = 1
        seen = {p, q}
        for start, prev in ((p, q), (q, p)):
            cur = start
            while len(self.adj[cur]) == 2:
                nxt = next(n for n in sorted(self.adj[cur]) if n != prev)
                if nxt in seen:
                    break
                seen.add(nxt)
                length += 1
                prev, cur = cur, nxt
        return length

    def _component_in_window(self, p, x0, y0) -> int:
        x1, y1 = x0 + SPARSE_WINDOW, y0 + SPARSE_WINDOW
        seen = {p}
        stack = [p]
        while stack:
            cur = stack.pop()
            for n in self.adj[cur]:
                if n not in seen and x0 <= n[0] < x1 and y0 <= n[1] < y1:
                    seen.add(n)
                    stack.append(n)
        return len(seen)

    def _resolve(self):
        g = self.g
        for y in range(self.H - 1):
            for x in range(self.W - 1):
                if not self.checker[y, x]:
                    continue
                if g[y, x]:
                    ink, bg = ((x, y), (x + 1, y + 1)), ((x + 1, y), (x, y + 1))
                else:
                    ink, bg = ((x + 1, y), (x, y + 1)), ((x, y), (x + 1, y + 1))
                v_ink, v_bg = INK_PRIOR, 0.0
                # Curves.
                ci, cb = self._curve_length(*ink), self._curve_length(*bg)
                if ci > cb:
                    v_ink += ci - cb
                else:
                    v_bg += cb - ci
                # Sparse pixels.
                x0, y0 = x - SPARSE_WINDOW // 2 + 1, y - SPARSE_WINDOW // 2 + 1
                si = self._component_in_window(ink[0], x0, y0)
                sb = self._component_in_window(bg[0], x0, y0)
                if si < sb:
                    v_ink += sb - si
                else:
                    v_bg += si - sb
                # Islands.
                if any(len(self.adj[n]) == 1 for n in ink):
                    v_ink += ISLAND_VOTE
                if any(len(self.adj[n]) == 1 for n in bg):
                    v_bg += ISLAND_VOTE
                key = (x + 1 - self.PAD, y + 1 - self.PAD)  # lattice corner, mask coords
                self.ink_diag[key] = v_ink >= v_bg
                if v_ink - INK_PRIOR < v_bg:
                    self.prior_decided.append(key)
                self.votes[key] = (v_ink, v_bg)

    # ------------------------------------------------------------ contours

    def contours(self):
        """Closed boundary loops of the reshaped ink region.

        Returns a list of loops; each loop is a list of turn vertices
        (corner, d_in, d_out, is_crossing) in mask lattice coordinates (y down),
        traversed with ink on the right (clockwise on screen for outer
        contours, anticlockwise for counters).
        """
        g, p = self.g, self.PAD
        out: dict[tuple[int, int], list] = {}
        for y in range(self.H):
            for x in range(self.W):
                if not g[y, x]:
                    continue
                X, Y = x - p, y - p
                if not g[y - 1, x]:
                    out.setdefault((X, Y), []).append(((X, Y), (X + 1, Y)))
                if not g[y, x + 1]:
                    out.setdefault((X + 1, Y), []).append(((X + 1, Y), (X + 1, Y + 1)))
                if not g[y + 1, x]:
                    out.setdefault((X + 1, Y + 1), []).append(((X + 1, Y + 1), (X, Y + 1)))
                if not g[y, x - 1]:
                    out.setdefault((X, Y + 1), []).append(((X, Y + 1), (X, Y)))
        used = set()
        loops = []
        for start_v in sorted(out):
            for e0 in sorted(out[start_v]):
                if e0 in used:
                    continue
                edges = []
                e = e0
                while e not in used:
                    used.add(e)
                    edges.append(e)
                    d_in = (e[1][0] - e[0][0], e[1][1] - e[0][1])
                    cands = out[e[1]]
                    if len(cands) == 1:
                        e = cands[0]
                    else:
                        want_left = self.ink_diag[e[1]]
                        e = next(
                            c
                            for c in cands
                            if (_cross(d_in, (c[1][0] - c[0][0], c[1][1] - c[0][1])) < 0) == want_left
                        )
                verts = []
                n = len(edges)
                for i in range(n):
                    a, b = edges[i - 1], edges[i]
                    d_in = (a[1][0] - a[0][0], a[1][1] - a[0][1])
                    d_out = (b[1][0] - b[0][0], b[1][1] - b[0][1])
                    if d_in != d_out:
                        verts.append((b[0], d_in, d_out, len(out[b[0]]) == 2))
                loops.append(verts)
        return loops


# ---------------------------------------------------------------- fitting


def fit_loop(verts, hot=(), boost=1.0):
    """Turn one reshaped contour into control points and optimise them.

    hot: pixel centres the curve must not cut through; control points within
    HOT_RADIUS of one get their positional weight multiplied by boost.

    Returns (points, flags): points is an (n, 2) array in mask pixel
    coordinates, flags marks on-curve points.
    """
    n = len(verts)
    corner_pt = [np.array(v[0], dtype=float) for v in verts]
    run_len = [int(abs(verts[(i + 1) % n][0][0] - verts[i][0][0]) + abs(verts[(i + 1) % n][0][1] - verts[i][0][1])) for i in range(n)]
    sense = [1 if _cross(v[1], v[2]) > 0 else -1 for v in verts]
    diag = [v[3] for v in verts]

    run_dir = [v[2] for v in verts]  # run i leaves vertex i along d_out

    def cap(i):  # a 1 pixel run whose two ends turn the same way: a U turn
        j = (i + 1) % n
        return run_len[i] == 1 and not diag[i] and not diag[j] and sense[i] == sense[j]

    # Staircase: parallel runs going the same way, offset by single 1 pixel
    # steps that all go the same way.  With two or more steps, every run
    # between two steps 2+ pixels long and at least two runs of 2+ pixels
    # (so a stem between a bar end and a hook, as on f, is not one), that is
    # a slanted edge (the arms of
    # v, the stroke of /, the diagonal of N), not a stem: its runs are not
    # pinned straight and its vertices are smoothed.  One step alone is a
    # notch or a hook (Menu g) and stays square, unless both its runs are
    # 2+ pixels and the chain ends in a cap: the inside of the arms of v.
    stair = [False] * n
    step = [False] * n

    def unit_step(i):
        a, b = (i - 1) % n, (i + 1) % n
        return run_len[i] == 1 and run_dir[a] == run_dir[b] and run_dir[a] != run_dir[i]

    seen = set()
    slant_chains: list[list[int]] = []
    for i in range(n):
        if i in seen or not unit_step(i):
            continue
        # Walk back to the first step of this chain, then forward.
        first = i
        for _ in range(n):
            q = (first - 2) % n
            if q != i and unit_step(q) and run_dir[q] == run_dir[i] and run_dir[(q + 1) % n] == run_dir[(i + 1) % n]:
                first = q
            else:
                break
        chain = [first]
        while True:
            q = (chain[-1] + 2) % n
            if q == first or not unit_step(q) or run_dir[q] != run_dir[first]:
                break
            if run_dir[(q + 1) % n] != run_dir[(first + 1) % n]:
                break
            chain.append(q)
        seen.update(chain)
        interior = [(k + 1) % n for k in chain[:-1]]
        runs = {(k + d) % n for k in chain for d in (-1, 1)}
        before, after = (chain[0] - 2) % n, (chain[-1] + 2) % n
        single_into_cap = (
            len(chain) == 1
            and all(run_len[r] >= 2 for r in runs)
            and (cap(before) or cap(after))
        )
        if single_into_cap or (
            len(chain) >= 2
            and all(run_len[r] >= 2 for r in interior)
            and sum(run_len[r] >= 2 for r in runs) >= 2
        ):
            for k in chain:
                step[k] = True
                for r in ((k - 1) % n, (k + 1) % n):
                    if run_len[r] >= 2:
                        stair[r] = True
            if len(chain) >= 2:
                slant_chains.append((chain, (chain[0] - 1) % n, (chain[-1] + 2) % n))
        elif len(chain) >= 2 and all(run_len[r] <= 2 for r in interior):
            # A 45 degree staircase of single pixels (the arms of the b12's
            # x, the strokes of Small's v and /): the same straight edge.  Its
            # end runs join the line when they are plain single-pixel treads;
            # a cap (a terminal's flat end, the foot of w) or a longer run (a
            # stem leading into it) keeps its own edge and meets the line at
            # the step's corner.
            for k in chain:
                step[k] = True
            r0, r1 = (chain[0] - 1) % n, (chain[-1] + 1) % n
            tread = max(run_len[r] for r in interior)
            for r in interior:
                if run_len[r] >= 2:
                    stair[r] = True
            slant_chains.append(
                (
                    chain,
                    r0 if run_len[r0] <= tread and not cap(r0) else chain[0],
                    (r1 + 1) % n if run_len[r1] <= tread and not cap(r1) else r1,
                )
            )

    # A notch: a run of 2 pixels sunk one pixel between two convex corners
    # (the V between the arms of the b12's x, 1 pixel deep).  Its floor is
    # the point of a V, not a flat: the floor becomes one on-curve point at
    # its middle, and its sides (notch_side) the straight inner edges of the
    # two arms, from their square terminals down to it.
    notch = [False] * n
    notch_side = [False] * n
    for i in range(n):
        a, b = (i - 1) % n, (i + 1) % n
        if (
            run_len[i] == 2
            and not stair[i]
            and sense[i] < 0
            and sense[b] < 0
            and run_len[a] == 1
            and run_len[b] == 1
            and sense[a] > 0
            and sense[(b + 1) % n] > 0
            and not any(diag[v] for v in (a, i, b, (b + 1) % n))
        ):
            notch[i] = True
            notch_side[a] = notch_side[b] = True

    def solid(i):
        """Run i ends in a sharp corner: a stem edge, a cap, or the 1 pixel
        underside of a serif / bar end (a step that leads into a cap)."""
        i %= n
        j = (i + 1) % n
        if run_len[i] >= 2:
            return not stair[i] and not notch[i]
        if cap(i) and sense[i] < 0:
            # A 1 pixel slot (concave both ends) is square when it is the
            # end of a counter between stems (the top of m's counters); one
            # between 1 pixel steps is the waist of a crossing (x) and is
            # smoothed with them.
            a, b = (i - 1) % n, j
            return any(run_len[r] >= 2 and not stair[r] and not notch[r] for r in (a, b))
        if cap(i):
            return True
        # The far end may be a reshaped corner: the side of the last pixel of
        # a 1 pixel diagonal (arms of k, x, y) keeps the terminal square.
        return run_len[i] == 1 and not step[i] and (cap((i - 1) % n) or cap(j))

    # A convex corner between a solid run and a slanted (staircase) edge
    # stays sharp too: square terminals on the arms of v, w, x, y and the
    # ends of /.  (A concave one -- the crotch where a slanted stroke meets
    # a stem, as in N -- stays smooth on its slanted side.)
    slant_run = [False] * n
    for chain, v0, v1 in slant_chains:
        for t in range((v1 - v0) % n):
            slant_run[(v0 + t) % n] = True
    sharp = [
        not diag[i]
        and (
            (solid(i - 1) and solid(i))
            or (sense[i] > 0 and solid(i - 1) and not slant_run[(i - 1) % n] and (stair[i] or notch_side[i] or slant_run[i]))
            or (sense[i] > 0 and (stair[(i - 1) % n] or notch_side[(i - 1) % n] or slant_run[(i - 1) % n]) and solid(i) and not slant_run[i])
        )
        for i in range(n)
    ]

    pts: list[np.ndarray] = []
    on: list[bool] = []
    line_of: list[list] = []  # per point: run directions it must stay on
    owner: list[int] = []  # per point: the vertex whose corner or run made it

    for i in range(n):
        c, d_in, d_out, _ = verts[i]
        c = corner_pt[i]
        din, dout = np.array(d_in, float), np.array(d_out, float)
        if notch[i]:
            pts.append(c + 0.5 * run_len[i] * dout)
            on.append(True)
            line_of.append([])
            owner.append(i)
            continue
        if notch[(i - 1) % n]:
            pass  # the floor's far corner is the V's point, emitted above
        elif diag[i]:
            pts += [c - CHAMFER * din, c + CHAMFER * dout]
            on += [False, False]
            line_of += [[], []]
        else:
            pts.append(c.copy())
            on.append(sharp[i])
            line_of.append([])
        # Straight section along run i.
        j = (i + 1) % n
        if run_len[i] >= 2 and not stair[i] and not notch[i]:
            a = 0.0 if sharp[i] else STRAIGHT_TRIM
            b = run_len[i] - (0.0 if sharp[j] else STRAIGHT_TRIM)
            if a > 0:
                pts.append(c + a * dout)
                on.append(True)
                line_of.append(["S", dout])
            if b < run_len[i]:
                pts.append(c + b * dout)
                on.append(True)
                line_of.append(["E", dout])
        else:
            # A free run keeps a node at every pixel corner, as in the
            # paper's polylines, so the control polygon is evenly spaced.
            for t in range(1, run_len[i]):
                pts.append(c + t * dout)
                on.append(False)
                line_of.append([])
        owner += [i] * (len(pts) - len(owner))

    m = len(pts)
    # A slanted edge (a staircase of two or more steps: /, the arms of v and
    # w, the diagonal of N) is ONE straight line: the least-squares line
    # through the midpoints of its steps.  Every node of the staircase is
    # held on that line (free to slide along it, not off it), and a sharp
    # corner at either end moves along its neighbouring run to meet it, so
    # the edge is straight from terminal to terminal instead of bowing
    # towards the pixel corners near its ends.
    on_line: dict[int, tuple[np.ndarray, np.ndarray]] = {}
    for chain, v0, v1 in slant_chains:
        span = [(v0 + t) % n for t in range((v1 - v0) % n + 1)]
        mids = np.array([corner_pt[k] + 0.5 * np.array(run_dir[k], float) for k in chain])
        centre = mids.mean(axis=0)
        _, _, vt = np.linalg.svd(mids - centre)
        direction = vt[0]
        normal = np.array([-direction[1], direction[0]])
        members = [k for k in range(m) if owner[k] in span[:-1] or (owner[k] == v1 and k == owner.index(v1))]
        for k in members:
            v = owner[k]
            if on[k] and v in (v0, v1) and np.allclose(pts[k], corner_pt[v]):
                # Slide the sharp end along its other run onto the line.
                d = np.array(run_dir[(v - 1) % n] if v == v0 else run_dir[v], float)
                denom = float(normal @ d)
                if abs(denom) < 1e-9:
                    continue
                t = float(normal @ (centre - pts[k])) / denom
                if abs(t) <= 1.0:
                    pts[k] = pts[k] + t * d
                    on_line[k] = (pts[k].copy(), None)
                continue
            if on[k]:
                continue
            pts[k] = pts[k] - float(normal @ (pts[k] - centre)) * normal
            on_line[k] = (pts[k].copy(), direction)

    # A loop of at most two pixels (a pin-hole counter in @ or %, a dot) is
    # kept as its reshaped polygon: smoothing would shrink it to a speck.
    xy = np.array(pts)
    area = 0.5 * abs(np.dot(xy[:, 0], np.roll(xy[:, 1], -1)) - np.dot(xy[:, 1], np.roll(xy[:, 0], -1)))
    if area <= SPECK_AREA:
        return xy, [True] * m
    P0 = np.array(pts)
    # Constraints: off-curve neighbours of a straight-section end stay on the run line.
    cons: list[list[np.ndarray]] = [[] for _ in range(m)]
    for k in range(m):
        if line_of[k] and line_of[k][0] in ("S", "E"):
            d = line_of[k][1]
            nb = (k - 1) % m if line_of[k][0] == "S" else (k + 1) % m
            if not on[nb]:
                cons[nb].append(d)
    basis = []
    for k in range(m):
        if on[k]:
            basis.append(None)
            continue
        if k in on_line:
            basis.append(on_line[k][1].reshape(2, 1))
            continue
        dirs = []
        for d in cons[k]:
            if not any(abs(_cross(d, e)) < 1e-9 for e in dirs):
                dirs.append(d)
        if len(dirs) == 0:
            basis.append(np.eye(2))
        elif len(dirs) == 1:
            basis.append(dirs[0].reshape(2, 1))
        else:
            basis.append(None)  # pinned at the intersection of two runs
    free = [k for k in range(m) if basis[k] is not None]
    if not free:
        return P0, on
    col = {}
    ncol = 0
    for k in free:
        col[k] = ncol
        ncol += basis[k].shape[1]

    stiff = np.ones(m)
    for cx, cy in hot:
        stiff[np.hypot(P0[:, 0] - cx, P0[:, 1] - cy) <= HOT_RADIUS] = boost
    w_pos = W_POS * stiff
    P = P0.copy()
    for _ in range(IRLS_ITERATIONS):
        rows_a, rows_b = [], []

        def add_residual(terms, weight):
            # residual = sum(coef * P[k]) ; P[k] = P0[k] + B_k t_k
            A = np.zeros((2, ncol))
            r0 = np.zeros(2)
            for k, coef in terms:
                r0 += coef * P0[k]
                if basis[k] is not None:
                    B = basis[k]
                    A[:, col[k] : col[k] + B.shape[1]] += coef * B
            s = np.sqrt(weight)
            rows_a.append(s * A)
            rows_b.append(-s * r0)

        for k in free:
            add_residual([((k - 1) % m, 1.0), (k, -2.0), ((k + 1) % m, 1.0)], W_SMOOTH)
            A = np.zeros((2, ncol))
            B = basis[k]
            A[:, col[k] : col[k] + B.shape[1]] = B
            rows_a.append(np.sqrt(w_pos[k]) * A)
            rows_b.append(np.zeros(2))
        A = np.vstack(rows_a)
        b = np.concatenate(rows_b)
        t, *_ = np.linalg.lstsq(A, b, rcond=None)
        P = P0.copy()
        for k in free:
            B = basis[k]
            P[k] = P0[k] + B @ t[col[k] : col[k] + B.shape[1]]
        disp = np.linalg.norm(P - P0, axis=1)
        w_pos = W_POS * stiff * np.maximum(1.0, (disp / POS_SOFT) ** 2)
    return P, on


# ---------------------------------------------------------------- glyphs


def flatten(P, on, steps=8):
    """Polyline of a closed TrueType quadratic contour (for the pixel check)."""
    n = len(P)
    ex = []
    for k in range(n):
        ex.append((P[k], on[k]))
        if not on[k] and not on[(k + 1) % n]:
            ex.append(((P[k] + P[(k + 1) % n]) / 2, True))
    start = next((k for k, (_, o) in enumerate(ex) if o), 0)
    ex = ex[start:] + ex[:start]
    out = []
    k, L = 0, len(ex)
    while k < L:
        p0 = ex[k][0]
        p1, o1 = ex[(k + 1) % L]
        if o1:
            out.append(p0)
            k += 1
        else:
            p2 = ex[(k + 2) % L][0]
            t = np.linspace(0, 1, steps, endpoint=False)[:, None]
            out.extend((1 - t) ** 2 * p0 + 2 * t * (1 - t) * p1 + t * t * p2)
            k += 2
    return np.array(out)


def pixel_coverage(loops, w, h, k=8):
    """Fraction of each mask pixel inked by the loops (even-odd, k x k samples)."""
    ys, xs = np.mgrid[0 : h * k, 0 : w * k]
    sx = (xs.ravel() + 0.5) / k
    sy = (ys.ravel() + 0.5) / k
    inside = np.zeros(sx.shape, bool)
    for P, on in loops:
        poly = flatten(P, on)
        a, b = poly, np.roll(poly, -1, axis=0)
        for (x0, y0), (x1, y1) in zip(a, b):
            if y0 == y1:
                continue
            crosses = (y0 > sy) != (y1 > sy)
            xi = x0 + (sy - y0) * (x1 - x0) / (y1 - y0)
            inside ^= crosses & (sx < xi)
    return inside.reshape(h, k, w, k).mean(axis=(1, 3))


def glyph_contours(font: base.SourceFont, index: int, masks):
    """Fit every contour; where the smooth curve would cut a source pixel
    (keep < FIDELITY_PIXEL_KEEP), stiffen the positional energy of the
    control points around that pixel and refit (the paper's positional term
    is what keeps the curve on its pixels; this makes it local)."""
    w, h = font.widths[index], font.heights[index]
    if not (w and h):
        return [], {}
    mask = np.frombuffer(masks[index], dtype=np.uint8).reshape(h, w) > 0
    dp = Depixelizer(mask)
    contours = dp.contours()
    hot: list[tuple[float, float]] = []
    boost = 1.0
    for attempt in range(HOT_ROUNDS + 1):
        loops = [fit_loop(verts, hot, boost) for verts in contours]
        cov = pixel_coverage(loops, w, h)
        lost = [(x + 0.5, y + 0.5) for y, x in zip(*np.nonzero(mask & (cov < FIDELITY_PIXEL_KEEP)))]
        if not lost or attempt == HOT_ROUNDS:
            break
        hot = sorted(set(hot) | set(lost))
        boost *= HOT_BOOST
    bg_wins = sorted(k for k, v in dp.ink_diag.items() if not v)
    return loops, {
        "crossings": len(dp.ink_diag),
        "background_kept": bg_wins,
        "ink_prior_decided": sorted(dp.prior_decided),
        "stiffened_pixels": [(int(x), int(y)) for x, y in hot],
        "lost_pixels_after_fit": [(int(x), int(y)) for x, y in lost],
    }


def to_units(font, index, P):
    x = (font.offset_x[index] + P[:, 0]) * U
    y = (font.line_height - font.offset_y[index] - P[:, 1]) * U
    return np.round(np.stack([x, y], axis=1)).astype(int)


def draw_contour(pen, pts, on):
    """Emit one closed TrueType contour from points + on-curve flags."""
    pts = [tuple(int(v) for v in p) for p in pts]
    # Drop consecutive duplicates.
    clean = []
    for p, o in zip(pts, on):
        if clean and clean[-1][0] == p:
            clean[-1] = (p, clean[-1][1] or o)
            continue
        clean.append((p, o))
    while len(clean) > 1 and clean[0][0] == clean[-1][0]:
        p, o = clean.pop()
        clean[0] = (p, clean[0][1] or o)
    if len(clean) < 3:
        return False
    first_on = next((i for i, (_, o) in enumerate(clean) if o), None)
    if first_on is None:
        pen.qCurveTo(*[p for p, _ in clean], None)
        pen.closePath()
        return True
    seq = clean[first_on:] + clean[:first_on]
    pen.moveTo(seq[0][0])
    offs = []
    for p, o in seq[1:] + [seq[0]]:
        if o:
            if offs:
                pen.qCurveTo(*offs, p)
                offs = []
            else:
                pen.lineTo(p)
        else:
            offs.append(p)
    pen.closePath()
    return True


def build_vector_ttf(font: base.SourceFont, destination: Path, digest: str):
    from fontTools.fontBuilder import FontBuilder
    from fontTools.pens.ttGlyphPen import TTGlyphPen
    from fontTools.ttLib import newTable
    from fontTools.ttLib.tables.O_S_2f_2 import Panose

    family, style, weight = FACE_FAMILY[font.symbol]
    assert font.css_size == font.line_height
    upm = font.line_height * U
    ascent = font.line_height * U
    descent = -(font.line_box - font.line_height) * U
    masks = base._source_masks(font)
    source_index = {cp: i for i, cp in enumerate(base.SOURCE_CODEPOINTS)}
    codepoints = tuple(range(0x20, 0x7F)) + (0x00A3,)
    order = [".notdef"]
    cmap = {}
    glyphs = {}
    hmtx = {}
    notes = {}
    pen = TTGlyphPen(None)
    glyphs[".notdef"] = pen.glyph()
    hmtx[".notdef"] = (font.advances[-1] * U, 0)
    for cp in codepoints:
        name = {0x20: "space", 0x60: "grave", 0x7C: "bar", 0xA3: "sterling"}.get(cp, f"uni{cp:04X}")
        cmap[cp] = name
        order.append(name)
        pen = TTGlyphPen(None)
        idx = source_index.get(cp)
        xmin = None
        if idx is not None and font.widths[idx] and font.heights[idx]:
            loops, info = glyph_contours(font, idx, masks)
            dropped = 0
            for P, on in loops:
                pu = to_units(font, idx, P)
                if draw_contour(pen, pu, on):
                    xmin = pu[:, 0].min() if xmin is None else min(xmin, pu[:, 0].min())
                else:
                    dropped += 1
            info["contours"] = len(loops) - dropped
            info["dropped"] = dropped
            notes[chr(cp)] = info
        glyphs[name] = pen.glyph()
        adv = font.advances[idx] if idx is not None else font.advances[-1]
        hmtx[name] = (adv * U, int(xmin) if xmin is not None else 0)

    fb = FontBuilder(upm, isTTF=True)
    fb.setupGlyphOrder(order)
    fb.setupCharacterMap(cmap)
    fb.setupGlyf(glyphs)
    fb.setupHorizontalMetrics(hmtx)
    fb.setupHorizontalHeader(ascent=ascent, descent=descent, lineGap=0)
    ps = base._postscript_name(family, style)
    fb.setupNameTable(
        {
            "familyName": family,
            "styleName": style,
            "uniqueFontIdentifier": f"ToriRS:{ps}:1.000:{digest[:16]}",
            "fullName": family if style == "Regular" else f"{family} {style}",
            "psName": ps,
            "version": "Version 1.000",
            "manufacturer": "ToriRS",
            "description": f"Depixelized vector synthesis of ToriRSChrome {font.symbol} (cache archive {font.archive_id}).",
        }
    )
    panose = Panose()
    panose.bFamilyType = 2
    panose.bWeight = 8 if weight >= 700 else 5
    fb.setupOS2(
        version=3,
        usWeightClass=weight,
        usWidthClass=5,
        fsType=0,
        panose=panose,
        achVendID="TRRS",
        fsSelection=0x20 if weight >= 700 else 0x40,
        usFirstCharIndex=0x20,
        usLastCharIndex=0xA3,
        sTypoAscender=ascent,
        sTypoDescender=descent,
        sTypoLineGap=0,
        usWinAscent=ascent,
        usWinDescent=-descent,
        sxHeight=face_zones(font)["x_height"] * U,
        sCapHeight=face_zones(font)["cap_height"] * U,
        yStrikeoutSize=U,
        yStrikeoutPosition=ascent * 7 // 10,
        ulCodePageRange1=1,
    )
    fb.setupPost(underlinePosition=-U, underlineThickness=U, keepGlyphNames=True)
    fb.setupMaxp()
    fb.setupHead(
        unitsPerEm=upm,
        created=base.MAC_EPOCH_AT_UNIX_EPOCH,
        modified=base.MAC_EPOCH_AT_UNIX_EPOCH,
        flags=0x0003,
        macStyle=1 if weight >= 700 else 0,
        lowestRecPPEM=6,
        fontDirectionHint=2,
    )
    # Smooth (unhinted grey) at every size: this face is for fractional scales.
    gasp = newTable("gasp")
    gasp.version = 1
    gasp.gaspRange = {0xFFFF: 0x000A}
    fb.font["gasp"] = gasp
    # An empty 'prep' marks the font as natively hinted so FreeType runs the
    # (no-op) bytecode path instead of the auto-hinter: the outlines render
    # exactly as synthesized.
    prep = newTable("prep")
    from fontTools.ttLib.tables import ttProgram

    prep.program = ttProgram.Program()
    prep.program.fromBytecode(b"")
    fb.font["prep"] = prep
    fb.font.recalcTimestamp = False
    fb.font.recalcBBoxes = True
    destination.parent.mkdir(parents=True, exist_ok=True)
    fb.save(destination)
    return notes


# A vectorisation is clean when it loses no source pixel (each mask pixel
# keeps at least FIDELITY_PIXEL_KEEP of its square inked) and puts no ink
# farther than FIDELITY_STRAY_PX from the mask.  IoU and area are reported
# for information only: a smoothed staircase necessarily differs from it.
FIDELITY_PIXEL_KEEP = 0.40
FIDELITY_STRAY_PX = 0.75


def glyph_fidelity(font: base.SourceFont, path: Path):
    """Per glyph, the vector glyph rendered through PIL at 8x against the 8x
    nearest mask: lost source pixels, stray ink, IoU, and outline area /
    mask area (from the outline itself, which also checks the winding)."""
    from fontTools.pens.areaPen import AreaPen
    from fontTools.ttLib import TTFont
    from PIL import Image, ImageDraw, ImageFont

    tt = TTFont(path)
    gs = tt.getGlyphSet()
    cmap = tt.getBestCmap()
    k = 8
    pil = ImageFont.truetype(str(path), size=font.line_height * k, layout_engine=ImageFont.Layout.BASIC)
    masks = base._source_masks(font)
    result = {}
    for i, cp in enumerate(base.SOURCE_CODEPOINTS):
        w, h = font.widths[i], font.heights[i]
        if not (w and h):
            continue
        m = np.frombuffer(masks[i], np.uint8).reshape(h, w) > 0
        pen = AreaPen(gs)
        gs[cmap[cp]].draw(pen)
        # AreaPen is positive for anticlockwise; TrueType ink is clockwise.
        area = -pen.value / (U * U)
        ref = np.zeros((font.line_box * k, max(font.advances[i], font.offset_x[i] + w) * k + 2 * k), bool)
        big = np.kron(m, np.ones((k, k), bool))
        ref[font.offset_y[i] * k : (font.offset_y[i] + h) * k, font.offset_x[i] * k : (font.offset_x[i] + w) * k] = big
        im = Image.new("L", (ref.shape[1], ref.shape[0]), 0)
        ImageDraw.Draw(im).text((0, font.line_height * k), chr(cp), font=pil, fill=255, anchor="ls")
        vec = np.asarray(im) >= 128
        iou = float((vec & ref).sum() / max(1, (vec | ref).sum()))
        cov = (np.asarray(im, float) / 255.0)[
            font.offset_y[i] * k : (font.offset_y[i] + h) * k, font.offset_x[i] * k : (font.offset_x[i] + w) * k
        ]
        cov = cov.reshape(h, k, w, k).mean(axis=(1, 3))
        lost = [(int(x), int(y)) for y, x in zip(*np.nonzero(m & (cov < FIDELITY_PIXEL_KEEP)))]
        r = int(round(FIDELITY_STRAY_PX * k))
        near = np.zeros_like(ref)
        yy, xx = np.nonzero(ref)
        for dy in range(-r, r + 1):
            for dx in range(-r, r + 1):
                if dx * dx + dy * dy <= r * r:
                    near[np.clip(yy + dy, 0, ref.shape[0] - 1), np.clip(xx + dx, 0, ref.shape[1] - 1)] = True
        stray = int((vec & ~near).sum())
        result[chr(cp)] = {
            "lost_pixels": lost,
            "stray_samples_8x": stray,
            "mask_px": int(m.sum()),
            "outline_area_px": round(area, 3),
            "area_ratio": round(area / m.sum(), 3),
            "iou_8x": round(iou, 3),
        }
    return result


def verify_ttf(font: base.SourceFont, path: Path, metrics_text: str):
    import re

    from fontTools.ttLib import TTFont

    tt = TTFont(path)
    cmap = tt.getBestCmap()
    hmtx = tt["hmtx"]
    src = {cp: i for i, cp in enumerate(base.SOURCE_CODEPOINTS)}
    table = re.search(
        rf"ToriRSChromeFont_{font.symbol}_advance_px\[256\]\s*=\s*\{{(.*?)\}};", metrics_text, re.S
    )
    header_adv = tuple(int(v) for v in re.findall(r"-?\d+", table.group(1))) if table else None
    bad = []
    for cp, name in cmap.items():
        want = font.advances[src[cp]] if cp in src else font.advances[-1]
        if hmtx[name][0] != want * U:
            bad.append(chr(cp))
        if header_adv and cp < 256 and cp != 0x60 and header_adv[cp] != want:
            bad.append(f"header:{chr(cp)}")
    hh = tt["hhea"]
    return {
        "units_per_em": tt["head"].unitsPerEm,
        "ascent_units": hh.ascent,
        "descent_units": hh.descent,
        "baseline_px": font.line_height,
        "line_box_px": font.line_box,
        "metrics_ok": tt["head"].unitsPerEm == font.line_height * U
        and hh.ascent == font.line_height * U
        and hh.descent == -(font.line_box - font.line_height) * U,
        "advances_checked": len(cmap),
        "advance_mismatches": bad,
        "advances_verified_equal": not bad,
        "bytes": path.stat().st_size,
        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
    }


# ---------------------------------------------------------------- sheets

SAMPLE = "The quick brown fox jumps over the lazy dog 0123456789 Oak tree (stump) Members only 1 to 100 of 63,307 Ctrl+P [Q] rotate"
SCALES = (1.0, 1.25, 1.5, 1.75, 2.0, 3.0)


class BitmapFace:
    def __init__(self, font: base.SourceFont):
        self.f = font
        self.masks = base._source_masks(font)
        self.index = {cp: i for i, cp in enumerate(base.SOURCE_CODEPOINTS)}

    def width(self, text):
        return sum(self.f.advances[self.index[ord(c)]] if ord(c) in self.index else self.f.advances[-1] for c in text)

    def render(self, text):
        """1x alpha image (line box tall) of text."""
        from PIL import Image

        f = self.f
        img = np.zeros((f.line_box, max(1, self.width(text))), np.uint8)
        x = 0
        for ch in text:
            i = self.index.get(ord(ch))
            if i is None:
                x += f.advances[-1]
                continue
            w, h = f.widths[i], f.heights[i]
            if w and h:
                m = np.frombuffer(self.masks[i], np.uint8).reshape(h, w)
                y0, x0 = f.offset_y[i], x + f.offset_x[i]
                img[y0 : y0 + h, x0 : x0 + w] = np.maximum(img[y0 : y0 + h, x0 : x0 + w], m)
            x += f.advances[i]
        return Image.fromarray(img, "L")

    def render_scaled(self, text, s):
        from PIL import Image

        im = self.render(text)
        return im.resize((max(1, round(im.width * s)), max(1, round(im.height * s))), Image.NEAREST)


_ADVANCES: dict[str, tuple[int, dict[str, int]]] = {}


def _ttf_advances(ttf: Path):
    """(units per em, {char: advance units}) read back from the TTF's hmtx."""
    key = str(ttf)
    if key not in _ADVANCES:
        from fontTools.ttLib import TTFont

        tt = TTFont(ttf)
        cmap = tt.getBestCmap()
        _ADVANCES[key] = (tt["head"].unitsPerEm, {chr(cp): tt["hmtx"][g][0] for cp, g in cmap.items()})
    return _ADVANCES[key]


def vector_alpha(ttf: Path, text, px, line_height, line_box, s):
    """Alpha image (line_box * s tall) of text rendered from the TTF via PIL.

    Each glyph is drawn by PIL/FreeType at its exact fractional pen position
    from the TTF's own hmtx advances (the chrome lays text out in 1x pixels
    and scales; PIL's string layout would round every advance to a whole
    device pixel and drift away from that layout).
    """
    from PIL import Image, ImageDraw, ImageFont

    font = ImageFont.truetype(str(ttf), size=px, layout_engine=ImageFont.Layout.BASIC)
    upm, adv = _ttf_advances(ttf)
    width = int(np.ceil(sum(adv[c] for c in text) * px / upm)) + 4
    im = Image.new("L", (width, int(np.ceil(line_box * s)) + 1), 0)
    d = ImageDraw.Draw(im)
    pen = 0
    for ch in text:
        if ch != " ":
            d.text((pen * px / upm, line_height * s), ch, font=font, fill=255, anchor="ls")
        pen += adv[ch]
    return im


def paste_text(canvas, alpha, xy, rgb, shadow=None):
    from PIL import Image

    if shadow is not None:
        canvas.paste(Image.new("RGB", alpha.size, shadow), (xy[0] + 1, xy[1] + 1), alpha)
    canvas.paste(Image.new("RGB", alpha.size, rgb), xy, alpha)


def magnify(im, k=2):
    from PIL import Image

    return im.resize((im.width * k, im.height * k), Image.NEAREST)


def sheet_compare(out: Path, face: str, font: base.SourceFont, ttf: Path, label: BitmapFace):
    from PIL import Image

    bmp = BitmapFace(font)
    rows = []
    for s in SCALES:
        px = font.line_height * s
        v = vector_alpha(ttf, SAMPLE, px, font.line_height, font.line_box, s)
        n = bmp.render_scaled(SAMPLE, s)
        rows.append((s, px, v, n))
    lab_w = 112
    W = lab_w + max(max(v.width, n.width) for _, _, v, n in rows) + 8
    H = sum(v.height + n.height + 14 for _, _, v, n in rows) + 8
    canvas = Image.new("RGB", (W, H), (255, 255, 255))
    y = 4
    for s, px, v, n in rows:
        for alpha, tag, col in ((v, "vector", (0, 0, 0)), (n, "nearest", (0, 0, 0))):
            lab = label.render(f"{s:g}x {px:g}px {tag}")
            paste_text(canvas, lab, (4, y + max(0, (alpha.height - lab.height) // 2)), (120, 120, 120))
            paste_text(canvas, alpha, (lab_w, y), col)
            y += alpha.height + 2
        canvas.paste((220, 220, 220), (0, y + 3, W, y + 4))
        y += 10
    magnify(canvas).save(out / f"sheet_compare_{face}.png", optimize=True)


def sheet_glyphs(out: Path, face: str, font: base.SourceFont, ttf: Path, label: BitmapFace):
    from PIL import Image, ImageDraw, ImageFont

    k = 4
    vfont = ImageFont.truetype(str(ttf), size=font.line_height * k, layout_engine=ImageFont.Layout.BASIC)
    masks = base._source_masks(font)
    cols = 8
    adv_max = max(font.advances)
    cell_w = adv_max * k + 8
    cell_h = font.line_box * k + 6
    pair_w = 2 * cell_w + 26
    n = len(base.SOURCE_CODEPOINTS)
    nrows = (n + cols - 1) // cols
    W = cols * pair_w + 8
    H = nrows * (cell_h + 16) + 8
    canvas = Image.new("RGB", (W, H), (255, 255, 255))
    d = ImageDraw.Draw(canvas)
    for i, cp in enumerate(base.SOURCE_CODEPOINTS):
        r, c = divmod(i, cols)
        x0 = 4 + c * pair_w
        y0 = 4 + r * (cell_h + 16)
        paste_text(canvas, label.render(f"{chr(cp)} {cp:02X}"), (x0, y0), (120, 120, 120))
        y0 += 12
        for j in range(2):
            bx = x0 + j * (cell_w + 4)
            d.rectangle((bx, y0, bx + cell_w - 1, y0 + font.line_box * k - 1), fill=(244, 244, 238))
            base_y = y0 + font.line_height * k
            d.line((bx, base_y, bx + cell_w - 1, base_y), fill=(255, 190, 190))
            d.line((bx + font.advances[i] * k, y0, bx + font.advances[i] * k, y0 + font.line_box * k - 1), fill=(190, 190, 255))
        w, h = font.widths[i], font.heights[i]
        if w and h:
            m = np.frombuffer(masks[i], np.uint8).reshape(h, w)
            im = Image.fromarray(m, "L").resize((w * k, h * k), Image.NEAREST)
            canvas.paste((0, 0, 0), (x0 + font.offset_x[i] * k, y0 + font.offset_y[i] * k, x0 + (font.offset_x[i] + w) * k, y0 + (font.offset_y[i] + h) * k), im)
        a = Image.new("L", (cell_w, font.line_box * k), 0)
        ImageDraw.Draw(a).text((0, font.line_height * k), chr(cp), font=vfont, fill=255, anchor="ls")
        canvas.paste((0, 0, 0), (x0 + cell_w + 4, y0), a)
    canvas.save(out / f"sheet_glyphs_{face}.png", optimize=True)


PANEL_BROWN = (0x5D, 0x54, 0x47)
ORANGE = (0xFF, 0x98, 0x1F)
WHITE = (0xFF, 0xFF, 0xFF)
YELLOW = (0xFF, 0xFF, 0x00)
BLACK = (0, 0, 0)


def sheet_panel(out: Path, fonts, ttfs, label: BitmapFace):
    from PIL import Image, ImageDraw

    s = 1.5
    # (face, text, colour, x, y) in 1x layout pixels (y = top of the line box).
    items = [
        ("Menu", "Catalog (F8)", (0xD8, 0xD0, 0xC0), 6, 3),
        ("Body", "Search", ORANGE, 8, 24),
        ("Body", "oak", WHITE, 128, 24),
        ("Body", "Layer", ORANGE, 8, 45),
        ("Body", "Wall", ORANGE, 170, 45),
        ("Body", "Members only", WHITE, 40, 66),
        ("Body", "Show hidden", WHITE, 40, 87),
        ("Body", "1 to 100 of 63,307", WHITE, 8, 108),
        ("Body", "Selected", ORANGE, 12, 132),
        ("Body", "1276 Oak tree", WHITE, 166, 132),
        ("Body", "1276  Oak tree", WHITE, 14, 157),
        ("Body", "1277  Oak tree (stump)", WHITE, 14, 175),
        ("Body", "1278  Willow", WHITE, 14, 193),
        ("Body", "Add to ...", WHITE, 12, 254),
        ("Small", "Ctrl+P", YELLOW, 84, 256),
    ]
    pw, ph = 276, 276
    PW, PH = int(pw * s), int(ph * s)
    canvas = Image.new("RGB", (2 * PW + 30, PH + 34), (30, 28, 24))
    titles = ("vector 1.5x (this face)", "bitmap nearest 1.5x (reference)")
    for side in range(2):
        ox = 10 + side * (PW + 10)
        oy = 24
        paste_text(canvas, label.render(titles[side]), (ox, 6), (220, 220, 220))
        d = ImageDraw.Draw(canvas)
        d.rectangle((ox, oy, ox + PW - 1, oy + PH - 1), fill=PANEL_BROWN)
        d.rectangle((ox, oy, ox + PW - 1, oy + int(18 * s)), fill=(0x2A, 0x26, 0x20))
        d.rectangle((ox + int(4 * s), oy + int(124 * s), ox + PW - int(4 * s), oy + int(146 * s)), fill=(0x3E, 0x37, 0x2D))
        d.rectangle((ox + int(4 * s), oy + int(152 * s), ox + PW - int(4 * s), oy + int(246 * s)), fill=(0x4A, 0x42, 0x37))
        d.rectangle((ox + int(80 * s), oy + int(254 * s), ox + int(116 * s), oy + int(266 * s)), fill=BLACK)
        for face, text, col, x, y in items:
            f = fonts[face]
            if side == 0:
                a = vector_alpha(ttfs[face], text, f.line_height * s, f.line_height, f.line_box, s)
            else:
                a = BitmapFace(f).render_scaled(text, s)
            shadow = None if face == "Small" else BLACK
            paste_text(canvas, a, (ox + round(x * s), oy + round(y * s)), col, shadow)
    magnify(canvas).save(out / "sheet_panel.png", optimize=True)


# ---------------------------------------------------------------- zones


def face_zones(font: base.SourceFont) -> dict[str, int]:
    """The face's vertical alignment zones, in 1x pixels above the baseline
    (the descender negative), read off the masks: the bottom of p, the
    baseline, the top of x, of H, and of d.  The native rasteriser snaps
    each to a whole device pixel (src/ui/torirs_chrome_bake.c)."""
    idx = {cp: i for i, cp in enumerate(base.SOURCE_CODEPOINTS)}

    def top(ch):
        return font.line_height - font.offset_y[idx[ord(ch)]]

    def bottom(ch):
        i = idx[ord(ch)]
        return font.line_height - (font.offset_y[i] + font.heights[i])

    return {
        "descender": bottom("p"),
        "baseline": 0,
        "x_height": top("x"),
        "cap_height": top("H"),
        "ascender": max(top("d"), top("H")),
    }


# ---------------------------------------------------------------- the bake

C_FACES = ("Small", "Body", "Menu")
C_PATH = "src/engine/torirs_chrome_font_vector_baked.c"
H_PATH = "src/engine/torirs_chrome_font_vector_baked.h"
TTF_DIR = "src/engine/fonts"
WEB_DIR = "res/plugin_chrome/font"


def c_header() -> str:
    return f"""/*
 * GENERATED by tools/chrome_font_vector.py -- do not edit; regenerate with
 * `python3 tools/chrome_font_vector.py` (`make -C src chrome-font-vector`
 * checks that a rerun is byte-identical).
 *
 * The chrome's synthesized VECTOR faces (approach: {APPROACH}, Kopf and
 * Lischinski 2011 adapted to 1-bit glyph masks), synthesized from the baked
 * 1x masks in torirs_debug_font_baked.c and nothing else. Each is a whole
 * TrueType file (also committed as src/engine/fonts/ToriRS<Face>-Vector.ttf):
 * units per em = 64 x the face's baseline, one 1x pixel = 64 units, every
 * advance = the bitmap advance x 64, ascent and descent = the baseline and
 * the line box. src/ui/torirs_chrome_bake.c rasterises them (stb_truetype)
 * at a fractional chrome scale, and at every scale under "smooth always".
 *
 * The arrays are `static const` and live for the life of the process.
 */
#ifndef TORIRS_CHROME_FONT_VECTOR_BAKED_H
#define TORIRS_CHROME_FONT_VECTOR_BAKED_H

/** The most alignment zones a face carries. */
#define TORIRS_CHROME_FONT_VECTOR_ZONES_MAX 5

struct ToriRSChromeFontVector
{{
    /** The TrueType file. */
    unsigned char const* ttf;
    int ttf_size;
    /** Font units per 1x pixel (64): units per em is this x baseline_px. */
    int units_per_px;
    /** The bitmap face's baseline (its ascent) and line box, 1x pixels. */
    int baseline_px;
    int line_box_px;
    /**
     * The vertical alignment zones, 1x pixels above the baseline, ascending
     * (the descender negative): the bottom of p, the baseline, the top of x,
     * of H, and of d, duplicates dropped. The rasteriser lands each on a
     * whole device pixel and scales linearly between them.
     */
    int zone_count;
    int zones[TORIRS_CHROME_FONT_VECTOR_ZONES_MAX];
}};

struct ToriRSChromeFontVector const*
ToriRSChromeFontVector_Small(void);
struct ToriRSChromeFontVector const*
ToriRSChromeFontVector_Body(void);
struct ToriRSChromeFontVector const*
ToriRSChromeFontVector_Menu(void);

#endif /* TORIRS_CHROME_FONT_VECTOR_BAKED_H */
"""


def c_source(fonts, ttf_bytes) -> str:
    out = [
        "/*",
        " * GENERATED by tools/chrome_font_vector.py -- do not edit. See",
        " * torirs_chrome_font_vector_baked.h.",
        " *",
    ]
    for sym in C_FACES:
        out.append(f" * ToriRS{sym}-Vector.ttf  {len(ttf_bytes[sym])} bytes  sha256 {hashlib.sha256(ttf_bytes[sym]).hexdigest()}")
    out += [" */", '#include "torirs_chrome_font_vector_baked.h"', ""]
    for sym in C_FACES:
        data = ttf_bytes[sym]
        out.append(f"static const unsigned char k_{sym.lower()}_ttf[{len(data)}] = {{")
        for i in range(0, len(data), 16):
            out.append("    " + ", ".join(f"0x{b:02x}" for b in data[i : i + 16]) + ",")
        out.append("};")
        out.append("")
    for sym in C_FACES:
        f = fonts[sym]
        z = sorted(set(face_zones(f).values()))
        assert len(z) <= 5
        zs = ", ".join(str(v) for v in z)
        out += [
            f"static const struct ToriRSChromeFontVector k_{sym.lower()} = {{",
            f"    k_{sym.lower()}_ttf, (int)sizeof(k_{sym.lower()}_ttf), {U}, {f.line_height}, {f.line_box}, {len(z)}, {{ {zs} }}",
            "};",
            "",
            "struct ToriRSChromeFontVector const*",
            f"ToriRSChromeFontVector_{sym}(void)",
            "{",
            f"    return &k_{sym.lower()};",
            "}",
            "",
        ]
    return "\n".join(out)


# ---------------------------------------------------------------- main


def main(argv=None) -> int:
    import tempfile

    repo = Path(__file__).resolve().parents[1]
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--source", type=Path, default=repo / "src/engine/torirs_debug_font_baked.c")
    ap.add_argument("--metrics", type=Path, default=repo / "src/ui/uitree_debug_font_metrics.h")
    ap.add_argument(
        "--sheets",
        type=Path,
        default=repo / "src/build_opt/fontsynth/final",
        help="where the review sheets and report.json go (not committed)",
    )
    ap.add_argument("--no-sheets", action="store_true")
    ap.add_argument(
        "--check",
        action="store_true",
        help="write nothing committed; fail when a rerun differs from what is committed",
    )
    args = ap.parse_args(argv)

    c_text = args.source.read_text(encoding="utf-8")
    metrics_text = args.metrics.read_text(encoding="utf-8")
    digest = hashlib.sha256(args.source.read_bytes()).hexdigest()
    fonts = {sym: base.parse_source_font(c_text, metrics_text, sym) for sym in C_FACES}
    sheets = args.sheets
    sheets.mkdir(parents=True, exist_ok=True)
    report = {
        "approach": "depixelize (Kopf-Lischinski 2011, 1-bit adaptation)",
        "generator": "tools/chrome_font_vector.py",
        "source_sha256": digest,
        "parameters": {
            "ink_prior": INK_PRIOR,
            "island_vote": ISLAND_VOTE,
            "sparse_window": SPARSE_WINDOW,
            "chamfer_px": CHAMFER,
            "straight_trim_px": STRAIGHT_TRIM,
            "speck_area_px2": SPECK_AREA,
            "w_smooth": W_SMOOTH,
            "w_pos": W_POS,
            "pos_soft_px": POS_SOFT,
            "irls_iterations": IRLS_ITERATIONS,
            "hot_radius_px": HOT_RADIUS,
            "hot_boost": HOT_BOOST,
            "hot_rounds": HOT_ROUNDS,
        },
        "faces": {},
    }
    outputs: dict[str, bytes] = {}
    ttf_bytes: dict[str, bytes] = {}
    failed = False
    with tempfile.TemporaryDirectory() as tmp_name:
        tmp = Path(tmp_name)
        ttfs = {}
        for sym, font in fonts.items():
            ttf = tmp / FACE_TTF[sym]
            notes = build_vector_ttf(font, ttf, digest)
            ttfs[sym] = ttf
            v = verify_ttf(font, ttf, metrics_text)
            fidelity = glyph_fidelity(font, ttf)
            unclean = {}
            for ch, n in notes.items():
                fd = fidelity[ch]
                why = []
                if n["dropped"]:
                    why.append(f"{n['dropped']} degenerate contour(s) dropped")
                if fd["lost_pixels"]:
                    why.append(f"source pixels lost: {fd['lost_pixels']}")
                if fd["stray_samples_8x"]:
                    why.append(f"{fd['stray_samples_8x']} 8x samples of ink beyond {FIDELITY_STRAY_PX}px of the mask")
                if fd["outline_area_px"] <= 0:
                    why.append("outline winding inverted")
                if why:
                    unclean[ch] = why
            if unclean or not v["advances_verified_equal"] or not v["metrics_ok"]:
                failed = True
            bgk = {ch: n["background_kept"] for ch, n in notes.items() if n["background_kept"]}
            report["faces"][sym] = {
                "archive": font.archive_id,
                "ttf": ttf.name,
                "zones_px": face_zones(font),
                "glyph_count": sum(1 for i in range(len(base.SOURCE_CODEPOINTS)) if font.widths[i] and font.heights[i]),
                "cmap_entries": v["advances_checked"],
                **v,
                "crossings_resolved": sum(n["crossings"] for n in notes.values()),
                "crossings_kept_background": bgk,
                "crossings_decided_by_ink_prior": {ch: n["ink_prior_decided"] for ch, n in notes.items() if n["ink_prior_decided"]},
                "not_vectorised_cleanly": unclean,
                "fidelity_thresholds": {"pixel_keep_min": FIDELITY_PIXEL_KEEP, "stray_px": FIDELITY_STRAY_PX},
                "area_ratio_mean": round(sum(v["area_ratio"] for v in fidelity.values()) / len(fidelity), 3),
                "fidelity_worst_iou": sorted(((v["iou_8x"], ch) for ch, v in fidelity.items()))[:5],
                "fidelity": fidelity,
            }
            print(
                f"{sym}: {ttf.name} {v['bytes']} B, advances equal={v['advances_verified_equal']}, "
                f"metrics ok={v['metrics_ok']}, unclean={list(unclean)}"
            )
            stem = FACE_TTF[sym][: -len(".ttf")]
            ttf_bytes[sym] = ttf.read_bytes()
            woff = tmp / f"{stem}.woff"
            eot = tmp / f"{stem}.eot"
            base.write_woff(ttf, woff, False)
            base.write_eot(ttf, eot)
            outputs[f"{TTF_DIR}/{stem}.ttf"] = ttf_bytes[sym]
            outputs[f"{WEB_DIR}/{stem}.ttf"] = ttf_bytes[sym]
            outputs[f"{WEB_DIR}/{stem}.woff"] = woff.read_bytes()
            outputs[f"{WEB_DIR}/{stem}.eot"] = eot.read_bytes()
        outputs[C_PATH] = c_source(fonts, ttf_bytes).encode("utf-8")
        outputs[H_PATH] = c_header().encode("utf-8")
        if not args.no_sheets:
            label = BitmapFace(fonts["Small"])
            for sym, font in fonts.items():
                sheet_compare(sheets, sym, font, ttfs[sym], label)
                sheet_glyphs(sheets, sym, font, ttfs[sym], label)
            sheet_panel(sheets, fonts, ttfs, label)
    (sheets / "report.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    if failed:
        print("a face failed its checks: see report.json", file=sys.stderr)
        return 1
    if args.check:
        stale = [rel for rel, data in sorted(outputs.items()) if not (repo / rel).is_file() or (repo / rel).read_bytes() != data]
        for rel in stale:
            print(f"differs from a rerun: {rel}", file=sys.stderr)
        if stale:
            print("run python3 tools/chrome_font_vector.py and commit what it writes", file=sys.stderr)
            return 1
        print(f"chrome-font-vector: {len(outputs)} files reproduce byte for byte")
        return 0
    for rel, data in sorted(outputs.items()):
        dest = repo / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(data)
    print(f"wrote {len(outputs)} files; sheets in {sheets}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
