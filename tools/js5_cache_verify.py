#!/usr/bin/env python3
"""Compare a JS5-built sparse cache against the reference cache it was built from.

A JS5-built cache is never byte-identical to its source on disk: groups arrive in
network order, so their sector chains are laid out differently, and the client
writes a 32-bit version trailer where an older cache may carry a 16-bit one (see
JS5_INCREMENTAL_CACHE.md, "Container vs. trailer"). What must match is the JS5
*container* of every group -- the bytes the client actually decodes.

So this compares containers, not files. The container length is self-describing
from its 5-byte header, which is what lets the trailer be split off exactly
rather than guessed at.

    tools/js5_cache_verify.py <reference-cache-dir> <built-cache-dir>

Exit status is 0 when every group present in the built cache matches the
reference and nothing is corrupt. Missing groups are reported but are not a
failure by themselves: a partially filled cache is the normal state of an
incremental one. Use --require-complete to demand a full mirror.
"""

import argparse
import os
import struct
import sys

SECTOR_SIZE = 520


def read_index(path):
    """Yield (group_id, size, first_sector) for each populated idx entry."""
    with open(path, "rb") as handle:
        blob = handle.read()
    for group in range(len(blob) // 6):
        entry = blob[group * 6 : group * 6 + 6]
        size = int.from_bytes(entry[0:3], "big")
        sector = int.from_bytes(entry[3:6], "big")
        # A zero sector is an empty slot: sector 0 is the first sector of the
        # dat2 itself and can never be a group's start.
        if size > 0 and sector > 0:
            yield group, size, sector


def read_group(dat2, archive, group, size, first_sector):
    """Walk the sector chain and return the stored blob, or None if it breaks."""
    out = bytearray()
    sector = first_sector
    chunk = 0
    extended = group > 0xFFFF
    header_size = 10 if extended else 8
    payload_size = SECTOR_SIZE - header_size

    while len(out) < size:
        if sector <= 0:
            return None
        dat2.seek(sector * SECTOR_SIZE)
        raw = dat2.read(SECTOR_SIZE)
        if len(raw) < header_size:
            return None

        if extended:
            got_group = struct.unpack_from(">I", raw, 0)[0]
            got_chunk = struct.unpack_from(">H", raw, 4)[0]
            next_sector = int.from_bytes(raw[6:9], "big")
            got_archive = raw[9]
        else:
            got_group = struct.unpack_from(">H", raw, 0)[0]
            got_chunk = struct.unpack_from(">H", raw, 2)[0]
            next_sector = int.from_bytes(raw[4:7], "big")
            got_archive = raw[7]

        # Every sector restates who owns it. Checking that is what turns a
        # truncated or half-overwritten chain into a reported error instead of
        # silently decoded garbage.
        if got_group != group or got_chunk != chunk or got_archive != archive:
            return None

        take = min(payload_size, size - len(out))
        out.extend(raw[header_size : header_size + take])
        sector = next_sector
        chunk += 1

    return bytes(out)


def container_length(blob):
    """Length of the JS5 container at the head of a stored group blob.

    The 5-byte header is self-describing: a compression type and the compressed
    payload length. Every compressed type carries an extra 4-byte decompressed
    length inside the container. Whatever follows is the local version trailer,
    which is cache-local bookkeeping and not part of what the server sent.
    """
    if len(blob) < 5:
        return None
    compression = blob[0]
    compressed = struct.unpack_from(">I", blob, 1)[0]
    length = 5 + compressed + (0 if compression == 0 else 4)
    return length if length <= len(blob) else None


def collect(cache_dir):
    """Map (archive, group) -> container bytes for every readable group."""
    groups = {}
    broken = []
    dat2_path = os.path.join(cache_dir, "main_file_cache.dat2")
    if not os.path.exists(dat2_path):
        sys.exit(f"js5_cache_verify: no main_file_cache.dat2 in {cache_dir}")

    with open(dat2_path, "rb") as dat2:
        for name in sorted(os.listdir(cache_dir)):
            if not name.startswith("main_file_cache.idx"):
                continue
            archive = int(name[len("main_file_cache.idx") :])
            for group, size, sector in read_index(os.path.join(cache_dir, name)):
                blob = read_group(dat2, archive, group, size, sector)
                if blob is None:
                    broken.append((archive, group))
                    continue
                length = container_length(blob)
                if length is None:
                    broken.append((archive, group))
                    continue
                groups[(archive, group)] = blob[:length]
    return groups, broken


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", help="cache the JS5 server was pointed at")
    parser.add_argument("built", help="cache the client built over JS5")
    parser.add_argument(
        "--require-complete",
        action="store_true",
        help="fail when the built cache is missing groups the reference has",
    )
    args = parser.parse_args()

    reference, reference_broken = collect(args.reference)
    built, built_broken = collect(args.built)

    missing = sorted(set(reference) - set(built))
    extra = sorted(set(built) - set(reference))
    mismatched = sorted(
        key for key in set(built) & set(reference) if built[key] != reference[key]
    )

    print(f"reference : {len(reference)} groups ({len(reference_broken)} unreadable)")
    print(f"built     : {len(built)} groups ({len(built_broken)} unreadable)")
    print(f"matching  : {len(set(built) & set(reference)) - len(mismatched)}")
    print(f"mismatched: {len(mismatched)}")
    print(f"missing   : {len(missing)}")
    print(f"extra     : {len(extra)}")

    for label, rows in (
        ("mismatched", mismatched),
        ("unreadable-in-built", built_broken),
        ("extra", extra),
    ):
        for archive, group in rows[:10]:
            print(f"  {label}: {archive}/{group}")
        if len(rows) > 10:
            print(f"  {label}: ... and {len(rows) - 10} more")

    if missing:
        coverage = 100.0 * len(built) / len(reference) if reference else 0.0
        print(f"coverage  : {coverage:.2f}%")

    failed = bool(mismatched or built_broken or extra)
    if args.require_complete and missing:
        failed = True
    print("RESULT: " + ("FAIL" if failed else "PASS"))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
