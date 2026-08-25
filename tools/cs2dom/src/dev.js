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
import { createBytecodePrograms } from './bytecode.js';
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
import { modelAssets, modelIndex, proxyModel, rawModel, startModelServer } from './model.js';
import {
    nativePreviewFingerprint, nativePreviewStatus, renderNativeInterface,
} from './native_preview.js';
import { prepareNativeOverlay } from './native_overlay.js';
import { nativeTreeInspector } from './native_tree.js';

const DEBOUNCE_MS = 40;
const MODULE_ROOT = dirname(fileURLToPath(import.meta.url));
const CS2VM_WEB_ROOT = join(MODULE_ROOT, '..', 'web');
const BROWSER_RUNTIME_MODULES = new Set([
    'components.js', 'cs2_commands.js', 'eval.js', 'expr.js', 'font_runtime.js', 'host.js',
    'host_activity.js', 'host_chat_social.js', 'host_db.js', 'host_loot.js',
    'host_overlay.js', 'host_runtime.js', 'host_subject.js', 'host_worldmap.js',
    'ops.js', 'preview.js',
    'wasm_runtime.js',
]);

export function serve(project, { port = 8099, open = true, log = console.log } = {}) {
    project = prepareDat2Project(project, { log });
    mkdirSync(project.sources, { recursive: true });
    let current = compile(project);
    const clients = new Set();
    const sources = interfaceSources(project);
    const sprites = new Map([...sources].map(([source, dir]) => [source, spriteIndex(dir)]));
    const models = new Map([...sources].map(([source, dir]) => [source, modelIndex(dir)]));
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

    const rebuild = () => {
        current = compile(project);
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
            return send(response, 200, 'text/html; charset=utf-8', page());

        if( url.pathname === '/state' ) {
            const state = url.searchParams.get('state');
            const selected = url.searchParams.get('interface');
            return send(response, 200, 'application/json',
                        JSON.stringify(view(project, current, selected, state, contentCatalog, sources,
                            bytecodeProgram, hostData)));
        }

        if( url.pathname === '/catalog' ) {
            return send(response, 200, 'application/json', JSON.stringify({
                interfaces: catalog(current, contentCatalog),
            }));
        }

        const hostDataMatch = /^\/host-data\/([^/]+)\.json$/.exec(url.pathname);
        if( hostDataMatch ) {
            let source;
            try { source = decodeURIComponent(hostDataMatch[1]); }
            catch { return send(response, 400, 'text/plain', 'invalid host-data source'); }
            const payload = hostDataJson.get(source);
            return payload === undefined
                ? send(response, 404, 'text/plain', 'no such host-data source')
                : send(response, 200, 'application/json', payload);
        }

        const runtimeModule = /^\/runtime\/([a-z0-9_]+\.js)$/.exec(url.pathname);
        if( runtimeModule && BROWSER_RUNTIME_MODULES.has(runtimeModule[1]) )
            return send(response, 200, 'text/javascript; charset=utf-8',
                readFileSync(join(MODULE_ROOT, runtimeModule[1])));

        const cs2vmAsset = /^\/cs2vm-wasm\/(cs2vm_wasm\.(?:js|wasm))$/.exec(url.pathname);
        if( cs2vmAsset ) {
            const file = join(CS2VM_WEB_ROOT, cs2vmAsset[1]);
            if( !existsSync(file) ) return send(response, 404, 'text/plain',
                'C CS2VM/WASM module is not built');
            return send(response, 200,
                cs2vmAsset[1].endsWith('.wasm') ? 'application/wasm' : 'text/javascript; charset=utf-8',
                readFileSync(file));
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
                    return send(response, 200, 'image/png', rendered.png);
                const inspector = nativeTreeInspector(rendered.tree, selectedIr);
                return send(response, 200, 'application/json', JSON.stringify({
                    viewport: inspector.viewport,
                    boxes: inspector.boxes,
                }));
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
            const png = contentDir ? spritePng(contentDir, sprites.get(source), id, {
                tiled: url.searchParams.get('tile') === '1',
            }) : null;
            if( !png ) return send(response, 404, 'text/plain', 'no such sprite');
            return send(response, 200, 'image/png', png);
        }

        const fontManifestMatch = /^\/font\/([^/]+)\/(\d+)\.json$/.exec(url.pathname);
        if( fontManifestMatch ) {
            const contentDir = sources.get(decodeURIComponent(fontManifestMatch[1]));
            const manifest = contentDir ? fontManifest(contentDir, Number(fontManifestMatch[2]), {
                source: decodeURIComponent(fontManifestMatch[1]),
            }) : null;
            return manifest ? send(response, 200, 'application/json', JSON.stringify(manifest))
                : send(response, 404, 'text/plain', 'no such font');
        }
        const fontGlyphMatch = /^\/font\/([^/]+)\/(\d+)\/(\d+)\.png$/.exec(url.pathname);
        if( fontGlyphMatch ) {
            const contentDir = sources.get(decodeURIComponent(fontGlyphMatch[1]));
            const png = contentDir ? fontGlyphPng(contentDir,
                Number(fontGlyphMatch[2]), Number(fontGlyphMatch[3])) : null;
            return png ? send(response, 200, 'image/png', png)
                : send(response, 404, 'text/plain', 'no such glyph');
        }

        if( url.pathname === '/toridraw/ev_wasm.js' && existsSync(rendererAssets.javascript) )
            return send(response, 200, 'text/javascript; charset=utf-8', readFileSync(rendererAssets.javascript));
        if( url.pathname === '/toridraw/ev_wasm.wasm' && existsSync(rendererAssets.wasm) )
            return send(response, 200, 'application/wasm', readFileSync(rendererAssets.wasm));

        if( url.pathname === '/model/textures.bin' )
            return proxyModel(renderer, response, { path: `/api/textures.bin${url.search}` });
        if( url.pathname.startsWith('/model/seq/') ) {
            const id = Number.parseInt(basename(url.pathname, '.anim'), 10);
            if( !Number.isInteger(id) ) return send(response, 404, 'text/plain', 'no such sequence');
            return proxyModel(renderer, response, { path: `/api/seq/${id}.anim` });
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
                return proxyModel(renderer, response, { path: `/api/${leaf}/${id}.model${url.search}` });
            }
            const id = Number.parseInt(basename(leaf, '.model'), 10);
            const bytes = rawModel(sources.get(source), models.get(source), id);
            if( !bytes ) return send(response, 404, 'text/plain', 'no such model');
            /* A UI archive model uses scene lighting and the native widget's
             * SD-texture gate. The entity viewer's general /api/modelfile path
             * intentionally uses an actor/HD preview profile, so keep the UI
             * route explicit. */
            return proxyModel(renderer, response, { method: 'POST', path: '/api/widgetmodel', body: bytes });
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

    let state = {};
    try { if( stateJson ) state = JSON.parse(stateJson); } catch { /* fall back to defaults */ }
    let renderedIr = result.ir;
    const bytecode = bytecodeProgram ? bytecodeProgram(result) : null;
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
    const name = index.get(id);
    if( !name ) return null;
    const dir = join(contentDir, 'sprites', name);
    if( !existsSync(dir) ) return null;
    /* A pack holds several sprites; the preview shows the first, which is the one
     * a component with a plain graphic id gets. */
    const bitmap = readdirSync(dir).filter((f) => extname(f) === '.bmp').sort()[0];
    if( !bitmap ) return null;
    try {
        const decoded = decodeBmp(readFileSync(join(dir, bitmap)));
        const frameIndex = Number.parseInt(basename(bitmap, '.bmp'), 10);
        const metaPath = join(dir, 'pack.meta');
        const meta = existsSync(metaPath) && Number.isInteger(frameIndex)
            ? parseSpriteMeta(readFileSync(metaPath, 'utf8')).get(frameIndex)
            : null;
        const pixels = options.tiled ? spriteTile(decoded, meta) : spriteCanvas(decoded, meta);
        return encodePng(pixels);
    } catch {
        return null;
    }
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
