#!/bin/zsh
# pshot.sh <shot-name> <plugin-id> <lane> [ENV=value ...] [-- "cfgkey=value" ...]
#
# One plugin, ISOLATED, photographed on one lane.
#
# shot.sh takes a whole prefs file and a lane; this wraps it so every shot in
# the per-plugin set is taken the same way: a private ini with exactly that one
# plugin enabled (plugin_ini.sh), the output under plugins/, and the binary
# this tree built. Anything after `--` is a config line for the plugin, written
# into its ini rather than pushed through TORIRS_SIM_PLUGIN_CONFIG, because a
# value that has to be in force at START (an art style, a frame choice) cannot
# be delivered by a tick-scheduled config write.
set -u

here=${0:A:h}
wt=${TORIRS_SHOT_WORKTREE:-${here:h:h:h}}

name=$1 plugin=$2 lane=$3; shift 3

# A TORIRS_SHOT_* name is read by SHOT.SH, out of its own environment, and
# everything else in the list is handed to the CLIENT. Passing one in the
# trailing list therefore set it on the client, where nothing reads it, and the
# shot was taken with the default -- silently. That cost a cannon capture
# (TORIRS_SHOT_FRAMES=1500 ran 700 frames, so the op scheduled at 1400 never
# fired) and a frame capture (TORIRS_SHOT_FRAME named a provider and the lane's
# own `auto` was used). They are hoisted here instead of being a rule to
# remember.
envs=()
cfgs=()
shot_env=()
seen_dashdash=0
for a in "$@"; do
  if [ "$a" = "--" ]; then seen_dashdash=1; continue; fi
  if [ $seen_dashdash = 1 ]; then cfgs+=("$a"); continue; fi
  case "$a" in
    TORIRS_SHOT_*) shot_env+=("$a") ;;
    *) envs+=("$a") ;;
  esac
done

mkdir -p $here/ini $here/plugins
zsh $here/plugin_ini.sh $here/ini/$name.ini $plugin "${cfgs[@]}"

env \
  TORIRS_SHOT_BIN=${TORIRS_SHOT_BIN:-$wt/src/torirs_shotsall} \
  TORIRS_SHOT_PREFS=$here/ini/$name.ini \
  TORIRS_SHOT_OUT=$here/plugins \
  "${shot_env[@]}" \
  zsh $here/shot.sh $name $lane "${envs[@]}"
