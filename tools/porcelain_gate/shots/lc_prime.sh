#!/bin/zsh
# lc_prime.sh [account]
#
# Prepare ONE LostCity account so every later cs1live shot opens in a playable
# state, and leave it logged out so the sweep can use it.
#
# Two runs, and it has to be two. The tutorial clears every sidebar tab on
# login and re-mounts them one at a time as %tutorial passes each threshold, so
# setting the var does nothing for the session that set it -- the tabs come
# back on the NEXT login, from the login script's own `%tutorial > threshold`
# tests. Run one sets the var; run two is the login that pays out.
#
# Without this a cs1live shot has no inventory, no skills tab and no prayer
# book, which is the offline lane's own failure wearing a login.
set -u

here=${0:A:h}
acct=${1:-tori01}
bin=${TORIRS_SHOT_BIN:?set TORIRS_SHOT_BIN}
wt=${TORIRS_SHOT_WORKTREE:-${here:h:h:h}}

echo "== prime $acct: run 1 (designer, skiptutorial, maxme)"
TORIRS_SHOT_BIN=$bin TORIRS_SHOT_WORKTREE=$wt TORIRS_SHOT_FRAMES=1800 TORIRS_LC_USER=$acct \
  zsh $here/pshot.sh lcprime1 minimap-orbs cs1live \
      TORIRS_SIM_CLICK_AT=300,259,285 \
      "TORIRS_SIM_CMD=600,~skiptutorial;900,~maxme" >/dev/null 2>&1

# The server holds a departed session for about a minute and answers login
# reply 5 ("already logged in") until it lets go. This wait is the difference
# between a primed account and a sweep of identical rejections.
echo "== waiting out the held session"
sleep 75

echo "== prime $acct: run 2 (the login that mounts the tabs)"
TORIRS_SHOT_BIN=$bin TORIRS_SHOT_WORKTREE=$wt TORIRS_SHOT_FRAMES=1400 TORIRS_LC_USER=$acct \
  zsh $here/pshot.sh lcprime2 minimap-orbs cs1live "TORIRS_SIM_CMD=600,~varrock" 2>&1 | tail -1
sleep 75
echo "== $acct primed and released"
