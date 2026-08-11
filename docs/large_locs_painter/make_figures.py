"""Figures for LARGE_LOCS_PAINTER.md.

Inputs (not in the repo — regenerate them with the commands in section 13 of
LARGE_LOCS_PAINTER.md, then point IN at the directory holding them):

    wedge.log        TORIRS_WEDGELOG capture with the seam bug present
    wedge_fixed.log  the same capture after the fix
    det1.bmp         TORIRS_EXIT_BMP screenshot with the bug present
    fixed.bmp        the same shot after the fix

    python docs/large_locs_painter/make_figures.py <input-dir> [output-dir]

Palette: the dataviz reference instance (light surface). Sequential encoding
uses the blue 100..700 ramp; the two-series comparisons use categorical slots
1 and 2. Requires matplotlib, numpy and Pillow.
"""
import sys
import os, re
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.colors import LinearSegmentedColormap
from matplotlib.patches import Rectangle, FancyArrowPatch, Circle
import matplotlib.patheffects as pe
import numpy as np

SCR = sys.argv[1] if len(sys.argv) > 1 else "."
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(__file__), "images")
os.makedirs(OUT, exist_ok=True)

SURFACE, INK, INK2, MUTED = "#fcfcfb", "#0b0b0b", "#52514e", "#898781"
GRID, AXIS = "#e1e0d9", "#c3c2b7"
S1, S2, CRIT, GOODC = "#2a78d6", "#eb6834", "#d03b3b", "#0ca30c"
SEQ = ["#cde2fb","#b7d3f6","#9ec5f4","#86b6ef","#6da7ec","#5598e7","#3987e5",
       "#2a78d6","#256abf","#1c5cab","#184f95","#104281","#0d366b"]
CMAP = LinearSegmentedColormap.from_list("seqblue", SEQ)

plt.rcParams.update({
    "figure.facecolor": SURFACE, "axes.facecolor": SURFACE,
    "savefig.facecolor": SURFACE, "font.family": ["Segoe UI", "DejaVu Sans"],
    "text.color": INK, "axes.labelcolor": INK2, "axes.edgecolor": AXIS,
    "xtick.color": MUTED, "ytick.color": MUTED,
    "xtick.labelcolor": INK2, "ytick.labelcolor": INK2,
    "axes.titlesize": 13, "axes.labelsize": 10.5,
    "xtick.labelsize": 9.5, "ytick.labelsize": 9.5,
    "axes.grid": True, "grid.color": GRID, "grid.linewidth": 0.8,
    "axes.spines.top": False, "axes.spines.right": False,
    "legend.frameon": False, "legend.fontsize": 10,
})

def finish(fig, path):
    fig.savefig(path, dpi=160, bbox_inches="tight", pad_inches=0.28)
    plt.close(fig)
    print("wrote", path)


# ---------------------------------------------------------------- wedgelog ---
def parse(path):
    cam, rows = None, []
    for line in open(path):
        if line.startswith("#path"):
            m = re.search(r"camTile=(\d+),(\d+)", line)
            cam = (int(m.group(1)), int(m.group(2)))
            continue
        t = line.split()
        if len(t) < 8 or t[6] in ("POP", "PUSH", "MARK", "SEED"):
            continue
        if not t[7].startswith("p="):
            continue
        rows.append(dict(p=int(t[7][2:]), plane=int(t[1]), x=int(t[2]), z=int(t[3]), what=t[6]))
    rows.sort(key=lambda r: r["p"])
    return cam, rows

CAM, ROWS_B = parse(SCR + "/wedge.log")
_,  ROWS_A  = parse(SCR + "/wedge_fixed.log")
CX, CZ = CAM

def floors(rows):  return [r for r in rows if r["plane"] == 0 and r["what"] == "floor"]
def dist(r):       return abs(r["x"] - CX) + abs(r["z"] - CZ)
FB, FA = floors(ROWS_B), floors(ROWS_A)

# the two 12x18 arena floor locs, read off the footprint pushes in the wedgelog
WEST = (38, 49, 48, 65)   # x0, x1, z0, z1
EAST = (50, 61, 48, 65)


