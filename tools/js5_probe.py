#!/usr/bin/env python3
"""
Ask a JS5 server for a group and print what came back.

This exists because the master index (archive 255, group 255) is the one JS5
response that is not stored in any cache -- the server computes it -- so its
layout cannot be read off disk. Rather than guess it, point this at a server
that already serves it and measure:

    tools/js5_probe.py --host oldschool1.runescape.com --rev 239 --archive 255 --group 255
    tools/js5_probe.py --host 127.0.0.1 --rev 239 --archive 255 --group 2

Handshake, from RSProt's LoginChannelHandler:

    ->  p1 15 (INIT_JS5REMOTE_CONNECTION), p4 revision, p4 seed x4
    <-  p1 0  (SUCCESSFUL)  -- 6 means the revision was rejected

then, per group:

    ->  p1 opcode (0 prefetch / 1 urgent), p1 archive, p2 group
    <-  p1 archive, p2 group, 509 bytes, then 0xFF + 511 bytes, repeating
"""

import argparse
import socket
import struct
import sys

BLOCK = 512


def js5_fetch(host, port, revision, archive, group, timeout=15.0):
    s = socket.create_connection((host, port), timeout=timeout)
    s.settimeout(timeout)
    try:
        s.sendall(bytes([15]) + struct.pack(">I", revision) + struct.pack(">4I", 0, 0, 0, 0))
        status = s.recv(1)
        if not status:
            raise SystemExit("server closed during the JS5 handshake")
        if status[0] != 0:
            raise SystemExit(f"JS5 handshake refused, status {status[0]} (6 = out of date)")

        s.sendall(bytes([1, archive]) + struct.pack(">H", group))

        # Header: archive, group, then the container's own 5-byte header, which
        # is what tells us how long the whole thing is.
        head = recv_exact(s, 8)
        got_archive, got_group = head[0], struct.unpack(">H", head[1:3])[0]
        compression = head[3]
        compressed_len = struct.unpack(">I", head[4:8])[0]
        total = 5 + compressed_len + (0 if compression == 0 else 4)

        payload = bytearray(head[3:])
        # Remaining bytes of the first block, then 0xFF-separated blocks.
        first_remaining = min(total - len(payload), BLOCK - 3 - len(payload))
        if first_remaining > 0:
            payload += recv_exact(s, first_remaining)
        while len(payload) < total:
            sep = recv_exact(s, 1)
            if sep[0] != 0xFF:
                raise SystemExit(
                    f"expected a 0xFF block marker at offset {len(payload)}, got {sep[0]:#x}"
                )
            take = min(total - len(payload), BLOCK - 1)
            payload += recv_exact(s, take)
        return got_archive, got_group, compression, bytes(payload)
    finally:
        s.close()


def recv_exact(s, n):
    buf = b""
    while len(buf) < n:
        chunk = s.recv(n - len(buf))
        if not chunk:
            raise SystemExit(f"connection closed with {n - len(buf)} bytes outstanding")
        buf += chunk
    return buf


def describe_master_index(payload):
    """Print the master index both ways it is plausibly laid out, so the shape
    that is self-consistent is visible rather than assumed."""
    body = payload[5:]  # past the container header
    print(f"\nmaster index body: {len(body)} bytes")
    if len(body) % 8 == 0:
        n = len(body) // 8
        print(f"  reads as {n} archives of (p4 crc, p4 version):")
        for i in range(n):
            crc, ver = struct.unpack(">iI", body[i * 8:(i + 1) * 8])
            print(f"    archive {i:3d}  crc {crc & 0xffffffff:#010x}  version {ver}")
    else:
        print("  NOT a multiple of 8 -- not the plain (crc, version) form.")
        print(f"  first byte {body[0]} may be a format/count prefix")
        print(f"  hex: {body[:64].hex()}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", required=True)
    ap.add_argument("--port", type=int, default=43594)
    ap.add_argument("--rev", type=int, required=True)
    ap.add_argument("--archive", type=int, default=255)
    ap.add_argument("--group", type=int, default=255)
    ap.add_argument("--out", help="write the raw container to this file")
    args = ap.parse_args()

    a, g, compression, payload = js5_fetch(
        args.host, args.port, args.rev, args.archive, args.group
    )
    print(f"{args.host}:{args.port} rev {args.rev} -> archive {a} group {g}")
    print(f"  compression {compression}, container {len(payload)} bytes")
    print(f"  first 32: {payload[:32].hex()}")
    if args.archive == 255 and args.group == 255 and compression == 0:
        describe_master_index(payload)
    if args.out:
        with open(args.out, "wb") as fh:
            fh.write(payload)
        print(f"  wrote {args.out}")


if __name__ == "__main__":
    main()
