/*
 * The dev server: save a file, see the interface.
 *
 * A watcher rebuilds on every save, an event stream tells the page, and the page
 * redraws — no reload, no rebuild of the client, no bake. What it shows is the
 * component tree laid out by the client's own IF3 rules (src/preview.js), with the
 * real sprites out of the content tree, and the generated .if and .cs2 beside it so
 * the cache records are never further away than a glance.
 *
 * Cache text is painted from its original bitmap font and models use the entity
 * viewer's toridraw/WASM path. Imported hooks run in the existing C CS2VM/WASM
 * and call synchronously into the browser-owned React tree through its JavaScript
 * HOST implementation. The production C client is retained as a reference
 * oracle, not the interactive surface.
 *
 * State is the other half. Everything the interface reads — each varp, varbit, stat
 * and varc — becomes a control in the page, and moving one re-evaluates the same
 * expression tree the emitter prints (src/eval.js). So "what does this look like at
 * 12% energy" is a slider rather than a login.
 */

import { createServer } from 'node:http';
import { readFileSync, existsSync, watch, readdirSync, writeFileSync, mkdirSync } from 'node:fs';
import { join, extname, basename, dirname } from 'node:path';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';

import { build } from './build.js';
import {
    BoundedAssetCache, assetRecord, fileVersion, filesVersion, sendAsset,
} from './asset_cache.js';
import { createBytecodePrograms } from './bytecode.js';
import { clientStateForRevision } from './client_state.js';
import { layout } from './preview.js';
import { stateInputs } from './eval.js';
import { decodeBmp, encodePng, spriteCanvas, spriteTile } from './png.js';
import { readCompack } from './build.js';
import { page } from './dev_page.js';
import {
    contentInterfaceCatalog, executeContentHooks, openContentInterface,
} from './content.js';
import { prepareDat2Project } from './dat2.js';
import { fontGlyphPng, fontManifest, parseSpriteMeta } from './font.js';
import { contentHostData } from './host_data.js';
import {
    modelAssets, modelIndex, proxyModel, rawModel, requestModel, startModelServer,
} from './model.js';
import {
    nativePreviewFingerprint, nativePreviewStatus, renderNativeInterface,
} from './native_preview.js';
import { prepareNativeOverlay } from './native_overlay.js';
import { nativeTreeInspector } from './native_tree.js';

const DEBOUNCE_MS = 40;
const MODULE_ROOT = dirname(fileURLToPath(import.meta.url));
const CS2VM_WEB_ROOT = join(MODULE_ROOT, '..', 'web');
const REACT_RUNTIME_WEB = join(CS2VM_WEB_ROOT, 'react_browser_runtime.js');
const TS_VM_RUNTIME_WEB = join(CS2VM_WEB_ROOT, 'cs2_vm_core.js');
const TS_ENGINE_RUNTIME_WEB = join(CS2VM_WEB_ROOT, 'cs2_engine_router.js');
export const BROWSER_RUNTIME_MODULES = new Set([
    'client_state.js', 'components.js', 'cs2_commands.js', 'cs2_host_requests.js', 'eval.js', 'expr.js',
    'font_runtime.js', 'host.js',
    'host_activity.js', 'host_chat_social.js', 'host_db.js', 'host_loot.js',
    'host_overlay.js', 'host_runtime.js', 'host_subject.js', 'host_worldmap.js',
    'model_render_controller.js', 'model_render_protocol.js', 'model_render_worker.js',
    'ops.js', 'pack.js', 'preview.js',
    'runtime_worker.js', 'runtime_worker_protocol.js', 'wasm_runtime.js',
    'ui_tree_store.js', 'worker_runtime_controller.js',
]);

