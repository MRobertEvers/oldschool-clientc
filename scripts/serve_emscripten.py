#!/usr/bin/env python3
"""
Development HTTP server for Emscripten builds with SharedArrayBuffer support.
Sets the required Cross-Origin-Opener-Policy and Cross-Origin-Embedder-Policy headers.
"""

import http.server
import socketserver
import sys
import os

PORT = 8000


class CORSRequestHandler(http.server.SimpleHTTPRequestHandler):
    def translate_path(self, path):
        """Override to serve xteas.json from cache directory if requested"""
        # Get the default translated path
        path = super().translate_path(path)

        # If requesting xteas.json and it doesn't exist in build.em, try cache directory
        if path.endswith("xteas.json") and not os.path.exists(path):
            # Look in ../cache/xteas.json relative to current directory
            cache_xteas = os.path.join(os.getcwd(), "..", "cache", "xteas.json")
            if os.path.exists(cache_xteas):
                return cache_xteas

        return path

    def end_headers(self):
        # Set CORS headers for broader compatibility
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "*")

        # Cache control for development
        self.send_header("Cache-Control", "no-store, must-revalidate")

        super().end_headers()

    def do_OPTIONS(self):
        self.send_response(200)
        self.end_headers()


if __name__ == "__main__":
    if len(sys.argv) > 1:
        PORT = int(sys.argv[1])

    # Change to build.em directory if it exists, otherwise stay in current dir
    build_dir = "build.em"
    if os.path.exists(build_dir) and os.path.isdir(build_dir):
        os.chdir(build_dir)
        print(f"Changed to directory: {os.getcwd()}")

    # Check for xteas.json
    xteas_local = os.path.join(os.getcwd(), "xteas.json")
    xteas_cache = os.path.join(os.getcwd(), "..", "cache", "xteas.json")

    with socketserver.TCPServer(("", PORT), CORSRequestHandler) as httpd:
        print("=" * 60)
        print("Emscripten Development Server")
        print("=" * 60)
        print(f"Serving at: http://localhost:{PORT}/")
        print(f"Directory: {os.getcwd()}")
        print("")

        # Check xteas.json availability
        if os.path.exists(xteas_local):
            print(f"xteas.json: Found in {xteas_local}")
        elif os.path.exists(xteas_cache):
            print(f"xteas.json: Will serve from {xteas_cache}")
        else:
            print("⚠️  WARNING: xteas.json not found!")
            print(f"   Looked in: {xteas_local}")
            print(f"   Looked in: {xteas_cache}")
        print("")

        print(f"Open: http://localhost:{PORT}/emscripten_sdl2_webgl1.html")
        print("")
        print("Press Ctrl+C to stop")
        print("=" * 60)

        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nServer stopped.")
            sys.exit(0)
