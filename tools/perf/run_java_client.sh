#!/usr/bin/env bash
# Launch RuneLite with the INSTRUMENTED osrs239 client against mock230/JS5.
#
#   tools/perf/run_java_client.sh [csv-path]
#
# Prerequisites, both already documented in the runelite checkout's README:
#   src/build/mock230 43594 --rev osrs239        (with MOCK230_JS5_* set)
#   tools/torirs_javconfig.py --host 127.0.0.1 --port 8080 --revision 239
#
# Classpath order matters and has bitten this before: leaving two client-*.jar
# versions on it silently runs the older one, because both carry logback.xml and
# net.runelite.client.RuneLite. The patched jars therefore go first and the
# repository2 sweep below excludes every client / injected-client jar.
#
# The --add-exports flags are not optional on macOS. RuneLite's own launcher
# passes them; without them the JVM dies in OSXFullScreenAdapter with an
# IllegalAccessError nowhere near the game. And --developer-mode without -ea is
# a fatal dialog rather than a warning.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

DEOB=${DEOB_REPO:-/Users/matthewevers/Documents/git_repos/Deobfuscator}
RL=${RL_REPO:-$HOME/.runelite/repository2}
PATCHED=$ROOT/build/rl239
INSTR=$DEOB/instr/build/out/injected-client-1.12.33-instr.jar
CSV=${1:-$ROOT/tools/perf/results/cvj/java-idle.csv}
JAVA_HOME=${JAVA_HOME:-/Library/Java/JavaVirtualMachines/temurin-21.jdk/Contents/Home}

[ -f "$INSTR" ] || { echo "no instrumented jar: $INSTR (run instr/build.sh)" >&2; exit 1; }
[ -f "$PATCHED/client-1.12.33.jar" ] || { echo "no patched client jar in $PATCHED" >&2; exit 1; }

CP="$PATCHED/client-1.12.33.jar:$INSTR"
for j in "$RL"/*.jar; do
    case "$(basename "$j")" in
        client-*|injected-client-*) continue ;;
        # Two runelite-api versions on one classpath is the same trap as two
        # client jars: whichever sorts first wins and the mismatch shows up as
        # an AbstractMethodError far from here.
        runelite-api-1.12.34*) continue ;;
    esac
    CP="$CP:$j"
done

mkdir -p "$(dirname "$CSV")"
echo "run_java_client: csv=$CSV"
exec env \
  JPROF=1 JPROF_CSV="$CSV" JPROF_WINDOW="${JPROF_WINDOW:-500}" \
  JCTL=1 JCTL_PORT="${JCTL_PORT:-43601}" \
  "$JAVA_HOME/bin/java" -ea \
  --add-exports java.desktop/com.apple.eawt=ALL-UNNAMED \
  --add-exports java.desktop/sun.awt=ALL-UNNAMED \
  --add-exports java.desktop/sun.java2d=ALL-UNNAMED \
  -cp "$CP" \
  net.runelite.client.RuneLite \
  --jav_config=http://127.0.0.1:8080/jav_config.ws \
  --safe-mode