export function serve(project, {
    port = 8099,
    open = true,
    log = console.log,
    assetCacheBytes = 64 * 1024 * 1024,
    assetCacheEntries = 4096,
    assetCacheItemBytes = 16 * 1024 * 1024,
} = {}) {
    project = prepareDat2Project(project, { log });
    mkdirSync(project.sources, { recursive: true });
    let current = compile(project);
    let buildRevision = 0;
    const assetCache = new BoundedAssetCache({
        maxBytes: assetCacheBytes,
        maxEntries: assetCacheEntries,
        maxItemBytes: assetCacheItemBytes,
    });
    const clients = new Set();
    const sources = interfaceSources(project);
    const sprites = new Map([...sources].map(([source, dir]) => [source, spriteIndex(dir)]));
    const models = new Map([...sources].map(([source, dir]) => [source, modelIndex(dir)]));
    const spritePackVersions = new Map([...sources].map(([source, dir]) =>
        [source, fileVersion(join(dir, 'pack', '8_sprites.pack'))]));
    const modelPackVersions = new Map([...sources].map(([source, dir]) =>
        [source, fileVersion(join(dir, 'pack', '7_models.pack'))]));
    const spriteFiles = new Map();
    const hostData = new Map([...sources].map(([source, dir]) => [source, contentHostData(dir)]));
    /* HOST lookups are several megabytes for a full cache. Serialize each source
     * once and let the page cache it across interface changes and hot reloads;
     * embedding it in /state made every preview refresh resend the whole cache. */
    const hostDataJson = new Map([...hostData].map(([source, value]) =>
        [source, JSON.stringify(value)]));
    const renderer = startModelServer(project, port + 1, log);
    const rendererAssets = modelAssets();
    const nativeFrames = new Map();
    const bytecodeProgram = createBytecodePrograms(project, { log });
    const contentCatalog = [...sources].flatMap(([source, dir]) =>
        contentInterfaceCatalog(dir, { source }));
    const pageAsset = assetRecord(page(), {
        type: 'text/html; charset=utf-8', version: 'server',
    });

    const rebuild = () => {
        current = compile(project);
        buildRevision++;
        assetCache.deletePrefix('catalog:');
        assetCache.deletePrefix('state:');
        const message = `data: ${JSON.stringify({ at: Date.now() })}\n\n`;
        for( const client of clients ) client.write(message);
        if( current.error ) log(`  ✗ ${current.error.message.split('\n')[0]}`);
        else log(`  ✓ ${current.results.map((r) => r.name).join(', ')}`);
    };

    let pending = null;
    watch(project.sources, { recursive: true }, (event, file) => {
        if( file && !/\.(tsx?|json)$/.test(file) ) return;
        clearTimeout(pending);
        pending = setTimeout(rebuild, DEBOUNCE_MS);
    });

    const server = createServer((request, response) => {
        const url = new URL(request.url, 'http://localhost');

        if( url.pathname === '/' )
            return sendAsset(request, response, pageAsset);

        if( url.pathname === '/state' ) {
            const state = url.searchParams.get('state');
            const selected = url.searchParams.get('interface');
            const key = `state:${url.search}`;
            const resident = assetCache.get(key);
            if( resident ) {
                const residentVersion = `${buildRevision}:${filesVersion(
                    resident.dependencies || [])}`;
                if( resident.version === residentVersion )
                    return sendAsset(request, response, resident, {
                        cacheControl: 'private, max-age=0, must-revalidate',
                    });
                assetCache.delete(key);
            }
            const value = view(project, current, selected, state, contentCatalog, sources,
                bytecodeProgram, hostData);
            const nextDependencies = viewDependencies(value);
            const version = `${buildRevision}:${filesVersion(nextDependencies)}`;
            const entry = assetRecord(JSON.stringify(value), {
                type: 'application/json', version, dependencies: nextDependencies,
            });
            assetCache.set(key, entry);
            return sendAsset(request, response, entry, {
                cacheControl: 'private, max-age=0, must-revalidate',
            });
        }

        if( url.pathname === '/catalog' ) {
            return serveCached(request, response, assetCache, 'catalog:all', String(buildRevision),
                () => JSON.stringify({ interfaces: catalog(current, contentCatalog) }),
                { type: 'application/json' });
        }

        const hostDataMatch = /^\/host-data\/([^/]+)\.json$/.exec(url.pathname);
        if( hostDataMatch ) {
            let source;
            try { source = decodeURIComponent(hostDataMatch[1]); }
            catch { return send(response, 400, 'text/plain', 'invalid host-data source'); }
            const payload = hostDataJson.get(source);
            return payload === undefined
                ? send(response, 404, 'text/plain', 'no such host-data source')
                : serveCached(request, response, assetCache, `host-data:${source}`, 'server',
                    () => payload, { type: 'application/json' });
        }

        const runtimeModule = /^\/runtime\/([a-z0-9_]+\.js)$/.exec(url.pathname);
        if( runtimeModule && BROWSER_RUNTIME_MODULES.has(runtimeModule[1]) ) {
            const file = join(MODULE_ROOT, runtimeModule[1]);
            return serveFile(request, response, assetCache,
                `file:runtime:${runtimeModule[1]}`, file, 'text/javascript; charset=utf-8');
        }

        if( url.pathname === '/react-runtime.js' ) {
            if( !existsSync(REACT_RUNTIME_WEB) ) return send(response, 503, 'text/plain',
                'React preview runtime is not built; run make react-runtime');
            return serveFile(request, response, assetCache, 'file:react-runtime',
                REACT_RUNTIME_WEB, 'text/javascript; charset=utf-8');
        }

        if( url.pathname === '/ts-vm/cs2_vm_core.js' ) {
            if( !existsSync(TS_VM_RUNTIME_WEB) ) return send(response, 503, 'text/plain',
                'TypeScript CS2VM runtime is not built; run make react-runtime');
            return serveFile(request, response, assetCache, 'file:ts-vm-runtime',
                TS_VM_RUNTIME_WEB, 'text/javascript; charset=utf-8');
        }

        if( url.pathname === '/ts-vm/cs2_engine_router.js' ) {
            if( !existsSync(TS_ENGINE_RUNTIME_WEB) ) return send(response, 503, 'text/plain',
                'TypeScript CS2 engine router is not built; run make react-runtime');
            return serveFile(request, response, assetCache, 'file:ts-engine-runtime',
                TS_ENGINE_RUNTIME_WEB, 'text/javascript; charset=utf-8');
        }

        const cs2vmAsset = /^\/cs2vm-wasm\/(cs2vm_wasm\.(?:js|wasm))$/.exec(url.pathname);
        if( cs2vmAsset ) {
            const file = join(CS2VM_WEB_ROOT, cs2vmAsset[1]);
            if( !existsSync(file) ) return send(response, 404, 'text/plain',
                'C CS2VM/WASM module is not built');
            return serveFile(request, response, assetCache, `file:cs2vm:${cs2vmAsset[1]}`, file,
                cs2vmAsset[1].endsWith('.wasm')
                    ? 'application/wasm' : 'text/javascript; charset=utf-8');
        }

        if( url.pathname === '/native/status' )
            return send(response, 200, 'application/json', JSON.stringify(nativePreviewStatus(project)));

        const nativeMatch = /^\/native\/interface\/(\d+)\.(png|tree\.json)$/.exec(url.pathname);
        if( nativeMatch ) {
            const interfaceId = Number.parseInt(nativeMatch[1], 10);
            const responseKind = nativeMatch[2];
            const width = queryDimension(url, 'width', 512);
            const height = queryDimension(url, 'height', 334);
            if( !width || !height )
                return send(response, 400, 'text/plain', 'native preview dimensions must be 1..4096');
            /* An unpacked .if is compiled into a content-addressed COW overlay;
             * Dat2 selections go straight to the original cache. Both then use
             * the identical production App/CS2/Soft3D render path. */
            const source = url.searchParams.get('source') || 'dat2';
            const name = url.searchParams.get('name') || String(interfaceId);
            let nativeState;
            let nativeProject;
            let selectedIr = null;
            let fingerprint;
            try {
                nativeState = url.searchParams.has('state')
                    ? JSON.parse(url.searchParams.get('state'))
                    : {};
                nativeProject = source === 'content'
                    ? prepareNativeOverlay(project, name, { log })
                    : project;
                if( responseKind === 'tree.json' && sources.has(source) )
                    selectedIr = openContentInterface(
                        sources.get(source), name, { source }).ir;
                fingerprint = nativePreviewFingerprint(
                    nativeProject, interfaceId, width, height, nativeState);
            } catch( error ) {
                return send(response, 400, 'text/plain', error.message);
            }
            if( !fingerprint )
                return send(response, 503, 'text/plain', nativePreviewStatus(nativeProject).reason);
            let frame = nativeFrames.get(fingerprint);
            if( !frame ) {
                while( nativeFrames.size >= 16 )
                    nativeFrames.delete(nativeFrames.keys().next().value);
                frame = renderNativeInterface(
                    nativeProject, interfaceId, { width, height, state: nativeState });
                nativeFrames.set(fingerprint, frame);
                frame.catch(() => nativeFrames.delete(fingerprint));
            }
            frame.then((rendered) => {
                if( responseKind === 'png' )
                    return serveCached(request, response, assetCache,
                        `native:${fingerprint}:png`, fingerprint,
                        () => rendered.png, {
                            type: 'image/png',
                            cacheControl: 'private, max-age=0, must-revalidate',
                        });
                const inspector = nativeTreeInspector(rendered.tree, selectedIr);
                return serveCached(request, response, assetCache,
                    `native:${fingerprint}:tree`, fingerprint,
                    () => JSON.stringify({
                        viewport: inspector.viewport,
                        boxes: inspector.boxes,
                    }), {
                        type: 'application/json',
                        cacheControl: 'private, max-age=0, must-revalidate',
                    });
            }).catch((error) => send(response, 500, 'text/plain', error.message));
            return undefined;
        }

        if( url.pathname === '/events' ) {
            response.writeHead(200, {
                'content-type': 'text/event-stream',
                'cache-control': 'no-cache',
                connection: 'keep-alive',
            });
            response.write('retry: 500\n\n');
            clients.add(response);
            request.on('close', () => clients.delete(response));
            return undefined;
        }

        if( url.pathname.startsWith('/sprite/') ) {
            const parts = url.pathname.split('/').filter(Boolean);
            const source = parts.length === 3 ? parts[1] : project.contentSource;
            const id = Number.parseInt(basename(parts.at(-1), '.png'), 10);
            const contentDir = sources.get(source);
            if( !contentDir ) return send(response, 404, 'text/plain', 'no such sprite');
            const index = refreshIndex(source, contentDir, 'sprite', sprites,
                spritePackVersions, assetCache, spriteIndex);
            const resolved = resolveSpriteFiles(contentDir, index, id, spriteFiles);
            if( !resolved ) return send(response, 404, 'text/plain', 'no such sprite');
            const tiled = url.searchParams.get('tile') === '1';
            const version = [
                spritePackVersions.get(source),
                resolved.directoryVersion,
                fileVersion(resolved.bitmap),
                fileVersion(resolved.metaPath),
            ].join(':');
            return serveCached(request, response, assetCache,
                `sprite:${source}:${id}:${tiled ? 1 : 0}`, version,
                () => spritePng(contentDir, index, id, { tiled, resolved }),
                { type: 'image/png', nullable: true, notFound: 'no such sprite' });
        }

        const fontManifestMatch = /^\/font\/([^/]+)\/(\d+)\.json$/.exec(url.pathname);
        if( fontManifestMatch ) {
            const source = decodeURIComponent(fontManifestMatch[1]);
            const contentDir = sources.get(source);
            if( !contentDir ) return send(response, 404, 'text/plain', 'no such font');
            const id = Number(fontManifestMatch[2]);
            const index = refreshIndex(source, contentDir, 'sprite', sprites,
                spritePackVersions, assetCache, spriteIndex);
            const spriteName = index.get(id);
            if( !spriteName ) return send(response, 404, 'text/plain', 'no such font');
            const spriteDir = join(contentDir, 'sprites', spriteName);
            const version = [
                spritePackVersions.get(source),
                fileVersion(join(contentDir, 'fonts', `font_${id}.fm`)),
                fileVersion(join(spriteDir, 'pack.meta')),
                fileVersion(spriteDir),
            ].join(':');
            return serveCached(request, response, assetCache,
                `font:${source}:${id}:manifest`, version,
                () => {
                    const manifest = fontManifest(contentDir, id, {
                        source, spriteNames: index,
                    });
                    return manifest ? JSON.stringify(manifest) : null;
                }, { type: 'application/json', nullable: true, notFound: 'no such font' });
        }
        const fontGlyphMatch = /^\/font\/([^/]+)\/(\d+)\/(\d+)\.png$/.exec(url.pathname);
        if( fontGlyphMatch ) {
            const source = decodeURIComponent(fontGlyphMatch[1]);
            const contentDir = sources.get(source);
            if( !contentDir ) return send(response, 404, 'text/plain', 'no such glyph');
            const id = Number(fontGlyphMatch[2]);
            const code = Number(fontGlyphMatch[3]);
            const index = refreshIndex(source, contentDir, 'sprite', sprites,
                spritePackVersions, assetCache, spriteIndex);
            const spriteName = index.get(id);
            if( !spriteName ) return send(response, 404, 'text/plain', 'no such glyph');
            const bitmap = join(contentDir, 'sprites', spriteName, `${code}.bmp`);
            const version = `${spritePackVersions.get(source)}:${fileVersion(bitmap)}`;
            return serveCached(request, response, assetCache,
                `font:${source}:${id}:glyph:${code}`, version,
                () => fontGlyphPng(contentDir, id, code, { spriteNames: index }),
                { type: 'image/png', nullable: true, notFound: 'no such glyph' });
        }

        if( url.pathname === '/toridraw/ev_wasm.js' && existsSync(rendererAssets.javascript) )
            return serveFile(request, response, assetCache, 'file:toridraw:classic',
                rendererAssets.javascript, 'text/javascript; charset=utf-8');
        if( url.pathname === '/toridraw/ev_wasm_module.js' && existsSync(rendererAssets.javascript) )
            return serveFile(request, response, assetCache, 'file:toridraw:module',
                rendererAssets.javascript, 'text/javascript; charset=utf-8', (body) =>
                Buffer.concat([
                    body,
                    Buffer.from('\nexport { EVModule };\n'),
                ]));
        if( url.pathname === '/toridraw/ev_wasm.wasm' && existsSync(rendererAssets.wasm) )
            return serveFile(request, response, assetCache, 'file:toridraw:wasm',
                rendererAssets.wasm, 'application/wasm');

        if( url.pathname === '/model/textures.bin' )
            return serveModel(request, response, assetCache,
                `model:renderer:textures:${url.search}`, 'renderer', renderer,
                { path: `/api/textures.bin${url.search}` });
        if( url.pathname.startsWith('/model/seq/') ) {
            const id = Number.parseInt(basename(url.pathname, '.anim'), 10);
            if( !Number.isInteger(id) ) return send(response, 404, 'text/plain', 'no such sequence');
            return serveModel(request, response, assetCache,
                `model:renderer:seq:${id}`, 'renderer', renderer,
                { path: `/api/seq/${id}.anim` });
        }

        if( url.pathname.startsWith('/model/') ) {
            const parts = url.pathname.split('/').filter(Boolean);
            const source = parts[1];
            const leaf = parts[2];
            if( !sources.has(source) ) return send(response, 404, 'text/plain', 'no such model source');
            if( leaf === 'player.model' )
                return proxyModel(renderer, response, { path: '/api/player.model' });
            if( ['obj', 'npc', 'loc', 'spot'].includes(leaf) && parts.length === 4 ) {
                const id = Number.parseInt(basename(parts[3], '.model'), 10);
                if( !Number.isInteger(id) )
                    return send(response, 404, 'text/plain', 'no such configured model');
                return serveModel(request, response, assetCache,
                    `model:renderer:${leaf}:${id}:${url.search}`, 'renderer', renderer,
                    { path: `/api/${leaf}/${id}.model${url.search}` });
            }
            const id = Number.parseInt(basename(leaf, '.model'), 10);
            const contentDir = sources.get(source);
            const index = refreshIndex(source, contentDir, 'model', models,
                modelPackVersions, assetCache, modelIndex);
            const modelName = index.get(id);
            const modelPath = modelName
                ? join(contentDir, 'models', `${modelName}.model`) : null;
            const modelVersion = modelPath ? fileVersion(modelPath) : '-';
            if( modelVersion === '-' )
                return send(response, 404, 'text/plain', 'no such model');
            /* A UI archive model uses scene lighting and the native widget's
             * SD-texture gate. The entity viewer's general /api/modelfile path
             * intentionally uses an actor/HD preview profile, so keep the UI
             * route explicit. */
            return serveModel(request, response, assetCache,
                `model:widget:${source}:${id}`,
                `${modelPackVersions.get(source)}:${modelVersion}`, renderer,
                {
                    method: 'POST',
                    path: '/api/widgetmodel',
                    body: () => rawModel(contentDir, index, id),
                });
        }

        if( url.pathname === '/new' && request.method === 'POST' ) {
            return readBody(request, (body) => {
                try {
                    const created = newComponent(project, JSON.parse(body).name);
                    send(response, 200, 'application/json', JSON.stringify({ created }));
                } catch( error ) {
                    send(response, 400, 'application/json', JSON.stringify({ error: error.message }));
                }
            });
        }

        return send(response, 404, 'text/plain', 'not found');
    });

    server.listen(port, () => {
        const address = `http://localhost:${port}`;
        log(`cs2dom dev — ${address}`);
        log(`  watching ${project.sources}`);
        if( sources.has('content') ) log(`  content  ${sources.get('content')}`);
        if( sources.has('dat2') ) {
            log(`  Dat2     ${project.cache}`);
            log(`  decoded  ${sources.get('dat2')} (derived, read-only)`);
        }
        for( const [source] of sources )
            log(`  indexed  ${contentCatalog.filter((entry) => entry.source === source).length} ${source === 'dat2' ? 'Dat2' : 'content'} interfaces`);
        if( current.error ) log(`  ✗ ${current.error.message.split('\n')[0]}`);
        else log(`  ✓ ${current.results.map((r) => r.name).join(', ') || 'no components yet'}`);
        if( open ) openBrowser(address);
    });

    server.on('close', () => renderer?.child.kill());

    /* Exposed for diagnostics/tests without making the cache mutable from the
     * browser. This is intentionally a snapshot function, not the cache itself. */
    server.assetCacheStats = () => assetCache.snapshot();

    return server;
}

