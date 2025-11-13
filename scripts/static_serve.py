from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import os


class MultiRootHandler(SimpleHTTPRequestHandler):
    # Map URL prefixes to folder paths
    ROUTES = {
        "/src": "src",
        "/test": "test",
        "/": "public/build",
    }

    def translate_path(self, path):
        # Find which route this request matches
        for prefix, folder in self.ROUTES.items():
            if path.startswith(prefix):
                # Strip prefix and map to physical directory
                rel_path = path[len(prefix) :].lstrip("/")
                return os.path.join(os.getcwd(), folder, rel_path)
        # Default behavior
        return super().translate_path(path)


if __name__ == "__main__":
    server = ThreadingHTTPServer(("0.0.0.0", 8000), MultiRootHandler)
    print("Serving on http://localhost:8000")
    server.serve_forever()
