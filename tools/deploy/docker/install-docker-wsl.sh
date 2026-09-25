#!/usr/bin/env bash
#
# Put Docker Engine into the WSL2 distro. Run once, inside the distro, as root:
#
#   wsl -d Ubuntu -- bash /mnt/c/.../install-docker-wsl.sh
#
# Ubuntu's own packages, not Docker's apt repo. The repo is keyed by release
# codename and lags new Ubuntu releases by months, and this distro is 26.04 —
# adding a repo that has no suite for it produces an apt error that reads like
# a network fault. Ubuntu ships Docker Engine 29 and the compose v2 plugin,
# which is everything here needs.
#
# This does NOT install Docker Desktop and does not need it. Desktop's value is
# the Windows-side integration; the server wants a daemon and nothing else.
set -euo pipefail

say() { printf 'install-docker: %s\n' "$*"; }
die() { printf 'install-docker: %s\n' "$*" >&2; exit 1; }

[ "$(id -u)" -eq 0 ] || die "run as root inside the distro"
grep -qi microsoft /proc/version || die "this is meant to run inside WSL"

say "installing docker.io, compose v2 and buildx"
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y --no-install-recommends \
    docker.io docker-compose-v2 docker-buildx

# systemd is already PID 1 in this distro (/etc/wsl.conf has [boot]
# systemd=true), which is what makes `enable` mean anything: the daemon comes
# back whenever the distro starts, and the container's `restart: unless-stopped`
# then brings the world up behind it. Without systemd this would need a shell
# hook and would not survive `wsl --shutdown`.
if [ "$(ps -p 1 -o comm=)" != "systemd" ]; then
    die "PID 1 is not systemd — add '[boot]\\nsystemd=true' to /etc/wsl.conf, 'wsl --shutdown', and re-run"
fi

say "enabling the daemon at distro start"
systemctl enable --now docker

# Start the distro on Windows login, so the world is up without anyone logging
# into WSL. WSL does not start a distro just because Windows booted; something
# has to poke it. See README.md for the scheduled task that does.
say "docker version: $(docker --version)"
say "compose version: $(docker compose version --short 2>/dev/null || echo 'MISSING')"

say "done"