/** Build without writing: a preview never touches the content tree. */
function compile(project) {
    try {
        const result = build(project, { dryRun: true });
        return { ...result, error: null };
    } catch( error ) {
        return { results: [], warnings: [], error };
    }
}

/** What the page needs for one render, at the state it asked for. */
function view(project, current, selected, stateJson, contentCatalog, sources, bytecodeProgram, hostData) {
    const available = catalog(current, contentCatalog);
    const key = selected || available[0]?.key;
    let result;

    if( !available.some((entry) => entry.key === key) ) {
        if( key?.startsWith('authored:') && current.error )
            return { error: current.error.message, interfaces: [] };
        return { error: `interface '${key || ''}' is not available`, interfaces: [] };
    }

    try {
        if( key?.startsWith('content:') || key?.startsWith('dat2:') ) {
            const source = key.startsWith('dat2:') ? 'dat2' : 'content';
            result = openContentInterface(sources.get(source), key.slice(source.length + 1), { source });
        }
        else if( key?.startsWith('authored:') ) {
            if( current.error ) throw current.error;
            const name = key.slice('authored:'.length);
            result = current.results.find((candidate) => candidate.name === name);
        }
    } catch( error ) {
        return { error: error.message, interfaces: [] };
    }

    if( !result )
        return { error: `interface '${key || ''}' is not available`, interfaces: [] };

    let explicitState = {};
    try {
        if( stateJson ) explicitState = JSON.parse(stateJson);
    } catch { /* fall back to revision defaults */ }
    let renderedIr = result.ir;
    const bytecode = bytecodeProgram ? bytecodeProgram(result) : null;
    const state = clientStateForRevision(bytecode?.revision || project.revision, explicitState);
    if( result.source === 'content' || result.source === 'dat2' ) {
        /* Execute against a second import for controls and the no-JS first
         * frame. The browser receives the pristine IR and mounts those hooks
         * into its live HostRuntime exactly once. */
        const analysis = openContentInterface(result.contentDir, result.name, { source: result.source });
        executeContentHooks(analysis, state);
        renderedIr = analysis.ir;
        result.scripts = analysis.scripts;
        result.warnings = analysis.warnings;
    }
    const viewport = previewViewport(result);

    return {
        error: null,
        warnings: [...(result.warnings || []), ...(result.source === 'authored' ? current.warnings : [])],
        interfaces: [result].map((result) => {
            /* Host reads the preview cannot answer are collected while laying out,
             * so the page can say which values it is showing as zero. */
            const unmodelled = new Set();
            const boxes = layout(renderedIr, state, viewport, unmodelled);
            return {
            name: result.name,
            interfaceId: result.interfaceId,
            file: result.file,
            source: result.source || 'authored',
            spriteSource: result.source === 'authored' ? project.contentSource : result.source,
            modelSource: result.source === 'authored' ? project.contentSource : result.source,
            viewport,
            boxes,
            runtime: {
                ir: browserRuntimeIr(result.ir),
                bytecode,
                hostDataUrl: hostData?.has(result.source === 'authored'
                    ? project.contentSource : result.source)
                    ? `/host-data/${encodeURIComponent(result.source === 'authored'
                        ? project.contentSource : result.source)}.json`
                    : null,
            },
            unmodelled: [...unmodelled],
            inputs: stateInputs(renderedIr),
            interfaceText: result.interfaceText,
            compackText: result.compackText,
            scripts: result.scripts,
            reactSource: result.reactSource || null,
            }; }),
    };
}

