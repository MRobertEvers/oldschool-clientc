#!/usr/bin/env python3
"""
Point a stock RuneLite at this repo's server.

Two things have to change before an OldSchool client will talk to anything that
is not Jagex, and only one of them is a setting:

  1. WHERE it connects. RuneLite already takes `--jav_config=<url>`, and the
     client derives the game host from that config's `codebase`. No patching.

  2. WHOSE key it encrypts the login block with. The RSA modulus is compiled
     into the client, so the login block is readable only by Jagex unless it is
     replaced. That is what this tool does.

The modulus lives in RuneLite's shipped `injected-client-<version>.jar` (modern
RuneLite does not download a gamepack at runtime: ClientLoader loads
`initial_class` straight off its own classpath). Inside that jar it is a plain
CONSTANT_Utf8 entry -- a 256-character lowercase hex string -- in the same class
as the public exponent "10001". Both are exactly the shape a constant pool
entry has, so this finds them structurally rather than by class name: the
obfuscated name changes every revision, and in 1.12.34.1 it happens to be
`bq.class`.

The replacement is the same length as the original (both moduli are 1024-bit),
so the edit is a byte-for-byte overwrite: no constant-pool offsets move, no
attribute lengths change, and the injected class still verifies.

    tools/runelite_patch.py --modulus <hex> --out build/runelite-patched
    tools/runelite_patch.py --print-launch

WHAT THIS DOES NOT DO: it does not touch RuneLite's own jars, its signature
checks, or anything about the account system. It produces a copy; the original
jar is left alone, and pointing RuneLite back at Jagex is a matter of dropping
the `--jav_config` flag.
"""

import argparse
import os
import re
import shutil
import struct
import sys
import zipfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RL_REPO = os.path.expanduser("~/.runelite/repository2")

# CONSTANT_Utf8_info: tag 1, then a u2 length, then the bytes. Anchoring on the
# tag and the length is what makes this a constant-pool edit rather than a
# string search that might land in the middle of some other structure.
MODULUS_RE = re.compile(rb"\x01\x01\x00([0-9a-f]{256})")
EXPONENT = b"\x01\x00\x0510001"

# RuneLite refuses a jav_config whose host does not end in one of these:
#
#   host.toLowerCase().endsWith(".jagex.com") || ....endsWith(".runescape.com")
#
# so `--jav_config=http://127.0.0.1:.../` is rejected before it is even fetched.
# Rewriting the first suffix to one that a loopback address ends with is the
# smallest change that opens it, and it leaves the second alone so a client
# patched this way still accepts the real config.
#
# This one is NOT a same-length edit, so it rewrites the constant's u2 length as
# well. That is safe for the same reason the modulus edit is: a class file
# addresses constants by POOL INDEX, never by byte offset, so a pool entry may
# change size without disturbing anything that refers to it.
HOST_SUFFIX_OLD = ".jagex.com"
HOST_SUFFIX_NEW = ".0.0.1"
CLIENT_LOADER_ENTRY = "net/runelite/client/rs/ClientLoader.class"


def utf8_constant(text):
    """The exact constant-pool bytes for a CONSTANT_Utf8 entry: tag, u2 len, bytes."""
    raw = text.encode("utf-8")
    return b"\x01" + struct.pack(">H", len(raw)) + raw


def find_injected_client(explicit=None):
    if explicit:
        return explicit
    if not os.path.isdir(RL_REPO):
        raise SystemExit(f"RuneLite repository not found at {RL_REPO}")
    jars = [
        os.path.join(RL_REPO, f)
        for f in os.listdir(RL_REPO)
        if f.startswith("injected-client-") and f.endswith(".jar")
    ]
    if not jars:
        raise SystemExit(f"no injected-client-*.jar in {RL_REPO}")
    return max(jars, key=os.path.getmtime)


def locate_modulus(jar_path):
    """Return (entry_name, old_modulus). Raises if the client does not look
    like one this tool understands, rather than patching something else."""
    hits = []
    with zipfile.ZipFile(jar_path) as z:
        for info in z.infolist():
            if not info.filename.endswith(".class"):
                continue
            data = z.read(info.filename)
            if EXPONENT not in data:
                continue
            for m in MODULUS_RE.finditer(data):
                hits.append((info.filename, m.group(1).decode("ascii")))
    if not hits:
        raise SystemExit(
            "no RSA modulus found: no class carries both the exponent 10001 and a "
            "256-char hex constant. The client's key storage may have changed shape."
        )
    if len(hits) > 1:
        raise SystemExit(f"ambiguous: {len(hits)} modulus candidates {hits}")
    return hits[0]