# =============================================================== figure 1 =====
# The symptom, and the same frame after the fix.
from PIL import Image as PILImage

VIEWPORT = (0, 0, 480, 340)      # the 3D viewport inside the 723x503 client frame
STRIP    = (348, 148, 382, 336)  # where the strip lands on screen

def load(name, box=VIEWPORT):
    return np.asarray(PILImage.open(SCR + "/" + name).convert("RGB").crop(box))

def symptom():
    fig, axes = plt.subplots(1, 2, figsize=(13.4, 5.4))
    for ax, img, title, sub, mark in (
        (axes[0], load("det1.bmp"), "Before", "a one-tile-wide strip of arena floor "
         "runs up over the platform", True),
        (axes[1], load("fixed.bmp"), "After", "the platform is continuous; the QBD "
         "is mid-animation, nothing else changed", False),
    ):
        ax.imshow(img); ax.axis("off")
        ax.set_title(title, loc="left", pad=20, fontweight="semibold", fontsize=14)
        ax.text(0, -0.035, sub, transform=ax.transAxes, color=INK2, fontsize=10.5,
                va="top")
        if mark:
            ax.add_patch(Rectangle((STRIP[0], STRIP[1]), STRIP[2] - STRIP[0],
                                   STRIP[3] - STRIP[1], fill=False, edgecolor=CRIT,
                                   lw=2.0))
            ax.annotate("level-0 floor drawn\non top of a level-0 loc",
                        xy=(STRIP[0] - 3, 250), xytext=(224, 258), color=CRIT,
                        fontsize=10.5, ha="right", va="center", fontweight="semibold",
                        bbox=dict(boxstyle="round,pad=0.45", fc="#fdf2f2",
                                  ec="#f0c8c8", lw=1),
                        arrowprops=dict(arrowstyle="->", color=CRIT, lw=1.8))
    fig.suptitle("QBD arena, software rasteriser, camera resting on the seam column",
                 x=0.09, ha="left", fontsize=15, fontweight="semibold", color=INK)
    finish(fig, OUT + "/01_symptom.png")


def symptom_zoom():
    box = (330, 150, 410, 330)
    fig, axes = plt.subplots(1, 2, figsize=(9.0, 6.4))
    for ax, img, title in ((axes[0], load("det1.bmp", box), "Before"),
                           (axes[1], load("fixed.bmp", box), "After")):
        ax.imshow(img, interpolation="nearest"); ax.axis("off")
        ax.set_title(title, loc="left", pad=12, fontweight="semibold", fontsize=13)
    for tip in ((40, 4), (44, 140)):
        axes[0].annotate("", xy=tip, xytext=(14, 72), color=CRIT,
                         arrowprops=dict(arrowstyle="->", color=CRIT, lw=1.8))
    axes[0].text(14, 72, "the same strip,\nnarrowing as it\nrecedes", color=CRIT,
                 fontsize=10.5, va="center", ha="center", fontweight="semibold",
                 bbox=dict(boxstyle="round,pad=0.4", fc="#fdf2f2", ec="#f0c8c8", lw=1))
    fig.suptitle("The same 80x180 pixels, 4x", x=0.075, ha="left", fontsize=14,
                 fontweight="semibold", color=INK)
    fig.text(0.075, 0.02, "TORIRS_PIXOWNER named the owner of those pixels:  "
             "TERRAIN tile=49,58 L0  and  TERRAIN tile=49,60 L0.",
             color=INK2, fontsize=10.5)
    finish(fig, OUT + "/02_symptom_zoom.png")


# =============================================================== figure 2 =====
# Violation map: for each plane-0 floor tile, how many NEARER floor tiles were
# already on screen when it was drawn. A correct painter's sweep makes this 0
# everywhere; anything above 0 is ground painted on top of ground in front of it.
def violations(rows):
    d = [dist(r) for r in rows]
    out = []
    for i in range(len(d)):
        out.append(sum(1 for j in range(i) if d[j] < d[i]))
    return out

