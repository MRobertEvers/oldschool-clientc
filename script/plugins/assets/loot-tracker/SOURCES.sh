#!/bin/sh
# How every file beside this one was made. Run from the repo root.
#
# The Loot Tracker draws the CS2 loot tracker's own layout (the torirs_loot_*
# clientscripts, which build into interface 650 `loottools`), so its art is
# that interface's art rather than anything drawn by hand:
#
#   script2907  the category header -- a 33-tall band with a parent-minus-4
#               tiled plate at x=2, graphic_897 normally and graphic_4948 when
#               the source is ignored, and its name in fontmetrics_496 at
#               0xff981f.
#   script3042  one item cell -- 40x36, five to a row, graphic_1120 normally
#               and graphic_155 when the item is ignored, with the obj drawn
#               36x32 at +2,+2 under cc_setoutline(1).
#   script3043  "No loot to display." in fontmetrics_494, also 0xff981f.
#
# The fixed overview controls live directly on interface 650. Their graphics
# are stateful pairs, not decorative approximations -- and each pair is picked
# on the varbit that says which state the control is IN, with the op the cache
# sets beside it naming where a click GOES. script4850, script7188, script7182
# and script7185 are the four that do the picking, and every file below is
# named for the STATE its art depicts, not for that op:
#
#   4915 / 4916   source view is up / drop view is up          (script4850)
#   4912 / 4911   the basis is cache value / high alchemy      (script7188)
#   4917 / 4919   something is expanded / everything is shut   (script7182)
#   4914 / 4913   ignored entries are shown / hidden           (script7185)
#
# Naming them for the op is what the first cut did -- `btn_alch` for the face
# 4912 wears while the basis is the CACHE value, because clicking it reaches
# high alchemy -- and it made every ternary in lt_draw_totals read backwards
# while rendering correctly, which is how the band came to be reported as
# wearing two conventions at once.
#
# The rail icon is the cache popout's Loot Tools graphic. enum_4067 slot 2
# resolves to struct_4531, whose param_1412 is graphic 4900.
#
set -e

make -C tools/dump_sprites
tools/dump_sprites/dump_sprites --dat2 --rev osrs239 cache.osrs239 \
    --out script/plugins/assets/loot-tracker \
    "panel_icon=4900:0" \
    "cat_spine=897:0" "cat_spine_ignored=4948:0" \
    "cell=1120:0" "cell_ignored=155:0" \
    "btn_view_source=4915:0" "btn_view_drop=4916:0" \
    "btn_value_cache=4912:0" "btn_value_alch=4911:0" \
    "btn_expanded=4917:0" "btn_collapsed=4919:0" \
    "btn_ignored_hidden=4913:0" "btn_ignored_shown=4914:0"

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
