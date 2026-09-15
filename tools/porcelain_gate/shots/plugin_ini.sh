#!/bin/zsh
# plugin_ini.sh <out.ini> <plugin-id> ["key=value" ...]
#
# One plugin on, every other plugin OFF.
#
# `enabled` DEFAULTS TO ON (torirs_plugin_host.c: the flag is only written when
# it differs from TORIRS_PLUGIN_DISABLED_BY_DEFAULT), so an ini that names one
# plugin with enabled=1 leaves the other eighteen running and the shot is of
# the whole set again. Every id is therefore listed and switched off by name.
#
# Extra "key=value" lines go in a SECOND section for the same plugin, which is
# the shape plugins_all.ini already uses for its config block.
set -u

out=${1:?out.ini}; only=${2:?plugin id}; shift 2

all=(
  gameframe-layout mobile-gameframe minimap-orbs xp-drop-orbs screenshot
  tile-indicator-c tile-indicator-lua performance-display ground-items
  entity-highlighter loot-beam xp-tracker loot-tracker item-stats
  client-settings feature-flags nxt-highlight nxt-bird-nest nxt-cannon-ammo
  widget-demo
)

# `all` is the interaction matrix's case: every plugin on at once, which is the
# only way two of them can reach for the same anchor in the same fence. `none`
# falls out of the same loop -- no id matches, so everything is written off.
: > $out
for p in $all; do
  if [ "$only" = "all" ] || [ "$p" = "$only" ]; then
    printf '[plugin:%s]\nenabled=1\n' $p >> $out
  else
    printf '[plugin:%s]\nenabled=0\n' $p >> $out
  fi
done

if [ $# -gt 0 ] && [ "$only" != "all" ] && [ "$only" != "none" ]; then
  printf '[plugin:%s]\n' $only >> $out
  for kv in "$@"; do printf '%s\n' "$kv" >> $out; done
fi
