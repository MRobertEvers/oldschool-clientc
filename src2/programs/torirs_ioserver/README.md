# ToriRS IO Server

A lightweight HTTP server for serving OSRS cache archives and Lua scripts with zero external dependencies.

## Features

- **Single Archive Loading**: GET endpoint for individual archives
- **Batch Archive Loading**: POST endpoint for loading multiple archives efficiently
- **Lua Script Serving**: Static file serving from scripts directory
- **Zero Dependencies**: Uses only Node.js built-ins + local cachelib_js addon
- **CORS Enabled**: Browser-friendly with CORS headers
- **Flexible Configuration**: Command-line args or environment variables

## Installation

No installation required! The server uses the cachelib_js addon from the same repository.

## Usage

### Starting the Server

**With command-line arguments:**

```bash
node server.js --cache=/path/to/cache --mode=dat2 --port=8080
```

**With environment variables:**

```bash
CACHE_DIR=/path/to/cache CACHE_MODE=dat2 PORT=8080 node server.js
```

**Mixed approach (env vars as defaults, args to override):**

```bash
CACHE_DIR=/path/to/cache node server.js --port=9000 --mode=dat1
```

**Quick start with defaults (using start script):**

```bash
# Use default cache location (~/.jagex_cache_32)
./start.sh

# Or specify a custom cache directory
CACHE_DIR=/path/to/cache ./start.sh

# Or customize other settings
CACHE_DIR=/path/to/cache PORT=9000 STATIC_DIR=./public ./start.sh
```

The `start.sh` script provides sensible defaults and validates the cache directory exists before starting.

### Configuration Options

| Option           | CLI Argument          | Environment Variable | Default              | Description                |
| ---------------- | --------------------- | -------------------- | -------------------- | -------------------------- |
| Cache Directory  | `--cache=<path>`      | `CACHE_DIR`          | (required)           | Path to cache directory    |
| Cache Mode       | `--mode=<dat1\|dat2>` | `CACHE_MODE`         | `dat2`               | Cache format version       |
| Port             | `--port=<number>`     | `PORT`               | `8080`               | Server port                |
| Host             | `--host=<address>`    | `HOST`               | `0.0.0.0`            | Bind address               |
| Lua Scripts Dir  | `--lua=<path>`        | `LUA_DIR`            | `../../revs/scripts` | Directory for Lua scripts  |
| Static Files Dir | `--static=<path>`     | `STATIC_DIR`         | `../browser/dist`    | Directory for static files |

## API Endpoints

### 1. Load Single Archive

**Endpoint:** `GET /archive/:tableId/:archiveId`

**Example:**

```bash
curl http://localhost:8080/archive/2/0 > archive.dat
```

**Response:**

- **200 OK**: Raw binary archive data (`application/octet-stream`)
- **400 Bad Request**: Invalid table or archive ID
- **500 Internal Server Error**: Archive loading failed

### 2. Load Multiple Archives (Batch)

**Endpoint:** `POST /archives`

**Request Body:**

```json
{
  "requests": [
    { "table": 2, "archive": 0 },
    { "table": 2, "archive": 1 },
    { "table": 7, "archive": 100 }
  ]
}
```

**Example:**

```bash
curl -X POST http://localhost:8080/archives \
  -H "Content-Type: application/json" \
  -d '{"requests":[{"table":2,"archive":0},{"table":2,"archive":1}]}' \
  > batch.dat
```

**Response Format:**

`multipart/form-data` with one part per successfully loaded archive. Each part has:

- `Content-Type: application/octet-stream`
- `X-Table-Id` and `X-Archive-Id` headers matching the request entry

Failed archives are omitted (no part). This matches the datserver batch endpoint behavior.

**Parsing Example (Node.js):**

