#!/usr/bin/env python3
"""Make the editor's icon face from the Lucide icon font.

    FONTTOOLS_DIR=<dir with fontTools> python tools/chrome_ui_icons_vendor.py \
        <lucide.ttf> <info.json> <LICENSE> <repo root>

Lucide (https://lucide.dev, ISC licence) ships ~1,600 icons in one TTF at
private-use code points. The chrome draws text as bytes, so this takes the
icons the editor uses, gives each a printable ASCII code, and writes:

    src/engine/fonts/vendor/ToriRSIcons.ttf       the subset, renamed (it is a
                                                  modified version)
    src/engine/fonts/vendor/Lucide-LICENSE.txt    the licence, which must
                                                  travel with it
    src/ui/torirs_chrome_icons.h                  one #define per icon: the
                                                  one-character string to draw
                                                  in the ICON font slot

Each purpose lists candidate Lucide names; the first that exists is used, so
a rename upstream fails loudly here instead of drawing a blank box.
"""
import io
import json
import os
import sys

sys.path.insert(0, os.environ.get("FONTTOOLS_DIR", ""))
from fontTools import subset
from fontTools.ttLib import TTFont

ICONS = [
    # (define, candidates)
    ("TOOL_SELECT", ["mouse-pointer-2", "mouse-pointer"]),
    ("TOOL_RECT", ["square-dashed", "box-select"]),
    ("TOOL_HEIGHT", ["mountain"]),
    ("TOOL_FLATTEN", ["fold-vertical", "minus"]),
    ("TOOL_SMOOTH", ["waves"]),
    ("TOOL_PAINT_UNDERLAY", ["paintbrush", "brush"]),
    ("TOOL_PAINT_OVERLAY", ["paint-bucket"]),
    ("TOOL_FLAGS", ["flag"]),
    ("TOOL_PLACE", ["package-plus", "box"]),
    ("TOOL_MOVE", ["move"]),
    ("TOOL_ROTATE", ["rotate-cw"]),
    ("TOOL_DELETE", ["trash-2", "trash"]),
    ("TOOL_PLACE_PLAYER", ["person-standing", "user"]),
    ("TOOL_PAINT_PATH", ["route", "spline"]),
    ("TOOL_ROUGHEN", ["mountain-snow", "activity"]),
    ("TOOL_SCATTER", ["trees", "tree-pine"]),
    ("TOOL_PLATEAU", ["rectangle-horizontal"]),
    ("TOOL_REGION", ["pentagon", "hexagon"]),
    ("TOOL_LASSO", ["lasso"]),
    ("TOOL_TIDY", ["eraser"]),
    ("EYEDROPPER", ["pipette"]),
    ("SAVE", ["save"]),
    ("RUN", ["play"]),
    ("STOP", ["square"]),
    ("BUILD", ["hammer"]),
    ("RELOAD", ["refresh-cw"]),
    ("SEARCH", ["search"]),
    ("CLOSE", ["x"]),
    ("POP_OUT", ["external-link"]),
    ("DOCK", ["panel-right", "panel-right-close"]),
    ("MAXIMISE", ["maximize-2", "maximize"]),
    ("CHEVRON_RIGHT", ["chevron-right"]),
    ("CHEVRON_DOWN", ["chevron-down"]),
    ("CHEVRON_UP", ["chevron-up"]),
    ("CHEVRON_LEFT", ["chevron-left"]),
    ("CHECK", ["check"]),
    ("WARNING", ["triangle-alert", "alert-triangle"]),
    ("ERROR", ["circle-x", "x-circle"]),
    ("INFO", ["info"]),
    ("FOLDER", ["folder-open"]),
    ("FILE_CODE", ["file-code", "file-code-2"]),
    ("COPY", ["copy"]),
    ("PLUS", ["plus"]),
    ("MINUS", ["minus"]),
    ("SETTINGS", ["settings"]),
    ("EYE", ["eye"]),
    ("EYE_OFF", ["eye-off"]),
    ("LOCK", ["lock"]),
    ("UNDO", ["undo-2", "undo"]),
    ("REDO", ["redo-2", "redo"]),
    ("GRID", ["grid-3x3", "grid"]),
    ("MAP", ["map"]),
    ("LAYERS", ["layers"]),
    ("STAR", ["star"]),
    ("TERMINAL", ["terminal"]),
    ("HISTORY", ["history"]),
    ("USER", ["user"]),
    ("GLOBE", ["globe"]),
]
# Printable ASCII, skipping the characters a C string literal has to escape.
CODES = [c for c in range(0x21, 0x7F) if chr(c) not in ('"', "\\", "?", "%")]


def main():
    ttf_path, info_path, licence_path, root = sys.argv[1:5]
    info = json.load(open(info_path, encoding="utf-8"))
    assert len(ICONS) <= len(CODES), "more icons than printable codes"

    chosen = []
    for define, candidates in ICONS:
        name = next((n for n in candidates if n in info), None)
        if name is None:
            raise SystemExit("no Lucide icon for %s: tried %s" % (define, candidates))
        chosen.append((define, name, int(info[name]["unicode"].strip("&#;"))))

    font = TTFont(ttf_path)
    options = subset.Options()
    options.layout_features = []
    options.hinting = False
    options.notdef_outline = True
    options.glyph_names = False
    options.drop_tables += ["GSUB", "GPOS", "GDEF", "DSIG"]
    sub = subset.Subsetter(options)
    sub.populate(unicodes=[cp for _, _, cp in chosen])
    sub.subset(font)

    by_code = font.getBestCmap()
    remap = {CODES[i]: by_code[cp] for i, (_, _, cp) in enumerate(chosen)}
    for table in font["cmap"].tables:
        table.cmap = dict(remap)
    for record in font["name"].names:
        if record.nameID in (1, 4, 16):
            record.string = "ToriRS Icons"
        elif record.nameID == 6:
            record.string = "ToriRSIcons"

    out_dir = os.path.join(root, "src", "engine", "fonts", "vendor")
    font.save(os.path.join(out_dir, "ToriRSIcons.ttf"))
    with open(licence_path, "rb") as src, open(os.path.join(out_dir, "Lucide-LICENSE.txt"), "wb") as dst:
        dst.write(src.read())

    lines = [
        "#ifndef SRC_UI_TORIRS_CHROME_ICONS_H",
        "#define SRC_UI_TORIRS_CHROME_ICONS_H",
        "",
        "/*",
        " * GENERATED by tools/chrome_ui_icons_vendor.py -- do not edit.",
        " *",
        " * The editor's icon face (src/engine/fonts/vendor/ToriRSIcons.ttf, a subset",
        " * of the Lucide icon font, ISC licence) holds one icon per printable ASCII",
        " * code. Each define below is the one-character string that draws that icon",
        " * in the chrome's icon font slot.",
        " */",
        "",
    ]
    for i, (define, name, _) in enumerate(chosen):
        lines.append('#define TORIRS_CHROME_ICON_%-20s "%s" /* %s */' % (define, chr(CODES[i]), name))
    lines += ["", "/** Icons in the face; its codes are contiguous in the order above. */",
              "#define TORIRS_CHROME_ICON_COUNT %d" % len(chosen), "", "#endif", ""]
    with io.open(os.path.join(root, "src", "ui", "torirs_chrome_icons.h"), "w", newline="\n") as f:
        f.write("\n".join(lines))

    size = os.path.getsize(os.path.join(out_dir, "ToriRSIcons.ttf"))
    print("ToriRSIcons.ttf %d bytes, %d icons, upem %d" % (size, len(chosen), font["head"].unitsPerEm))


if __name__ == "__main__":
    main()
