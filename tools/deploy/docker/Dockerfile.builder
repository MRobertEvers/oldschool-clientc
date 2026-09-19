# The toolchain that produces the two Linux server binaries.
#
# Separate from the runtime image, and never deployed. It exists so that
# building `torirsserver` and `io_server` for Linux needs nothing installed in
# the WSL distro — no gcc, no make, no python — and so that the binaries are
# built against a pinned userland instead of whatever the distro has drifted to.
#
# MUST stay on the same Debian release as Dockerfile. The binaries this image
# produces are dynamically linked against its glibc and run against the runtime
# image's; bumping one alone is how a deploy dies with a GLIBC version error.
FROM debian:bookworm

# build-essential for cc/make; python3 because tools/deploy/build_osrs239_package.py
# is python and this image is also how a full Linux package gets staged; git
# because that script stamps VERSION.txt from the repo.
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      build-essential python3 git ca-certificates \
 && rm -rf /var/lib/apt/lists/*

# The repo is bind-mounted at run time rather than copied in: it is tens of
# gigabytes with the caches and the content tree, and copying it into a layer
# would make every build a full transfer of it.
WORKDIR /repo

# git refuses to operate in a tree owned by another uid, which is every
# bind-mounted repo. The build only reads the sha for a version stamp.
RUN git config --system --add safe.directory '*'

CMD ["bash"]
