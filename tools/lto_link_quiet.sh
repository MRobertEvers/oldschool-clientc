#!/bin/sh
# Run a GCC link that uses -flto-incremental on Windows, dropping the one
# lto-wrapper line that is not about this build:
#
#   lto-wrapper.exe: warning: using ltrans cache without file locking
#   support, do not use in parallel
#
# GCC locks its LTRANS cache with fcntl, which MinGW does not have, so on
# Windows it prints that on every link. The lock guards a cache directory that
# two links share at once. Ours never do: each link has its own directory
# (src/makefile: $(LTO_CACHE_DIR) for the client, lto_cache_torirsserver for
# the game server) and two makes in one objdir are already ruled out. Dropping
# the cache to silence it cost ~10 s per relink (2026-11-11, win64 client
# with nothing changed: 13.2 s with the cache, 22.9 s without).
#
# Everything else the link writes to stderr is passed on, after the link ends,
# and the link's exit status is this script's.
#
#   sh tools/lto_link_quiet.sh gcc ... -flto-incremental=<dir>

err=$(mktemp) || exit 1
"$@" 2>"$err"
rc=$?
grep -v -F 'using ltrans cache without file locking support' "$err" >&2
rm -f "$err"
exit $rc