def violation_map():
    xs = [r["x"] for r in FB + FA]; zs = [r["z"] for r in FB + FA]
    x0, x1, z0, z1 = min(xs), max(xs), min(zs), max(zs)
    W, H = x1 - x0 + 1, z1 - z0 + 1
    VB, VA = violations(FB), violations(FA)
    vmax = max(VB)

    fig, axes = plt.subplots(1, 2, figsize=(13.8, 7.2))
    for ax, rows, vals, title, sub in (
        (axes[0], FB, VB, "Before — 32 tiles out of order",
         "16 of them are the seam column; the worst covers 225 nearer tiles"),
        (axes[1], FA, VA, "After — none",
         "every floor tile is drawn before everything nearer than it"),
    ):
        grid = np.full((H, W), np.nan)
        for r, v in zip(rows, vals):
            grid[r["z"] - z0, r["x"] - x0] = v
        cm = CMAP.copy(); cm.set_bad("#f4f3f0")
        im = ax.imshow(np.ma.masked_less(grid, 1), origin="lower", cmap=cm,
                       interpolation="nearest", vmin=1, vmax=vmax,
                       extent=[x0 - .5, x1 + .5, z0 - .5, z1 + .5])
        # tiles that were fine: a flat recessive wash, so the map reads as
        # "grey = correct" rather than "grey = no data".
        ok = np.where(~np.isnan(grid) & (grid < 1), 1.0, np.nan)
        ax.imshow(np.ma.masked_invalid(ok), origin="lower",
                  cmap=LinearSegmentedColormap.from_list("ok", ["#e6e5df", "#e6e5df"]),
                  interpolation="nearest",
                  extent=[x0 - .5, x1 + .5, z0 - .5, z1 + .5], zorder=0)
        ax.set_facecolor(SURFACE)

        for loc in (WEST, EAST):
            ax.add_patch(Rectangle((loc[0] - .5, loc[2] - .5), loc[1] - loc[0] + 1,
                                   loc[3] - loc[2] + 1, fill=False, edgecolor=INK2,
                                   lw=1.4, ls=(0, (5, 3)), zorder=3))
        ax.text((WEST[0] + WEST[1]) / 2, WEST[2] - 1.6, "west loc 12x18",
                ha="center", va="top", color=INK2, fontsize=9.5,
                path_effects=[pe.withStroke(linewidth=3, foreground=SURFACE)])
        ax.text((EAST[0] + EAST[1]) / 2, EAST[2] - 1.6, "east loc 12x18",
                ha="center", va="top", color=INK2, fontsize=9.5,
                path_effects=[pe.withStroke(linewidth=3, foreground=SURFACE)])

        ax.plot([CX], [CZ], marker="o", ms=9, mfc=SURFACE, mec=INK, mew=2.0, zorder=6)
        ax.annotate("eye tile (49,43)", xy=(CX, CZ), xytext=(CX - 15, CZ - 3.5),
                    color=INK, fontsize=9.5,
                    arrowprops=dict(arrowstyle="-", color=INK, lw=1.1))
        if vals is VB:
            ax.annotate("column x=49", xy=(CX, 63), xytext=(CX + 8, 70),
                        color=CRIT, fontsize=10.5, fontweight="semibold",
                        arrowprops=dict(arrowstyle="->", color=CRIT, lw=1.5))

        ax.set_title(title, color=INK, pad=22, loc="left", fontweight="semibold")
        ax.text(0, 1.012, sub, transform=ax.transAxes, color=INK2, fontsize=10)
        ax.set_xlabel("tile x"); ax.set_ylabel("tile z")
        ax.grid(False); ax.set_aspect("equal")

    cb = fig.colorbar(im, ax=axes, fraction=0.028, pad=0.02)
    cb.set_label("nearer floor tiles this tile was painted over", color=INK2, fontsize=10)
    cb.outline.set_edgecolor(AXIS)
    fig.suptitle("Painter's-order violations per floor tile, QBD arena, one frame",
                 x=0.125, ha="left", fontsize=15, fontweight="semibold", color=INK)
    fig.text(0.125, 0.015, "Grey = drawn in the right order.  768 plane-0 floor "
                           "commands per frame, same camera, same scene.",
             color=INK2, fontsize=10)
    finish(fig, OUT + "/03_violation_map.png")


