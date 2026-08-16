#!/usr/bin/env python3
"""Small multi-connection loopback TCP forwarder for isolated client tests."""

from __future__ import annotations

import argparse
import socket
import threading


def pump(source: socket.socket, destination: socket.socket) -> None:
    try:
        while data := source.recv(65536):
            destination.sendall(data)
    except OSError:
        pass
    finally:
        try:
            destination.shutdown(socket.SHUT_WR)
        except OSError:
            pass


def relay(client: socket.socket, target_host: str, target_port: int) -> None:
    upstream = socket.create_connection((target_host, target_port))
    sockets = (client, upstream)
    try:
        outgoing = threading.Thread(target=pump, args=(client, upstream),
                                    daemon=True)
        incoming = threading.Thread(target=pump, args=(upstream, client),
                                    daemon=True)
        outgoing.start()
        incoming.start()
        outgoing.join()
        incoming.join()
    finally:
        for sock in sockets:
            try:
                sock.close()
            except OSError:
                pass


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--listen-host", default="127.0.0.1")
    parser.add_argument("--listen-port", type=int, required=True)
    parser.add_argument("--target-host", default="127.0.0.1")
    parser.add_argument("--target-port", type=int, required=True)
    args = parser.parse_args()

    with socket.create_server((args.listen_host, args.listen_port),
                              reuse_port=False) as listener:
        print(f"tcp_proxy: {args.listen_host}:{args.listen_port} -> "
              f"{args.target_host}:{args.target_port}", flush=True)
        while True:
            client, address = listener.accept()
            print(f"tcp_proxy: accepted {address[0]}:{address[1]}", flush=True)
            threading.Thread(target=relay, args=(client, args.target_host,
                                                 args.target_port),
                             daemon=True).start()


if __name__ == "__main__":
    main()
