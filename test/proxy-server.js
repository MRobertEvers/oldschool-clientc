const http = require('http');
const httpProxy = require('http-proxy');

// Configuration
const PROXY_PORT = 8080;
const TARGET_HTTP = 'http://localhost:4949';  // HTTP backend target
const TARGET_WS = 'ws://localhost:4949';      // WebSocket backend target

// Create a proxy server with custom application logic
const proxy = httpProxy.createProxyServer({});

// Create your custom server and proxy requests
const server = http.createServer((req, res) => {
  console.log(`[HTTP] ${req.method} ${req.url}`);
  
  // Add CORS headers if needed
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Methods', 'GET, POST, PUT, DELETE, OPTIONS');
  res.setHeader('Access-Control-Allow-Headers', 'Content-Type, Authorization');
  
  // Handle preflight requests
  if (req.method === 'OPTIONS') {
    res.writeHead(200);
    res.end();
    return;
  }
  
  // Proxy the HTTP request to the target
  proxy.web(req, res, { target: TARGET_HTTP }, (err) => {
    console.error('[HTTP Error]', err.message);
    res.writeHead(502, { 'Content-Type': 'text/plain' });
    res.end('Bad Gateway: Could not connect to backend server');
  });
});

// Handle WebSocket upgrade requests
server.on('upgrade', (req, socket, head) => {
  console.log(`[WebSocket] Upgrade request for ${req.url}`);
  
  // Proxy the WebSocket upgrade to the target
  proxy.ws(req, socket, head, { target: TARGET_WS }, (err) => {
    console.error('[WebSocket Error]', err.message);
    socket.destroy();
  });
});

// Handle proxy errors
proxy.on('error', (err, req, res) => {
  console.error('[Proxy Error]', err.message);
  if (res && !res.headersSent) {
    res.writeHead(500, { 'Content-Type': 'text/plain' });
    res.end('Proxy Error: ' + err.message);
  }
});

// Handle WebSocket proxy errors
proxy.on('ws-error', (err, req, socket) => {
  console.error('[WebSocket Proxy Error]', err.message);
  socket.end();
});

// Log successful proxy requests
proxy.on('proxyRes', (proxyRes, req, res) => {
  console.log(`[HTTP Response] ${req.method} ${req.url} - Status: ${proxyRes.statusCode}`);
});

// Start the server
server.listen(PROXY_PORT, () => {
  console.log(`Proxy server listening on port ${PROXY_PORT}`);
  console.log(`HTTP requests will be proxied to: ${TARGET_HTTP}`);
  console.log(`WebSocket requests will be proxied to: ${TARGET_WS}`);
  console.log('\nConfiguration can be modified at the top of this file.');
});

// Graceful shutdown
process.on('SIGINT', () => {
  console.log('\nShutting down proxy server...');
  server.close(() => {
    console.log('Server closed');
    process.exit(0);
  });
});



