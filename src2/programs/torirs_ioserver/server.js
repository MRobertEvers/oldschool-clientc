#!/usr/bin/env node

const crypto = require("crypto");
const http = require("http");
const fs = require("fs");
const path = require("path");
const {
  CacheLib,
  CACHE_MODE_DAT1,
  CACHE_MODE_DAT2,
} = require("../cachelib_js");

// Parse command-line arguments
function parseArgs(argv) {
  const args = {};
  for (let i = 0; i < argv.length; i++) {
    const arg = argv[i];
    if (arg.startsWith("--")) {
      const eqIndex = arg.indexOf("=");
      if (eqIndex !== -1) {
        // --key=value format
        const key = arg.slice(2, eqIndex);
        const value = arg.slice(eqIndex + 1);
        args[key] = value;
      } else {
        // --key value format
        const key = arg.slice(2);
        if (i + 1 < argv.length && !argv[i + 1].startsWith("--")) {
          args[key] = argv[i + 1];
          i++;
        } else {
          args[key] = true;
        }
      }
    }
  }
  return args;
}

// Parse cache mode string to constant
function parseCacheMode(mode) {
  return mode === "dat1" ? CACHE_MODE_DAT1 : CACHE_MODE_DAT2;
}

// Parse configuration from env vars and command-line args
function parseConfig() {
  const args = parseArgs(process.argv.slice(2));

  const config = {
    cacheDir: args.cache || process.env.CACHE_DIR || null,
    cacheMode: parseCacheMode(args.mode || process.env.CACHE_MODE || "dat2"),
    port: parseInt(args.port || process.env.PORT || "8080"),
    host: args.host || process.env.HOST || "0.0.0.0",
    scriptsDir: path.resolve(__dirname, "../../revs/scripts"),
    staticDir:
      args.static ||
      process.env.STATIC_DIR ||
      path.resolve(__dirname, "../browser/dist"),
  };

  // Validate required config
  if (!config.cacheDir) {
    console.error("Error: Cache directory not specified.");
    console.error(
      "Use --cache=/path/to/cache or set CACHE_DIR environment variable.",
    );
    process.exit(1);
  }

  return config;
}

// Configuration
const config = parseConfig();
console.log(`Initializing cache from: ${config.cacheDir}`);
console.log(
  `Cache mode: ${config.cacheMode === CACHE_MODE_DAT1 ? "DAT1" : "DAT2"}`,
);
console.log(`Scripts directory: ${config.scriptsDir}`);
if (config.staticDir) {
  console.log(`Static files directory: ${config.staticDir}`);
}

// Initialize cache once on startup
let cache;
try {
  cache = new CacheLib({
    mode: config.cacheMode,
    directory: config.cacheDir,
  });
  console.log("✓ Cache initialized successfully");
} catch (error) {
  console.error("✗ Failed to initialize cache:", error.message);
  process.exit(1);
}

// Handle single archive request: GET /archive/:tableId/:archiveId
function handleSingleArchive(req, res, url) {
  const parts = url.pathname.split("/");
  const tableId = parseInt(parts[2]);
  const archiveId = parseInt(parts[3]);

  if (isNaN(tableId) || isNaN(archiveId)) {
    res.writeHead(400, { "Content-Type": "text/plain" });
    res.end("Invalid table or archive ID");
    return;
  }

  try {
    const buffer = cache.loadArchiveSerialized(tableId, archiveId);
    res.writeHead(200, {
      "Content-Type": "application/octet-stream",
      "Content-Length": buffer.length,
      "Access-Control-Allow-Origin": "*",
    });
    res.end(buffer);
    console.log(
      `[${new Date().toISOString()}] GET /archive/${tableId}/${archiveId} - 200 (${buffer.length} bytes)`,
    );
  } catch (error) {
    console.error(
      `[${new Date().toISOString()}] GET /archive/${tableId}/${archiveId} - ERROR:`,
      error.message,
    );
    res.writeHead(500, { "Content-Type": "text/plain" });
    res.end(`Error loading archive: ${error.message}`);
  }
}

