#!/usr/bin/env python3
"""A stand-in game server for the editor's live link tests.

Speaks the game server's control protocol (docs/MAPED_IMPLEMENTATION.md
section 4) on one TCP port and nothing else: the first byte of a connection
must be 0xED, then little-endian frames [u32 type][u32 length][payload],
strings u32 length + bytes.

    mock_control_peer.py <port> [--rev NAME]

The second form is exactly how the editor server's native link LAUNCHES a
game server (`<server_bin> <game_port> --rev <rev>`), so this script serves as
the launched torirsserver too.

Environment:
    MOCK_PEER_LOG     file every request is appended to, one JSON object per
                      line (required); the launch command line and the
                      TORIRSSERVER_* environment are its first line
    MOCK_PEER_TICKET  when set, CTL_HELLO must carry this ticket

Cheat lines steer the next reload (they are answered like any cheat):
    failnext     the next RELOAD answers status 1
    restartnext  the next RELOAD lists a restart-needed reason
    dropnext     the next RELOAD closes the connection without answering
"""

import json
import os
import socket
import struct
import sys

CTL_HELLO, CTL_STATUS, CTL_RELOAD, CTL_CHEAT, CTL_SUBSCRIBE, CTL_BYE = 1, 2, 3, 4, 5, 6
F_HELLO, F_STATUS, F_LINE, F_RELOAD_DONE, F_VARS = 129, 130, 131, 132, 133


def log(record):
    with open(os.environ["MOCK_PEER_LOG"], "a", encoding="utf-8") as out:
        out.write(json.dumps(record) + "\n")


def u32(value):
    return struct.pack("<I", value & 0xFFFFFFFF)


def string(text):
    data = text.encode("utf-8")
    return u32(len(data)) + data


class Reader:
    def __init__(self, data):
        self.data = data
        self.at = 0

    def u32(self):
        value = struct.unpack_from("<I", self.data, self.at)[0]
        self.at += 4
        return value

    def i32(self):
        value = struct.unpack_from("<i", self.data, self.at)[0]
        self.at += 4
        return value

    def string(self):
        length = self.u32()
        text = self.data[self.at:self.at + length].decode("utf-8")
        self.at += length
        return text

    def done(self):
        return self.at == len(self.data)


class World:
    def __init__(self):
        self.tick = 100
        self.reloads = 0
        self.stale = ["server/scripts/areas/edited/scripts/stale_example.rs2"]
        self.fail_next = False
        self.restart_next = False
        self.drop_next = False


def frame(kind, payload=b""):
    return u32(kind) + u32(len(payload)) + payload


def status_payload(world):
    world.tick += 1
    body = u32(2) + u32(world.tick)
    body += string("scripts-%d" % world.reloads) + string("servpack-%d" % world.reloads)
    body += u32(len(world.stale))
    for row in world.stale:
        body += string(row)
    return body


def recv_exact(conn, count):
    data = b""
    while len(data) < count:
        chunk = conn.recv(count - len(data))
        if not chunk:
            return None
        data += chunk
    return data


def serve(conn, world):
    first = recv_exact(conn, 1)
    if first is None or first[0] != 0xED:
        log({"event": "bad-select", "byte": None if first is None else first[0]})
        return
    while True:
        head = recv_exact(conn, 8)
        if head is None:
            log({"event": "closed"})
            return
        kind, length = struct.unpack("<II", head)
        body = recv_exact(conn, length) if length else b""
        if body is None:
            log({"event": "closed"})
            return
        r = Reader(body)
        if kind == CTL_HELLO:
            version = r.u32()
            ticket = r.string()
            wanted = os.environ.get("MOCK_PEER_TICKET")
            ok = 1 if version == 1 and (wanted is None or ticket == wanted) else 0
            log({"req": "hello", "version": version, "ticket": ticket, "ok": ok})
            conn.sendall(frame(F_HELLO, u32(1) + u32(ok) + string("mockworld") + u32(2)))
            if not ok:
                return
        elif kind == CTL_STATUS:
            log({"req": "status"})
            conn.sendall(frame(F_STATUS, status_payload(world)))
        elif kind == CTL_RELOAD:
            manifest = r.string()
            archives = [[r.u32(), r.u32()] for _ in range(r.u32())]
            squares = [[r.i32(), r.i32()] for _ in range(r.u32())]
            spawns = [r.string() for _ in range(r.u32())]
            log({"req": "reload", "manifest": manifest, "archives": archives, "squares": squares,
                 "spawns": spawns, "well_formed": r.done()})
            if world.drop_next:
                world.drop_next = False
                log({"event": "dropped"})
                return
            conn.sendall(frame(F_LINE, string("reloading %d archive(s)" % len(archives))))
            status = 0
            restart = []
            if world.fail_next:
                world.fail_next = False
                status = 1
            if world.restart_next:
                world.restart_next = False
                restart = ["fields/npc.ini changed"]
            if status == 0:
                world.reloads += 1
                world.stale = []
            done = u32(status) + u32(3) + u32(len(archives)) + u32(len(squares)) + u32(len(spawns))
            done += u32(len(restart))
            for row in restart:
                done += string(row)
            conn.sendall(frame(F_RELOAD_DONE, done))
        elif kind == CTL_CHEAT:
            player = r.string()
            line = r.string()
            log({"req": "cheat", "player": player, "line": line})
            if line == "failnext":
                world.fail_next = True
            elif line == "restartnext":
                world.restart_next = True
            elif line == "dropnext":
                world.drop_next = True
            conn.sendall(frame(F_LINE, string("ran ::%s for %s" % (line, player))))
            conn.sendall(frame(F_STATUS, status_payload(world)))
        elif kind == CTL_SUBSCRIBE:
            player = r.string()
            varps = [r.u32() for _ in range(r.u32())]
            log({"req": "subscribe", "player": player, "varps": varps})
            if varps:
                body = string(player) + u32(len(varps))
                for varp in varps:
                    body += u32(varp) + u32(varp * 10)
                conn.sendall(frame(F_VARS, body))
        elif kind == CTL_BYE:
            log({"req": "bye"})
            return
        else:
            log({"req": "unknown", "type": kind})


def main():
    port = int(sys.argv[1])
    log({"event": "start", "argv": sys.argv[1:],
         "env": {k: v for k, v in os.environ.items() if k.startswith("TORIRSSERVER_")}})
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(("127.0.0.1", port))
    listener.listen(4)
    log({"event": "listening", "port": port})
    world = World()
    while True:
        conn, _ = listener.accept()
        log({"event": "accepted"})
        try:
            serve(conn, world)
        except (ConnectionError, OSError) as error:
            log({"event": "error", "what": str(error)})
        finally:
            conn.close()


if __name__ == "__main__":
    main()
