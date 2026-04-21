#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
Serve a directory over HTTP (Python 3.2+).

Similar to ``python -m http.server`` on newer interpreters: single-threaded
``SimpleHTTPRequestHandler``, optional port and bind address, and a root
directory (via ``chdir``, matching how the early stdlib behaved).

Example::

    python scripts/dir_http_server_py32.py 8080 --directory ./public
"""

from __future__ import print_function

import argparse
import os
import sys


def main():
    parser = argparse.ArgumentParser(
        description="Serve files from a directory over HTTP "
        "(like python -m http.server)."
    )
    parser.add_argument(
        "port",
        nargs="?",
        type=int,
        default=8000,
        help="Port to listen on (default: 8000)",
    )
    parser.add_argument(
        "--bind",
        "-b",
        metavar="ADDRESS",
        default="",
        help="Address to bind (default: all interfaces)",
    )
    parser.add_argument(
        "--directory",
        "-d",
        metavar="PATH",
        default=os.getcwd(),
        help="Directory to serve (default: current working directory)",
    )
    args = parser.parse_args()

    try:
        import http.server
    except ImportError:
        sys.stderr.write("This script requires Python 3.x with http.server.\n")
        return 1

    root = os.path.abspath(os.path.expanduser(args.directory))
    if not os.path.isdir(root):
        sys.stderr.write("Not a directory: {0}\n".format(root))
        return 1

    os.chdir(root)

    handler = http.server.SimpleHTTPRequestHandler
    server_address = (args.bind, args.port)

    httpd = http.server.HTTPServer(server_address, handler)

    host = args.bind if args.bind else "0.0.0.0"
    sys.stdout.write(
        "Serving HTTP on {0} port {1} (http://127.0.0.1:{1}/) ...\n".format(
            host, args.port
        )
    )
    sys.stdout.write("Serving directory: {0}\n".format(root))
    sys.stdout.flush()

    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        sys.stderr.write("\nKeyboard interrupt received, exiting.\n")
    finally:
        httpd.close()

    return 0


if __name__ == "__main__":
    sys.exit(main())
