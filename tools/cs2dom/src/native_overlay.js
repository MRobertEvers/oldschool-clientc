/*
 * A source interface rendered by the production C client.
 *
 * The C client consumes Dat2 archives, while the content picker consumes the
 * editable `.if` and `.cs2` files cachepack writes. Bridging them by baking the
 * whole 218 MB cache on every click is exact but far too slow. This module makes
 * a content-addressed copy-on-write cache, replaces only the selected interface
 * and the source scripts reachable from its hooks, and leaves every other table
 * backed by the base cache's unchanged bytes.
 *
 * It intentionally does not render anything itself. Pass the returned project to
 * `renderNativeInterface` in native_preview.js; App/UITree/CS2/ToriDraw then own
 * layout, clipping, conditionals, fonts, sprites and models end to end.
 */

import { createHash } from 'node:crypto';
import { spawnSync } from 'node:child_process';
import {
    constants as fsConstants,
    copyFileSync,
    existsSync,
    mkdirSync,
    mkdtempSync,
    readFileSync,
    readdirSync,
    renameSync,
    rmSync,
    statSync,
    writeFileSync,
} from 'node:fs';
import { homedir, tmpdir } from 'node:os';
import { basename, dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import { CACHEPACK_BUILD, CACHEPACK_TOOL, findCachepack } from './dat2.js';

const HERE = dirname(fileURLToPath(import.meta.url));
const OVERLAY_SCHEMA = 2;
const MAX_PACK_LOG = 64 * 1024 * 1024;

/**
 * Build or reuse a Dat2 overlay for one editable content interface.
 *
 * The returned object is still a project object, so it can be passed directly
 * to `renderNativeInterface`. `cache` points at the immutable overlay and
 * `nativeOverlay` describes what was replaced.
 */
export function prepareNativeOverlay(project, interfaceNameOrId, options = {}) {
    const revision = project.revision || 'osrs239';
    if( !project.cache ) throw new Error('native source preview requires a base Dat2 cache');
    if( !project.content && !project.contentDir )
        throw new Error('native source preview requires an OSRS-Content directory');
    const baseCache = resolve(project.cache);
    const content = resolve(project.content || project.contentDir);
    requireFile(join(baseCache, 'main_file_cache.dat2'), 'base Dat2 cache');
    requireDirectory(content, 'content directory');

    const tool = resolve(options.tool || findCachepack(options.from || HERE) || '');
    if( !tool || !existsSync(tool) )
        throw new Error(`${CACHEPACK_TOOL} is not built; build it with \`${CACHEPACK_BUILD}\``);

    const selected = resolveInterface(content, interfaceNameOrId);
    const scripts = collectInterfaceScripts(content, selected.name);
    const cs2Names = resolveCs2Names(project, options);
    const cacheRoot = resolve(options.cacheRoot || join(tmpdir(), 'cs2dom-native-overlays'));
    mkdirSync(cacheRoot, { recursive: true });

    const key = nativeOverlayKey({
        baseCache, content, revision, tool, cs2Names,
        interfaceId: selected.id, interfaceName: selected.name, scripts,
    });
    const target = join(cacheRoot, key);
    const markerPath = join(target, '.cs2dom-native-overlay.json');
    const existing = readMarker(markerPath);
    if( existing?.key === key )
        return overlayProject(project, baseCache, target, selected, scripts, key, true,
                              existing.timings || null);
    if( existsSync(target) )
        throw new Error(`incomplete native overlay already exists at ${target}`);

    const log = options.log || (() => {});
    log(`  composing native cache overlay for ${selected.name} (${selected.id})`);
    const staging = mkdtempSync(join(cacheRoot, `${key}.staging-`));
    let cloneMs = 0;
    let packMs = 0;
    try {
        let started = performance.now();
        cloneCache(baseCache, staging);
        cloneMs = performance.now() - started;

        const source = join(staging, '.source');
        stageSource(content, source, selected, scripts);
        const archiveList = join(staging, '.archives');
        writeFileSync(archiveList, [
            `interfaces=${selected.id}`,
            ...scripts.map((script) => `scripts=${script.id}`),
            '',
        ].join('\n'));

        const args = [
            'pack', '--src', source, '--out', staging, '--rev', revision,
            '--asset-only', '--assets=interfaces,scripts', '--archive-list', archiveList,
            '--warn', '5',
        ];
        started = performance.now();
        const run = spawnSync(tool, args, {
            encoding: 'utf8',
            maxBuffer: MAX_PACK_LOG,
            timeout: options.timeoutMs || 120_000,
            env: cs2Names ? { ...process.env, CACHEPACK_CS2_NAMES: cs2Names } : process.env,
        });
        packMs = performance.now() - started;
        if( run.status !== 0 )
            throw new Error(packFailure(run, tool, args, cs2Names));

        /* The overlay no longer needs its compiler inputs. Keeping only the cache
         * and marker makes reuse cheap and prevents a stale source symlink from
         * looking like part of the artifact. */
        rmSync(source, { recursive: true, force: true });
        rmSync(archiveList, { force: true });
        const timings = { cloneMs: Math.round(cloneMs), packMs: Math.round(packMs) };
        writeFileSync(join(staging, '.cs2dom-native-overlay.json'), JSON.stringify({
            schema: OVERLAY_SCHEMA,
            key,
            baseCache,
            content,
            revision,
            interfaceId: selected.id,
            interfaceName: selected.name,
            scriptIds: scripts.map((script) => script.id),
            cs2Names,
            timings,
            generatedAt: new Date().toISOString(),
        }, null, 2) + '\n');

        /* Immutable keys make duplicate concurrent builds harmless. Whichever
         * process publishes first wins; the other discards only its private dir. */
        const winner = readMarker(markerPath);
        if( winner?.key === key )
            rmSync(staging, { recursive: true, force: true });
        else if( !existsSync(target) )
            renameSync(staging, target);
        else
            throw new Error(`another process left an incomplete overlay at ${target}`);

        log(`  native overlay ready in ${Math.round(cloneMs + packMs)} ms ` +
            `(${scripts.length} source scripts)`);
        return overlayProject(project, baseCache, target, selected, scripts, key, false,
                              { cloneMs: Math.round(cloneMs), packMs: Math.round(packMs) });
    } catch( error ) {
        rmSync(staging, { recursive: true, force: true });
        throw error;
    }
}

/** Compose the source overlay and render it through App/UITree/ToriDraw in one call. */
export async function renderNativeContentInterface(project, interfaceNameOrId, options = {}) {
    const overlayProject = prepareNativeOverlay(project, interfaceNameOrId, options);
    const { renderNativeInterface } = await import('./native_preview.js');
    const rendered = await renderNativeInterface(
        overlayProject,
        overlayProject.nativeOverlay.interfaceId,
        options,
    );
    return { ...rendered, project: overlayProject, overlay: overlayProject.nativeOverlay };
}

/** Return the source-script closure rooted at every hook in an interface file. */
export function collectInterfaceScripts(contentDir, interfaceName) {
    const content = resolve(contentDir);
    const interfacePath = join(content, 'interfaces', `${interfaceName}.if`);
    requireFile(interfacePath, 'interface source');
    const scriptPack = readPack(join(content, 'pack', '12_clientscripts.pack'));
    const nameToId = new Map([...scriptPack].map(([id, name]) => [name, id]));
    const hookIds = new Set();
    const interfaceText = readFileSync(interfacePath, 'utf8');
    const hooks = /^\s*on[a-z0-9_]*\s*=\s*i:(-?\d+)/gim;
    for( let match; (match = hooks.exec(interfaceText)); ) {
        const id = Number(match[1]);
        if( id >= 0 ) hookIds.add(id);
    }

    const selected = new Map();
    const pending = [...hookIds].sort((left, right) => left - right);
    while( pending.length ) {
        const id = pending.shift();
        if( selected.has(id) ) continue;
        const name = scriptPack.get(id);
        if( !name ) continue; /* a base-cache-only script remains the exact fallback */
        const path = join(content, 'scripts', `${name}.cs2`);
        if( !existsSync(path) ) continue; /* undecompilable `.cs2b`: keep base bytes */
        const source = readFileSync(path, 'utf8');
        selected.set(id, { id, name, path, source });

        /* Procedures are ordinary `~name(...)` calls. Event bindings are script
         * strings (`if_setontimer("name(0)", ...)`) which the compiler also has
         * to resolve and which become future runtime roots. Follow both forms. */
        const callPatterns = [
            /~([A-Za-z_][A-Za-z0-9_]*)/g,
            /* Deferred hook strings can be either `name(args)` or the bare
             * `name` spelling used by no-argument callbacks. Ordinary labels
             * are harmless because only names present in the script ledger are
             * followed. */
            /["']([A-Za-z_][A-Za-z0-9_]*)(?=\s*(?:[({]|["']))/g,
        ];
        for( const calls of callPatterns ) {
            for( let call; (call = calls.exec(source)); ) {
                const numeric = /^script_?(\d+)$/.exec(call[1]);
                const callee = numeric ? Number(numeric[1]) : nameToId.get(call[1]);
                if( Number.isInteger(callee) && !selected.has(callee) ) pending.push(callee);
            }
        }
    }
    return [...selected.values()].sort((left, right) => left.id - right.id);
}

/** Stable key for the exact cache, source closure and compiler inputs. */
export function nativeOverlayKey({
    baseCache, content, revision, tool, cs2Names, interfaceId, interfaceName, scripts,
}) {
    const hash = createHash('sha256');
    hash.update(JSON.stringify({
        schema: OVERLAY_SCHEMA,
        baseCache: resolve(baseCache),
        revision,
        interfaceId,
        interfaceName,
        cache: cacheFingerprint(baseCache),
        compiler: fileFingerprint(tool),
        names: directoryContentHash(cs2Names),
    }));
    addDirectoryFiles(hash, join(content, 'pack'), () => true);
    addDirectoryFiles(hash, join(content, 'configs'), (name) => name.endsWith('.compack'));
    addFile(hash, join(content, 'interfaces', `${interfaceName}.if`));
    const compack = join(content, 'interfaces', `${interfaceName}.compack`);
    if( existsSync(compack) ) addFile(hash, compack);
    for( const script of scripts ) {
        hash.update(`\nscript:${script.id}:${script.name}\n`);
        hash.update(script.source);
    }
    return hash.digest('hex').slice(0, 24);
}

function resolveInterface(content, nameOrId) {
    const pack = readPack(join(content, 'pack', '3_interfaces.pack'));
    let id = null;
    let name = null;
    if( Number.isInteger(nameOrId) || /^\d+$/.test(String(nameOrId || '')) ) {
        id = Number(nameOrId);
        name = pack.get(id) || null;
    } else {
        name = String(nameOrId || '');
        for( const [candidate, candidateName] of pack ) {
            if( candidateName === name ) { id = candidate; break; }
        }
    }
    if( id === null || !name )
        throw new Error(`interface '${nameOrId}' is absent from pack/3_interfaces.pack`);
    requireFile(join(content, 'interfaces', `${name}.if`), 'interface source');
    return { id, name };
}

function stageSource(content, target, selected, scripts) {
    mkdirSync(target, { recursive: true });
    /* Full name ledgers are lookup context, not work: archive-list below is the
     * only authority for what gets written. Copy-on-write snapshots keep the
     * stage small and stable even if the author saves while cachepack runs. */
    cloneDirectoryFiles(join(content, 'pack'), join(target, 'pack'), () => true);
    cloneDirectoryFiles(join(content, 'configs'), join(target, 'configs'),
                        (name) => name.endsWith('.compack'));
    mkdirSync(join(target, 'interfaces'));
    mkdirSync(join(target, 'scripts'));
    cloneFile(join(content, 'interfaces', `${selected.name}.if`),
              join(target, 'interfaces', `${selected.name}.if`));
    const compack = join(content, 'interfaces', `${selected.name}.compack`);
    if( existsSync(compack) )
        cloneFile(compack, join(target, 'interfaces', `${selected.name}.compack`));
    for( const script of scripts )
        cloneFile(script.path, join(target, 'scripts', basename(script.path)));
}

function cloneCache(base, target) {
    const files = readdirSync(base).filter((name) =>
        name === 'main_file_cache.dat2' || name === 'main_file_cache.dat' ||
        name === 'xteas.json' || /^main_file_cache\.idx\d+$/.test(name));
    if( !files.includes('main_file_cache.dat2') )
        throw new Error(`${base} is not a Dat2 cache`);
    for( const name of files ) cloneFile(join(base, name), join(target, name));
}

function cloneFile(source, target) {
    /* COPYFILE_FICLONE asks APFS/Btrfs/XFS for copy-on-write and portably falls
     * back to a byte copy when the filesystem cannot clone. */
    copyFileSync(source, target, fsConstants.COPYFILE_FICLONE);
}

function cloneDirectoryFiles(source, target, include) {
    mkdirSync(target, { recursive: true });
    for( const entry of readdirSync(source, { withFileTypes: true }) ) {
        if( !entry.isFile() || !include(entry.name) ) continue;
        cloneFile(join(source, entry.name), join(target, entry.name));
    }
}

function overlayProject(project, baseCache, cache, selected, scripts, key, reused, timings) {
    return {
        ...project,
        cache,
        nativeOverlay: {
            key,
            baseCache,
            interfaceId: selected.id,
            interfaceName: selected.name,
            scriptIds: scripts.map((script) => script.id),
            reused,
            timings,
        },
    };
}

function resolveCs2Names(project, options) {
    const candidates = [
        options.cs2Names,
        project.cs2Names,
        process.env.CACHEPACK_CS2_NAMES,
        join(homedir(), 'Documents', 'git_repos', 'cs2', 'src', 'main', 'resources',
             'org', 'runestar', 'cs2'),
    ].filter(Boolean).map((path) => resolve(path));
    return candidates.find((path) => existsSync(path) && statSync(path).isDirectory()) || null;
}

function readPack(path) {
    requireFile(path, 'pack index');
    const pack = new Map();
    for( const raw of readFileSync(path, 'utf8').split(/\r?\n/) ) {
        const line = raw.replace(/\/\/.*$/, '').trim();
        if( !line ) continue;
        const equals = line.indexOf('=');
        if( equals < 1 ) continue;
        const id = Number(line.slice(0, equals).trim());
        const name = line.slice(equals + 1).trim();
        if( Number.isInteger(id) && id >= 0 && name ) pack.set(id, name);
    }
    return pack;
}

function cacheFingerprint(cache) {
    return readdirSync(cache)
        .filter((name) => name === 'main_file_cache.dat2' ||
            /^main_file_cache\.idx\d+$/.test(name))
        .sort()
        .map((name) => [name, ...fileFingerprint(join(cache, name))]);
}

function fileFingerprint(path) {
    const info = statSync(path);
    return [info.size, info.mtimeMs];
}

function directoryContentHash(path) {
    if( !path ) return null;
    const hash = createHash('sha256');
    for( const name of readdirSync(path).sort() ) {
        const full = join(path, name);
        const info = statSync(full);
        if( !info.isFile() ) continue;
        hash.update(`\n${name}\n`);
        hash.update(readFileSync(full));
    }
    return [resolve(path), hash.digest('hex')];
}

function addFile(hash, path) {
    hash.update(`\nfile:${path}\n`);
    hash.update(readFileSync(path));
}

function addDirectoryFiles(hash, path, include) {
    for( const name of readdirSync(path).sort() ) {
        const full = join(path, name);
        if( include(name) && statSync(full).isFile() ) addFile(hash, full);
    }
}

function readMarker(path) {
    if( !existsSync(path) ) return null;
    try { return JSON.parse(readFileSync(path, 'utf8')); }
    catch { return null; }
}

function packFailure(run, tool, args, cs2Names) {
    const output = `${run.stdout || ''}${run.stderr || ''}`.trim();
    const names = cs2Names ? '' :
        '\nNo RuneStar CS2 name directory was found; set CACHEPACK_CS2_NAMES so symbolic ' +
        'constants and procedures compile.';
    if( run.error?.code === 'ETIMEDOUT' )
        return `cachepack timed out while composing the native overlay${names}`;
    return `cachepack could not compose the native overlay ` +
        `(${tool} ${args.join(' ')}, exit ${run.status ?? run.signal}):\n` +
        `${output || run.error?.message || 'no diagnostics'}${names}`;
}

function requireFile(path, label) {
    if( !path || !existsSync(path) || !statSync(path).isFile() )
        throw new Error(`${label} is missing: ${path}`);
}

function requireDirectory(path, label) {
    if( !path || !existsSync(path) || !statSync(path).isDirectory() )
        throw new Error(`${label} is missing: ${path}`);
}
