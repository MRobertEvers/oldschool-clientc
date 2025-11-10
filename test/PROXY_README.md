# HTTP and WebSocket Proxy Server

A Node.js proxy server that forwards HTTP requests and WebSocket connections to a backend server.

## Features

- ✅ HTTP request proxying
- ✅ WebSocket upgrade handling
- ✅ CORS support (configurable)
- ✅ Error handling and logging
- ✅ Graceful shutdown

## Installation

```bash
cd test
npm install
```

## Configuration

Edit the configuration at the top of `proxy-server.js`:

```javascript
const PROXY_PORT = 8080;                      // Port the proxy listens on
const TARGET_HTTP = 'http://localhost:4949';  // HTTP backend
const TARGET_WS = 'ws://localhost:4949';      // WebSocket backend
```

## Usage

### Start the server:

```bash
npm start
# or
node proxy-server.js
```

### Test HTTP proxying:

```bash
curl http://localhost:8080/api/some-endpoint
```

### Test WebSocket connection:

```javascript
// In browser or Node.js
const ws = new WebSocket('ws://localhost:8080');
ws.onopen = () => console.log('Connected');
ws.onmessage = (event) => console.log('Message:', event.data);
```

### From your web application:

```javascript
// HTTP request
fetch('http://localhost:8080/api/data')
  .then(res => res.json())
  .then(data => console.log(data));

// WebSocket connection
const socket = new WebSocket('ws://localhost:8080');
socket.addEventListener('message', (event) => {
  console.log('Message from server:', event.data);
});
```

## How It Works

1. **HTTP Requests**: All HTTP requests to the proxy server are forwarded to `TARGET_HTTP`
2. **WebSocket Upgrades**: Connection upgrade requests are forwarded to `TARGET_WS`
3. **Error Handling**: Connection errors return appropriate HTTP status codes (500/502)
4. **CORS**: Enabled by default for cross-origin requests

## Stopping the Server

Press `Ctrl+C` to gracefully shut down the server.

## Logs

The server logs all requests and responses:
- `[HTTP]` - HTTP request received
- `[HTTP Response]` - Backend response status
- `[WebSocket]` - WebSocket upgrade request
- `[HTTP Error]` / `[WebSocket Error]` - Connection errors

## Advanced Configuration

### Custom request/response handling:

You can modify requests before proxying or responses before sending them back:

```javascript
// Modify request before proxying
proxy.on('proxyReq', (proxyReq, req, res) => {
  // Add custom headers
  proxyReq.setHeader('X-Custom-Header', 'value');
});

// Modify response before sending
proxy.on('proxyRes', (proxyRes, req, res) => {
  // Log or modify response
  console.log('Status:', proxyRes.statusCode);
});
```

### Route-specific proxying:

```javascript
server.on('request', (req, res) => {
  if (req.url.startsWith('/api/')) {
    proxy.web(req, res, { target: 'http://api-server:3000' });
  } else {
    proxy.web(req, res, { target: 'http://web-server:8000' });
  }
});
```

## Dependencies

- `http-proxy` (^1.18.1) - Full-featured HTTP proxy library
- Node.js >= 14.0.0

## Troubleshooting

### Port already in use:
Change `PROXY_PORT` to an available port.

### Backend not reachable:
Verify `TARGET_HTTP` and `TARGET_WS` point to the correct backend server.

### CORS issues:
CORS headers are enabled by default. Adjust them in the request handler if needed.



