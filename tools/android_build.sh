#!/usr/bin/env bash
# The Android app's two builds, and what each one needs. android/README.md has the why.
#
#   tools/android_build.sh dev   [--abi v7|arm64|all] [--debug] [--install]
#       The DEBUG ENVIRONMENT app, "ToriRS Dev" (com.torirs.client): the boot
#       menu over whatever tools/android_push_data.sh put on the phone, and the
#       native client built HERE, packaged in the APK. What every local test
#       runs. --debug builds the native client with OPT=0 (assert() live).
#       Default ABI: v7 (armeabi-v7a, the oldest device this lane targets).
#
#   tools/android_build.sh prod  [--install]
#       The PRODUCTION app, "ToriRS" (com.torirs.client.live): signs in to the
#       IaP and the torirs gate, downloads the game pack the server publishes
#       (tools/deploy/build_osrs239_package.py --android) and boots
#       manifests/manifest_latest.ini. No native code in the APK, so nothing is
#       compiled here but Java; a new client is a server deploy, not a new APK.
#       Writes build/android/ToriRS.apk, the file to sideload.
#
#   tools/android_build.sh publish-apk
#       `prod`, then copy the APK to the NUC (~/torirs/android/torirs.apk),
#       where the next deploy serves it at /android/torirs.apk for phones whose
#       app is too old for the current game pack (a JNI_ABI bump).
#
# Both apps install side by side. The NUC address: TORIRS_NUC (default the LAN one).
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NUC="${TORIRS_NUC:-192.168.1.150}"
NUC_SSH=(ssh -o User=matthew -i "$HOME/.ssh/matthew@matthew-mbp-m4" -o IdentitiesOnly=yes)
NUC_SCP=(scp -o User=matthew -i "$HOME/.ssh/matthew@matthew-mbp-m4" -o IdentitiesOnly=yes)

usage() {
    sed -n '2,24p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
}

gradle() {
    (cd "$REPO/android" && ./gradlew --console=plain -q "$@")
}

cmd="${1:-}"
[ -n "$cmd" ] || usage
shift

case "$cmd" in
dev|debug)
    abis=(armeabi-v7a)
    opt=1
    install=0
    while [ $# -gt 0 ]; do
        case "$1" in
        --abi)
            case "${2:-}" in
            v7|armeabi-v7a) abis=(armeabi-v7a) ;;
            arm64|arm64-v8a) abis=(arm64-v8a) ;;
            all) abis=(armeabi-v7a arm64-v8a) ;;
            *) echo "--abi v7|arm64|all" >&2; exit 2 ;;
            esac
            shift 2 ;;
        --debug) opt=0; shift ;;
        --install) install=1; shift ;;
        *) usage ;;
        esac
    done
    for abi in "${abis[@]}"; do
        echo "==> native client, $abi (OPT=$opt) -> android/src/main/jniLibs/$abi"
        make -C "$REPO/src" -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc)" \
            PLATFORM=android ANDROID_ABI="$abi" OPT="$opt" all
    done
    abi_prop=()
    [ "${#abis[@]}" -eq 1 ] && abi_prop=(-PtorirsAbi="${abis[0]}")
    if [ "$install" = 1 ]; then
        echo "==> installDevDebug"
        gradle ${abi_prop[@]+"${abi_prop[@]}"} installDevDebug
        echo "data: tools/android_push_data.sh [cache dirs]   (once, or when manifests change)"
    else
        echo "==> assembleDevDebug"
        gradle ${abi_prop[@]+"${abi_prop[@]}"} assembleDevDebug
        ls -l "$REPO"/android/build/outputs/apk/dev/debug/*.apk
    fi
    ;;
prod|release|publish-apk)
    install=0
    while [ $# -gt 0 ]; do
        case "$1" in
        --install) install=1; shift ;;
        *) usage ;;
        esac
    done
    echo "==> assembleProdRelease (no native code: the game pack comes from the server)"
    gradle assembleProdRelease
    mkdir -p "$REPO/build/android"
    cp "$REPO"/android/build/outputs/apk/prod/release/*.apk "$REPO/build/android/ToriRS.apk"
    ls -l "$REPO/build/android/ToriRS.apk"
    if [ "$install" = 1 ]; then
        echo "==> adb install"
        adb install -r "$REPO/build/android/ToriRS.apk"
    fi
    if [ "$cmd" = publish-apk ]; then
        echo "==> $NUC:~/torirs/android/torirs.apk (served from the next deploy)"
        "${NUC_SSH[@]}" "$NUC" 'mkdir -p ~/torirs/android'
        "${NUC_SCP[@]}" "$REPO/build/android/ToriRS.apk" "$NUC:torirs/android/torirs.apk"
    fi
    ;;
*)
    usage ;;
esac
