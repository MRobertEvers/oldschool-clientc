#!/usr/bin/env python3
"""The browser's route into ToriRSMapEd, exercised as a browser would take it.

A page cannot open a TCP socket, so an emscripten client's connect() arrives
as an HTTP upgrade and every byte after it is RFC 6455 framed. This probe is
deliberately written against the wire rather than against our own client code:
it speaks the handshake and the framing by hand, so it fails if the daemon
stops being reachable from a real browser, not merely if it stops agreeing
with our C.

Three things it pins down, each a way a WebSocket server is silently
unreachable from a page:

  * a correct Sec-WebSocket-Accept, or the browser closes the connection
  * the offered subprotocol echoed back — emscripten asks for "binary" and a
    browser fails the connection outright if the server confirms nothing
  * the ToriRSMapEd protocol carried intact across frame boundaries, which is
    the part a hand-rolled framing layer gets wrong

    maped_ws_probe.py <port>
"""

import base64
import hashlib
import os
import socket
import struct
import sys

WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

checks = 0
failures = 0


def check(condition, what):
    global checks, failures
    checks += 1
    if not condition:
        print(f"FAIL: {what}")
        failures += 1


def mask_frame(payload, opcode=0x2):
    """A client frame. Clients MUST mask (RFC 6455 §5.1)."""
    mask = os.urandom(4)
    n = len(payload)
    if n < 126:
        header = bytes([0x80 | opcode, 0x80 | n])
    else:
        header = bytes([0x80 | opcode, 0x80 | 126]) + struct.pack(">H", n)
    return header + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(payload))


class Stream:
    """The protocol byte stream, reassembled out of WebSocket payloads.

    The daemon writes a protocol frame as its header and its body, so one
    ToriRSMapEd frame can arrive as several WebSocket messages — exactly what
    emscripten's socket emulation flattens back into a byte stream, and what
    this has to do too.
    """

    def __init__(self, sock):
        self.sock = sock
        self.bytes = bytearray()
        self.raw = bytearray()

    def _pump(self):
        chunk = self.sock.recv(65536)
        if not chunk:
            raise RuntimeError("the daemon closed the connection")
        self.raw += chunk
        while len(self.raw) >= 2:
            length = self.raw[1] & 0x7F
            offset = 2
            if length == 126:
                if len(self.raw) < 4:
                    return
                length = struct.unpack(">H", self.raw[2:4])[0]
                offset = 4
            check(self.raw[1] & 0x80 == 0, "a server frame is never masked")
            if len(self.raw) < offset + length:
                return
            self.bytes += self.raw[offset : offset + length]
            del self.raw[: offset + length]

    def take(self, count):
        while len(self.bytes) < count:
            self._pump()
        taken = bytes(self.bytes[:count])
        del self.bytes[:count]
        return taken

    def frame(self):
        """One ToriRSMapEd frame: [u32 type][u32 length][payload]."""
        frame_type, length = struct.unpack("<II", self.take(8))
        return frame_type, self.take(length)


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 43610
    sock = socket.create_connection(("127.0.0.1", port), timeout=10)
    sock.settimeout(10)

    key = base64.b64encode(os.urandom(16)).decode()
    sock.sendall(
        (
            "GET / HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {key}\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "Sec-WebSocket-Protocol: binary\r\n"
            "\r\n"
        ).encode()
    )
    response = sock.recv(8192).decode(errors="replace")

    check("101 Switching Protocols" in response, "the daemon answers 101")
    expected = base64.b64encode(
        hashlib.sha1((key + WS_GUID).encode()).digest()
    ).decode()
    check(expected in response, "Sec-WebSocket-Accept is derived from our key")
    check(
        "Sec-WebSocket-Protocol: binary" in response,
        "the offered subprotocol is echoed back",
    )

    stream = Stream(sock)

    # HELLO as a controller asking for a fresh Client.
    body = struct.pack("<III", 1, 2, 0)
    sock.sendall(mask_frame(struct.pack("<II", 1, len(body)) + body))
    frame_type, payload = stream.frame()
    check(frame_type == 129, "HELLO is answered with FACT_HELLO")
    version, writable, client_id = struct.unpack("<III", payload)
    check(version == 1, "the protocol version crosses the wire intact")
    check(client_id > 0, "the browser connection is granted a Client id")
    check(writable in (0, 1), "writability is reported")

    # SQUARE_LIST — a reply large enough to prove reassembly, not just framing.
    sock.sendall(mask_frame(struct.pack("<II", 2, 0)))
    frame_type, payload = stream.frame()
    check(frame_type == 130, "SQUARE_LIST is answered with FACT_SQUARE_LIST")
    status, count = struct.unpack("<II", payload[:8])
    check(status == 0, "the square list answers OK")
    check(
        len(payload) == 8 + count * 8,
        "the whole payload arrived, across however many WebSocket messages",
    )

    # A ping must come back as a pong, or a browser eventually drops the link.
    sock.sendall(mask_frame(b"ping", opcode=0x9))
    raw = sock.recv(4096)
    check(raw[0] & 0x0F == 0xA, "a ping is answered with a pong")

    sock.close()
    print(f"maped_ws_probe: {checks} checks, {failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
