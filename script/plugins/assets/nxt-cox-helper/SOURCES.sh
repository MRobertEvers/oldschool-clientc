#!/bin/sh
# How every file beside this one was made. Run from the repo root.
#
# The Chambers of Xeric helper is the C++ client's own overlay, drawn natively
# when raids_overlay_content (4663) steps aside. Its only published picture is
# the wiki's (Settings, "Chambers of Xeric helper": Chambers_of_Xeric_helper.png,
# 140x182), and every number in nxt_cox_helper.c is measured off it. The art is
# the cache's, not drawn by hand -- the same the CS2 points box uses:
#
#   graphic_297   the tiled plate (raids_overlay_content's iftype_graphic)
#   0x383023 / 0x5a5245   the two 1px frames (script1192), drawn, not baked
#   fontmetrics_494  p11 -- every word width in the picture is p11's ink width
#                    (Corridor 41, Combat 34, Resource 45, Total 23, 00:00:00 46)
#
# The collapse arrow is not a cache graphic we have found; it is the picture's
# own 11x11 at (124,166), cut out with the plate made transparent.
set -e

make -C tools/dump_sprites
tools/dump_sprites/dump_sprites --dat2 --rev osrs239 cache.osrs239 \
    --out script/plugins/assets/nxt-cox-helper "plate=297:0"

CHARS='ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!"£$%^&*()-_=+[{]};:'"'"'@#~,<.>/?\ '
3rd/rscache/tools/fontbake/fontbake --rev osrs239 cache.osrs239 \
    --font 494=Text --out /tmp/bake494.c
python3 tools/fontbake_atlas.py /tmp/bake494.c \
    script/plugins/assets/nxt-cox-helper text "$CHARS" FFFFFF 000000

# blank.png is the 1x1 transparent picture the arrow's control wears.
#
# The arrow: curl the wiki picture and cut it.
#   curl -sL -o /tmp/cox_helper.png \
#       https://oldschool.runescape.wiki/images/Chambers_of_Xeric_helper.png
#   python3 - <<'PY'
#   from PIL import Image
#   im = Image.open('/tmp/cox_helper.png').convert('RGBA')
#   a = im.crop((124, 166, 135, 177))
#   a.putdata([p if p[0] > 200 else (0, 0, 0, 0) for p in a.getdata()])
#   a.save('script/plugins/assets/nxt-cox-helper/arrow.png')
#   PY
