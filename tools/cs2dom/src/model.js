/* Native cache decode + browser toridraw bridge for interface model components. */

import { request as httpRequest } from 'node:http';
import { existsSync, readFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawn } from 'node:child_process';

const HERE = dirname(fileURLToPath(import.meta.url));
const VIEWER = resolve(HERE, '..', '..', 'entity_viewer');

export function modelAssets() {
    return {
        javascript: join(VIEWER, 'web', 'ev_wasm.js'),
        wasm: join(VIEWER, 'web', 'ev_wasm.wasm'),
    };
}

export function modelIndex(contentDir) {
    const out = new Map();
    const pack = join(contentDir, 'pack', '7_models.pack');
    if( !existsSync(pack) ) return out;
    for( const line of readFileSync(pack, 'utf8').split(/\r?\n/) ) {
        const split = line.indexOf('=');
        if( split < 1 ) continue;
        const id = Number.parseInt(line.slice(0, split), 10);
        if( !Number.isNaN(id) ) out.set(id, line.slice(split + 1).trim());
    }
    return out;
}

export function rawModel(contentDir, index, id) {
    const name = index.get(id);
    if( !name ) return null;
    const path = join(contentDir, 'models', `${name}.model`);
    return existsSync(path) ? readFileSync(path) : null;
}

/** Start the existing entity-viewer cache half on a private port. */
export function startModelServer(project, port, log = () => {}) {
    const binary = join(VIEWER, 'ev_server');
    const revision = project.revision || inferRevision(project) || 'osrs239';
    const cache = project.cache || discoverCache(revision);
    if( !existsSync(binary) || !cache ) {
        log('  models    unavailable (build tools/entity_viewer/ev_server and configure a Dat2 cache)');
        return null;
    }
    const names = project.unpackedContent || project.content;
    const args = ['--rev', revision, cache, '--port', String(port), '--web', join(VIEWER, 'web')];
    if( names && existsSync(names) ) args.push('--names', names);
    const child = spawn(binary, args, { stdio: ['ignore', 'ignore', 'pipe'] });
    let reported = false;
    child.stderr.on('data', (chunk) => {
        const line = String(chunk).trim();
        if( !reported && /error|failed|usage/i.test(line) ) { reported = true; log(`  models    ${line.split('\n')[0]}`); }
    });
    child.on('error', (error) => log(`  models    ${error.message}`));
    log(`  models    toridraw/WASM via ${cache}`);
    return { child, port };
}

export function proxyModel(server, response, { method = 'GET', path, body = null } = {}) {
    if( !server ) {
        response.writeHead(503, { 'content-type': 'text/plain' });
        response.end('model renderer is unavailable');
        return;
    }
    const attempt = (remaining) => {
        const upstream = httpRequest({
            hostname: '127.0.0.1', port: server.port, path, method,
            headers: body ? { 'content-length': body.length, 'content-type': 'application/octet-stream' } : {},
        }, (incoming) => {
            const headers = { ...incoming.headers, 'cache-control': 'no-store' };
            delete headers.connection;
            response.writeHead(incoming.statusCode || 502, headers);
            incoming.pipe(response);
        });
        upstream.on('error', (error) => {
            if( remaining > 0 ) return setTimeout(() => attempt(remaining - 1), 100);
            if( !response.headersSent ) response.writeHead(502, { 'content-type': 'text/plain' });
            response.end(`model renderer did not start: ${error.message}`);
        });
        if( body ) upstream.end(body); else upstream.end();
    };
    attempt(30);
}

/**
 * Buffered counterpart used by the dev server's bounded response cache. Model
 * blobs are already consumed whole by the renderer worker, so buffering here
 * removes a renderer round-trip on every repeat without changing the payload.
 */
export function requestModel(server, { method = 'GET', path, body = null } = {}) {
    if( !server ) return Promise.resolve({
        status: 503,
        headers: { 'content-type': 'text/plain' },
        body: Buffer.from('model renderer is unavailable'),
    });
    return new Promise((resolveRequest, rejectRequest) => {
        const attempt = (remaining) => {
            const upstream = httpRequest({
                hostname: '127.0.0.1', port: server.port, path, method,
                headers: body ? {
                    'content-length': body.length,
                    'content-type': 'application/octet-stream',
                } : {},
            }, (incoming) => {
                const chunks = [];
                let length = 0;
                incoming.on('data', (chunk) => {
                    chunks.push(chunk);
                    length += chunk.length;
                });
                incoming.on('end', () => resolveRequest({
                    status: incoming.statusCode || 502,
                    headers: incoming.headers,
                    body: Buffer.concat(chunks, length),
                }));
                incoming.on('error', rejectRequest);
            });
            upstream.on('error', (error) => {
                if( remaining > 0 ) return setTimeout(() => attempt(remaining - 1), 100);
                rejectRequest(error);
            });
            if( body ) upstream.end(body); else upstream.end();
        };
        attempt(30);
    });
}

function inferRevision(project) {
    const match = /(?:^|[/\\])(osrs\d+)(?:-content)?(?:[/\\]|$)/.exec(project.content || project.unpackedContent || '');
    return match?.[1] || null;
}

function discoverCache(revision) {
    let dir = resolve(HERE);
    for( let i = 0; i < 8; i++ ) {
        for( const name of [`cache.${revision}`, 'cache.osrs239'] ) {
            const candidate = join(dir, name);
            if( existsSync(join(candidate, 'main_file_cache.dat2')) ) return candidate;
        }
        const parent = resolve(dir, '..');
        if( parent === dir ) break;
        dir = parent;
    }
    return null;
}