// Build multipart/form-data body for batch archive responses (datserver-compatible).
function buildMultipartArchivesResponse(parts) {
  const boundary = `BOUNDARY${crypto.randomBytes(8).toString("hex")}`;
  const chunks = [];

  for (const { table, archive, buffer } of parts) {
    if (!buffer || buffer.length === 0) continue;

    const partHeader = Buffer.from(
      `--${boundary}\r\n` +
        `Content-Type: application/octet-stream\r\n` +
        `X-Table-Id: ${table}\r\n` +
        `X-Archive-Id: ${archive}\r\n` +
        `\r\n`,
      "utf8",
    );
    chunks.push(partHeader, buffer, Buffer.from("\r\n", "utf8"));
  }

  chunks.push(Buffer.from(`--${boundary}--\r\n`, "utf8"));
  return { body: Buffer.concat(chunks), boundary };
}

// Handle batch archives request: POST /archives
function handleBatchArchives(req, res) {
  let body = "";

  req.on("data", (chunk) => {
    body += chunk;
  });

  req.on("end", () => {
    try {
      const { requests } = JSON.parse(body);

      if (!Array.isArray(requests)) {
        res.writeHead(400, { "Content-Type": "text/plain" });
        res.end("Invalid request format: expected {requests: [...]}");
        return;
      }

      const parts = [];
      let successCount = 0;
      for (const r of requests) {
        try {
          const buffer = cache.loadArchiveSerialized(r.table, r.archive);
          parts.push({ table: r.table, archive: r.archive, buffer });
          successCount++;
        } catch (error) {
          console.error(
            `Error loading archive ${r.table}/${r.archive}:`,
            error.message,
          );
        }
      }

      const { body, boundary } = buildMultipartArchivesResponse(parts);
      res.writeHead(200, {
        "Content-Type": `multipart/form-data; boundary=${boundary}`,
        "Content-Length": body.length,
        "Access-Control-Allow-Origin": "*",
      });
      res.end(body);
      console.log(
        `[${new Date().toISOString()}] POST /archives - 200 (${requests.length} requests, ${successCount} successful, ${body.length} bytes)`,
      );
    } catch (error) {
      console.error(
        `[${new Date().toISOString()}] POST /archives - ERROR:`,
        error.message,
      );
      res.writeHead(500, { "Content-Type": "text/plain" });
      res.end(`Error: ${error.message}`);
    }
  });

  req.on("error", (error) => {
    console.error(
      `[${new Date().toISOString()}] POST /archives - Request error:`,
      error.message,
    );
    res.writeHead(500, { "Content-Type": "text/plain" });
    res.end(`Request error: ${error.message}`);
  });
}

