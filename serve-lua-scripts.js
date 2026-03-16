#!/usr/bin/env node
/**
 * Serves Lua scripts from src/osrs/scripts.
 * Usage: node serve-lua-scripts.js [port]
 * Default port: 8080
 */

const http = require("http");
const fs = require("fs");
const path = require("path");

const PORT = parseInt(process.argv[2], 10) || 8080;
const SCRIPTS_DIR = path.resolve(__dirname, "src", "osrs", "scripts");
const MIME = { ".lua": "text/plain", ".h": "text/plain" };

const server = http.createServer((req, res) => {
  const urlPath = req.url === "/" ? "/index" : req.url.replace(/^\//, "");
  const filePath = path.join(SCRIPTS_DIR, path.normalize(urlPath));

  console.log(`filePath: ${filePath}`);
  // Block directory traversal
  if (!filePath.startsWith(SCRIPTS_DIR)) {
    res.writeHead(403);
    res.end("Forbidden");
    return;
  }

  fs.readFile(filePath, (err, data) => {
    if (err) {
      if (err.code === "ENOENT") {
        res.writeHead(404, { "Content-Type": "text/plain" });
        res.end("Not Found");
      } else {
        res.writeHead(500, { "Content-Type": "text/plain" });
        res.end("Internal Server Error");
      }
      return;
    }

    const ext = path.extname(filePath);
    const contentType = MIME[ext] || "application/octet-stream";
    res.writeHead(200, {
      "Content-Type": contentType,
      "Access-Control-Allow-Origin": "*",
    });
    res.end(data);
  });
});

server.listen(PORT, () => {
  console.log(`Serving Lua scripts from ${SCRIPTS_DIR}`);
  console.log(`http://localhost:${PORT}/`);
  console.log(`Example: http://localhost:${PORT}/init.lua`);
});
