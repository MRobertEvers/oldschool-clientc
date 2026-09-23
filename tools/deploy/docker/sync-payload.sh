#!/usr/bin/env bash
#
# Copy the deployment payload from the Windows disk onto the distro's own
# filesystem, which is what the container actually runs from.
#
#   ./sync-payload.sh            # uses .env
#
# Run it after changing a binary or content, then `docker compose restart`.
#
# WHY this exists, and it is not premature optimisation. /mnt/c is a 9p mount,
# and the world's boot reads a great many small files out of the content tree.
# Measured on this box, same binaries, same content:
#
#     payload on /mnt/c (9p)   boot had not finished after 8 minutes
#     payload on ext4          see README
#
# The saves and logs stay on the Windows disk regardless — those are small,
# written rarely, and being directly visible and hand-editable from Windows is
# the point of them. It is only the read-heavy payload that moves.
#
# The Windows copy stays the canonical one: it is where a deploy lands, what
# the native supervise.ps1 lane runs, and what gets backed up. This is a cache
# of it, and deleting it costs nothing but a re-sync.
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck disable=SC1091
[ -f "$HERE/.env" ] && set -a && . "$HERE/.env" && set +a

SRC=${TORIRS_PAYLOAD_SRC:-/mnt/c/torirs/osrs239}
DST=${TORIRS_DEPLOY_ROOT:-/var/lib/torirs/osrs239}

say() { printf 'sync-payload: %s\n' "$*"; }
die() { printf 'sync-payload: %s\n' "$*" >&2; exit 1; }

[ -d "$SRC" ] || die "no payload at $SRC"
[ -d "$SRC/bin-linux" ] || die "$SRC/bin-linux is missing — run build-linux-binaries.sh first"

command -v rsync >/dev/null 2>&1 || {
    say "installing rsync"
    DEBIAN_FRONTEND=noninteractive apt-get install -y -qq rsync >/dev/null
}

mkdir -p "$DST"

# saves/ and logs/ are excluded deliberately: they are bind-mounted straight
# from the Windows disk over the top of this tree, so copying them here would
# only create a second, stale set that nothing reads and everyone mistakes for
# the real one.
say "$SRC -> $DST"
rsync -a --delete \
      --exclude '/saves/' \
      --exclude '/logs/' \
      --exclude '/bin/' \
      "$SRC/" "$DST/"

# The two bind mounts land INSIDE this tree, and the tree is mounted read-only,
# so the mountpoints have to exist here already: runc cannot mkdir them under a
# read-only parent and fails the container with "create mountpoint ...:
# read-only file system", which names the saves path and reads like a
# permissions problem rather than a missing directory.
mkdir -p "$DST/saves" "$DST/logs"

say "payload is $(du -sh "$DST" | cut -f1); binaries:"
ls -la "$DST/bin-linux"
say "done — 'docker compose restart' to run it"
