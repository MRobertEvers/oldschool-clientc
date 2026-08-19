"""Generate the seam-rule diagrams for docs/painter_seam_rule.md.

Each diagram is a top-down tile grid: x east, z north (up). Cells carry their
Manhattan ring from the eye. Locs are coloured footprints, T is the popped tile,
its far-neighbour gates are drawn as arrows labelled lateral/depth and
relax/wait, and a caption strip under the grid states the resulting order.
"""
from PIL import Image, ImageDraw, ImageFont
import os

OUT = os.path.dirname(os.path.abspath(__file__))
os.makedirs(OUT, exist_ok=True)

FONT = "/System/Library/Fonts/Supplemental/Arial.ttf"
FONTB = "/System/Library/Fonts/Supplemental/Arial Bold.ttf"
MONO = "/System/Library/Fonts/Monaco.ttf"


def font(sz, bold=False):
    return ImageFont.truetype(FONTB if bold else FONT, sz)


def mono(sz):
    return ImageFont.truetype(MONO, sz)


BG = (250, 250, 248)
GRID = (200, 200, 200)
TXT = (30, 30, 30)
EYE = (40, 90, 200)
TCOL = (255, 200, 60)
LOCS = {
    "A": (120, 170, 230),
    "W": (150, 190, 150),
    "B": (230, 170, 120),
    "L": (190, 150, 200),
    "M": (190, 150, 200),
    "P": (120, 170, 230),
    "n": (200, 120, 120),
    "b": (220, 70, 70),
}
RELAX = (40, 150, 60)
WAIT = (200, 40, 40)
SPAN = (90, 90, 200)