```javascript
function parseMultipartParts(data, contentType) {
  const boundaryMatch = (contentType || "").match(/boundary=([^;,\s]+)/i);
  if (!boundaryMatch) return [];
  let boundary = boundaryMatch[1].trim();
  if (boundary.startsWith('"') && boundary.endsWith('"')) {
    boundary = boundary.slice(1, -1);
  }

  const boundaryBuf = Buffer.from(`--${boundary}`);
  const crlf = Buffer.from("\r\n");
  const headerSep = Buffer.from("\r\n\r\n");
  const parts = [];
  let pos = data.indexOf(boundaryBuf);
  if (pos === -1) return [];

  while (true) {
    const lineEnd = data.indexOf(crlf, pos);
    if (lineEnd === -1) break;
    const afterBoundary = lineEnd + crlf.length;
    const headerEnd = data.indexOf(headerSep, afterBoundary);
    if (headerEnd === -1) break;
    const bodyStart = headerEnd + headerSep.length;
    const nextBoundary = data.indexOf(boundaryBuf, bodyStart);
    if (nextBoundary === -1) break;
    let bodyEnd = nextBoundary;
    if (data[bodyEnd - 2] === 0x0d && data[bodyEnd - 1] === 0x0a) {
      bodyEnd -= 2;
    }
    parts.push(data.slice(bodyStart, bodyEnd));
    pos = nextBoundary;
  }
  return parts;
}

const data = fs.readFileSync("batch.dat");
const contentType = "multipart/form-data; boundary=..."; // from response header
const archives = parseMultipartParts(data, contentType);
```

**Response:**

- **200 OK**: `multipart/form-data` body
- **400 Bad Request**: Invalid request format
- **500 Internal Server Error**: Batch processing failed

### 3. Load Lua Script

**Endpoint:** `GET /scripts/:filename.lua`

**Example:**

```bash
curl http://localhost:8080/scripts/init.lua
```

Serves Lua scripts from `src2/revs/scripts/` directory.

**Response:**

- **200 OK**: Lua script content (`text/plain; charset=utf-8`)
- **400 Bad Request**: Invalid filename (path traversal attempt)
- **404 Not Found**: Script doesn't exist

### 4. Server Info

**Endpoint:** `GET /`

Returns server configuration and available endpoints.

## Examples

### Loading a Single Model

```bash
# Download model archive from table 7, archive 1000
curl http://localhost:8080/archive/7/1000 -o model_1000.dat

# Check the file size
ls -lh model_1000.dat
```

### Loading Multiple Config Archives

```javascript
// Node.js example
const http = require("http");

function parseMultipartParts(data, contentType) {
  const boundaryMatch = (contentType || "").match(/boundary=([^;,\s]+)/i);
  if (!boundaryMatch) return [];
  let boundary = boundaryMatch[1].trim();
  if (boundary.startsWith('"') && boundary.endsWith('"')) {
    boundary = boundary.slice(1, -1);
  }
  const boundaryBuf = Buffer.from(`--${boundary}`);
  const crlf = Buffer.from("\r\n");
  const headerSep = Buffer.from("\r\n\r\n");
  const parts = [];
  let pos = data.indexOf(boundaryBuf);
  if (pos === -1) return [];
  while (true) {
    const lineEnd = data.indexOf(crlf, pos);
    if (lineEnd === -1) break;
    const afterBoundary = lineEnd + crlf.length;
    const headerEnd = data.indexOf(headerSep, afterBoundary);
    if (headerEnd === -1) break;
    const bodyStart = headerEnd + headerSep.length;
    const nextBoundary = data.indexOf(boundaryBuf, bodyStart);
    if (nextBoundary === -1) break;
    let bodyEnd = nextBoundary;
    if (data[bodyEnd - 2] === 0x0d && data[bodyEnd - 1] === 0x0a) {
      bodyEnd -= 2;
    }
    parts.push(data.slice(bodyStart, bodyEnd));
    pos = nextBoundary;
  }
  return parts;
}

const requestData = JSON.stringify({
  requests: [
    { table: 2, archive: 0 }, // Configs
    { table: 2, archive: 10 },
    { table: 2, archive: 20 },
  ],
});

const options = {
  hostname: "localhost",
  port: 8080,
  path: "/archives",
  method: "POST",
  headers: {
    "Content-Type": "application/json",
    "Content-Length": requestData.length,
  },
};

const req = http.request(options, (res) => {
  const chunks = [];
  res.on("data", (chunk) => chunks.push(chunk));
  res.on("end", () => {
    const data = Buffer.concat(chunks);
    const contentType = res.headers["content-type"] || "";
    const archives = parseMultipartParts(data, contentType);
    console.log(`Loaded ${archives.length} archives`);
  });
});

req.write(requestData);
req.end();
```

