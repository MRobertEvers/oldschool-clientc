#!/bin/zsh
set -u
WT=/Users/matthewevers/Documents/git_repos/3draster/.claude/worktrees/wf_31184ec1-070-26
BIN=$1 NAME=$2 PLUGIN=$3 LANE=$4
CLICKS='TORIRS_SIM_CLICK_AT=300,259,285;450,259,285;600,259,285;750,259,285'
CMDS='TORIRS_SIM_CMD=900,~skiptutorial;1000,~varrock;1100,~skiptutorial;1300,~torirskit;1450,~torirscoins'
extra=()
if [ "$LANE" = cs1live ]; then extra+=("$CLICKS" "$CMDS" TORIRS_SHOT_FRAMES=2200); fi
env TORIRS_SHOT_BIN=$BIN TORIRS_SHOT_WORKTREE=$WT \
  zsh $WT/tools/porcelain_gate/shots/pshot.sh $NAME $PLUGIN $LANE \
    TORIRS_WIDGET_DEMO=only TORIRS_TRACE_NATIVE_UI=1 TORIRS_DUMP_BOUNDS=all TORIRS_DUMP_ROLES=1 "${extra[@]}"