function viewDependencies(value) {
    const paths = new Set();
    for( const iface of value?.interfaces || [] ) {
        if( iface.file ) paths.add(iface.file);
        for( const script of iface.scripts || [] )
            if( script?.file ) paths.add(script.file);
    }
    return [...paths];
}

function previewViewport(result) {
    if( result.source === 'content' || result.source === 'dat2' )
        return { width: 512, height: 334 };
    const root = result.ir.components.find((component) => component.layer === null);
    const width = root && (root.static.widthMode | 0) === 0 && Number(root.static.width) > 0
        ? Number(root.static.width) : 512;
    const height = root && (root.static.heightMode | 0) === 0 && Number(root.static.height) > 0
        ? Number(root.static.height) : 334;
    return { width: Math.max(1, width | 0), height: Math.max(1, height | 0) };
}

function browserRuntimeIr(ir) {
    return {
        name: ir.name,
        interfaceId: ir.interfaceId,
        states: jsonValue(ir.states || []),
        components: ir.components.map((component) => ({
            fileId: component.fileId,
            name: component.name,
            kind: component.kind,
            type: component.type,
            if3: component.if3 !== false,
            layer: component.layer,
            subId: component.subId,
            props: jsonValue(component.props || component.static || {}),
            static: jsonValue(component.static || {}),
            authoredProps: [...(component.authoredProps || [])],
            dynamic: (component.dynamic || []).map((binding) => ({
                prop: binding.prop,
                expr: jsonValue(binding.expr),
            })),
            ops: jsonValue(component.ops || []),
            events: {},
            hooks: Object.fromEntries(Object.entries(component.hooks || {}).map(([name, binding]) =>
                [name, browserHook(binding)])),
            triggers: jsonValue(component.triggers || {}),
            dependencies: jsonValue(component.dependencies || []),
            scriptBindings: jsonValue(component.scriptBindings || []),
            rawFields: jsonValue(component.rawFields || {}),
            runtimeDynamic: Boolean(component.runtimeDynamic),
        })),
    };
}

