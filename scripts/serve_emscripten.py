#!/usr/bin/env python3
import http.server
import argparse
import functools
import socketserver


class NoCacheRequestHandler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        # Disable all caching
        self.send_header(
            "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0")
        self.send_header("Pragma", "no-cache")
        self.send_header("Expires", "0")
        super().end_headers()


class ReusableThreadingTCPServer(socketserver.ThreadingTCPServer):
    # This enables immediate reuse of the port
    allow_reuse_address = True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("port", nargs="?", default=8000, type=int,
                        help="Specify alternate port (default: 8000)")
    parser.add_argument("--bind", "-b", default="",
                        help="Specify alternate bind address (default: all interfaces)")
    parser.add_argument("--directory", "-d", default=None,
                        help="Specify alternative directory (default: current directory)")
    args = parser.parse_args()

    handler_class = functools.partial(
        NoCacheRequestHandler,
        directory=args.directory
    )

    with ReusableThreadingTCPServer((args.bind, args.port), handler_class) as httpd:
        addr = args.bind or "0.0.0.0"
        print(f"Serving HTTP (no-cache) on http://{addr}:{args.port}/ "
              f"(directory: {args.directory or 'current directory'})")
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nShutting down.")
            httpd.server_close()


if __name__ == "__main__":
    main()
