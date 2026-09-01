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
#   highlight    = redstone1, the 2004 pressed-tab stone. The frame's WORKHORSE:
#                  the plugin turns it a quarter turn at runtime and lays
#                  fourteen of them edge to edge to build the tab rail, plus a
#                  dimmed copy for the cells that are not open. It is 34x36, so
#                  a rail cell is 36x34.
#   stone        = backvmid2, the surround's vertical strip, tiled to make the
#                  bar the chat filter buttons stand on. It backs nothing else:
#                  the rail used to be tiled out of this and seamed every 37
#                  columns, because it is a piece cut for a different job.
#   drawer       = invback, the side panel, which the drawer IS.
#   chat_sheet   = NOT cut here any more, and not cut at all -- see below. The
#                  bar that used to go under it was never a cut of backbase1
#                  either: that is a shaped corner plate, 496 wide against the
#                  sheet's 479 and cut to mate with a surround this frame does
#                  not have.
#   map_housing  = mapback. The minimap and compass holes are at the offsets the
#                  classic [layout:fixed] gives them (25,5 and 0,0), which is
#                  what mobile_gameframe.c's MOBILE_MAP_HOLE_* repeat.
#   sideicon_*   = the sideicons atlas, frames 0..12. THIRTEEN pictures for
#                  fourteen tabs: the 2004 atlas has none for tab 7, so the
#                  plugin's table is shifted from tab 8 on.
#
# Four pictures are NOT here because they are shapes rather than art, and a
# shape is the one thing a cut cannot be: the turned stone and its dimmed twin,
# and the two round alpha cut-outs that make the minimap and the compass round.
# The plugin rasterises all four itself. @see mobile_build_art.
#
# There is deliberately no switch.png either. The chat switch is two turned
# stones with the client's own font over them, because no 2004 cache has a chat
# glyph anywhere in it and a bubble drawn here would be art this plugin invented
# for a frame whose whole claim is that it did not.
#
# One file beside this script is hand-authored rather than cut:
# chat_sheet_rs289.png, the torn parchment the chat sheet is drawn on.
# `chatback.dat` is a flat 479x96 rectangle in every dat1 cache in this tree --
# cache254.lostcity and cache.rs289lc dump a byte-identical file -- and four
# hard corners are the one shape this frame cannot use. The desktop surround
# explains a rectangle; a sheet floating on the scene has nothing to explain
# one, and it read as a beige box lying on the grass. So the sheet is 517x130:
# that same 479x96 surface at offset (17,17), inside a torn fringe.
#
# Recutting it means remeasuring MOBILE_CHAT_FRINGE_X/Y in
# src/plugin/plugins/mobile_gameframe.c off the new file's opaque core -- the
# whole block is placed from those two, and a wrong pair draws the chatbox
# slightly off its own backing with nothing to say so.
#
# It is named apart from chat_sheet rather than replacing it precisely because
# this script rewrites the whole set: a hand-authored file under a name the
# dump owns is one that vanishes the next time anyone runs this.
set -e
cd "$(dirname "$0")/../../../.."
OUT=script/plugins/assets/mobile-gameframe
DUMP=tools/dump_sprites/dump_sprites

"$DUMP" --dat1 cache254.lostcity --out "$OUT" \
  stone=backvmid2.dat \
  highlight=redstone1.dat \
  drawer=invback.dat \
  map_housing=mapback.dat \
  sideicon=sideicons.dat:0-12
