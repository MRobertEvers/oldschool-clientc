#!/usr/bin/env python3
"""Loopback integration test for the dedicated JS5 server."""

import argparse
import socket
import struct
import subprocess
import sys
import time


def free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(("127.0.0.1", 0))
        return listener.getsockname()[1]


def recv_exact(sock, size, xor_key=0):
    data = bytearray()
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise AssertionError("server closed before the expected response")
        data.extend(byte ^ xor_key for byte in chunk)
    return bytes(data)


def handshake(port, revision=239):
    sock = socket.create_connection(("127.0.0.1", port), timeout=5.0)
    sock.settimeout(5.0)
    sock.sendall(bytes([15]) + struct.pack(">I4I", revision, 0, 0, 0, 0))
    status = recv_exact(sock, 1)[0]
    return sock, status


def fetch(sock, archive, group, xor_key=0, urgent=True):
    sock.sendall(bytes([1 if urgent else 0, archive]) + struct.pack(">H", group))
    head = recv_exact(sock, 8, xor_key)
    assert head[0] == archive and struct.unpack(">H", head[1:3])[0] == group
    compression = head[3]
    compressed_size = struct.unpack(">I", head[4:8])[0]
    total = 5 + compressed_size + (0 if compression == 0 else 4)
    container = bytearray(head[3:])
    wire_position = 8
    while len(container) < total:
        if wire_position % 512 == 0:
            marker = recv_exact(sock, 1, xor_key)
            if marker != b"\xff":
                raise AssertionError(f"invalid JS5 block marker: {marker!r}")
            wire_position += 1
        take = min(total - len(container), 512 - (wire_position % 512))
        container.extend(recv_exact(sock, take, xor_key))
        wire_position += take
    return bytes(container)


def start_server(executable, cache, port, max_clients):
    process = subprocess.Popen(
        [
            executable,
            "--cache",
            cache,
            "--bind",
            "127.0.0.1",
            "--port",
            str(port),
            "--revision",
            "239",
            "--max-clients",
            str(max_clients),
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    deadline = time.monotonic() + 10.0
    while time.monotonic() < deadline:
        if process.poll() is not None:
            stderr = process.stderr.read()
            raise AssertionError(f"server exited early ({process.returncode}): {stderr}")
        line = process.stdout.readline().strip()
        if line.startswith("READY "):
            return process
    raise AssertionError("server did not print READY")


def stop_server(process):
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5.0)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5.0)


def run(executable, client_executable, cache):
    checks = 0
    port = free_port()
    process = start_server(executable, cache, port, max_clients=4)
    try:
        client, status = handshake(port)
        assert status == 0
        checks += 1
        master = fetch(client, 255, 255)
        assert len(master) == 205 and master[0] == 0
        checks += 1
        client.close()

        wrong, status = handshake(port, revision=238)
        assert status == 6
        checks += 1
        wrong.close()

        keyed, status = handshake(port)
        assert status == 0
        key = 0x5A
        keyed.sendall(bytes([4, key, 0, 0]))
        keyed_master = fetch(keyed, 255, 255, xor_key=key)
        assert keyed_master == master
        checks += 1
        keyed.close()

        # A non-reading large response must not prevent another client from
        # receiving the master index through the single-threaded reactor.
        stalled, status = handshake(port)
        assert status == 0
        stalled.sendall(bytes([1, 22]) + struct.pack(">H", 837))
        peer, status = handshake(port)
        assert status == 0
        peer_master = fetch(peer, 255, 255)
        if peer_master != master:
            raise AssertionError("peer master index differs from the initial response")
        checks += 1
        peer.close()
        stalled.close()

        completed = subprocess.run(
            [client_executable, str(port)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=30.0,
        )
        assert completed.returncode == 0, completed.stdout + completed.stderr
        checks += 1
    finally:
        stop_server(process)

    port = free_port()
    process = start_server(executable, cache, port, max_clients=1)
    try:
        occupying, status = handshake(port)
        assert status == 0
        checks += 1
        rejected, status = handshake(port)
        assert status == 7
        checks += 1
        rejected.close()
        occupying.close()
    finally:
        stop_server(process)

    print(f"js5_server_loopback: {checks} checks passed")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", required=True)
    parser.add_argument("--client", required=True)
    parser.add_argument("--cache", required=True)
    args = parser.parse_args()
    run(args.server, args.client, args.cache)


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"js5_server_loopback: FAIL: {error}", file=sys.stderr)
        raise