function browserHook(binding) {
    if( !binding ) return null;
    return {
        script: {
            id: Number(binding.script?.id ?? binding.scriptId ?? binding.script_id ?? -1),
            name: binding.script?.name || null,
        },
        args: jsonValue(binding.args || []),
        signature: binding.signature || null,
        triggerIds: jsonValue(binding.triggerIds || binding.trigger_ids || []),
    };
}

function jsonValue(value, seen = new WeakSet()) {
    if( value === null || typeof value !== 'object' ) return typeof value === 'function' ? null : value;
    if( seen.has(value) ) return null;
    seen.add(value);
    if( Array.isArray(value) ) {
        const result = value.map((item) => jsonValue(item, seen));
        seen.delete(value);
        return result;
    }
    if( value instanceof Set ) {
        const result = [...value].map((item) => jsonValue(item, seen));
        seen.delete(value);
        return result;
    }
    const result = {};
    for( const [key, item] of Object.entries(value) ) {
        if( typeof item === 'function' ) continue;
        result[key] = jsonValue(item, seen);
    }
    seen.delete(value);
    return result;
}

function interfaceSources(project) {
    const sources = new Map();
    const unpacked = project.unpackedContent ||
        (!project.dat2Content && project.contentSource === 'content' ? project.content : null);
    if( unpacked ) sources.set('content', unpacked);
    if( project.dat2Content ) sources.set('dat2', project.dat2Content);
    return sources;
}

