#!/bin/sh
# How every PNG beside this file was authored, and from which cache.
#
# Run from the repo root. It rewrites the whole set, so it is also the way to
# re-cut the art after a cache is replaced -- the files are the plugin's now,
# but they are not hand-drawn, and a folder of pictures with no provenance is
# one nobody can correct.
#
#   make -C tools/dump_sprites
#   sh script/plugins/assets/mobile-gameframe/SOURCES.sh
#
# One cache: cache254.lostcity's dat1 media jagfile, the 2004 gameframe's own
# art. The Stone Drawer frame is a phone layout built out of it -- there is no
# mobile art in any 2004 cache to cut, so what this set contains is the pieces
# of the desktop surround that a floating frame can be assembled FROM.
#
# The names are this plugin's and not the media file's, because the pieces are
# used for something the 2004 client never used them for and the old names would
# describe the wrong thing:
#
#   stone        = backvmid2, the surround's vertical strip. TILED here, as the
#                  backing for the tab rail and the chat switch. The 2004 frame
#                  never needed a backing -- its stones sit on a surround that
#                  is already stone -- so a floating rail brings its own, and
#                  this is the piece whose texture tiles without a seam.
#   highlight    = redstone1, the pressed-tab highlight, at the exact 34x36 the
#                  rail's cell is sized to.
#   drawer       = invback, the side panel, which the drawer IS.
#   chat_sheet   = chatback, and chat_strip = backbase1, the strip the four
#                  filter buttons stand on.
#   map_housing  = mapback. The minimap and compass holes are at the offsets the
#                  classic [layout:fixed] gives them (25,5 and 0,0), which is
#                  what mobile_gameframe.c's MOBILE_MAP_HOLE_* repeat.
#   sideicon_*   = the sideicons atlas, frames 0..12. THIRTEEN pictures for
#                  fourteen tabs: the 2004 atlas has none for tab 7, so the
#                  plugin's table is shifted from tab 8 on.
#
# There is deliberately no switch.png. The chat switch is the `stone` texture
# tiled to 68x36 with the client's own font over it, because no 2004 cache has a
# chat glyph anywhere in it and a bubble drawn here would be art this plugin
# invented for a frame whose whole claim is that it did not.
set -e
cd "$(dirname "$0")/../../../.."
OUT=script/plugins/assets/mobile-gameframe
DUMP=tools/dump_sprites/dump_sprites

"$DUMP" --dat1 cache254.lostcity --out "$OUT" \
  stone=backvmid2.dat \
  highlight=redstone1.dat \
  drawer=invback.dat \
  chat_sheet=chatback.dat \
  chat_strip=backbase1.dat \
  map_housing=mapback.dat \
  sideicon=sideicons.dat:0-12
