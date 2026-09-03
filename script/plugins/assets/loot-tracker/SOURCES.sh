#!/bin/sh
# How every file beside this one was made. Run from the repo root.
#
# The Loot Tracker draws the CS2 loot tracker's own layout (the torirs_loot_*
# clientscripts, which build into interface 650 `loottools`), so its art is
# that interface's art rather than anything drawn by hand:
#
#   script2907  the category header -- a 33-tall band with a 4px tiled spine
#               at x=2, graphic_897 normally and graphic_4948 when the source
#               is ignored, and its name in fontmetrics_496 at 0xff981f.
#   script3042  one item cell -- 40x36, five to a row, graphic_1120 normally
#               and graphic_155 when the item is ignored, with the obj drawn
#               36x32 at +2,+2 under cc_setoutline(1).
#   script3043  "No loot to display." in fontmetrics_494, also 0xff981f.
#
set -e

3rd/rscache/tools/dump_sprites/dump_sprites --dat2 --rev osrs239 cache.osrs239 \
    --out script/plugins/assets/loot-tracker \
    "cat_spine=897:0" "cat_spine_ignored=4948:0" \
    "cell=1120:0" "cell_ignored=155:0"

# The two faces those scripts set their text in. One WHITE row each: the blit
# multiplies the ink by a tint, so one bake serves the header orange, the
# 0xcccccc keys and anything else, with the baked black shadow staying black.
CHARS='ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!"£$%^&*()-_=+[{]};:'"'"'@#~,<.>/?\ '

3rd/rscache/tools/fontbake/fontbake --rev osrs239 cache.osrs239 \
    --font 494=Text --out /tmp/bake494.c
python3 tools/fontbake_atlas.py /tmp/bake494.c \
    script/plugins/assets/loot-tracker text "$CHARS" FFFFFF 000000

3rd/rscache/tools/fontbake/fontbake --rev osrs239 cache.osrs239 \
    --font 496=Bold --out /tmp/bake496.c
python3 tools/fontbake_atlas.py /tmp/bake496.c \
    script/plugins/assets/loot-tracker bold "$CHARS" FFFFFF 000000