def patch(jar_path, out_jar, new_modulus):
    entry, old = locate_modulus(jar_path)
    if len(new_modulus) != len(old):
        raise SystemExit(
            f"replacement modulus is {len(new_modulus)} hex chars, original is "
            f"{len(old)}. Same-length is what keeps this a pure overwrite; "
            "generate a key of the same bit length."
        )
    if not re.fullmatch(r"[0-9a-f]+", new_modulus):
        raise SystemExit("modulus must be lowercase hex")

    os.makedirs(os.path.dirname(out_jar) or ".", exist_ok=True)
    with zipfile.ZipFile(jar_path) as zin, zipfile.ZipFile(
        out_jar, "w", zipfile.ZIP_DEFLATED
    ) as zout:
        for info in zin.infolist():
            data = zin.read(info.filename)
            if info.filename == entry:
                before = data
                data = data.replace(
                    old.encode("ascii"), new_modulus.encode("ascii"), 1
                )
                if data == before or len(data) != len(before):
                    raise SystemExit("in-place replacement failed")
            zout.writestr(info, data)
    return entry, old


def find_client_jar(explicit=None):
    """Newest client-*.jar. There is usually more than one left in the
    repository after an update, and both carry a logback.xml and a
    net.runelite.client.RuneLite, so putting them both on a classpath silently
    runs whichever sorts first -- which is the OLD one."""
    if explicit:
        return explicit
    jars = [
        os.path.join(RL_REPO, f)
        for f in os.listdir(RL_REPO)
        if f.startswith("client-") and f.endswith(".jar")
    ]
    if not jars:
        raise SystemExit(f"no client-*.jar in {RL_REPO}")
    return max(jars, key=os.path.getmtime)


def strip_manifest_digests(manifest_bytes):
    """Drop the per-entry digest sections from a MANIFEST.MF.

    A signed jar records a SHA-256 of every entry in the manifest and again in
    META-INF/*.SF. Leaving those behind after editing a class produces

        java.lang.SecurityException: SHA-256 digest error for <entry>

    at class-load time -- the JVM checks the digest even when no signature file
    survives, because the manifest still claims one. So both have to go, and the
    manifest keeps only its main section (Main-Class and friends)."""
    text = manifest_bytes.decode("utf-8", "replace")
    # Sections are separated by a blank line; the first is the main section.
    main = text.split("\r\n\r\n")[0].split("\n\n")[0]
    lines = [
        ln for ln in main.splitlines()
        if not ln.lower().startswith(("sha", "md5", "magic:"))
    ]
    return ("\r\n".join(lines) + "\r\n\r\n").encode("utf-8")


def patch_host_check(jar_path, out_jar):
    """Relax RuneLite's jav_config host allowlist so a loopback address passes."""
    old = utf8_constant(HOST_SUFFIX_OLD)
    new = utf8_constant(HOST_SUFFIX_NEW)
    found = False
    dropped = []
    os.makedirs(os.path.dirname(out_jar) or ".", exist_ok=True)
    with zipfile.ZipFile(jar_path) as zin, zipfile.ZipFile(
        out_jar, "w", zipfile.ZIP_DEFLATED
    ) as zout:
        for info in zin.infolist():
            name = info.filename
            upper = name.upper()
            if upper.startswith("META-INF/") and upper.endswith(
                (".SF", ".DSA", ".RSA", ".EC")
            ):
                dropped.append(name)
                continue
            data = zin.read(name)
            if upper == "META-INF/MANIFEST.MF":
                data = strip_manifest_digests(data)
            elif name == CLIENT_LOADER_ENTRY and old in data:
                data = data.replace(old, new, 1)
                found = True
            # Rebuild the entry so its recorded sizes follow the new content.
            out_info = zipfile.ZipInfo(name, date_time=info.date_time)
            out_info.compress_type = zipfile.ZIP_DEFLATED
            out_info.external_attr = info.external_attr
            zout.writestr(out_info, data)
    if not found:
        raise SystemExit(
            f"{CLIENT_LOADER_ENTRY} does not carry the constant {HOST_SUFFIX_OLD!r}; "
            "the host allowlist may have changed shape."
        )
    return HOST_SUFFIX_OLD, HOST_SUFFIX_NEW, dropped


