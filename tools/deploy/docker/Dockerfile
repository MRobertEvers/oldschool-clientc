# The osrs239 world's execution environment — and nothing else.
#
# This image deliberately contains NO game binary, NO cache, NO content and NO
# web client. All of that is bind-mounted in from the Windows disk at run time
# (see compose.yaml), so shipping a new server build or a content change is a
# file copy and a `docker restart`, never an image rebuild. What the image owns
# is the part that genuinely belongs to the container: a pinned userland for
# the binaries to run against, an unprivileged user to run them as, and the
# supervisor that starts both processes and stops them in the right order.
#
# Pinned to bookworm on purpose. The binaries are built by Dockerfile.builder,
# which is pinned to the same release, so the glibc they link against is the
# glibc they find here. Moving one without the other is how a deploy dies with
# "GLIBC_2.38 not found" on a box that has never been touched.
FROM debian:bookworm-slim

# UID/GID of the user the servers run as.
#
# This is not cosmetic on WSL2: /mnt/c is a drvfs mount presented with a fixed
# uid/gid (1000 for the distro's first user), and drvfs enforces it — a
# container running as any other uid cannot write the bind-mounted saves, and
# the failure looks like a player whose progress silently never persists.
# Override both to match `id -u` in the distro if it is not 1000.
ARG TORIRS_UID=1000
ARG TORIRS_GID=1000

# ca-certificates is NOT installed: nothing in here dials out. procps is here
# only so `docker top`/`ps` inside the container are useful when something is
# wrong at 2am, and it is a few hundred kB.
RUN apt-get update \
 && apt-get install -y --no-install-recommends procps \
 && rm -rf /var/lib/apt/lists/* \
 && groupadd --gid "${TORIRS_GID}" torirs 2>/dev/null || true \
 && useradd --uid "${TORIRS_UID}" --gid "${TORIRS_GID}" \
      --home-dir /opt/torirs --no-create-home --shell /usr/sbin/nologin torirs \
      2>/dev/null || true

COPY entrypoint.sh /usr/local/bin/torirs-entrypoint
RUN chmod 0755 /usr/local/bin/torirs-entrypoint

# The deployment root. compose.yaml mounts the real one over the top; the
# directory exists here so the mount has somewhere to land and so that running
# the image with nothing mounted fails with the entrypoint's own diagnostic
# rather than a bare "no such file or directory".
RUN mkdir -p /opt/torirs && chown "${TORIRS_UID}:${TORIRS_GID}" /opt/torirs
WORKDIR /opt/torirs

USER ${TORIRS_UID}:${TORIRS_GID}

# Documentation only — publishing is compose's job.
EXPOSE 43594 8088

ENTRYPOINT ["/usr/local/bin/torirs-entrypoint"]
