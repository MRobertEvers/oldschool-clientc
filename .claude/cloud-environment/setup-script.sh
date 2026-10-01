#!/bin/bash
# Paste this into the cloud environment's "Setup script" field
# (session title bar -> cloud environment menu -> Edit -> Setup script).
# It runs once per new session, before Claude starts. The repo's own
# SessionStart hook (.claude/hooks/session-start.sh) does the rest.
set -euo pipefail

apt-get update
apt-get install -y libsdl2-dev libgl1-mesa-dev imagemagick xdotool
