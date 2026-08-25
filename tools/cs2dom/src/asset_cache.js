/* Bounded response caching for the dev server's immutable-by-identity assets. */

import { createHash } from 'node:crypto';
import { statSync } from 'node:fs';

const DEFAULT_MAX_BYTES = 64 * 1024 * 1024;
const DEFAULT_MAX_ENTRIES = 4096;
const DEFAULT_MAX_ITEM_BYTES = 16 * 1024 * 1024;

/**
 * A byte-accounted LRU. Pending loaders are shared, but never count toward the
 * resident bound and failures are never retained.
 */
export class BoundedAssetCache {
    constructor(options = {}) {
        this.maxBytes = positive(options.maxBytes, DEFAULT_MAX_BYTES);
        this.maxEntries = positive(options.maxEntries, DEFAULT_MAX_ENTRIES);
        this.maxItemBytes = positive(options.maxItemBytes,
            Math.min(DEFAULT_MAX_ITEM_BYTES, this.maxBytes));
        this.entries = new Map();
        this.pending = new Map();
        this.latestPending = new Map();
        this.epoch = 0;
        this.bytes = 0;
        this.hits = 0;
        this.misses = 0;
        this.evictions = 0;
    }

    get(key, version = undefined) {
        const entry = this.entries.get(key);
        if( !entry ) {
            this.misses++;
            return null;
        }
        if( version !== undefined && entry.version !== version ) {
            this._delete(key, entry);
            this.misses++;
            return null;
        }
        this.entries.delete(key);
        this.entries.set(key, entry);
        this.hits++;
        return entry;
    }

    set(key, entry) {
        if( !entry || !Buffer.isBuffer(entry.body) )
            throw new TypeError('asset cache entries require a Buffer body');
        const prior = this.entries.get(key);
        if( prior ) this._delete(key, prior);
        if( entry.body.length > this.maxItemBytes || entry.body.length > this.maxBytes )
            return entry;
        this.entries.set(key, entry);
        this.bytes += entry.body.length;
        this._trim();
        return entry;
    }

    getOrLoad(key, version, loader) {
        const resident = this.get(key, version);
        if( resident ) return Promise.resolve(resident);
        const pendingKey = `${key}\0${version ?? ''}`;
        let pending = this.pending.get(pendingKey);
        if( pending ) return pending;
        const epoch = this.epoch;
        pending = Promise.resolve().then(loader).then((entry) => {
            if( entry?.cacheable !== false && this.epoch === epoch &&
                this.latestPending.get(key) === pendingKey ) this.set(key, entry);
            return entry;
        }).finally(() => {
            this.pending.delete(pendingKey);
            if( this.latestPending.get(key) === pendingKey ) this.latestPending.delete(key);
        });
        this.pending.set(pendingKey, pending);
        this.latestPending.set(key, pendingKey);
        return pending;
    }

    deletePrefix(prefix) {
        for( const [key, entry] of this.entries )
            if( key.startsWith(prefix) ) this._delete(key, entry);
        for( const key of this.latestPending.keys() )
            if( key.startsWith(prefix) ) this.latestPending.delete(key);
    }

    delete(key) {
        const entry = this.entries.get(key);
        if( entry ) this._delete(key, entry);
        this.latestPending.delete(key);
    }

    clear() {
        this.entries.clear();
        this.latestPending.clear();
        this.bytes = 0;
        this.epoch++;
    }

    snapshot() {
        return Object.freeze({
            entries: this.entries.size,
            bytes: this.bytes,
            pending: this.pending.size,
            hits: this.hits,
            misses: this.misses,
            evictions: this.evictions,
            maxBytes: this.maxBytes,
            maxEntries: this.maxEntries,
            maxItemBytes: this.maxItemBytes,
        });
    }

    _delete(key, entry) {
        if( !this.entries.delete(key) ) return;
        this.bytes -= entry.body.length;
    }

    _trim() {
        while( this.bytes > this.maxBytes || this.entries.size > this.maxEntries ) {
            const oldest = this.entries.entries().next().value;
            if( !oldest ) break;
            this._delete(oldest[0], oldest[1]);
            this.evictions++;
        }
    }
}

/** Build one strong validator while the body is already hot in memory. */
export function assetRecord(body, options = {}) {
    body = Buffer.isBuffer(body) ? body : Buffer.from(body ?? '');
    const headers = cleanHeaders(options.headers);
    const type = options.type || headers['content-type'] || 'application/octet-stream';
    delete headers['content-type'];
    delete headers['content-length'];
    delete headers.etag;
    delete headers['cache-control'];
    return Object.freeze({
        status: Number(options.status) || 200,
        type,
        body,
        version: options.version,
        etag: `"${createHash('sha256').update(body).digest('base64url')}"`,
        headers: Object.freeze(headers),
        cacheable: options.cacheable !== false,
        dependencies: options.dependencies
            ? Object.freeze([...options.dependencies]) : null,
    });
}

/** Send GET/HEAD assets with weak-compatible If-None-Match handling. */
export function sendAsset(request, response, entry, options = {}) {
    const cacheControl = options.cacheControl || 'public, max-age=0, must-revalidate';
    const headers = {
        ...entry.headers,
        'content-type': entry.type,
        'cache-control': cacheControl,
        etag: entry.etag,
        'content-length': entry.body.length,
    };
    const method = String(request?.method || 'GET').toUpperCase();
    if( entry.status === 200 && (method === 'GET' || method === 'HEAD') &&
        etagMatches(request?.headers?.['if-none-match'], entry.etag) ) {
        delete headers['content-length'];
        response.writeHead(304, headers);
        response.end();
        return;
    }
    response.writeHead(entry.status, headers);
    response.end(method === 'HEAD' ? undefined : entry.body);
}

/** A cheap identity check: stat, but no read/decode/encode. */
export function fileVersion(path) {
    try {
        const stat = statSync(path, { bigint: true });
        return `${stat.dev}:${stat.ino}:${stat.size}:${stat.mtimeNs}:${stat.ctimeNs}`;
    } catch( error ) {
        if( error?.code === 'ENOENT' || error?.code === 'ENOTDIR' ) return '-';
        throw error;
    }
}

export function filesVersion(paths) {
    const hash = createHash('sha256');
    for( const path of paths ) {
        hash.update(String(path));
        hash.update('\0');
        hash.update(fileVersion(path));
        hash.update('\u0001');
    }
    return hash.digest('base64url');
}

function etagMatches(header, etag) {
    if( !header ) return false;
    for( let candidate of String(header).split(',') ) {
        candidate = candidate.trim();
        if( candidate === '*' ) return true;
        if( candidate.startsWith('W/') ) candidate = candidate.slice(2);
        if( candidate === etag ) return true;
    }
    return false;
}

function cleanHeaders(headers = {}) {
    const result = {};
    const excluded = new Set([
        'connection', 'keep-alive', 'proxy-authenticate', 'proxy-authorization',
        'te', 'trailer', 'transfer-encoding', 'upgrade',
    ]);
    for( const [name, value] of Object.entries(headers) ) {
        const lower = name.toLowerCase();
        if( value !== undefined && !excluded.has(lower) ) result[lower] = value;
    }
    return result;
}

function positive(value, fallback) {
    value = Number(value);
    return Number.isSafeInteger(value) && value > 0 ? value : fallback;
}

export const __assetCacheTest = Object.freeze({ etagMatches });