# =============================================================== figure 3 =====
# The sweep: distance from the eye against emission order.
def sweep():
    fig, ax = plt.subplots(figsize=(12.4, 5.6))
    for rows, color, label, lw in ((FB, S2, "before the fix", 1.9),
                                   (FA, S1, "after the fix", 1.9)):
        ax.plot([i for i, _ in enumerate(rows)], [dist(r) for r in rows],
                color=color, lw=lw, label=label, solid_joinstyle="round")

    # the break
    bi = next(i for i in range(1, len(FB)) if dist(FB[i]) - dist(FB[i - 1]) > 10)
    ax.annotate("the seam column resumes:\n"
                "back out to distance 22 after\n"
                "the drain had reached distance 6",
                xy=(bi, dist(FB[bi])), xytext=(bi - 300, 39),
                color=S2, fontsize=10,
                arrowprops=dict(arrowstyle="->", color=S2, lw=1.4,
                                connectionstyle="arc3,rad=-0.18"))
    ax.plot([bi], [dist(FB[bi])], marker="o", ms=8, color=S2, zorder=5)

    li = next(i for i, r in enumerate(ROWS_B) if r["what"] == "loc" and r["p"] == 729)
    lp = sum(1 for r in FB if r["p"] < 729)
    ax.axvline(lp, color=MUTED, lw=1.1, ls=(0, (4, 3)))
    ax.text(lp - 8, 45.5, "east loc drawn here (distance 6)", rotation=90,
            color=INK2, fontsize=9.5, va="top", ha="right")

    ax.text(len(FA) - 140, 2.2, "after the fix", color=S1,
            fontsize=10.5, ha="right", fontweight="semibold")
    ax.text(bi + 22, 24.0, "before the fix", color=S2, fontsize=10.5,
            fontweight="semibold")
    ax.text(6, 3.0, "The two runs coincide for the first 460 emissions —\n"
                    "the orange line is underneath the blue one there.",
            color=MUTED, fontsize=9.5, va="bottom")

    ax.set_xlabel("emission order  (nth plane-0 floor command in the frame)")
    ax.set_ylabel("Manhattan distance from the eye tile")
    ax.set_title("A correct painter's sweep only ever moves toward the eye",
                 loc="left", pad=26, fontweight="semibold")
    ax.text(0, 1.02, "768 plane-0 floor emissions, same camera, same scene. "
                     "Any rise is geometry painted on top of something nearer.",
            transform=ax.transAxes, color=INK2, fontsize=10)
    ax.legend(loc="lower left", ncol=2)
    ax.set_ylim(-2, 50)
    finish(fig, OUT + "/04_sweep.png")


# =============================================================== figure 4 =====
# The camera column's own timeline: the 282-paint stall.
def column_stall():
    fig, ax = plt.subplots(figsize=(12.4, 4.6))
    for rows, color, label, off in ((FB, S2, "before the fix", 0.0),
                                    (FA, S1, "after the fix", 0.0)):
        col = [r for r in rows if r["x"] == CX]
        ax.plot([r["p"] for r in col], [dist(r) for r in col], color=color, lw=1.8,
                marker="o", ms=4.5, mfc=color, mec=SURFACE, mew=0.8, label=label)

    colb = [r for r in FB if r["x"] == CX]
    g = max(range(1, len(colb)), key=lambda i: colb[i]["p"] - colb[i - 1]["p"])
    a, b = colb[g - 1]["p"], colb[g]["p"]
    ax.annotate("", xy=(a, dist(colb[g - 1])), xytext=(b, dist(colb[g])),
                arrowprops=dict(arrowstyle="<->", color=CRIT, lw=1.6))
    ax.text((a + b) / 2, dist(colb[g]) + 2.2,
            "282 paints of nothing\nthe whole rest of the box goes down first",
            color=CRIT, fontsize=10, ha="center")

    ax.set_xlabel("paint number within the frame")
    ax.set_ylabel("distance from the eye")
    ax.set_title("The seam column alone: 32 tiles, before and after",
                 loc="left", pad=26, fontweight="semibold")
    ax.text(0, 1.03, "Every tile of column x=49 at plane 0, plotted where in the "
                     "frame it was emitted.", transform=ax.transAxes,
            color=INK2, fontsize=10)
    ax.legend(loc="upper right", ncol=2)
    finish(fig, OUT + "/05_column_stall.png")