function catalog(current, contentCatalog) {
    const authored = current.error ? [] : current.results.map((result) => ({
        key: `authored:${result.name}`,
        name: result.name,
        label: `${result.name} · authored`,
        interfaceId: result.interfaceId,
        source: 'authored',
    }));
    return [...authored, ...contentCatalog];
}

/* ---- sprites ------------------------------------------------------------- */

function spriteIndex(contentDir) {
    const path = join(contentDir, 'pack', '8_sprites.pack');
    if( !existsSync(path) ) return new Map();
    const byName = readCompack(path);
    const byId = new Map();
    for( const [name, id] of byName ) byId.set(id, name);
    return byId;
}

export function spritePng(contentDir, index, id, options = {}) {
    const resolved = options.resolved || resolveSpriteFiles(contentDir, index, id);
    if( !resolved ) return null;
    try {
        const decoded = decodeBmp(readFileSync(resolved.bitmap));
        const meta = existsSync(resolved.metaPath) && Number.isInteger(resolved.frameIndex)
            ? parseSpriteMeta(readFileSync(resolved.metaPath, 'utf8')).get(resolved.frameIndex)
            : null;
        const pixels = options.tiled ? spriteTile(decoded, meta) : spriteCanvas(decoded, meta);
        return encodePng(pixels);
    } catch {
        return null;
    }
}

