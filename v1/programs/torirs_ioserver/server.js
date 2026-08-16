#!/usr/bin/env node

const crypto = require("crypto");
const http = require("http");
const fs = require("fs");
const path = require("path");
const { spawnSync } = require("child_process");
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

const VALID_CACHE_SOURCES = new Set(["dat1", "dat2", "kronos"]);

const CACHE_SPECS = [
  { key: "dat1", subdir: "cache254", mainFile: "main_file_cache.dat", mode: CACHE_MODE_DAT1 },
  { key: "dat2", subdir: "cache", mainFile: "main_file_cache.dat2", mode: CACHE_MODE_DAT2 },
  { key: "kronos", subdir: "cache.kronos", mainFile: "main_file_cache.dat2", mode: CACHE_MODE_DAT2 },
];

function dirHasCacheFile(dir, mainFile) {
  try {
    fs.accessSync(path.join(dir, mainFile), fs.constants.R_OK);
    return true;
  } catch {
    return false;
  }
}

function isRepoRoot(dir) {
  try {
    return fs.statSync(path.join(dir, "src2")).isDirectory();
  } catch {
    return false;
  }
}

function findRepoRoot(startDir) {
  let dir = path.resolve(startDir);
  for (let depth = 0; depth < 12; depth++) {
    if (isRepoRoot(dir)) {
      return dir;
    }
    const parent = path.dirname(dir);
    if (parent === dir) {
      break;
    }
    dir = parent;
  }
  return null;
}

function detectRepoCaches(repoRoot) {
  const detected = {};
  for (const spec of CACHE_SPECS) {
    const dir = path.join(repoRoot, spec.subdir);
    if (!dirHasCacheFile(dir, spec.mainFile)) {
      continue;
    }
    if (!canOpenCache(spec.mode, dir)) {
      console.warn(`✗ Skipping cache [${spec.key}] (${dir}): failed to open`);
      continue;
    }
    detected[spec.key] = { directory: dir, mode: spec.mode };
  }
  return detected;
}

function canOpenCache(mode, directory) {
  const cachelibPath = path.join(__dirname, "../cachelib_js");
  const script = `
    const { CacheLib } = require(${JSON.stringify(cachelibPath)});
    const cache = new CacheLib({ mode: ${mode}, directory: ${JSON.stringify(directory)} });
    cache.free();
  `;
  const result = spawnSync(process.execPath, ["-e", script], {
    stdio: "ignore",
    timeout: 60000,
  });
  return result.status === 0;
}

function parseCacheSource(value) {
  const source = (value || "dat1").toLowerCase();
  if (!VALID_CACHE_SOURCES.has(source)) {
    return null;
  }
  return source;
}

function getCacheForSource(caches, source) {
  const cache = caches[source];
  if (!cache) {
    return null;
  }
  return cache;
}

// Parse configuration from env vars and command-line args
function parseConfig() {
  const args = parseArgs(process.argv.slice(2));

  return {
    cacheDir: args.cache || process.env.CACHE_DIR || null,
    cacheMode: parseCacheMode(args.mode || process.env.CACHE_MODE || "dat1"),
    port: parseInt(args.port || process.env.PORT || "8080", 10),
    host: args.host || process.env.HOST || "0.0.0.0",
    scriptsDir:
      args.lua ||
      process.env.LUA_DIR ||
      path.resolve(__dirname, "../../revs/scripts"),
    configDir:
      args.config ||
      process.env.CONFIG_DIR ||
      path.resolve(__dirname, "../../../src/osrs/revconfig/configs"),
    staticDir:
      args.static ||
      process.env.STATIC_DIR ||
      path.resolve(__dirname, "../browser/dist"),
  };
}

function initializeCaches(config) {
  const caches = {};

  if (config.cacheDir) {
    const source = config.cacheMode === CACHE_MODE_DAT1 ? "dat1" : "dat2";
    try {
      caches[source] = new CacheLib({
        mode: config.cacheMode,
        directory: config.cacheDir,
      });
      console.log(`✓ Cache [${source}] initialized from manual override: ${config.cacheDir}`);
    } catch (error) {
      console.error(`✗ Failed to initialize manual cache [${source}]:`, error.message);
      process.exit(1);
    }
    return caches;
  }

  const repoRoot = findRepoRoot(__dirname);
  if (!repoRoot) {
    console.error("Error: Could not find repo root (expected src2/ directory).");
    console.error("Use --cache=/path/to/cache or set CACHE_DIR environment variable.");
    process.exit(1);
  }

  console.log(`Repo root: ${repoRoot}`);
  const detected = detectRepoCaches(repoRoot);
  for (const [key, spec] of Object.entries(detected)) {
    try {
      caches[key] = new CacheLib({
        mode: spec.mode,
        directory: spec.directory,
      });
      console.log(`✓ Cache [${key}] initialized: ${spec.directory}`);
    } catch (error) {
      console.warn(`✗ Skipping cache [${key}] (${spec.directory}): ${error.message}`);
    }
  }

  if (Object.keys(caches).length === 0) {
    console.error("Error: No caches found under repo root.");
    console.error("Expected cache254/, cache/, and/or cache.kronos/ with main cache files.");
    process.exit(1);
  }

  return caches;
}

// Configuration
const config = parseConfig();
console.log(`Scripts directory: ${config.scriptsDir}`);
console.log(`Config directory: ${config.configDir}`);
if (config.staticDir) {
  console.log(`Static files directory: ${config.staticDir}`);
}

const caches = initializeCaches(config);

