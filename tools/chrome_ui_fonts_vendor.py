"""Extract and subset the vendored UI fonts.

    PYTHONPATH=<fonttools> python -I vendor_fonts.py <inter.zip> <jbm.zip> <out dir>

Keeps Basic Latin, Latin-1 and the punctuation, arrows and marks the editor's
labels use; drops hinting programs and layout tables the rasteriser
(stb_truetype) does not read. The licences are copied beside the faces.
"""
import io
import os
import sys
import zipfile

sys.path.insert(0, os.environ.get("FONTTOOLS_DIR", ""))
from fontTools import subset
from fontTools.ttLib import TTFont

inter_zip, jbm_zip, out = sys.argv[1:4]
UNICODES = list(range(0x20, 0x7F)) + list(range(0xA0, 0x100)) + [
    0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026, 0x2190, 0x2191, 0x2192,
    0x2193, 0x2212, 0x2713, 0x2715, 0x25B2, 0x25B6, 0x25BC, 0x25C0, 0x2318, 0x21E7, 0x2325,
]
FACES = [
    (inter_zip, "extras/ttf/Inter-Regular.ttf", "Inter-Regular.ttf"),
    (inter_zip, "extras/ttf/Inter-Medium.ttf", "Inter-Medium.ttf"),
    (inter_zip, "extras/ttf/Inter-SemiBold.ttf", "Inter-SemiBold.ttf"),
    (jbm_zip, "fonts/ttf/JetBrainsMono-Regular.ttf", "JetBrainsMono-Regular.ttf"),
]
for archive, member, name in FACES:
    data = zipfile.ZipFile(archive).read(member)
    font = TTFont(io.BytesIO(data))
    options = subset.Options()
    options.layout_features = []
    options.hinting = False
    options.name_IDs = [0, 1, 2, 3, 4, 5, 6, 13, 14]
    options.notdef_outline = True
    options.glyph_names = False
    options.drop_tables += ["GSUB", "GPOS", "GDEF", "DSIG", "STAT", "MVAR", "meta"]
    sub = subset.Subsetter(options)
    sub.populate(unicodes=UNICODES)
    sub.subset(font)
    path = os.path.join(out, name)
    font.save(path)
    cmap = font.getBestCmap()
    print("%-28s %6d bytes  %d glyphs  upem %d  missing %s" % (
        name, os.path.getsize(path), len(font.getGlyphOrder()), font["head"].unitsPerEm,
        [hex(u) for u in UNICODES if u not in cmap][:12]))
with open(os.path.join(out, "Inter-LICENSE.txt"), "wb") as f:
    f.write(zipfile.ZipFile(inter_zip).read("LICENSE.txt"))
with open(os.path.join(out, "JetBrainsMono-OFL.txt"), "wb") as f:
    f.write(zipfile.ZipFile(jbm_zip).read("OFL.txt"))
