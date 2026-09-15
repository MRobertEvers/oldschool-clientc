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
#   stone        = backvmid2, the surround's vertical strip retained for the
#                  classic Stone Drawer set.
#   drawer       = invback, the side panel, which the drawer IS.
#   chat_plate*  = backbase1/backbase2, the two classic base sprites from which
#                  the four actual chat-control regions are cut at runtime.
#                  Report crosses their seam, so both source pictures travel.
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
# Recutting it means re-running the cutter below and re-checking the fringe it
# reports -- the whole block is placed from those numbers, and a wrong set draws
# the chatbox slightly off its own backing with nothing to say so.
#
# It is named apart from chat_sheet rather than replacing it precisely because
# this script rewrites the whole set: a hand-authored file under a name the
# dump owns is one that vanishes the next time anyone runs this.
#
# The chat_paper_*.png beside it are neither cut nor hand-drawn: they are that
# sheet CUT UP, by tools/cut_chat_sheet_tiles.py, and that script is their
# provenance the way this one is everything else's.
#
#   python3 tools/cut_chat_sheet_tiles.py
#
# The sheet itself is no longer loaded at runtime -- it is the source the
# pieces come from, and it stays here because a cut with no original is one
# nobody can redo. What the frame draws is a nine-patch composed out of them
# at whatever size the chatbox actually is, because one 517x130 picture can
# only reach a bigger box by being scaled, and a scaled tear stops looking
# like a tear. @see mobile_compose_paper, and the cutter's own header, which
# explains where each cut lands and why the right edge is the left edge
# mirrored.
#
# FOUR corners (tl, tr, bl, br) are cut once each -- a corner is the one piece
# that is not repeated, so there is nothing for a second copy to break up.
# Every REPEATING position -- top, bottom, left, right, and the middle fill --
# instead gets as many real, DISTINCT variants as its own source supports,
# because one tile laid end to end is still a stamp: the eye catches the
# interval before it catches anything else about the paper. The plugin picks
# between a position's variants with a deterministic hash of the repeat cell,
# so the sheet does not shimmer between two draws of the same box and does not
# fall into one fixed A-B-C cycle either.
#
# How many variants exist per position is NOT a constant the cutter aims for;
# it is the count that clears its quality gate (an alpha-weighted seam cost
# under 6.0), and the sheet's own tear decides that count: 3 for the top edge,
# 2 for the bottom, 1 for the left and right -- the left tear only wraps
# cleanly in one place in this source, and a forced second variant there
# measured twenty times the seam cost of the one real piece. The gap in the
# numbering (chat_paper_bottom_3.png does not exist) is that limit, not a
# missing file; mobile_gameframe.c's MOBILE_IMAGE_FILE says NULL for it rather
# than a name that would fail to load. The fill has no such scarcity -- it is
# grain rather than a shape, so it tolerates blending and always finds three
# disjoint low-blotch patches to cut from.
#
# Recutting them means re-checking MOBILE_PAPER_FRINGE_* in
# src/plugin/plugins/mobile_gameframe.c, which the cutter can measure off its
# own output: `--proof <dir>` composes sheets at four sizes and prints the
# fringe it finds in each. It is the same 17/17/21/17 the single sheet had.
# The variant COUNTS are printed too, on every run, whether or not `--proof`
# is given -- a recut that changes how many pieces exist at a position needs
# no code change (MOBILE_PAPER_EDGE_VARIANTS_MAX is a ceiling, not a count
# either), but it does mean re-checking that printed line against this
# comment's 3/2/1/1/3 if the numbers ever seem to matter again.
set -e
cd "$(dirname "$0")/../../../.."
OUT=script/plugins/assets/mobile-gameframe
DUMP=tools/dump_sprites/dump_sprites

# The keyboard switch uses OldSchool's dedicated `osm_keyboard` sprite 4794.
# Keep its full 33x36 transparent canvas: the visible keyboard is centred in
# that authored box and mobile_gameframe.c centres the box on either switch.
# The canonical export lives in res so this recipe copies it byte-for-byte
# instead of re-encoding or recropping it.
cp res/osm_keyboard_icon_sprite4794.png "$OUT/icon_keyboard.png"

"$DUMP" --dat1 cache254.lostcity --out "$OUT" \
  stone=backvmid2.dat \
  highlight=redstone1.dat \
  drawer=invback.dat \
  chat_plate=backbase1.dat \
  chat_plate_end=backbase2.dat \
  map_housing=mapback.dat \
  sideicon=sideicons.dat:0-12

# The OldSchool family, from cache.osrs239's sprite table: what the drawer
# wears on an OldSchool lane (or when the `art` setting asks for it). These
# are the mobile client's OWN pieces -- interface 601 `toplevel_osm` is built
# from exactly these ids -- so on that lane the frame is assembled from the art
# the cache's mobile frame uses, not from a desktop frame's cut down.
#
#   osrs_stone / _hover / _lit  tli_button01_square_40x40 0/1/2: the 40x40 tab
#                                stone idle, under the pointer, and selected (red).
#   osrs_border_0..8             9slice_dark01_3x3 0..8: the dark 3x3 border
#                                601 draws around each tab column, corners,
#                                edges and middle in reading order.
#   osrs_map_ring                border_map_compass (5832): the thin ring 601
#                                puts around the map and the compass. Its two
#                                windows are read off the picture like the
#                                other housings'.
#   osrs_drawer                  the fixed frame's 190x261 side panel plate.
#   osrs_sideicon_*              the resizable frame's fourteen tab icons, in
#                                tab order, with 601's own door (side_icons_39)
#                                for the logout tab.
"$DUMP" --dat2 --rev osrs239 cache.osrs239 --out "$OUT" \
  osrs_stone=5767 osrs_stone_hover=5768 osrs_stone_lit=5769 \
  osrs_border_0=5814 osrs_border_1=5815 osrs_border_2=5816 \
  osrs_border_3=5817 osrs_border_4=5818 osrs_border_5=5819 \
  osrs_border_6=5820 osrs_border_7=5821 osrs_border_8=5822 \
  osrs_map_ring=5832 osrs_drawer=1031 \
  osrs_sideicon_0=168 osrs_sideicon_1=898 osrs_sideicon_2=899 osrs_sideicon_3=900 \
  osrs_sideicon_4=901 osrs_sideicon_5=902 osrs_sideicon_6=903 osrs_sideicon_7=904 \
  osrs_sideicon_8=1709 osrs_sideicon_9=905 osrs_sideicon_10=3560 osrs_sideicon_11=908 \
  osrs_sideicon_12=909 osrs_sideicon_13=910

# The 2004 compass rose, for the OldSchool lane: there the cache's own rose is
# OldSchool's, and a Stone Drawer wants the one its map plate was cut with.
"$DUMP" --dat1 cache254.lostcity --out "$OUT" compass=compass.dat
