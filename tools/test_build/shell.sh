#!/bin/sh
# The SHELL `make check-test-builds` hands make: every recipe line of a test
# target passes through here, so the target can be BUILT without being RUN.
#
# make calls `$(SHELL) -c '<line>'` for recipe lines and for $(shell ...) at
# parse time alike. A $(shell) call reads its stdout through a pipe; a recipe
# line writes to make's own stdout, which test_build/sweep.py points at a
# regular log file. So a pipe on stdout means "parse time: run it as is", and
# everything else is a recipe line that classify.py decides on.
[ "$1" = "-c" ] && shift
if [ -p /dev/stdout ]; then
    exec /bin/sh -c "$1"
fi
exec python3 -I "$(dirname "$0")/classify.py" "$1"
