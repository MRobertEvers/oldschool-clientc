import assert from 'node:assert/strict';
import { mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

import {
    BoundedAssetCache, __assetCacheTest, assetRecord, fileVersion, filesVersion, sendAsset,
} from '../src/asset_cache.js';

function response() {
    return {
        status: 0,
        headers: {},
        body: null,
        writeHead(status, headers) { this.status = status; this.headers = headers; },
        end(body) { this.body = body; },
    };
}

{
    const cache = new BoundedAssetCache({ maxBytes: 10, maxEntries: 2, maxItemBytes: 10 });
    cache.set('a', assetRecord(Buffer.alloc(6), { version: '1' }));
    cache.set('b', assetRecord(Buffer.alloc(6), { version: '1' }));
    assert.equal(cache.get('a'), null, 'byte bound did not evict the oldest entry');
    assert.equal(cache.get('b')?.body.length, 6, 'newest entry was not retained');
    cache.set('too-large', assetRecord(Buffer.alloc(11), { version: '1' }));
    assert.equal(cache.get('too-large'), null, 'oversized item entered the resident cache');
    const stats = cache.snapshot();
    assert(stats.bytes <= stats.maxBytes && stats.entries <= stats.maxEntries,
        `cache exceeded its bound: ${JSON.stringify(stats)}`);
}

{
    const cache = new BoundedAssetCache();
    let loads = 0;
    const load = async () => {
        loads++;
        await Promise.resolve();
        return assetRecord('shared', { version: 'v1' });
    };
    const [first, second] = await Promise.all([
        cache.getOrLoad('shared', 'v1', load),
        cache.getOrLoad('shared', 'v1', load),
    ]);
    assert.equal(loads, 1, 'concurrent misses did not share their loader');
    assert.strictEqual(first, second, 'concurrent callers received distinct records');
    assert.strictEqual(await cache.getOrLoad('shared', 'v1', load), first,
        'warm lookup did not use the resident record');
    assert.equal(loads, 1, 'warm lookup reran its loader');
}

{
    const cache = new BoundedAssetCache();
    let releaseOld;
    const old = cache.getOrLoad('state:bank', 'old', () => new Promise((resolve) => {
        releaseOld = () => resolve(assetRecord('old', { version: 'old' }));
    }));
    await Promise.resolve();
    const fresh = cache.getOrLoad('state:bank', 'fresh', async () =>
        assetRecord('fresh', { version: 'fresh' }));
    assert.equal(String((await fresh).body), 'fresh');
    releaseOld();
    await old;
    assert.equal(String(cache.get('state:bank', 'fresh').body), 'fresh',
        'an older in-flight load replaced a newer source identity');

    let releaseReload;
    const beforeReload = cache.getOrLoad('model:widget:1', 'v1', () =>
        new Promise((resolve) => {
            releaseReload = () => resolve(assetRecord('stale', { version: 'v1' }));
        }));
    await Promise.resolve();
    cache.deletePrefix('model:');
    releaseReload();
    await beforeReload;
    assert.equal(cache.get('model:widget:1'), null,
        'a pre-reload in-flight asset repopulated the invalidated cache');
}

{
    const record = assetRecord('validator', {
        type: 'image/png',
        headers: {
            connection: 'close',
            'content-length': '999',
            'x-texture-ids': '4,5',
        },
    });
    const fresh = response();
    sendAsset({ method: 'GET', headers: {} }, fresh, record);
    assert.equal(fresh.status, 200);
    assert.equal(String(fresh.body), 'validator');
    assert.equal(fresh.headers['content-length'], 9);
    assert.equal(fresh.headers.connection, undefined, 'hop-by-hop header leaked');
    assert.equal(fresh.headers['x-texture-ids'], '4,5', 'renderer metadata was dropped');

    const conditional = response();
    sendAsset({
        method: 'GET', headers: { 'if-none-match': `W/${record.etag}, "other"` },
    }, conditional, record);
    assert.equal(conditional.status, 304, 'matching validator did not produce 304');
    assert.equal(conditional.body, undefined, '304 included a response body');
    assert.equal(conditional.headers['content-length'], undefined, '304 included a body length');

    const head = response();
    sendAsset({ method: 'HEAD', headers: {} }, head, record);
    assert.equal(head.status, 200);
    assert.equal(head.body, undefined, 'HEAD included a response body');
    assert(__assetCacheTest.etagMatches('*', record.etag));
}

{
    const root = mkdtempSync(join(tmpdir(), 'cs2dom-asset-cache-'));
    try {
        const first = join(root, 'first.bin');
        const second = join(root, 'second.bin');
        writeFileSync(first, 'one');
        writeFileSync(second, 'two');
        const initial = filesVersion([first, second]);
        const cache = new BoundedAssetCache();
        cache.set('files', assetRecord('old', { version: initial }));
        writeFileSync(first, 'one changed');
        const changed = filesVersion([first, second]);
        assert.notEqual(changed, initial, 'source edit retained the same file identity');
        assert.equal(cache.get('files', changed), null,
            'source edit reused a stale derived response');
        assert.equal(fileVersion(join(root, 'missing')), '-', 'missing-file identity was unstable');
    } finally {
        rmSync(root, { recursive: true, force: true });
    }
}

console.log('asset cache tests passed');