### Loading a Lua Script

```bash
# Download init.lua script
curl http://localhost:8080/scripts/init.lua

# Or use it directly in your application
curl http://localhost:8080/scripts/init.lua | lua
```

### Browser Usage (with CORS)

```javascript
// Fetch a single archive
fetch("http://localhost:8080/archive/2/0")
  .then((res) => res.arrayBuffer())
  .then((data) => {
    console.log(`Archive size: ${data.byteLength} bytes`);
    // Process the archive data
  });

// Fetch multiple archives
fetch("http://localhost:8080/archives", {
  method: "POST",
  headers: { "Content-Type": "application/json" },
  body: JSON.stringify({
    requests: [
      { table: 2, archive: 0 },
      { table: 2, archive: 1 },
    ],
  }),
}).then(async (res) => {
  const contentType = res.headers.get("Content-Type") || "";
  const raw = new Uint8Array(await res.arrayBuffer());
  // Use parseMultipartPartsWithHeaders from luajs_sidecar.js, or the Node parser above
  console.log(`Content-Type: ${contentType}, ${raw.byteLength} bytes`);
});
```

## Performance Considerations

- **Cache Initialization**: Cache is loaded once on startup and reused for all requests
- **Synchronous Loading**: Archives are loaded synchronously (blocking per request)
- **Memory Efficient**: Archives are copied to buffers and freed immediately
- **Concurrency**: For high load, consider:
  - Running multiple server instances behind a load balancer
  - Using Node.js cluster module
  - Implementing request queuing

## Security

- **Path Traversal Protection**: Script requests validate filenames
- **Input Validation**: Table/archive IDs are validated
- **CORS**: Enabled for browser access (can be disabled if needed)
- **No Authentication**: Currently no auth - add if exposing publicly
- **Rate Limiting**: Not implemented - consider adding for production

## Error Handling

The server continues running even if individual requests fail:

- Invalid requests return appropriate HTTP error codes
- Errors are logged to stderr with timestamps
- Failed archives in batch requests are omitted from the multipart response
- Cache remains valid across failed requests

## Logging

All requests are logged to stdout with timestamps:

```
[2026-05-18T19:00:00.000Z] GET /archive/2/0 - 200 (1234 bytes)
[2026-05-18T19:00:01.000Z] POST /archives - 200 (5 requests, 5 successful, 6789 bytes)
[2026-05-18T19:00:02.000Z] GET /scripts/init.lua - 200 (456 bytes)
```

Errors are logged to stderr:

```
[2026-05-18T19:00:03.000Z] GET /archive/99/99 - ERROR: Failed to load archive
```

## Graceful Shutdown

The server handles shutdown signals gracefully:

```bash
# Send SIGINT (Ctrl+C) or SIGTERM
kill <pid>
```

The server will:

1. Stop accepting new connections
2. Wait for active requests to complete
3. Free cache resources
4. Exit cleanly

## Troubleshooting

### Server won't start

**Error:** `Error: Cache directory not specified`

- **Solution:** Provide cache directory via `--cache` arg or `CACHE_DIR` env var

**Error:** `Failed to initialize cache`

- **Solution:** Check that the cache directory exists and contains valid cache files
- **Solution:** Verify the cache mode matches your cache format (dat1 vs dat2)

### Archive loading fails

**Error:** `500 Internal Server Error`

- Check that the table/archive IDs are valid
- Verify the archive exists in your cache
- Check server logs for detailed error messages

### Scripts not found

**Error:** `404 Not Found` for scripts

- Verify scripts exist in `src2/revs/scripts/` directory
- Check filename spelling (case-sensitive)
- Ensure filename ends with `.lua`

## Development

### Running in Development

```bash
# With example cache
node server.js --cache=./cache_test --mode=dat2 --port=3000
node server.js --cache=./../../../cache254 --mode=dat1 --port=8080
```

### Testing

A test client is included to verify all endpoints work correctly. See the plan documentation for the test client implementation.

## License

Same as parent project.
