import http.server
import argparse
import os

class CORSRequestHandler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        # Allow cross-origin requests
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'GET, POST, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'X-Requested-With, Content-Type')
        # Ensure no-cache for easier debugging in Chrome 85
        self.send_header('Cache-Control', 'no-store, no-cache, must-revalidate')
        super().end_headers()

    def do_OPTIONS(self):
        # Handle preflight requests for ES Modules
        self.send_response(200)
        self.end_headers()

def main():
    parser = argparse.ArgumentParser(description="CORS-enabled HTTP Server")
    parser.add_argument('port', type=int, default=8000, nargs='?', help='Port to listen on (default: 8000)')
    parser.add_argument('--bind', '-b', default='0.0.0.0', help='Address to bind to (default: all interfaces)')
    parser.add_argument('--directory', '-d', default=os.getcwd(), help='Directory to serve (default: current)')

    args = parser.parse_args()

    # Change directory if specified
    os.chdir(args.directory)

    server_address = (args.bind, args.port)
    httpd = http.server.HTTPServer(server_address, CORSRequestHandler)

    print(f"Serving HTTP on {args.bind} port {args.port} (http://{args.bind}:{args.port}/) ...")
    print(f"Serving directory: {args.directory}")
    
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nKeyboard interrupt received, exiting.")

if __name__ == '__main__':
    main()