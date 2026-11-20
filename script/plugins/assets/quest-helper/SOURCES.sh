#!/bin/sh
# How every file beside this one was made. Run from the repo root.
#
# The Quest Helper port (docs/QUEST_HELPER_PORT.md) loads RuneLite resources
# by their classpath path; ImageUtil.loadImageResource(cls, "/a/b.png") reads
# the plugin asset "a_b.png" (leading '/' dropped, '/' -> '_'), and
# SkillIconManager reads skill_icons_<skill>.png / skill_icons_small_<skill>.png.
# So these are byte-for-byte copies, renamed by that rule:
#
#   * the Quest Helper's own src/main/resources/*.png (BSD-2, Zoinkwiz et al.)
#   * RuneLite's client jar: util/clue_arrow.png and skill_icons*/ (BSD-2)
set -e
QH=${QUEST_HELPER:-/Users/matthewevers/Documents/git_repos/quest-helper}
RL_JAR=${RUNELITE_CLIENT_JAR:-$(ls "$HOME"/.gradle/caches/modules-2/files-2.1/net.runelite/client/1.13.3/*/client-1.13.3.jar | head -1)}
OUT=script/plugins/assets/quest-helper
for f in "$QH"/src/main/resources/*.png; do cp "$f" "$OUT/$(basename "$f")"; done
TMP=$(mktemp -d)
( cd "$TMP" && unzip -q -o "$RL_JAR" 'util/clue_arrow.png' 'skill_icons/*.png' 'skill_icons_small/*.png' )
cp "$TMP/util/clue_arrow.png" "$OUT/util_clue_arrow.png"
for f in "$TMP"/skill_icons/*.png; do cp "$f" "$OUT/skill_icons_$(basename "$f")"; done
for f in "$TMP"/skill_icons_small/*.png; do cp "$f" "$OUT/skill_icons_small_$(basename "$f")"; done
rm -rf "$TMP"
