#!/bin/sh
# Build every server pack: the pristine script pack `ToriRSServer --selftest`
# reads, the lane script pack `run-live.sh` loads for
# manifests/manifest_osrs239_torirs.ini, and the server pack. They are separate
# compiles, not copies (`lanes: 0 of 3` vs `2 of 3`) — see memory
# `two-script-packs-live-vs-selftest`.
#
# One command, and it is incremental: a script edit recompiles that file and
# what depends on it, a config edit repacks that record's type, and nothing to do
# costs a fraction of a second (docs/serverpack.md). Every tool's output is
# printed in full; nothing is cut to a last line any more. Extra arguments go to
# tools/build_packs.py through PACKS_ARGS, e.g.
#   PACKS_ARGS=--verbose ./tools/tob_build_packs.sh
#   PACKS_ARGS="--explain tob_maiden.rs2" ./tools/tob_build_packs.sh
set -e
cd "$(dirname "$0")/.."
exec make --no-print-directory -C src torirsserver-packs PACKS_ARGS="${PACKS_ARGS:-}"