# =============================================================== figure 5 =====
# Schematic: the seam, the gate, the span flags.
def seam_schematic():
    fig, axes = plt.subplots(1, 2, figsize=(14.2, 6.6),
                             gridspec_kw=dict(width_ratios=[1.0, 1.25], wspace=0.16))

    # --- left: the arena, at tile scale ---------------------------------------
    ax = axes[0]; ax.set_aspect("equal"); ax.axis("off")
    ax.add_patch(Rectangle((WEST[0] - .5, WEST[2] - .5), 12, 18,
                           facecolor="#dbe9fb", edgecolor=S1, lw=1.8))
    ax.add_patch(Rectangle((EAST[0] - .5, EAST[2] - .5), 12, 18,
                           facecolor="#fbe2d6", edgecolor=S2, lw=1.8))
    ax.text(43.5, 66.4, "west loc\nx[38,49] z[48,65]", color=S1, fontsize=10.5,
            ha="center", va="bottom", fontweight="semibold")
    ax.text(55.5, 66.4, "east loc\nx[50,61] z[48,65]", color=S2, fontsize=10.5,
            ha="center", va="bottom", fontweight="semibold")
    ax.plot([CX + .5, CX + .5], [47.5, 65.5], color=INK, lw=2.6, zorder=3)
    ax.add_patch(Rectangle((CX - .5, 54.5), 1, 5, facecolor="none", edgecolor=CRIT,
                           lw=1.8, zorder=4))
    ax.annotate("zoomed right", xy=(CX + .5, 57), xytext=(41, 59.5), color=CRIT,
                fontsize=10, ha="right",
                arrowprops=dict(arrowstyle="->", color=CRIT, lw=1.4))
    ax.plot([CX], [43], marker="o", ms=11, mfc=SURFACE, mec=INK, mew=2.2, zorder=6)
    ax.text(CX, 41.8, "eye tile (49,43)", color=INK, fontsize=10.5, ha="center", va="top")
    ax.plot([CX, CX], [44.2, 47.0], color=MUTED, lw=1.2, ls=(0, (3, 2)))
    ax.set_xlim(35, 64); ax.set_ylim(38, 70)
    ax.set_title("The arena floor is two locs", loc="left", pad=14,
                 fontweight="semibold", fontsize=13)

    # --- right: the gate, zoomed ---------------------------------------------
    ax = axes[1]; ax.set_aspect("equal"); ax.axis("off")
    zx0, zx1, zz0, zz1 = 47, 52, 56, 60
    for x in range(zx0, zx1 + 1):
        for z in range(zz0, zz1 + 1):
            fc = "#dbe9fb" if x <= 49 else "#fbe2d6"
            ax.add_patch(Rectangle((x - .5, z - .5), 1, 1, facecolor=fc,
                                   edgecolor="#ffffff", lw=1.4))
    ax.plot([49.5, 49.5], [zz0 - .5, zz1 + .5], color=INK, lw=3.0, zorder=3)
    ax.text(49.5, zz1 + 0.8, "the seam", color=INK, fontsize=10.5, ha="center")

    tz = 58
    ax.add_patch(Rectangle((CX - .5, tz - .5), 1, 1, facecolor="#fdd9d9",
                           edgecolor=CRIT, lw=2.4, zorder=4))
    ax.add_patch(Rectangle((CX + .5, tz - .5), 1, 1, facecolor="#fbe2d6",
                           edgecolor=S2, lw=2.4, zorder=4))
    ax.plot([CX - .2, CX - 1.1], [tz - .45, zz0 - 1.15], color=CRIT, lw=1.1, zorder=6)
    ax.text(CX - 1.2, zz0 - 1.25, "(49,58)", color=CRIT, fontsize=10.5, ha="right",
            va="center", fontweight="semibold")
    ax.plot([CX + 1.2, CX + 2.1], [tz - .45, zz0 - 1.15], color=S2, lw=1.1, zorder=6)
    ax.text(CX + 2.2, zz0 - 1.25, "(50,58)", color=S2, fontsize=10.5, ha="left",
            va="center", fontweight="semibold")

    ax.add_patch(FancyArrowPatch((CX - .18, tz), (CX - .95, tz), arrowstyle="-|>",
                                 mutation_scale=16, color=INK2, lw=2.2, zorder=7))
    ax.text(CX - 1.25, tz, "west gate\nsatisfied", color=INK2, fontsize=10,
            ha="right", va="center", zorder=9,
            path_effects=[pe.withStroke(linewidth=3.5, foreground="#ffffff")])
    ax.add_patch(FancyArrowPatch((CX + 1.18, tz), (CX + 1.95, tz), arrowstyle="-|>",
                                 mutation_scale=16, color=CRIT, lw=2.4, zorder=7))
    ax.text(CX + 2.25, tz, "east gate\nBLOCKS", color=CRIT, fontsize=10.5,
            ha="left", va="center", fontweight="semibold", zorder=9,
            path_effects=[pe.withStroke(linewidth=3.5, foreground="#ffffff")])

    ax.text(CX, zz0 - 2.9,
            "sx == camera_sx satisfies both  x <= cameraX  and  x >= cameraX,\n"
            "so a tile on the eye's own column is gated on BOTH neighbours.",
            color=INK2, fontsize=10.4, ha="center", va="top")
    ax.text(CX, zz0 - 4.6,
            "(49,58).spans = WEST | NORTH | SOUTH.  No EAST bit — (50,58) belongs to the\n"
            "OTHER loc — so the span exception cannot fire, and (49,58) must wait for\n"
            "(50,58) to reach PAINT_STEP_DONE.",
            color=CRIT, fontsize=10.2, ha="center", va="top",
            bbox=dict(boxstyle="round,pad=0.55", fc="#fdf2f2", ec="#f0c8c8", lw=1))
    ax.set_xlim(CX - 6.2, CX + 6.2); ax.set_ylim(zz0 - 8.6, zz1 + 1.6)
    ax.set_title("...and the eye's column is gated on both of them", loc="left",
                 pad=14, fontweight="semibold", fontsize=13)

    finish(fig, OUT + "/06_seam.png")


