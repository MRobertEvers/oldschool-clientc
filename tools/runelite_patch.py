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


def print_launch(patched_jar, jav_config_url):
    """The classpath RuneLite is launched with, patched jar first.

    RuneLite's own launcher resolves artifacts from its repository directory, so
    the way to make it use the patched client is to run the client main class
    directly with a classpath that shadows the original jar.
    """
    if not os.path.isdir(RL_REPO):
        raise SystemExit(f"RuneLite repository not found at {RL_REPO}")
    jars = [
        os.path.join(RL_REPO, f)
        for f in sorted(os.listdir(RL_REPO))
        if f.endswith(".jar")
    ]
    original = os.path.basename(find_injected_client())
    cp = [patched_jar] + [j for j in jars if os.path.basename(j) != original]
    print("java \\")
    print(f"  -cp {os.pathsep.join(cp)} \\")
    print("  net.runelite.client.RuneLite \\")
    print(f"  --jav_config={jav_config_url} \\")
    print("  --developer-mode --disable-telemetry --noupdate --insecure-skip-tls-verification")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--jar", help="injected-client jar (default: newest in ~/.runelite/repository2)")
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
        raise SystemExit("--modulus is required (see MOCK230_RSA_PUBLIC_MODULUS)")

    entry, old = patch(jar, out_jar, args.modulus.lower())
    print(f"patched {entry} in {os.path.basename(jar)}", file=sys.stderr)
    print(f"  old {old}", file=sys.stderr)
    print(f"  new {args.modulus.lower()}", file=sys.stderr)
    print(f"  -> {out_jar}", file=sys.stderr)
    if args.print_launch:
        print_launch(out_jar, args.jav_config)


if __name__ == "__main__":
    main()
