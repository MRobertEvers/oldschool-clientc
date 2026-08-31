#!/usr/bin/env bash
#
# Push the client's data to an Android device.
#
# WHY THE LAYOUT ON THE DEVICE MIRRORS THE REPO
#
# A boot manifest states its cache and its RevConfig as paths RELATIVE TO
# ITSELF -- `dir=../cache.osrs239.sparse`, `revconfig_ui=../revconfig/...` (see
# src/bootmanifest/bootmanifest.h). So if the device reproduces the repo's
# shape, every manifest resolves on the phone exactly as it does on the desktop,
# unedited. The alternative -- rewriting paths during the push -- would mean the
# manifest on the device is not the manifest in the tree, and a path bug would
# be invisible until it failed on the device only.
#
# Everything lands under the app's own external files directory:
#
#     /sdcard/Android/data/com.torirs.client/files/
#       manifests/     the boot menu lists these
#       revconfig/     what the manifests' revconfig_ui= point at
#       cache.<name>/  what their dir= points at
#
# That directory needs NO storage permission on any API level, is reachable by
# adb push, and is removed when the app is uninstalled -- which is the right
# lifetime for a cache.
#
# USAGE
#
#     tools/android_push_data.sh                       # manifests + revconfig only
#     tools/android_push_data.sh cache.osrs239         # ... and one cache
#     tools/android_push_data.sh cache.osrs239 cache254.lostcity
#     TORIRS_ANDROID_SERIAL=<serial> tools/android_push_data.sh ...   # pick a device
#
# A cache is hundreds of megabytes and pushing one takes minutes, so caches are
# named explicitly rather than pushed by default: re-pushing the manifests after
# an edit is a two-second operation and must stay one.

set -euo pipefail

PKG="com.torirs.client"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="/sdcard/Android/data/${PKG}/files"

ADB="${ADB:-adb}"
if ! command -v "$ADB" >/dev/null 2>&1; then
    # The SDK's platform-tools are not always on PATH.
    for candidate in "${ANDROID_HOME:-$HOME/Library/Android/sdk}/platform-tools/adb" \
                     "${ANDROID_SDK_ROOT:-}/platform-tools/adb"; do
        if [ -x "$candidate" ]; then ADB="$candidate"; break; fi
    done
fi
if ! command -v "$ADB" >/dev/null 2>&1 && [ ! -x "$ADB" ]; then
    echo "error: adb not found. Set ADB=/path/to/adb or add platform-tools to PATH." >&2
    exit 1
fi

ADB_ARGS=()
if [ -n "${TORIRS_ANDROID_SERIAL:-}" ]; then
    ADB_ARGS=(-s "$TORIRS_ANDROID_SERIAL")
fi

adb_() { "$ADB" "${ADB_ARGS[@]}" "$@"; }

# One device, or a named one. With several attached and none named, adb would
# pick nothing and fail with a message about which -- catching it here says the
# useful thing instead.
device_count="$(adb_ devices | grep -c -E '\sdevice$' || true)"
if [ "$device_count" -eq 0 ]; then
    echo "error: no device. Check 'adb devices' and that USB debugging is authorised." >&2
    exit 1
fi
if [ "$device_count" -gt 1 ] && [ -z "${TORIRS_ANDROID_SERIAL:-}" ]; then
    echo "error: several devices attached; set TORIRS_ANDROID_SERIAL=<serial>." >&2
    adb_ devices >&2
    exit 1
fi

# The app must have been installed at least once: Android creates
# Android/data/<pkg>/ on install, and a push into a directory that does not
# exist fails with a permission error that suggests the wrong cause entirely.
if ! adb_ shell "test -d /sdcard/Android/data/${PKG}" 2>/dev/null; then
    echo "error: /sdcard/Android/data/${PKG} does not exist." >&2
    echo "       Install the app once first:  (cd android && ./gradlew installDebug)" >&2
    exit 1
fi

adb_ shell "mkdir -p '${DEST}/manifests'" >/dev/null

echo "==> manifests"
adb_ push "${REPO_ROOT}/manifests/." "${DEST}/manifests/" >/dev/null
echo "    $(ls -1 "${REPO_ROOT}/manifests" | wc -l | tr -d ' ') files"

echo "==> revconfig"
adb_ push "${REPO_ROOT}/revconfig" "${DEST}/" >/dev/null

for cache in "$@"; do
    src="${REPO_ROOT}/${cache}"
    if [ ! -d "$src" ]; then
        echo "error: no such cache directory: ${src}" >&2
        exit 1
    fi
    size="$(du -sh "$src" | cut -f1)"
    echo "==> ${cache}  (${size} -- this takes a while)"
    adb_ push "$src" "${DEST}/" >/dev/null
done

echo
echo "on the device, under ${DEST}:"
# Plain `ls`: Android 5.x ships a toolbox whose ls has no -1 flag, and it
# aborts on one rather than ignoring it.
adb_ shell "ls '${DEST}'" | tr -d '\r' | tr ' ' '\n' | sed '/^$/d;s/^/    /'
echo
echo "Launch the app; the boot menu lists every manifest whose cache is present."
