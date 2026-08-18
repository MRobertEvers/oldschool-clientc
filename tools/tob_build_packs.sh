#!/bin/sh
# Build BOTH server script packs: the pristine one `mock230 --selftest` reads and
# the lane pack `run-live.sh` loads for manifest_osrs239_torirs.ini.
#
# They are separate compiles, not copies (`lanes: 2 of 4` vs `4 of 4`), and
# building only the first is how a session verifies a fix against a pack the
# game never loads. See memory `two-script-packs-live-vs-selftest`.
set -e
cd "$(dirname "$0")/.."
make -C src mock230-scripts-lanes MOCK230_SCRIPT_LANES="" \
     MOCK230_SCRIPT_OUT="$PWD/OSRS-Content/osrs239-content/server/scripts/build" 2>&1 | tail -1
make -C src mock230-scripts-lanes \
     MOCK230_SCRIPT_LANES="scape2009_summoning rs558_ancient_curses" \
     MOCK230_SCRIPT_OUT="$PWD/OSRS-Content/osrs239-content/server/scripts/build_summoning_curses" 2>&1 | tail -1