# =============================================================== figure 6 =====
# Where a multi-tile loc actually lands in the sweep.
def loc_release():
    fig, ax = plt.subplots(figsize=(9.6, 6.4))
    ax.set_aspect("equal"); ax.axis("off")
    ex, ez = 49, 43
    for d in range(4, 30, 4):
        ax.plot([ex - d, ex, ex + d, ex, ex - d], [ez, ez + d, ez, ez - d, ez],
                color=GRID, lw=1.0, zorder=0)
        ax.text(ex + d * 0.70, ez + d * 0.30, "d=%d" % d, color=MUTED, fontsize=8.5,
                ha="left", va="bottom")

    ax.add_patch(Rectangle((EAST[0] - .5, EAST[2] - .5), 12, 18,
                           facecolor="#fbe2d6", edgecolor=S2, lw=1.8, zorder=1))
    ax.text(55.5, 66.8, "east loc  x[50,61] z[48,65]", color=S2, fontsize=11,
            ha="center", fontweight="semibold")

    ax.plot([50], [48], marker="s", ms=12, mfc=SURFACE, mec=CRIT, mew=2.4, zorder=4)
    ax.annotate("NEAREST footprint tile (50,48), d=6\n"
                "the last of the 216 to get its ground,\n"
                "so this is where the whole loc is drawn",
                xy=(49.4, 47.6), xytext=(25, 42.5), color=CRIT, fontsize=10,
                ha="left", va="top",
                arrowprops=dict(arrowstyle="->", color=CRIT, lw=1.5))
    ax.plot([61], [65], marker="s", ms=12, mfc=SURFACE, mec=INK2, mew=2.0, zorder=4)
    ax.annotate("farthest corner (61,65), d=34", xy=(61.6, 65), xytext=(64, 60),
                color=INK2, fontsize=10,
                arrowprops=dict(arrowstyle="->", color=INK2, lw=1.2))

    # the tile the gate held behind it
    ax.plot([49], [58], marker="s", ms=12, mfc="#fdd9d9", mec=CRIT, mew=2.0, zorder=4)
    ax.annotate("(49,58), d=15 — held behind that loc,\n"
                "so it painted over it and everything\n"
                "else the drain had already reached",
                xy=(48.4, 58), xytext=(25, 62), color=CRIT, fontsize=10,
                ha="left", va="center",
                arrowprops=dict(arrowstyle="->", color=CRIT, lw=1.5))

    ax.plot([ex], [ez], marker="o", ms=11, mfc=SURFACE, mec=INK, mew=2.2, zorder=5)
    ax.text(ex, ez - 2.0, "eye", color=INK, fontsize=10.5, ha="center", va="top")

    ax.set_xlim(22, 84); ax.set_ylim(34, 72)
    ax.set_title("A multi-tile loc is drawn at its NEAREST tile, not its farthest",
                 loc="left", pad=16, fontweight="semibold", fontsize=14)
    ax.text(0, -0.02, "That is the whole reason the wait is unnecessary: a loc reaching "
                      "closer to the eye than the tile\nbeing held is drawn closer than "
                      "that tile no matter what the gate does.",
            transform=ax.transAxes, color=INK2, fontsize=10.4, va="top")
    finish(fig, OUT + "/07_loc_release.png")