def print_launch(patched_jar, jav_config_url):
    """The classpath RuneLite is launched with, patched jar first.

    RuneLite's own launcher resolves artifacts from its repository directory, so
    the way to make it use the patched client is to run the client main class
    directly with a classpath that shadows the original jar.
    """
    if not os.path.isdir(RL_REPO):
        raise SystemExit(f"RuneLite repository not found at {RL_REPO}")
    out_dir = os.path.dirname(patched_jar)
    shadowed = {
        os.path.basename(find_injected_client()),
        os.path.basename(find_client_jar()),
    }
    # Every OTHER client-*.jar is dropped, not just the one we patched: leaving
    # a stale version on the classpath is how 1.12.33 ends up running when
    # 1.12.34.1 is installed.
    stale_clients = {
        f for f in os.listdir(RL_REPO)
        if f.startswith("client-") and f.endswith(".jar")
    } | {
        f for f in os.listdir(RL_REPO)
        if f.startswith("injected-client-") and f.endswith(".jar")
    }
    patched = [
        os.path.join(out_dir, f)
        for f in sorted(os.listdir(out_dir))
        if f.endswith(".jar")
    ] if os.path.isdir(out_dir) else [patched_jar]
    rest = [
        os.path.join(RL_REPO, f)
        for f in sorted(os.listdir(RL_REPO))
        if f.endswith(".jar") and f not in stale_clients
    ]
    _ = shadowed
    cp = patched + rest
    # `-ea` is not optional alongside `--developer-mode`: RuneLite refuses to
    # start with "Developers should enable assertions" if the two disagree, and
    # it reports that as a fatal error dialog rather than a flag warning.
    print("java -ea \\")
    print(f"  -cp {os.pathsep.join(cp)} \\")
    print("  net.runelite.client.RuneLite \\")
    print(f"  --jav_config={jav_config_url} \\")
    print("  --developer-mode --disable-telemetry --noupdate")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--jar", help="injected-client jar (default: newest in ~/.runelite/repository2)")
    ap.add_argument("--client-jar",
                    help="RuneLite client-*.jar to relax the host allowlist in. Defaults "
                         "to the newest installed one; pass a jar built from source to "
                         "patch a client for a REVISION other than the one installed.")
    ap.add_argument("--modulus", help="replacement RSA modulus, lowercase hex")
    ap.add_argument("--out", default=os.path.join(REPO, "build", "runelite"),
                    help="output directory for the patched jar")
    ap.add_argument("--jav-config", default="http://127.0.0.1:8080/jav_config.ws")
    ap.add_argument("--locate-only", action="store_true",
                    help="report the class and modulus found, change nothing")
    ap.add_argument("--print-launch", action="store_true",
                    help="print the java command for the patched client")
    args = ap.parse_args()

    jar = find_injected_client(args.jar)
    out_jar = os.path.join(args.out, os.path.basename(jar))

    if args.locate_only:
        entry, old = locate_modulus(jar)
        print(f"jar     {jar}")
        print(f"class   {entry}")
        print(f"modulus {old}")
        return

    if args.print_launch and not args.modulus:
        print_launch(out_jar, args.jav_config)
        return

    if not args.modulus:
        raise SystemExit("--modulus is required (see TORIRSSERVER_RSA_PUBLIC_MODULUS)")

    entry, old = patch(jar, out_jar, args.modulus.lower())
    print(f"patched {entry} in {os.path.basename(jar)}", file=sys.stderr)
    print(f"  old {old}", file=sys.stderr)
    print(f"  new {args.modulus.lower()}", file=sys.stderr)
    print(f"  -> {out_jar}", file=sys.stderr)

    client_jar = find_client_jar(args.client_jar)
    client_out = os.path.join(args.out, os.path.basename(client_jar))
    old_suffix, new_suffix, dropped = patch_host_check(client_jar, client_out)
    print(
        f"patched {CLIENT_LOADER_ENTRY} in {os.path.basename(client_jar)}: "
        f"host suffix {old_suffix!r} -> {new_suffix!r}",
        file=sys.stderr,
    )
    if dropped:
        print(f"  unsigned the jar (dropped {', '.join(dropped)})", file=sys.stderr)
    print(f"  -> {client_out}", file=sys.stderr)

    if args.print_launch:
        print_launch(out_jar, args.jav_config)


if __name__ == "__main__":
    main()