class Diagram:
    def __init__(self, x0, x1, z0, z1, eye, cell=44, title="", subtitle=""):
        self.x0, self.x1, self.z0, self.z1 = x0, x1, z0, z1
        self.eye = eye
        self.cell = cell
        self.title = title
        self.subtitle = subtitle
        self.locs = []      # (name, xa, xb, za, zb)
        self.marks = []     # (x, z, label)
        self.arrows = []    # (from(x,z), to(x,z), kind, text)
        self.walls = []     # (x, z, side)
        self.caption = []   # lines under the grid
        self.legend = []    # (colour, text)
        self.pad_l = 64
        self.pad_t = 96
        self.right_w = 430

    def loc(self, name, xa, xb, za, zb):
        self.locs.append((name, xa, xb, za, zb))

    def mark(self, x, z, label):
        self.marks.append((x, z, label))

    def arrow(self, frm, to, kind, text):
        self.arrows.append((frm, to, kind, text))

    def wall(self, x, z, side):
        self.walls.append((x, z, side))

    def cap(self, *lines):
        self.caption.extend(lines)

    def cell_rect(self, x, z):
        c = self.cell
        px = self.pad_l + (x - self.x0) * c
        pz = self.pad_t + (self.z1 - z) * c
        return (px, pz, px + c, pz + c)

    def center(self, x, z):
        r = self.cell_rect(x, z)
        return ((r[0] + r[2]) // 2, (r[1] + r[3]) // 2)

    def render(self, path):
        c = self.cell
        gw = (self.x1 - self.x0 + 1) * c
        gh = (self.z1 - self.z0 + 1) * c
        cap_h = 26 * len(self.caption) + 20
        W = self.pad_l + gw + 30 + self.right_w
        longest = max([len(l) for l in self.caption] + [len(self.subtitle) // 1, 0])
        W = max(W, 32 + int(longest * 7.9), 32 + int(len(self.title) * 10.5))
        H = self.pad_t + gh + 40 + cap_h
        im = Image.new("RGB", (W, H), BG)
        d = ImageDraw.Draw(im)
        d.text((16, 12), self.title, fill=TXT, font=font(20, True))
        if self.subtitle:
            d.text((16, 40), self.subtitle, fill=(90, 90, 90), font=font(14))

        # loc footprints
        for name, xa, xb, za, zb in self.locs:
            r0 = self.cell_rect(xa, zb)
            r1 = self.cell_rect(xb, za)
            d.rectangle((r0[0] + 2, r0[1] + 2, r1[2] - 2, r1[3] - 2), fill=LOCS[name],
                        outline=(60, 60, 60), width=2)
        # grid + ring numbers
        ex, ez = self.eye
        for x in range(self.x0, self.x1 + 1):
            for z in range(self.z0, self.z1 + 1):
                r = self.cell_rect(x, z)
                d.rectangle(r, outline=GRID)
                ring = abs(x - ex) + abs(z - ez)
                d.text((r[0] + 3, r[1] + 2), str(ring), fill=(140, 140, 140), font=font(11))
        # loc names (centred)
        for name, xa, xb, za, zb in self.locs:
            # name in the footprint's south-west cell, out of the way of the
            # arrows that usually cross the middle
            r = self.cell_rect(xa, za)
            d.text((r[0] + 6, r[3] - 24), name, fill=(40, 40, 40), font=font(18, True))
        # axis labels
        for x in range(self.x0, self.x1 + 1):
            r = self.cell_rect(x, self.z0)
            d.text((r[0] + c // 2 - 5, r[3] + 4), str(x), fill=TXT, font=font(12))
        for z in range(self.z0, self.z1 + 1):
            r = self.cell_rect(self.x0, z)
            d.text((r[0] - 26, r[1] + c // 2 - 7), str(z), fill=TXT, font=font(12))
        d.text((self.pad_l + gw // 2 - 40, self.pad_t + gh + 20), "x (east) →", fill=TXT,
               font=font(12))
        d.text((8, self.pad_t - 18), "z (north) ↑", fill=TXT, font=font(12))
        # walls
        for x, z, side in self.walls:
            r = self.cell_rect(x, z)
            if side == "S":
                d.line((r[0] + 2, r[3] - 3, r[2] - 2, r[3] - 3), fill=(20, 20, 20), width=5)
            elif side == "W":
                d.line((r[0] + 3, r[1] + 2, r[0] + 3, r[3] - 2), fill=(20, 20, 20), width=5)
            elif side == "E":
                d.line((r[2] - 3, r[1] + 2, r[2] - 3, r[3] - 2), fill=(20, 20, 20), width=5)
            elif side == "N":
                d.line((r[0] + 2, r[1] + 3, r[2] - 2, r[1] + 3), fill=(20, 20, 20), width=5)
        # eye
        if self.z0 <= ez <= self.z1 and self.x0 <= ex <= self.x1:
            cx, cz = self.center(ex, ez)
            d.ellipse((cx - 13, cz - 13, cx + 13, cz + 13), fill=EYE)
            d.text((cx - 6, cz - 9), "E", fill="white", font=font(15, True))
        # marks
        for x, z, label in self.marks:
            r = self.cell_rect(x, z)
            if label == "T":
                d.rectangle((r[0] + 4, r[1] + 4, r[2] - 4, r[3] - 4), outline=TCOL, width=4)
                d.text((r[0] + c // 2 - 6, r[1] + c // 2 - 9), "T", fill=(120, 80, 0),
                       font=font(17, True))
            else:
                d.text((r[0] + c // 2 - 5, r[1] + c // 2 - 8), label, fill=TXT,
                       font=font(14, True))
        # arrows with labels
        for frm, to, kind, text in self.arrows:
            a = self.center(*frm)
            b = self.center(*to)
            col = {"relax": RELAX, "wait": WAIT, "span": SPAN, "info": (90, 90, 90)}[kind]
            d.line((a, b), fill=col, width=4)
            # arrow head
            dx, dz = b[0] - a[0], b[1] - a[1]
            L = max(1, (dx * dx + dz * dz) ** 0.5)
            ux, uz = dx / L, dz / L
            hx, hz = b[0] - ux * 10, b[1] - uz * 10
            d.polygon([b, (hx - uz * 6, hz + ux * 6), (hx + uz * 6, hz - ux * 6)], fill=col)
            tx = b[0] + (12 if dx >= 0 else -12)
            tz = b[1] - 34 if dz < 0 else b[1] + 16
            if abs(dx) > abs(dz):
                tz = b[1] - 8
                tx = b[0] + (c // 2 + 4 if dx > 0 else -(c // 2 + 4 + 7 * len(text)))
            d.text((tx, tz), text, fill=col, font=font(13, True))
        # right panel: legend + caption
        rx = self.pad_l + gw + 30
        ry = self.pad_t
        d.text((rx, ry - 26), "Legend", fill=TXT, font=font(14, True))
        items = [(EYE, "E  eye tile (camera)"), (TCOL, "T  tile being popped"),
                 (RELAX, "→ gate relaxes (seam exception)"), (WAIT, "→ gate holds: T waits"),
                 (SPAN, "→ span exception (shared loc)")] + self.legend
        for i, (col, text) in enumerate(items):
            yy = ry + i * 20
            d.rectangle((rx, yy + 3, rx + 14, yy + 17), fill=col)
            d.text((rx + 22, yy), text, fill=TXT, font=font(13))
        d.text((rx, ry + len(items) * 20 + 8), "cell number = Manhattan ring from E",
               fill=(100, 100, 100), font=font(12))
        # caption under grid
        cy = self.pad_t + gh + 44
        for i, line in enumerate(self.caption):
            d.text((16, cy + i * 26), line, fill=TXT, font=mono(13))
        im.save(path, optimize=True)
        print("wrote", path)


# ---------------------------------------------------------------- diagrams

# 1. ordinary wait
dg = Diagram(0, 4, 0, 5, (2, 0), title="Example 1 - the reference gate: ordinary wait",
             subtitle="N (ring 5) holds a 1x1 loc n; T (ring 4) must wait until N is DONE")
dg.loc("n", 2, 2, 4, 4)
dg.mark(2, 3, "T")
dg.arrow((2, 3), (2, 4), "wait", "N not DONE: wait")
dg.cap("pop N: floor(N), loc n, N -> DONE   (n's footprint is N alone)",
       "pop T: N is DONE  -> floor(T)",
       "emitted order: floor(N)  n  floor(T)      n is behind T on screen: correct")
dg.render(f"{OUT}/ex01_reference_gate.png")

# 2. span exception
dg = Diagram(0, 4, 0, 5, (2, 0), title="Example 2 - the span exception (shared loc)",
             subtitle="A is 3x2; (2,3) and (2,4) are both under it, so (2,3).spans has NORTH")
dg.loc("A", 1, 3, 3, 4)
dg.mark(2, 3, "T")
dg.arrow((2, 3), (2, 4), "span", "shared loc: GROUND is enough")
dg.cap("(2,4) is GROUND, not DONE (A pending on it) - but T.spans has NORTH",
       "-> T's ground goes down -> A's last footprint tile is GROUND -> A released at T (ring 3)",
       "without this every multi-tile loc would deadlock against its own footprint")
dg.render(f"{OUT}/ex02_span_exception.png")

# 3. QBD seam, reference behaviour
dg = Diagram(0, 7, 0, 8, (3, 0), title="Example 3 - two abutting large locs on the eye's column (QBD seam)",
             subtitle="W: x[0,3], B: x[4,7], both z[2,8]. E is on column 3, the seam. T = (3,7) under W.")
dg.loc("W", 0, 3, 2, 8)
dg.loc("B", 4, 7, 2, 8)
dg.mark(3, 7, "T")
dg.arrow((3, 7), (2, 7), "span", "W: span")
dg.arrow((3, 7), (4, 7), "wait", "B does not cover T")
dg.mark(4, 2, "*")
dg.legend.append(((0, 0, 0), "*  B's nearest tile (4,2), ring 3: B is released here"))
dg.cap("x == Ex, so T is gated on BOTH W and E neighbours.  (4,7) is under B; T has no span to it.",
       "reference: (4,7) must be DONE -> B must be drawn -> B is released at (4,2), ring 3",
       "=> the whole column x=3, rings 7..3, waits; B draws at ring 3; THEN the column's floor",
       "   (farther than B) is emitted ON TOP of B.  On screen: a floor strip up the platform.",
       "seam exception: (4,7) is GROUND and all it holds is B, nearest ring 3 < T's ring 7",
       "-> T's ground may go down now. Column sweeps in order, B lands after it.")
dg.render(f"{OUT}/ex03_qbd_seam.png")

# 4. Xarpus ledge: loc behind T (depth gate)
dg = Diagram(0, 8, 5, 11, (8, -17), title="Example 4 - loc directly BEHIND the tile (Xarpus ledge): depth gate, no relax",
             subtitle="Real numbers shifted by (-42,-60): E=(50,43)->(8,-17), T=(44,66)->(2,6), L=32748 6x5 z[67,71]->z[7,11]")
dg.loc("L", 1, 6, 7, 11)
dg.mark(2, 6, "T")
dg.arrow((2, 6), (2, 7), "wait", "DEPTH gate: L is behind T")
dg.mark(6, 7, "*")
dg.legend.append(((0, 0, 0), "*  L's nearest tile (6,7), ring 26 (E is 17 rows south)"))
dg.cap("T ring 29, L's nearest corner ring 26 < 29: the OLD rule relaxed T.",
       "old: floor(T) at ring 29, then L at ring 26 -> L's tall z=7 row paints OVER T's floor",
       "      = the grey slab over the mossy floor (painter_sweeps/defects/xarpus_ledge_zoom*)",
       "eye->T = (dx,dz) = (-6,+23): |dz| > |dx| -> z is the DEPTH axis, the N gate is depth",
       "new: depth gates never relax -> T waits for (2,7) DONE -> L first, then floor(T).  Correct.")
dg.render(f"{OUT}/ex04_xarpus_ledge_depth.png")

# 4b. the axis picture
im = Image.new("RGB", (820, 330), BG)
d = ImageDraw.Draw(im)
d.text((16, 12), "Which gates are lateral?  Decided per tile from (dx, dz) = T - E", fill=TXT,
       font=font(18, True))
panels = [("|dz| > |dx|   (T ahead of the eye)", "depth", "lateral", "lateral", "depth"),
          ("|dx| > |dz|   (T beside the eye)", "lateral", "depth", "depth", "lateral"),
          ("|dx| == |dz|  (diagonal: tie)", "depth", "depth", "depth", "depth")]
for i, (title, n, w, e, s_) in enumerate(panels):
    ox = 60 + i * 260
    oy = 60
    d.text((ox, oy), title, fill=TXT, font=font(12, True))
    cx, cy = ox + 110, oy + 120
    d.rectangle((cx - 22, cy - 22, cx + 22, cy + 22), outline=TCOL, width=4)
    d.text((cx - 6, cy - 9), "T", fill=(120, 80, 0), font=font(16, True))
    for (lbl, dx_, dy_, txt) in (("N", 0, -1, n), ("S", 0, 1, s_), ("W", -1, 0, w), ("E", 1, 0, e)):
        col = RELAX if txt == "lateral" else WAIT
        ax, ay = cx + dx_ * 30, cy + dy_ * 30
        bx, by = cx + dx_ * 70, cy + dy_ * 70
        d.line((ax, ay, bx, by), fill=col, width=4)
        tx = bx + (6 if dx_ > 0 else (-60 if dx_ < 0 else -22))
        ty = by + (-20 if dy_ < 0 else (4 if dy_ > 0 else -8))
        d.text((tx, ty), txt, fill=col, font=font(12, True))
d.text((16, 300), "green = may relax (seam exception);  red = strict reference gate.  QBD seam: dx == 0 -> W/E lateral.",
       fill=TXT, font=font(13))
im.save(f"{OUT}/ex04b_lateral_axes.png", optimize=True)

# 5. QBD seam under the new rule
dg = Diagram(0, 7, 0, 8, (3, 0), title="Example 5 - the QBD seam is a LATERAL gate, so it still relaxes",
             subtitle="T=(3,7): (dx,dz)=(0,7), |dz|>|dx| -> W/E gates lateral. (Example 11 adds the ground-only hold.)")
dg.loc("W", 0, 3, 2, 8)
dg.loc("B", 4, 7, 2, 8)
dg.mark(3, 7, "T")
dg.arrow((3, 7), (2, 7), "span", "W: span")
dg.arrow((3, 7), (4, 7), "relax", "lateral + B nearest ring 3 < 7")
dg.cap("rings 7..4 of column 3 relax (B's nearest ring 3 is < their ring);",
       "(3,3) and (3,2) do NOT (3 is not < 3, not < 2): they wait for B like the reference.",
       "order: floor col3 rings 7..4, B, floor (3,3),(3,2), W   -> no strip; W beside B.")
dg.render(f"{OUT}/ex05_qbd_lateral.png")

# 6. yawed camera: eye at west edge
dg = Diagram(0, 6, 0, 6, (0, 3), title="Example 6 - camera yawed 90 degrees: 'lateral' follows (dx,dz), not the x axis",
             subtitle="E at the west edge. M is 4x3 at x[3,6] z[2,4]. Two tiles: T1=(1,5) and T2=(2,3).")
dg.loc("M", 3, 6, 2, 4)
dg.mark(1, 5, "T")
dg.mark(2, 3, "T")
dg.arrow((2, 3), (3, 3), "wait", "DEPTH gate (x)")
dg.arrow((1, 5), (1, 6), "info", "N: empty")
dg.arrow((1, 5), (2, 5), "info", "E: empty")
dg.cap("T1=(1,5): (dx,dz)=(1,2) -> z depth, W/E lateral. Its far neighbours are empty: just draws.",
       "T2=(2,3): (dx,dz)=(2,0) -> x is DEPTH, N/S lateral. Its E neighbour (3,3) is under M,",
       "   across the DEPTH gate -> strict: M (which reaches the eye's row) is drawn before T2.",
       "The rule never looks at yaw; only at where T sits relative to E in tile space.")
dg.render(f"{OUT}/ex06_yawed_camera.png")

# 7. wall disqualifies
dg = Diagram(0, 4, 0, 4, (3, 0), title="Example 7 - lateral neighbour carries a wall: no relaxation",
             subtitle="T=(1,3): (dx,dz)=(-2,3) -> z depth, E gate lateral. N=(2,3) has a near-side wall + a pending nearer loc.")
dg.loc("P", 2, 3, 3, 3)
dg.wall(2, 3, "S")
dg.mark(1, 3, "T")
dg.arrow((1, 3), (2, 3), "wait", "N has a wall: disqualified")
dg.cap("a far tile's NEAR wall is emitted at its DONE and must precede a nearer tile's ground",
       "-> any wall / wall-decor on the neighbour disqualifies it; T waits.",
       "(seam_scan memo records DISQUALIFIED: the chain is never re-walked this paint)")
dg.render(f"{OUT}/ex07_wall_disqualifies.png")

# 8. loc not reaching nearer
dg = Diagram(0, 5, 0, 5, (2, 0), title="Example 8 - the neighbour's pending loc does NOT reach nearer than T: ordinary wait",
             subtitle="T=(1,3) ring 4, (dx,dz)=(-1,3): E gate lateral. P is 2x2 at x[2,3] z[4,5]: nearest corner (2,4), ring 4.")
dg.loc("P", 2, 3, 4, 5)
dg.mark(1, 3, "T")
dg.arrow((1, 3), (2, 3), "info", "E: (2,3) empty -> DONE early")
dg.arrow((1, 3), (1, 4), "info", "N: empty")
dg.mark(2, 4, "*")
dg.legend.append(((0, 0, 0), "*  P's nearest tile, ring 4 = T's ring"))
dg.cap("Suppose instead P covered (2,3) too: its nearest ring would be 3 < 4 -> relax.",
       "As drawn, nothing pending on (2,3); and a loc whose nearest ring is >= T's ring",
       "(seam_scan - 1 >= dist) never relaxes: that is the ordinary 'wait for the tile behind me'.")
dg.render(f"{OUT}/ex08_not_nearer.png")

# 10. barrier beside ledge
dg = Diagram(42, 50, 43, 51, (50, 43), cell=40,
             title="Example 10 - barrier beside a ledge (Xarpus (49,51)): ground relaxes, the barrier is HELD",
             subtitle="L=32748 6x5 x[43,48] z[47,51], nearest (48,47) ring 6. b = 1x1 barrier on T=(49,51), ring 9. E=(50,43).")
dg.loc("L", 43, 48, 47, 51)
dg.loc("b", 49, 49, 51, 51)
dg.mark(49, 51, "T")
dg.arrow((49, 51), (48, 51), "relax", "lateral, 6 < 9: GROUND only")
dg.cap("(dx,dz)=(-1,8): z depth, W gate lateral; (48,51) is GROUND holding L (nearest 6 < 9) -> T's TERRAIN goes down.",
       "old: b drawn in the same pop, BEFORE L -> L's z=51 row (abutting T) painted over b's west edge.",
       "new: T is seam_relaxed -> its walls/decor/scenery (b) wait until the reference gate passes",
       "     ((48,51) DONE, i.e. L drawn). Order: floor(T) @9, L @6, (48,51) DONE -> b.  = world3d.")
dg.render(f"{OUT}/ex10_barrier_beside_ledge.png")

# 11. QBD with hold (timeline as a diagram of the column)
dg = Diagram(0, 7, 0, 8, (3, 0), title="Example 11 - QBD seam with the ground-only hold",
             subtitle="Relaxed tiles (3,7..4) put their floor down and defer their scenery (W itself) until (4,z) is DONE.")
dg.loc("W", 0, 3, 2, 8)
dg.loc("B", 4, 7, 2, 8)
for z in range(4, 9):
    dg.mark(3, z, "r")
dg.mark(3, 3, "w")
dg.mark(3, 2, "w")
dg.legend.append((RELAX, "r  relaxed: floor now, scenery held"))
dg.legend.append((WAIT, "w  waits for B (nearest ring 3 not < own ring)"))
dg.cap("1. floor (3,7),(3,6),(3,5),(3,4) relaxed, each flagged seam_relaxed",
       "2. B released at (4,2) ring 3; (4,z) complete and push their inward neighbours (3,z)",
       "3. (3,3),(3,2) floor; W released at (3,2); relaxed tiles re-pop, gate passes, complete",
       "result: floor col 3 (7..4), B, floor (3,3),(3,2), W   - B and W side by side, no strip")
dg.render(f"{OUT}/ex11_qbd_with_hold.png")

# flat-scene unit test picture (loc-behind test)
dg = Diagram(8, 18, 4, 12, (16, 4), cell=34,
             title="Unit test: test_loc_behind_a_tile_in_depth_is_emitted_first (flat 32x32 scene, cropped)",
             subtitle="E=(16,4). L is 6x5 at x[10,15] z[8,12] (test uses z[28,32]; shifted by -20 to fit). Floor in front of L must follow L.")
dg.loc("L", 10, 15, 8, 12)
for x in range(10, 14):
    dg.mark(x, 7, "T")
dg.arrow((11, 7), (11, 8), "wait", "depth")
dg.cap("L's nearest tile (15,8): ring 1+4 = 5 (test: 25).  Tiles (10..13,7) are rings 6..9 (26..29): nearer in",
       "depth, farther in ring.  Any-axis rule: 10/144 floor tiles before L (FAIL).  Lateral rule: 0 (pass).",
       "Hold-only (no lateral): 1 tile before L (FAIL) - Xarpus rendering right with it was scene luck.")
dg.render(f"{OUT}/ex_unit_loc_behind.png")

# QBD seam unit test picture
dg = Diagram(6, 27, 4, 24, (16, 4), cell=24,
             title="Unit test: test_seam_between_two_large_locs_keeps_the_sweep (32x32 scene, cropped)",
             subtitle="E=(16,4). West loc x[8,16], east loc x[17,25], both z[8,23]. The seam is the eye's column x=16.")
dg.loc("W", 8, 16, 8, 23)
dg.loc("B", 17, 25, 8, 23)
for z in range(10, 24):
    dg.mark(16, z, "r")
dg.mark(16, 9, "w")
dg.mark(16, 8, "w")
dg.mark(17, 8, "*")
dg.legend.append((RELAX, "r  relaxed (B nearest ring 5 < own ring)"))
dg.legend.append((WAIT, "w  rings 4,5: wait for B"))
dg.legend.append(((0, 0, 0), "*  B released here, ring 5"))
dg.cap("asserts: no seam-column floor farther than ring 5 after B; no floor beside/behind the locs",
       "farther than ring 5 after B.  (13,7) and (19,7), diagonally in front of a loc corner, may follow it.")
dg.render(f"{OUT}/ex_unit_seam.png")
print("done")
