#!/bin/bash
# SessionStart hook for Claude Code on the web (remote sessions only).
# Brings a fresh cloud container to the state the client needs:
#   - SDL2 / GL headers and the screenshot tools (if the environment's setup
#     script has not already installed them),
#   - both submodules on their pinned commits,
#   - the osrs239 cache at cache.osrs239/ (OpenRS2 #2639, the content tree's source build).
# Idempotent: every step checks before it acts. See
# .claude/cloud-environment/README.md for the environment settings it assumes.
set -euo pipefail

if [ "${CLAUDE_CODE_REMOTE:-}" != "true" ]; then
	exit 0
fi

cd "${CLAUDE_PROJECT_DIR:-$(git rev-parse --show-toplevel)}"
export GIT_TERMINAL_PROMPT=0

# 1. System packages.
if ! pkg-config --exists sdl2 2>/dev/null || ! command -v xdotool >/dev/null || ! command -v import >/dev/null; then
	echo "session-start: installing SDL2/GL headers and screenshot tools"
	apt-get update -qq
	apt-get install -y -qq libsdl2-dev libgl1-mesa-dev imagemagick xdotool >/dev/null
fi

# 2. Submodules. Client-TS is public. OSRS-Content is private: it clones only
#    once the session has been granted MRobertEvers/OSRS-Content, so a
#    failure here is reported, not fatal.
#    OSRS-Content is a PARTIAL clone (--filter=blob:none): its history holds
#    hundreds of committed evidence screenshots per quest, so a full clone runs
#    to gigabytes of a cloud session's fixed disk allowance. Commits and trees
#    come down now; a blob is fetched only when a checkout or a diff needs it.
#    Never fetch it with --depth afterwards: shallow fetches renegotiate badly
#    and re-download blobs into duplicate packs (2026-10-02, vm: 10.4 GB in 11
#    packs, 6 of them pure duplicates). Use a plain `git fetch origin <branch>`.
git submodule update --init Client-TS
if ! git submodule update --init --filter=blob:none OSRS-Content; then
	echo "session-start: WARNING: could not clone OSRS-Content (private)." >&2
	echo "session-start: add MRobertEvers/OSRS-Content to the session, then run" >&2
	echo "session-start:   git submodule update --init --filter=blob:none OSRS-Content" >&2
fi

# 3. The osrs239 cache: OpenRS2 #2639, the build OSRS-Content/osrs239-content
#    was unpacked from (its meta.ini [source] dat2 crc32 d5d59ebb). `cachepack
#    pack --base` refuses any other. Needs archive.openrs2.org allowed by the
#    environment's network policy.
if ! CACHE_OSRS239_URL="${CACHE_OSRS239_URL:-https://archive.openrs2.org/caches/runescape/2639/disk.zip}" \
	tools/fetch_cache_osrs239.sh; then
	echo "session-start: WARNING: cache.osrs239 download failed;" >&2
	echo "session-start: is archive.openrs2.org allowed by the network policy?" >&2
fi
