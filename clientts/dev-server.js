#!/usr/bin/env bun

import { serve } from 'bun';

const server = serve({
    port: 3000,
    fetch(req) {
        const url = new URL(req.url);
        const path = url.pathname;

        // Serve static files from the out directory
        let filePath = `out${path}`;

        // Default to index.html for root
        if (path === '/') {
            filePath = 'out/index.html';
        }

        try {
            const file = Bun.file(filePath);

            // Set correct MIME types for source maps
            if (path.endsWith('.map')) {
                return new Response(file, {
                    headers: {
                        'Content-Type': 'application/json',
                        'Access-Control-Allow-Origin': '*',
                        'Access-Control-Allow-Methods': 'GET',
                        'Access-Control-Allow-Headers': 'Content-Type'
                    }
                });
            }

            return new Response(file, {
                headers: {
                    'Access-Control-Allow-Origin': '*',
                    'Access-Control-Allow-Methods': 'GET',
                    'Access-Control-Allow-Headers': 'Content-Type'
                }
            });
        } catch (error) {
            return new Response(`File not found: ${path}`, { status: 404 });
        }
    }
});

console.log(`🚀 Development server running at http://localhost:${server.port}`);
console.log(`📁 Serving files from: out/`);
console.log(`🔧 Source maps enabled for debugging`);
console.log(`\n💡 Open your browser and navigate to:`);
console.log(`   http://localhost:${server.port}`);
console.log(`\n🔍 To debug:`);
console.log(`   1. Open browser dev tools (F12)`);
console.log(`   2. Go to Sources tab`);
console.log(`   3. You should see your TypeScript files under webpack://`);
console.log(`\n⏹️  Press Ctrl+C to stop the server`);
