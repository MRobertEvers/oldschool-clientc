#!/usr/bin/env bash
#
# Implements the WinXP SSE2 package flow from:
#   .cursor/plans/winxp_sse2_package_build_5c026799.plan.md
#
# Required env (set by build_host.py):
#   CI_REPO_ROOT  — repository root
#   CI_OUT_ZIP    — path to write the final zip (package_build output)
#
# Required env (you must set in .env or environment):
#   MINGW_I686    — path to the i686 MinGW "mingw32" root (contains bin/gcc.exe, bin/SDL2.dll, etc.)
#                   Example (MSYS): /c/Users/you/Downloads/.../mingw32
#
set -euo pipefail

: "${CI_REPO_ROOT:?CI_REPO_ROOT must be set}"
: "${CI_OUT_ZIP:?CI_OUT_ZIP must be set}"
: "${MINGW_I686:?MINGW_I686 must be set (i686 mingw32 root; see winxp_sse2_package_build plan)}"

REPO="${CI_REPO_ROOT}"
OUT="${CI_OUT_ZIP}"
MINGW_I686="${MINGW_I686%/}"

cd "${REPO}"

# Step 1 — configure (same flags as plan)
cmake -B build-winxp -G "MinGW Makefiles" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER="${MINGW_I686}/bin/gcc.exe" \
  -DCMAKE_CXX_COMPILER="${MINGW_I686}/bin/g++.exe" \
  -DCMAKE_C_FLAGS="-march=pentium4 -msse2 -mfpmath=sse -D_WIN32_WINNT=0x0501 -DWINVER=0x0501" \
  -DCMAKE_CXX_FLAGS="-march=pentium4 -msse2 -mfpmath=sse -D_WIN32_WINNT=0x0501 -DWINVER=0x0501" \
  -DCMAKE_EXE_LINKER_FLAGS="-static-libgcc -static-libstdc++ -Wl,--subsystem,console:5.01" \
  -DENABLE_PACKAGE_BUILD=ON

# Step 2 — build
if command -v nproc >/dev/null 2>&1; then
  JOBS="$(nproc)"
else
  JOBS="${NUMBER_OF_PROCESSORS:-4}"
fi
cmake --build build-winxp "-j${JOBS}"

# Step 3 — package (same as plan; output goes to CI_OUT_ZIP for the CI host)
PYTHON="${PYTHON:-python}"
${PYTHON} tools/ci/package_build.py \
  --build-dir build-winxp \
  --sdl2-dll "${MINGW_I686}/bin/SDL2.dll" \
  --libwinpthread-dll "${MINGW_I686}/bin/libwinpthread-1.dll" \
  -o "${OUT}"
