#!/usr/bin/env bash
#
# Build torirsserver and io_server for Linux and install them into the
# deployment's bin-linux/, using the builder image so the WSL distro needs no
# toolchain of its own.
#
#   ./build-linux-binaries.sh [REPO] [DEPLOY_ROOT]
#
# Defaults come from .env beside this script. After it runs:
#
#   docker compose restart
#
# and the world is on the new binary. The image is not rebuilt and does not
# need to be — it holds no binary (see Dockerfile).
#
# bin-linux/ is deliberately NOT bin/. bin/ holds the Windows .exe that the
# native supervise.ps1 lane runs, and leaving those in place is the rollback:
# stop the container, start supervise.ps1, and the world is back on Windows
# binaries against the same saves.
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck disable=SC1091
[ -f "$HERE/.env" ] && set -a && . "$HERE/.env" && set +a

REPO=${1:-${TORIRS_REPO:-}}
DEPLOY_ROOT=${2:-${TORIRS_DEPLOY_ROOT:-}}
JOBS=${TORIRS_BUILD_JOBS:-$(nproc)}

say() { printf 'build-linux: %s\n' "$*"; }
die() { printf 'build-linux: %s\n' "$*" >&2; exit 1; }

[ -n "$REPO" ] || die "no repo path: pass one, or set TORIRS_REPO in .env"
[ -d "$REPO/src" ] || die "$REPO does not look like the repo (no src/)"
[ -n "$DEPLOY_ROOT" ] || die "no deployment root: pass one, or set TORIRS_DEPLOY_ROOT in .env"
[ -d "$DEPLOY_ROOT" ] || die "deployment root $DEPLOY_ROOT does not exist"

say "building the builder image"
docker build -f "$HERE/Dockerfile.builder" -t torirs-linux-builder:latest "$HERE"

# Private objdirs.
#
# Not optional: on this box the same checkout is also built for Windows through
# make.ps1, and that build uses src/build_opt and src/build. Sharing an objdir
# between a MinGW build and this one mixes object files of two architectures
# into one directory, and the link failure that eventually comes out of it
# names neither cause. IO_SERVER_OBJ_DIR is hardcoded to `build` in the
# makefile and is overridden here for the same reason.
OBJ_GAME=build_linux
OBJ_IO=build_linux_io

say "compiling torirsserver and io_server (-j$JOBS)"
docker run --rm \
    -v "$REPO":/repo \
    -w /repo \
    -u "$(id -u):$(id -g)" \
    torirs-linux-builder:latest \
    bash -c "
        set -e
        make -C src OPT=1 PLATFORM_OBJ_BASE=$OBJ_GAME torirsserver -j$JOBS
        make -C src io-server IO_SERVER_OBJ_DIR=$OBJ_IO -j$JOBS
    "

GAME_BIN="$REPO/src/${OBJ_GAME}_opt/torirsserver"
IO_BIN="$REPO/src/$OBJ_IO/io_server"
[ -x "$GAME_BIN" ] || die "expected $GAME_BIN; the make target did not produce it"
[ -x "$IO_BIN" ] || die "expected $IO_BIN; the make target did not produce it"

# Verified rather than assumed: a Windows checkout can leave a stale .exe or a
# PE file under a linux-looking name, and an ELF check here is cheaper than a
# container that crash-loops with "exec format error".
for bin in "$GAME_BIN" "$IO_BIN"; do
    head -c 4 "$bin" | grep -q $'\x7fELF' || die "$bin is not an ELF binary"
done

mkdir -p "$DEPLOY_ROOT/bin-linux"
# Copy to a temporary name and rename, so a running container never sees a
# half-written binary if this is run before the restart.
for pair in "$GAME_BIN:torirsserver" "$IO_BIN:io_server"; do
    src=${pair%:*}; name=${pair##*:}
    cp "$src" "$DEPLOY_ROOT/bin-linux/.$name.new"
    chmod 0755 "$DEPLOY_ROOT/bin-linux/.$name.new"
    mv "$DEPLOY_ROOT/bin-linux/.$name.new" "$DEPLOY_ROOT/bin-linux/$name"
    say "installed $DEPLOY_ROOT/bin-linux/$name"
done

say "done — 'docker compose restart' to run them"
