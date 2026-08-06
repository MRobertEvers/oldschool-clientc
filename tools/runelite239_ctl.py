#!/usr/bin/env python3
"""Small CLI for the instrumented revision-239 client's JCTL channel."""

from __future__ import annotations

import argparse
import socket
import sys
import time


HOST = "127.0.0.1"
PORT = 43601


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--events", action="store_true", help="subscribe and stream EVENTS")
    parser.add_argument("--host", default=HOST)
    parser.add_argument("--port", default=PORT, type=int)
    parser.add_argument(
        "--retry",
        type=float,
        default=0.0,
        metavar="SECONDS",
        help="wait up to this long for JCTL to listen (useful before client launch)",
    )
    parser.add_argument("commands", nargs="*", help="one quoted JCTL command per argument")
    args = parser.parse_args()

    deadline = time.monotonic() + args.retry

    def connect() -> socket.socket:
        while True:
            try:
                return socket.create_connection((args.host, args.port), timeout=5)
            except OSError:
                if args.retry <= 0 or time.monotonic() >= deadline:
                    raise
                time.sleep(0.05)

    if args.events:
        # A launch can briefly expose the previous client's listener before the
        # replacement JVM owns the port. Treat a reset/EOF during --retry as
        # another not-ready state, otherwise an EVENTS process started before
        # RuneLite can silently die exactly while the client is coming up.
        while True:
            try:
                with connect() as sock:
                    sock.settimeout(None)
                    stream = sock.makefile("rwb", buffering=0)
                    stream.write(b"EVENTS\n")
                    for raw in stream:
                        print(
                            raw.decode("utf-8", errors="replace").rstrip("\r\n"),
                            flush=True,
                        )
            except OSError:
                if args.retry <= 0 or time.monotonic() >= deadline:
                    raise
            if args.retry <= 0 or time.monotonic() >= deadline:
                return 0
            time.sleep(0.05)

    sock = connect()
    with sock:
        sock.settimeout(None)
        stream = sock.makefile("rwb", buffering=0)

        commands = args.commands or [line.rstrip("\r\n") for line in sys.stdin if line.strip()]
        if not commands:
            parser.error("provide a command, commands on stdin, or --events")
        for command in commands:
            stream.write((command + "\n").encode())
            reply = stream.readline().decode("utf-8", errors="replace").rstrip("\r\n")
            print(reply)
            if not reply.startswith("ok "):
                return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
