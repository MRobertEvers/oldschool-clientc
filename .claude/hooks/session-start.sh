#!/bin/bash
# SessionStart hook for Claude Code on the web (remote sessions only).
# Brings a fresh cloud container to the state the client needs:
#   - SDL2 / GL headers and the screenshot tools (if the environment's setup
#     script has not already installed them),
#   - both submodules on their pinned commits,
#   - the osrs239 cache at cache.osrs239/ (OpenRS2 #2644).
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
git submodule update --init Client-TS
if ! git submodule update --init OSRS-Content; then
	echo "session-start: WARNING: could not clone OSRS-Content (private)." >&2
	echo "session-start: add MRobertEvers/OSRS-Content to the session, then run" >&2
	echo "session-start:   git submodule update --init OSRS-Content" >&2
fi

# 3. The osrs239 cache. Needs archive.openrs2.org allowed by the environment's
#    network policy.
if ! tools/fetch_cache_osrs239.sh; then
	echo "session-start: WARNING: cache.osrs239 download failed;" >&2
	echo "session-start: is archive.openrs2.org allowed by the network policy?" >&2
fi
