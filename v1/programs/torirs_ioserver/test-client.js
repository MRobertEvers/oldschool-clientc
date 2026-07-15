#!/usr/bin/env node

/**
 * Test client for ToriRS IO Server
 * 
 * Tests all endpoints to verify server functionality
 * 
 * Usage: node test-client.js [server-url]
 *   Default server: http://localhost:8080
 */

const http = require('http');
const https = require('https');

const serverUrl = process.argv[2] || 'http://localhost:8080';
const url = new URL(serverUrl);
const client = url.protocol === 'https:' ? https : http;

console.log(`Testing server at: ${serverUrl}\n`);

let testsPassed = 0;
let testsFailed = 0;

function parseMultipartParts(data, contentType) {
    const boundaryMatch = (contentType || '').match(/boundary=([^;,\s]+)/i);
    if (!boundaryMatch) return [];

    let boundary = boundaryMatch[1].trim();
    if (boundary.startsWith('"') && boundary.endsWith('"')) {
        boundary = boundary.slice(1, -1);
    }

    const boundaryBuf = Buffer.from(`--${boundary}`);
    const crlf = Buffer.from('\r\n');
    const headerSep = Buffer.from('\r\n\r\n');
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
        if (
            bodyEnd >= 2 &&
            data[bodyEnd - 2] === 0x0d &&
            data[bodyEnd - 1] === 0x0a
        ) {
            bodyEnd -= 2;
        }
        parts.push(data.slice(bodyStart, bodyEnd));
        pos = nextBoundary;
    }

    return parts;
}

function test(name, fn) {
    console.log(`Testing: ${name}`);
    fn().then(() => {
        console.log(`  ✓ PASS\n`);
        testsPassed++;
    }).catch((error) => {
        console.error(`  ✗ FAIL: ${error.message}\n`);
        testsFailed++;
    });
}

// Test 1: Server info endpoint
function testServerInfo() {
    return new Promise((resolve, reject) => {
        const req = client.get(`${serverUrl}/`, (res) => {
            if (res.statusCode !== 200) {
                reject(new Error(`Expected 200, got ${res.statusCode}`));
                return;
            }
            
            let data = '';
            res.on('data', chunk => data += chunk);
            res.on('end', () => {
                if (data.includes('ToriRS IO Server')) {
                    console.log(`  Status: ${res.statusCode}`);
                    console.log(`  Content: ${data.split('\n')[0]}...`);
                    resolve();
                } else {
                    reject(new Error('Response does not contain expected content'));
                }
            });
        });
        
        req.on('error', reject);
        req.setTimeout(5000, () => reject(new Error('Request timeout')));
    });
}

// Test 2: Single archive
function testSingleArchive() {
    return new Promise((resolve, reject) => {
        const req = client.get(`${serverUrl}/archive/2/0`, (res) => {
            if (res.statusCode !== 200) {
                reject(new Error(`Expected 200, got ${res.statusCode}`));
                return;
            }
            
            const chunks = [];
            res.on('data', chunk => chunks.push(chunk));
            res.on('end', () => {
                const data = Buffer.concat(chunks);
                console.log(`  Status: ${res.statusCode}`);
                console.log(`  Content-Type: ${res.headers['content-type']}`);
                console.log(`  Size: ${data.length} bytes`);
                
                if (data.length > 0) {
                    resolve();
                } else {
                    reject(new Error('Archive is empty'));
                }
            });
        });
        
        req.on('error', reject);
        req.setTimeout(5000, () => reject(new Error('Request timeout')));
    });
}

// Test 3: Invalid archive (should return error)
function testInvalidArchive() {
    return new Promise((resolve, reject) => {
        const req = client.get(`${serverUrl}/archive/invalid/id`, (res) => {
            if (res.statusCode === 400) {
                console.log(`  Status: ${res.statusCode} (expected)`);
                resolve();
            } else {
                reject(new Error(`Expected 400, got ${res.statusCode}`));
            }
        });
        
        req.on('error', reject);
        req.setTimeout(5000, () => reject(new Error('Request timeout')));
    });
}

