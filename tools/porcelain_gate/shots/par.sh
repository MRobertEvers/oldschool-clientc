#!/bin/zsh
# par.sh <jobs-file>
#
# Run pshot.sh lines N at a time. Each line of <jobs-file> is one pshot.sh
# argument list; blank lines and # comments are skipped.
#
# A shot is a whole client boot -- about fifty seconds of it, almost all
# spent loading the cache and running the embedded server up to the first
# rendered frame -- and forty of them serially is most of an hour. The runs are
# independent (each has its own run dir, its own prefs copy and its own
# embedded server), so they go in parallel; PAR_JOBS keeps that off the number
# of cores rather than the number of shots.
set -u

here=${0:A:h}
jobs=${1:?jobs file}
n=${PAR_JOBS:-4}

running=0
while IFS= read -r line; do
  case "$line" in ''|'#'*) continue;; esac
  eval "zsh $here/pshot.sh $line" &
  running=$((running+1))
  if [ $running -ge $n ]; then wait; running=0; fi
done < $jobs
wait
