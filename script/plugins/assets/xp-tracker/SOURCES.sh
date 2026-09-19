#!/bin/sh
# Regenerate the two XP Tracker icons directly from the osrs239 cache.
#
# The two similar bar-chart graphics are not interchangeable:
#
#   panel_icon     popout_icons,2 (sprite 3579), selected for the XP Tracker
#                  slot by torirs_popout_icons -> enum_4067 -> struct_3742.
#   overview_icon  staticons2,7 (sprite 222), named by
#                  xptracker_build_components_5363 for the overall row.
#
# Keep each sprite's complete logical canvas. That retains the transparent
# offset the cache renderer uses and prevents the painted bounds being scaled
# or clipped differently from the CS2 interface.
set -e
cd "$(dirname "$0")/../../../.."

make -C tools/dump_sprites
tools/dump_sprites/dump_sprites --dat2 --rev osrs239 cache.osrs239 \
    --out script/plugins/assets/xp-tracker \
    panel_icon=3579 overview_icon=222