# =============================================================== figure 7 =====
# The regression-test scene.
def test_scene():
    fig, ax = plt.subplots(figsize=(7.6, 7.0))
    ax.set_aspect("equal"); ax.axis("off")
    for i in range(0, 33):
        ax.plot([i - .5, i - .5], [-.5, 31.5], color="#eeede8", lw=0.6, zorder=0)
        ax.plot([-.5, 31.5], [i - .5, i - .5], color="#eeede8", lw=0.6, zorder=0)
    ax.add_patch(Rectangle((7.5, 7.5), 9, 16, facecolor="#dbe9fb", edgecolor=S1, lw=1.8))
    ax.add_patch(Rectangle((16.5, 7.5), 9, 16, facecolor="#fbe2d6", edgecolor=S2, lw=1.8))
    ax.text(12, 24.4, "x[8,16] z[8,23]", color=S1, fontsize=10, ha="center")
    ax.text(21, 24.4, "x[17,25] z[8,23]", color=S2, fontsize=10, ha="center")
    ax.plot([16.5, 16.5], [7.5, 23.5], color=INK, lw=2.4)
    ax.plot([16], [4], marker="o", ms=11, mfc=SURFACE, mec=CRIT, mew=2.2, zorder=5)
    ax.text(16, 2.6, "camera (16,4)", color=CRIT, fontsize=10, ha="center", va="top")
    ax.plot([16, 16], [5.0, 7.0], color=CRIT, lw=1.4, ls=(0, (3, 2)))
    ax.set_xlim(-1.5, 32.5); ax.set_ylim(-1.5, 27.5)
    ax.set_title("test_seam_between_two_large_locs_keeps_the_sweep",
                 loc="left", pad=14, fontweight="semibold", fontsize=13)
    ax.text(0, -0.01, "32x32 tiles, floor on plane 0 only, two 9x16 locs meeting on "
                      "the camera column.\nBefore: 3 monotone runs, floor at distance 19 "
                      "after the loc. After: 1 run.",
            transform=ax.transAxes, color=INK2, fontsize=10.2, va="top")
    finish(fig, OUT + "/08_test_scene.png")


symptom()
symptom_zoom()
violation_map()
sweep()
column_stall()
seam_schematic()
loc_release()
test_scene()
