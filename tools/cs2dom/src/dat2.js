/*
 * A Dat2 cache as a cs2dom content source.
 *
 * cachepack already owns the revision-aware interface, CS2 and sprite decoders.
 * Reimplementing those formats in JavaScript would give the preview a second,
 * drifting cache reader, so a Dat2 project asks cachepack for only the asset
 * tables cs2dom can display.  The derived tree lives in the OS temp directory and
 * is keyed by the cache bytes, revision and decoder build.  The first open does the
 * decoding; later opens of the same cache reuse it.
 */

import { createHash } from 'node:crypto';
import { spawnSync } from 'node:child_process';
import {
    existsSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, renameSync, rmSync, statSync,
    writeFileSync,
} from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = dirname(fileURLToPath(import.meta.url));

export const CACHEPACK_TOOL = '3rd/rscache/tools/cachepack/cachepack';
export const CACHEPACK_BUILD = 'make -C 3rd/rscache/tools cachepack';

/** Add a cached, selective Dat2 decode to a project as a read-only source. */
export function prepareDat2Project(project, options = {}) {
    if( !project.cache )
        return { ...project, contentSource: project.contentSource || 'content' };
    if( !project.revision )
        throw new Error(
            'a Dat2 source needs its cachepack profile name; set "revision": "osrs239" ' +
            'in cs2dom.json or pass --rev osrs239');

    const cache = resolve(project.cache);
    const dat2 = join(cache, 'main_file_cache.dat2');
    if( !existsSync(dat2) )
        throw new Error(`${cache} is not a Dat2 cache (main_file_cache.dat2 is missing)`);

    const tool = options.tool || findCachepack(options.from || HERE);
    if( !tool || !existsSync(tool) )
        throw new Error(
            `${CACHEPACK_TOOL} is not built; build it with \`${CACHEPACK_BUILD}\``);

    const cacheRoot = options.cacheRoot || join(tmpdir(), 'cs2dom-dat2');
    mkdirSync(cacheRoot, { recursive: true });
    const key = dat2CacheKey(cache, project.revision, tool);
    const target = join(cacheRoot, key);
    const marker = join(target, '.cs2dom-ready.json');
    if( ready(marker, cache, project.revision) )
        return derivedProject(project, cache, target, true);
    if( existsSync(target) )
        rmSync(target, { recursive: true, force: true });

    const log = options.log || (() => {});
    log(`  decoding Dat2 cache ${cache}`);
    log('  first open extracts interfaces, clientscripts, sprites and models; later opens reuse it');

    const staging = mkdtempSync(join(cacheRoot, `${key}.staging-`));
    const args = [
        'unpack', '--cache', cache, '--rev', project.revision, '--src', staging,
        '--types', 'varp,varbit,inv,varc',
        '--assets=interfaces,scripts,sprites,models', '--warn', '5',
    ];
    const run = spawnSync(tool, args, { encoding: 'utf8', maxBuffer: 64 * 1024 * 1024 });
    if( run.status !== 0 ) {
        rmSync(staging, { recursive: true, force: true });
        const output = `${run.stdout || ''}${run.stderr || ''}`.trim();
        throw new Error(`cachepack could not open ${cache}:\n${output || `exit ${run.status}`}`);
    }

    writeFileSync(join(staging, '.cs2dom-ready.json'), JSON.stringify({
        cache,
        revision: project.revision,
        generatedAt: new Date().toISOString(),
    }, null, 2) + '\n');

    /* Another dev server may have completed the same immutable key while this one
     * decoded. Keep the completed target and discard only our private staging dir. */
    if( ready(join(target, '.cs2dom-ready.json'), cache, project.revision) )
        rmSync(staging, { recursive: true, force: true });
    else if( !existsSync(target) )
        renameSync(staging, target);
    else {
        rmSync(staging, { recursive: true, force: true });
        throw new Error(`another Dat2 decode left an incomplete cache at ${target}`);
    }

    log(`  cached Dat2 decode ${target}`);
    return derivedProject(project, cache, target, false);
}

export function dat2CacheKey(cacheDir, revision, tool) {
    const cache = resolve(cacheDir);
    const decoder = statSync(tool);
    const files = readdirSync(cache)
        .filter((name) => name === 'main_file_cache.dat2' || /^main_file_cache\.idx\d+$/.test(name))
        .sort()
        .map((name) => {
            const info = statSync(join(cache, name));
            return [name, info.size, info.mtimeMs];
        });
    return createHash('sha256').update(JSON.stringify({
        schema: 2,
        cache,
        revision,
        files,
        cs2Names: treeFingerprint(process.env.CACHEPACK_CS2_NAMES),
        decoderSize: decoder.size,
        decoderMtime: decoder.mtimeMs,
    })).digest('hex').slice(0, 20);
}

function treeFingerprint(path) {
    if( !path ) return null;
    const root = resolve(path);
    if( !existsSync(root) ) return [root, 'missing'];
    const files = [];
    const visit = (dir, prefix = '') => {
        const entries = readdirSync(dir, { withFileTypes: true })
            .sort((left, right) => left.name.localeCompare(right.name));
        for( const entry of entries ) {
            const relative = prefix ? `${prefix}/${entry.name}` : entry.name;
            const full = join(dir, entry.name);
            if( entry.isDirectory() ) visit(full, relative);
            else {
                const info = statSync(full);
                files.push([relative, info.size, info.mtimeMs]);
            }
        }
    };
    visit(root);
    return [root, files];
}

export function findCachepack(from) {
    let dir = resolve(from);
    for( let i = 0; i < 8; i++ ) {
        const candidate = join(dir, CACHEPACK_TOOL);
        if( existsSync(candidate) ) return candidate;
        dir = resolve(dir, '..');
    }
    return null;
}

function ready(marker, cache, revision) {
    if( !existsSync(marker) ) return false;
    try {
        const value = JSON.parse(readFileSync(marker, 'utf8'));
        return value.cache === cache && value.revision === revision;
    } catch {
        return false;
    }
}

function derivedProject(project, cache, content, reused) {
    return {
        ...project,
        cache,
        /* Authored TSX uses the unpacked tree's ledgers when one was configured;
         * a cache-only project uses the derived ledgers instead. */
        content: project.content || content,
        dat2Content: content,
        contentSource: project.content ? 'content' : 'dat2',
        derivedContent: true,
        reusedDat2Decode: reused,
    };
}