function resolveSpriteFiles(contentDir, index, id, cache = null) {
    const name = index.get(id);
    if( !name ) return null;
    const dir = join(contentDir, 'sprites', name);
    const directoryVersion = fileVersion(dir);
    if( directoryVersion === '-' ) return null;
    const key = `${contentDir}\0${id}`;
    const prior = cache?.get(key);
    if( prior?.name === name && prior.directoryVersion === directoryVersion ) {
        cache.delete(key);
        cache.set(key, prior);
        return prior;
    }
    /* A pack holds several sprites; the preview shows the first, which is the one
     * a component with a plain graphic id gets. Resolve that filename once per
     * directory identity rather than scanning the directory on every request. */
    const filename = readdirSync(dir).filter((file) => extname(file) === '.bmp').sort()[0];
    if( !filename ) return null;
    const bitmap = join(dir, filename);
    const metaPath = join(dir, 'pack.meta');
    const resolved = {
        name,
        directoryVersion,
        bitmap,
        metaPath,
        frameIndex: Number.parseInt(basename(filename, '.bmp'), 10),
        dependencies: [
            join(contentDir, 'pack', '8_sprites.pack'), dir, bitmap, metaPath,
        ],
    };
    if( cache ) {
        cache.set(key, resolved);
        while( cache.size > 4096 ) cache.delete(cache.keys().next().value);
    }
    return resolved;
}