// Test 4: Batch archives
function testBatchArchives() {
    return new Promise((resolve, reject) => {
        const requestData = JSON.stringify({
            requests: [
                {table: 2, archive: 0},
                {table: 2, archive: 1}
            ]
        });
        
        const options = {
            hostname: url.hostname,
            port: url.port || (url.protocol === 'https:' ? 443 : 80),
            path: '/archives',
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                'Content-Length': requestData.length
            }
        };
        
        const req = client.request(options, (res) => {
            if (res.statusCode !== 200) {
                reject(new Error(`Expected 200, got ${res.statusCode}`));
                return;
            }
            
            const chunks = [];
            res.on('data', chunk => chunks.push(chunk));
            res.on('end', () => {
                const data = Buffer.concat(chunks);
                const contentType = res.headers['content-type'] || '';
                console.log(`  Status: ${res.statusCode}`);
                console.log(`  Content-Type: ${contentType}`);
                console.log(`  Total size: ${data.length} bytes`);

                if (!contentType.includes('multipart/form-data')) {
                    reject(new Error(`Expected multipart/form-data, got ${contentType}`));
                    return;
                }

                const archives = parseMultipartParts(data, contentType);
                console.log(`  Archives received: ${archives.length}`);
                console.log(`  Successful: ${archives.filter(a => a.length > 0).length}`);

                if (archives.length === 2 && archives.every(a => a.length > 0)) {
                    resolve();
                } else {
                    reject(new Error(`Expected 2 non-empty archives, got ${archives.length}`));
                }
            });
        });
        
        req.on('error', reject);
        req.setTimeout(5000, () => reject(new Error('Request timeout')));
        req.write(requestData);
        req.end();
    });
}

// Test 5: Lua script
function testLuaScript() {
    return new Promise((resolve, reject) => {
        const req = client.get(`${serverUrl}/scripts/init.lua`, (res) => {
            if (res.statusCode !== 200) {
                reject(new Error(`Expected 200, got ${res.statusCode}`));
                return;
            }
            
            let data = '';
            res.on('data', chunk => data += chunk);
            res.on('end', () => {
                console.log(`  Status: ${res.statusCode}`);
                console.log(`  Content-Type: ${res.headers['content-type']}`);
                console.log(`  Size: ${data.length} bytes`);
                console.log(`  Preview: ${data.split('\n')[0]}`);
                
                if (data.length > 0) {
                    resolve();
                } else {
                    reject(new Error('Script is empty'));
                }
            });
        });
        
        req.on('error', reject);
        req.setTimeout(5000, () => reject(new Error('Request timeout')));
    });
}

// Test 6: Invalid script (path traversal attempt)
function testInvalidScript() {
    return new Promise((resolve, reject) => {
        const req = client.get(`${serverUrl}/scripts/../../../etc/passwd`, (res) => {
            if (res.statusCode === 400) {
                console.log(`  Status: ${res.statusCode} (expected, path traversal blocked)`);
                resolve();
            } else {
                reject(new Error(`Expected 400, got ${res.statusCode}`));
            }
        });
        
        req.on('error', reject);
        req.setTimeout(5000, () => reject(new Error('Request timeout')));
    });
}

// Test 7: CORS headers
function testCORS() {
    return new Promise((resolve, reject) => {
        const req = client.get(`${serverUrl}/archive/2/0`, (res) => {
            const corsHeader = res.headers['access-control-allow-origin'];
            console.log(`  CORS header: ${corsHeader}`);
            
            if (corsHeader === '*') {
                resolve();
            } else {
                reject(new Error(`Expected CORS header '*', got '${corsHeader}'`));
            }
        });
        
        req.on('error', reject);
        req.setTimeout(5000, () => reject(new Error('Request timeout')));
    });
}

// Run all tests
async function runTests() {
    await test('1. Server info endpoint', testServerInfo);
    await test('2. Single archive loading', testSingleArchive);
    await test('3. Invalid archive (error handling)', testInvalidArchive);
    await test('4. Batch archive loading', testBatchArchives);
    await test('5. Lua script loading', testLuaScript);
    await test('6. Invalid script (security)', testInvalidScript);
    await test('7. CORS headers', testCORS);
    
    // Wait a bit for last test to complete
    setTimeout(() => {
        console.log('='.repeat(50));
        console.log(`Tests completed: ${testsPassed} passed, ${testsFailed} failed`);
        console.log('='.repeat(50));
        
        if (testsFailed > 0) {
            process.exit(1);
        }
    }, 1000);
}

runTests().catch(error => {
    console.error('Test suite error:', error);
    process.exit(1);
});
