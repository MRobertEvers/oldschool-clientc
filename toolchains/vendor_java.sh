#!/usr/bin/env bash
# Collect everything Java that run-runelite.sh needs into one zip.
#
#   toolchains/vendor_java.sh [--out <zip>]
#
# Three separate installs have to be present for a RuneLite run, and none of
# them is in any repository: a JDK, RuneLite's own jar repository, and the two
# reference jars the deob build and its API gate read. Each is a silent
# prerequisite — a missing one fails deep inside javac, inside a classpath, or
# inside verify_api.py, and none of those failures names the thing that is
# absent. This packs all three, with a manifest of where each came from and
# what it hashed to, so a second machine (or this one after a reinstall) is one
# unzip away from a working launcher.
#
# What is deliberately NOT here:
#
#   Deobfuscator.jar   the deobfuscator itself, a gradle output of that repo.
#                      It is needed to RE-DECOMPILE a gamepack, not to compile
#                      instr/src, which is what run-runelite.sh does.
#   gamepack.jar       the obfuscated original, for the same reason.
#   gradle             the Deobfuscator's gradlew downloads its own.
#
# Sources are overridable: JAVA_HOME, RL_REPO, DEOB_REPO.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

JAVA_HOME=${JAVA_HOME:-/Library/Java/JavaVirtualMachines/temurin-21.jdk/Contents/Home}
RL_REPO=${RL_REPO:-$HOME/.runelite/repository2}
DEOB=${DEOB_REPO:-$HOME/Documents/git_repos/Deobfuscator}
RL_VERSION=${RL_VERSION:-1.12.33}
DEOB_TMP=$DEOB/tmp/deob_run_rl1_12_33

OUT=$ROOT/toolchains/java-toolchain-osrs239.zip
[ "${1:-}" = "--out" ] && { OUT=$2; shift 2; }

die() { printf 'vendor_java: %s\n' "$*" >&2; exit 1; }

# The JDK is the .jdk BUNDLE, not just Contents/Home: dropped into
# /Library/Java/JavaVirtualMachines it is then a JDK the system's java_home
# knows about, and used in place it is the same tree either way.
JDK_BUNDLE=$JAVA_HOME
while [ "$(basename "$JDK_BUNDLE")" != "" ] && [ "${JDK_BUNDLE%.jdk}" = "$JDK_BUNDLE" ] && [ "$JDK_BUNDLE" != / ]; do
    JDK_BUNDLE=$(dirname "$JDK_BUNDLE")
done
[ "${JDK_BUNDLE%.jdk}" != "$JDK_BUNDLE" ] || die "JAVA_HOME=$JAVA_HOME is not inside a *.jdk bundle"
[ -x "$JAVA_HOME/bin/javac" ] || die "no javac at $JAVA_HOME/bin/javac — a JRE cannot build the deob"
[ -d "$RL_REPO" ] || die "no RuneLite jar repository at $RL_REPO"
[ -f "$RL_REPO/client-$RL_VERSION.jar" ] \
    || die "no client-$RL_VERSION.jar in $RL_REPO — 1.12.33 is the revision-239 release"

STAGE=$(mktemp -d "${TMPDIR:-/tmp}/vendor_java.XXXXXX")
trap 'rm -rf "$STAGE"' EXIT

mkdir -p "$STAGE/jdk" "$STAGE/runelite/repository2" "$STAGE/deob"

# ditto, not cp: the JDK carries symlinks (Contents/MacOS/libjli.dylib) and
# extended attributes, and a plain copy that dereferences them produces a tree
# that is both larger and no longer a valid bundle.
printf 'vendor_java: staging the JDK (%s)…\n' "$(basename "$JDK_BUNDLE")"
ditto "$JDK_BUNDLE" "$STAGE/jdk/$(basename "$JDK_BUNDLE")"

printf 'vendor_java: staging %s jars from %s…\n' "$(ls "$RL_REPO"/*.jar | wc -l | tr -d ' ')" "$RL_REPO"
cp "$RL_REPO"/*.jar "$STAGE/runelite/repository2/"

# The deob build reads runelite-api-<ver>-runtime.jar by an absolute path inside
# the Deobfuscator checkout, verify_api.py reads gamepack-deob.jar as the API
# baseline, and runelite_patch.py reads the stock injected client. All three are
# outputs of a decompile run that is not repeated on every machine.
for f in "runelite-api-$RL_VERSION-runtime.jar" "injected-client-$RL_VERSION.jar" gamepack-deob.jar; do
    if [ -f "$DEOB_TMP/$f" ]; then
        cp "$DEOB_TMP/$f" "$STAGE/deob/"
    else
        printf 'vendor_java: warning — no %s in %s\n' "$f" "$DEOB_TMP" >&2
    fi
done

{
    printf 'java toolchain for run-runelite.sh (OSRS revision 239)\n\n'
    printf 'packed on this machine from:\n'
    printf '  jdk/                  %s\n' "$JDK_BUNDLE"
    printf '  runelite/repository2/ %s\n' "$RL_REPO"
    printf '  deob/                 %s\n' "$DEOB_TMP"
    printf '\njava -version:\n'
    "$JAVA_HOME/bin/java" -version 2>&1 | sed 's/^/  /'
    printf '\nsha256:\n'
    ( cd "$STAGE" && find runelite deob -type f -name '*.jar' | sort \
        | while read -r f; do printf '  %s  %s\n' "$(shasum -a 256 "$f" | cut -d' ' -f1)" "$f"; done )
} > "$STAGE/MANIFEST.txt"

cp "$ROOT/toolchains/README.md" "$STAGE/README.md" 2>/dev/null || true

mkdir -p "$(dirname "$OUT")"
rm -f "$OUT"
printf 'vendor_java: zipping -> %s\n' "$OUT"
# -y keeps symlinks as symlinks; -1 because the JDK's jmods and every jar in
# here are already deflated, so the slower levels buy almost nothing.
( cd "$STAGE" && zip -q -r -y -1 "$OUT" . )

printf 'vendor_java: wrote %s (%s)\n' "$OUT" "$(du -h "$OUT" | cut -f1)"