/* ---- scaffolding --------------------------------------------------------- */

const TEMPLATE = (name) => `import { Layer, Text, useVarp } from 'cs2dom';
import { fonts } from './cache.gen';

export default function ${name.replace(/(^|_)(\w)/g, (_, __, c) => c.toUpperCase())}() {
    return (
        <Layer id="root" width={200} height={100}>
            <Text id="title" x={0} y={0} width={200} height={16}
                  font={fonts.p12_full} color={0xffffff} halign="centre">
                ${name}
            </Text>
        </Layer>
    );
}
`;

/** "New component" in the page: write a starter file and let the watcher do the rest. */
function newComponent(project, rawName) {
    const name = String(rawName || '').trim().toLowerCase().replace(/[^a-z0-9_]/g, '_');
    if( !name ) throw new Error('a component needs a name');
    const path = join(project.sources, `${name}.tsx`);
    if( existsSync(path) ) throw new Error(`${name}.tsx already exists`);
    mkdirSync(project.sources, { recursive: true });
    writeFileSync(path, TEMPLATE(name));
    return path;
}

/* ---- plumbing ------------------------------------------------------------ */

function serveCached(request, response, cache, key, version, load, options = {}) {
    let entry = cache.get(key, version);
    if( !entry ) {
        const body = load();
        if( body === null || body === undefined ) {
            if( options.nullable )
                return send(response, 404, 'text/plain', options.notFound || 'not found');
            throw new Error(`asset loader '${key}' returned no body`);
        }
        entry = assetRecord(body, {
            type: options.type,
            headers: options.headers,
            version,
        });
        cache.set(key, entry);
    }
    return sendAsset(request, response, entry, { cacheControl: options.cacheControl });
}

function serveFile(request, response, cache, key, path, type, transform = null) {
    const version = fileVersion(path);
    if( version === '-' ) return send(response, 404, 'text/plain', 'asset is not built');
    return serveCached(request, response, cache, key, version,
        () => transform ? transform(readFileSync(path)) : readFileSync(path), { type });
}

function serveModel(request, response, cache, key, version, renderer, options) {
    cache.getOrLoad(key, version, async () => {
        const body = typeof options.body === 'function' ? options.body() : options.body;
        if( options.body && !body ) return assetRecord('no such model', {
            status: 404,
            type: 'text/plain',
            version,
            cacheable: false,
        });
        const result = await requestModel(renderer, { ...options, body });
        return assetRecord(result.body, {
            status: result.status,
            type: result.headers['content-type'] || 'application/octet-stream',
            headers: result.headers,
            version,
            cacheable: result.status < 500,
        });
    }).then((entry) => sendAsset(request, response, entry))
      .catch((error) => {
          if( !response.headersSent )
              return send(response, 502, 'text/plain',
                  `model renderer did not start: ${error.message}`);
          response.destroy(error);
      });
    return undefined;
}

function refreshIndex(source, contentDir, kind, indexes, versions, cache, load) {
    const pack = join(contentDir, 'pack', kind === 'sprite'
        ? '8_sprites.pack' : '7_models.pack');
    const version = fileVersion(pack);
    if( versions.get(source) === version ) return indexes.get(source);
    const index = load(contentDir);
    indexes.set(source, index);
    versions.set(source, version);
    cache.deletePrefix(`${kind}:${source}:`);
    if( kind === 'sprite' ) cache.deletePrefix(`font:${source}:`);
    else cache.deletePrefix(`model:widget:${source}:`);
    return index;
}

function send(response, status, type, body) {
    response.writeHead(status, { 'content-type': type, 'cache-control': 'no-store' });
    response.end(body);
}

function queryDimension(url, name, fallback) {
    if( !url.searchParams.has(name) ) return fallback;
    const value = Number.parseInt(url.searchParams.get(name), 10);
    return Number.isInteger(value) && value > 0 && value <= 4096 ? value : 0;
}

function readBody(request, done) {
    let body = '';
    request.on('data', (chunk) => { body += chunk; });
    request.on('end', () => done(body));
}

function openBrowser(address) {
    const command = process.platform === 'darwin' ? 'open'
        : process.platform === 'win32' ? 'start' : 'xdg-open';
    try {
        spawn(command, [address], { stdio: 'ignore', detached: true }).unref();
    } catch { /* a dev server that cannot open a browser is still a dev server */ }
}