// Handle Lua script request: GET /scripts/:path
function handleLuaScript(req, res, url) {
  // Extract the path after /scripts/
  const scriptRelPath = url.pathname.replace(/^\/revs\/scripts\//, "");

  // Security: prevent path traversal
  const normalized = path.normalize(scriptRelPath);
  if (
    normalized.includes("..") ||
    normalized.startsWith("/") ||
    !normalized.endsWith(".lua")
  ) {
    res.writeHead(400, { "Content-Type": "text/plain" });
    res.end("Invalid script name");
    console.error(
      `[${new Date().toISOString()}] GET ${url.pathname} - 400 (invalid script name)`,
    );
    return;
  }

  const scriptPath = path.join(config.scriptsDir, normalized);

  fs.readFile(scriptPath, "utf8", (err, data) => {
    if (err) {
      res.writeHead(404, { "Content-Type": "text/plain" });
      res.end("Script not found");
      console.error(
        `[${new Date().toISOString()}] GET /scripts/${normalized} - 404`,
      );
      return;
    }

    res.writeHead(200, {
      "Content-Type": "text/plain; charset=utf-8",
      "Content-Length": Buffer.byteLength(data),
      "Access-Control-Allow-Origin": "*",
    });
    res.end(data);
    console.log(
      `[${new Date().toISOString()}] GET /scripts/${normalized} - 200 (${Buffer.byteLength(data)} bytes)`,
    );
  });
}

// Handle CORS preflight requests
function handleOptions(req, res) {
  res.writeHead(200, {
    "Access-Control-Allow-Origin": "*",
    "Access-Control-Allow-Methods": "GET, POST, OPTIONS",
    "Access-Control-Allow-Headers": "Content-Type",
    "Access-Control-Max-Age": "86400",
  });
  res.end();
}

// Get MIME type based on file extension
function getMimeType(filepath) {
  const ext = path.extname(filepath).toLowerCase();
  const mimeTypes = {
    ".html": "text/html",
    ".htm": "text/html",
    ".css": "text/css",
    ".js": "application/javascript",
    ".json": "application/json",
    ".png": "image/png",
    ".jpg": "image/jpeg",
    ".jpeg": "image/jpeg",
    ".gif": "image/gif",
    ".svg": "image/svg+xml",
    ".ico": "image/x-icon",
    ".txt": "text/plain",
    ".xml": "application/xml",
    ".pdf": "application/pdf",
    ".wasm": "application/wasm",
    ".lua": "text/plain",
  };
  return mimeTypes[ext] || "application/octet-stream";
}

// Handle static file requests (fallback)
function handleStaticFile(req, res, url) {
  if (!config.staticDir) {
    res.writeHead(404, { "Content-Type": "text/plain" });
    res.end("Not Found");
    console.log(
      `[${new Date().toISOString()}] ${req.method} ${url.pathname} - 404 (no static dir configured)`,
    );
    return;
  }

  // Security: normalize path and prevent traversal
  const requestPath = url.pathname === "/" ? "/index.html" : url.pathname;
  const normalizedPath = path
    .normalize(requestPath)
    .replace(/^(\.\.[\/\\])+/, "");
  const filepath = path.join(config.staticDir, normalizedPath);

  // Verify the resolved path is within staticDir
  const resolvedPath = path.resolve(filepath);
  const resolvedStaticDir = path.resolve(config.staticDir);

  if (!resolvedPath.startsWith(resolvedStaticDir)) {
    res.writeHead(403, { "Content-Type": "text/plain" });
    res.end("Forbidden");
    console.error(
      `[${new Date().toISOString()}] GET ${url.pathname} - 403 (path traversal attempt)`,
    );
    return;
  }

  fs.readFile(filepath, (err, data) => {
    if (err) {
      if (err.code === "ENOENT") {
        res.writeHead(404, { "Content-Type": "text/plain" });
        res.end("Not Found");
        console.log(`[${new Date().toISOString()}] GET ${url.pathname} - 404`);
      } else if (err.code === "EISDIR") {
        // Try index.html if directory
        const indexPath = path.join(filepath, "index.html");
        fs.readFile(indexPath, (err2, data2) => {
          if (err2) {
            res.writeHead(404, { "Content-Type": "text/plain" });
            res.end("Not Found");
            console.log(
              `[${new Date().toISOString()}] GET ${url.pathname} - 404`,
            );
          } else {
            res.writeHead(200, {
              "Content-Type": "text/html",
              "Content-Length": data2.length,
              "Access-Control-Allow-Origin": "*",
            });
            res.end(data2);
            console.log(
              `[${new Date().toISOString()}] GET ${url.pathname} - 200 (${data2.length} bytes)`,
            );
          }
        });
      } else {
        res.writeHead(500, { "Content-Type": "text/plain" });
        res.end("Internal Server Error");
        console.error(
          `[${new Date().toISOString()}] GET ${url.pathname} - 500:`,
          err.message,
        );
      }
      return;
    }

    const mimeType = getMimeType(filepath);
    res.writeHead(200, {
      "Content-Type": mimeType,
      "Content-Length": data.length,
      "Access-Control-Allow-Origin": "*",
    });
    res.end(data);
    console.log(
      `[${new Date().toISOString()}] GET ${url.pathname} - 200 (${data.length} bytes, ${mimeType})`,
    );
  });
}

// Main request router
function handleRequest(req, res) {
  try {
    const url = new URL(req.url, `http://${req.headers.host}`);

    // Handle CORS preflight
    if (req.method === "OPTIONS") {
      handleOptions(req, res);
      return;
    }

    // Route based on path
    if (req.method === "GET" && url.pathname.startsWith("/archive/")) {
      handleSingleArchive(req, res, url);
    } else if (req.method === "POST" && url.pathname === "/archives") {
      handleBatchArchives(req, res);
    } else if (
      req.method === "GET" &&
      url.pathname.startsWith("/revs/scripts/")
    ) {
      handleLuaScript(req, res, url);
    } else if (req.method === "GET" && url.pathname === "/") {
      // Root endpoint - show help if no static dir, otherwise serve static files
      if (!config.staticDir) {
        res.writeHead(200, { "Content-Type": "text/plain" });
        res.end(`ToriRS IO Server
            
Available endpoints:
  GET  /archive/:tableId/:archiveId  - Load single archive
  POST /archives                     - Load multiple archives (JSON body, multipart response)
  GET  /scripts/:filename.lua        - Load Lua script

Server running on ${config.host}:${config.port}
Cache: ${config.cacheDir} (${config.cacheMode === CACHE_MODE_DAT1 ? "DAT1" : "DAT2"})
`);
      } else {
        handleStaticFile(req, res, url);
      }
    } else if (req.method === "GET") {
      // Try static files as fallback for GET requests
      handleStaticFile(req, res, url);
    } else {
      res.writeHead(404, { "Content-Type": "text/plain" });
      res.end("Not Found");
      console.log(
        `[${new Date().toISOString()}] ${req.method} ${url.pathname} - 404`,
      );
    }
  } catch (error) {
    console.error(`[${new Date().toISOString()}] Unhandled error:`, error);
    res.writeHead(500, { "Content-Type": "text/plain" });
    res.end("Internal Server Error");
  }
}

// Create and start server
const server = http.createServer(handleRequest);

// Track active connections for graceful shutdown
const connections = new Set();
server.on("connection", (conn) => {
  connections.add(conn);
  conn.on("close", () => {
    connections.delete(conn);
  });
});

server.listen(config.port, config.host, () => {
  console.log(`\n✓ Server listening on http://${config.host}:${config.port}`);
  console.log(`  Press Ctrl+C to stop\n`);
});

// Graceful shutdown handler
let isShuttingDown = false;

function shutdown(signal) {
  if (isShuttingDown) {
    console.log("Force closing...");
    process.exit(1);
  }

  isShuttingDown = true;
  console.log(`\n\nShutting down gracefully... (Ctrl+C again to force)`);

  // Set a timeout to force exit if graceful shutdown takes too long
  const forceExitTimeout = setTimeout(() => {
    console.log("Graceful shutdown timed out, forcing exit...");
    process.exit(1);
  }, 5000);

  // Stop accepting new connections
  server.close(() => {
    clearTimeout(forceExitTimeout);
    console.log("Server closed");
    cache.free();
    console.log("Cache freed");
    process.exit(0);
  });

  // Force close all active connections
  for (const conn of connections) {
    conn.destroy();
  }
}

// Graceful shutdown
process.on("SIGINT", () => shutdown("SIGINT"));
process.on("SIGTERM", () => shutdown("SIGTERM"));

// Handle uncaught errors
process.on("uncaughtException", (error) => {
  console.error("Uncaught exception:", error);
  process.exit(1);
});

process.on("unhandledRejection", (reason, promise) => {
  console.error("Unhandled rejection at:", promise, "reason:", reason);
});
