#!/bin/sh
# Regenerate script/plugins/quest_helper/gen from the RuneLite Quest Helper.
# Needs JDK 21 (Gradle 8.10 and Lombok 1.18.30 do not run on 24) and the
# Gradle cache that `./gradlew build` in the quest-helper checkout fills.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
QH=${QUEST_HELPER:-/Users/matthewevers/Documents/git_repos/quest-helper}
JAVA_HOME=${JAVA_HOME_21:-/Library/Java/JavaVirtualMachines/temurin-21.jdk/Contents/Home}
export JAVA_HOME
WORK=${QH_WORK:-$REPO/build/questhelper_port}
mkdir -p "$WORK/classes"
if [ ! -s "$WORK/classpath" ] || [ "$QH/build.gradle" -nt "$WORK/classpath" ]; then
  cat > "$WORK/cp.gradle" <<'G'
allprojects { tasks.register('printCp') { doLast {
  println "CP=" + sourceSets.main.compileClasspath.asPath
  println "AP=" + configurations.annotationProcessor.asPath } } }
G
  ( cd "$QH" && ./gradlew --offline -q -I "$WORK/cp.gradle" printCp ) 2>/dev/null > "$WORK/cp.txt"
  sed -n 's/^CP=//p' "$WORK/cp.txt" > "$WORK/classpath"
  sed -n 's/^AP=//p' "$WORK/cp.txt" > "$WORK/aptpath"
fi
X="--add-exports=jdk.compiler/com.sun.tools.javac.api=ALL-UNNAMED --add-exports=jdk.compiler/com.sun.tools.javac.code=ALL-UNNAMED --add-exports=jdk.compiler/com.sun.tools.javac.tree=ALL-UNNAMED --add-exports=jdk.compiler/com.sun.tools.javac.util=ALL-UNNAMED"
"$JAVA_HOME/bin/javac" $X -d "$WORK/classes" "$HERE/QhToLua.java"
OUT="$REPO/script/plugins/quest_helper/gen"
rm -rf "$OUT"
"$JAVA_HOME/bin/java" $X -Xss64m -cp "$WORK/classes" QhToLua \
  --src "$QH/src/main/java" --cp "$(cat "$WORK/classpath")" --ap "$(cat "$WORK/aptpath")" \
  --aliases "$HERE/aliases.txt" --out "$OUT"
# The plugin definition's config list (static: it is read before any module
# can load), from the same function the runtime decides keys with.
LUA=${LUA:-/opt/homebrew/bin/lua}
"$LUA" "$HERE/gen_config_decl.lua" "$WORK/config_decl.lua" 2>/dev/null
"$LUA" "$HERE/splice_config.lua" "$WORK/config_decl.lua"