// Handle single archive request: GET /archive/:tableId/:archiveId
function handleSingleArchive(req, res, url) {
  const parts = url.pathname.split("/");
  const tableId = parseInt(parts[2]);
  const archiveId = parseInt(parts[3]);

  if (isNaN(tableId) || isNaN(archiveId)) {
    res.writeHead(400, {
      "Content-Type": "text/plain",
      "Access-Control-Allow-Origin": "*",
    });
    res.end("Invalid table or archive ID");
    return;
  }

  const flags = parseInt(url.searchParams.get("flags") || "0", 10);
  const cacheSource = parseCacheSource(url.searchParams.get("cache"));
  if (!cacheSource) {
    res.writeHead(400, {
      "Content-Type": "text/plain",
      "Access-Control-Allow-Origin": "*",
    });
    res.end("Invalid cache source (expected dat1, dat2, or kronos)");
    return;
  }

  const cache = getCacheForSource(caches, cacheSource);
  if (!cache) {
    res.writeHead(404, {
      "Content-Type": "text/plain",
      "Access-Control-Allow-Origin": "*",
    });
    res.end(`Cache source not available: ${cacheSource}`);
    console.error(
      `[${new Date().toISOString()}] GET /archive/${tableId}/${archiveId}?cache=${cacheSource} - 404 (cache unavailable)`,
    );
    return;
  }

  try {
    const buffer = cache.loadArchiveSerialized(tableId, archiveId, flags);
    res.writeHead(200, {
      "Content-Type": "application/octet-stream",
      "Content-Length": buffer.length,
      "Access-Control-Allow-Origin": "*",
    });
    res.end(buffer);
    console.log(
      `[${new Date().toISOString()}] GET /archive/${tableId}/${archiveId}?flags=${flags}&cache=${cacheSource} - 200 (${buffer.length} bytes)`,
    );
  } catch (error) {
    console.error(
      `[${new Date().toISOString()}] GET /archive/${tableId}/${archiveId}?flags=${flags}&cache=${cacheSource} - ERROR:`,
      error.message,
    );
    res.writeHead(500, {
      "Content-Type": "text/plain",
      "Access-Control-Allow-Origin": "*",
    });
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
        const cacheSource = parseCacheSource(r.cache);
        if (!cacheSource) {
          console.error(
            `Error loading archive ${r.table}/${r.archive}: invalid cache source`,
          );
          continue;
        }
        const cache = getCacheForSource(caches, cacheSource);
        if (!cache) {
          console.error(
            `Error loading archive ${r.table}/${r.archive}: cache [${cacheSource}] unavailable`,
          );
          continue;
        }
        try {
          const buffer = cache.loadArchiveSerialized(
            r.table,
            r.archive,
            r.flags || 0,
          );
          parts.push({ table: r.table, archive: r.archive, buffer });
          successCount++;
        } catch (error) {
          console.error(
            `Error loading archive ${r.table}/${r.archive} (cache=${cacheSource}):`,
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

// Handle revconfig INI request: GET /config/:path
function handleConfigFile(req, res, url) {
  const configRelPath = url.pathname.replace(/^\/config\//, "");

  const normalized = path.normalize(configRelPath);
  if (
    normalized.includes("..") ||
    normalized.startsWith("/") ||
    !normalized.endsWith(".ini")
  ) {
    res.writeHead(400, { "Content-Type": "text/plain" });
    res.end("Invalid config path");
    console.error(
      `[${new Date().toISOString()}] GET ${url.pathname} - 400 (invalid config path)`,
    );
    return;
  }

  const configPath = path.join(config.configDir, normalized);
  const resolvedPath = path.resolve(configPath);
  const resolvedConfigDir = path.resolve(config.configDir);

  if (!resolvedPath.startsWith(resolvedConfigDir)) {
    res.writeHead(403, { "Content-Type": "text/plain" });
    res.end("Forbidden");
    console.error(
      `[${new Date().toISOString()}] GET ${url.pathname} - 403 (path traversal attempt)`,
    );
    return;
  }

  fs.readFile(configPath, (err, data) => {
    if (err) {
      res.writeHead(404, { "Content-Type": "text/plain" });
      res.end("Config not found");
      console.error(
        `[${new Date().toISOString()}] GET /config/${normalized} - 404`,
      );
      return;
    }

    res.writeHead(200, {
      "Content-Type": "text/plain; charset=utf-8",
      "Content-Length": data.length,
      "Access-Control-Allow-Origin": "*",
    });
    res.end(data);
    console.log(
      `[${new Date().toISOString()}] GET /config/${normalized} - 200 (${data.length} bytes)`,
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
    } else if (req.method === "GET" && url.pathname.startsWith("/config/")) {
      handleConfigFile(req, res, url);
    } else if (req.method === "GET" && url.pathname === "/") {
      // Root endpoint - show help if no static dir, otherwise serve static files
      if (!config.staticDir) {
        res.writeHead(200, { "Content-Type": "text/plain" });
        res.end(`ToriRS IO Server
            
Available endpoints:
  GET  /archive/:tableId/:archiveId?cache=dat1|dat2|kronos  - Load single archive
  POST /archives                     - Load multiple archives (JSON body, multipart response)
  GET  /config/:path.ini             - Load revconfig INI file
  GET  /scripts/:filename.lua        - Load Lua script

Server running on ${config.host}:${config.port}
Caches: ${Object.keys(caches).join(", ") || "none"}
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
    for (const [key, cache] of Object.entries(caches)) {
      cache.free();
      console.log(`Cache [${key}] freed`);
    }
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
