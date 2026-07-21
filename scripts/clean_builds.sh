#!/bin/sh
# Prune stale root-level build output directories (all gitignored via /build*/).
# Review the list before running: this permanently deletes them.
set -eu
cd "$(dirname "$0")/.."
for d in build-asan-test build-em build-em-native-test build-emscripten build-native \
         build-osx build-test build-web build.em build2 build_agent_test build_asan \
         build_cmake build_em build_emscripten build_emscripten_release build_judge \
         build_metal_check build_native build_neon build_no_neon build_release \
         build_scalar build_test build_try buildr; do
    [ -d "$d" ] && { echo "rm -rf $d"; rm -rf "$d"; }
done
echo "done (kept ./build, the active output dir)"
