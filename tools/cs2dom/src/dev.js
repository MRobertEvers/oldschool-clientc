/*
 * The dev server: save a file, see the interface.
 *
 * A watcher rebuilds on every save, an event stream tells the page, and the page
 * redraws — no reload, no rebuild of the client, no bake. What it shows is the
 * component tree laid out by the client's own IF3 rules (src/preview.js), with the
 * real sprites out of the content tree, and the generated .if and .cs2 beside it so
 * the cache records are never further away than a glance.
 *
 * It is a preview, and the README says where it stops: browser text is not the
 * cache's bitmap font. Models use the entity viewer's toridraw/WASM path and
 * imported source hooks run in a bounded UI-focused CS2 interpreter. The fidelity
 * path is unchanged — `cs2dom build` then a bake, into the real client. This buys the twenty
 * seconds between having an idea and seeing whether the geometry works.
 *
 * State is the other half. Everything the interface reads — each varp, varbit, stat
 * and varc — becomes a control in the page, and moving one re-evaluates the same
 * expression tree the emitter prints (src/eval.js). So "what does this look like at
 * 12% energy" is a slider rather than a login.
 */

import { createServer } from 'node:http';
import { readFileSync, existsSync, watch, readdirSync, writeFileSync, mkdirSync } from 'node:fs';
import { join, extname, basename } from 'node:path';
import { spawn } from 'node:child_process';

import { build } from './build.js';
import { layout } from './preview.js';
import { stateInputs } from './eval.js';
import { bmpToPng } from './png.js';
import { readCompack } from './build.js';
import { page } from './dev_page.js';
import { contentInterfaceCatalog, executeContentHooks, openContentInterface } from './content.js';
import { prepareDat2Project } from './dat2.js';
import { modelAssets, modelIndex, proxyModel, rawModel, startModelServer } from './model.js';
import {
    nativePreviewFingerprint, nativePreviewStatus, renderNativeInterface,
} from './native_preview.js';
import { prepareNativeOverlay } from './native_overlay.js';
import { nativeTreeInspector } from './native_tree.js';

const DEBOUNCE_MS = 40;

export function serve(project, { port = 8099, open = true, log = console.log } = {}) {
    project = prepareDat2Project(project, { log });
    mkdirSync(project.sources, { recursive: true });
    let current = compile(project);
    const clients = new Set();
    const sources = interfaceSources(project);
    const sprites = new Map([...sources].map(([source, dir]) => [source, spriteIndex(dir)]));
    const models = new Map([...sources].map(([source, dir]) => [source, modelIndex(dir)]));
    const renderer = startModelServer(project, port + 1, log);
    const rendererAssets = modelAssets();
    const nativeFrames = new Map();
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
                        JSON.stringify(view(project, current, selected, state, contentCatalog, sources)));
        }

        if( url.pathname === '/catalog' ) {
            return send(response, 200, 'application/json', JSON.stringify({
                interfaces: catalog(current, contentCatalog),
            }));
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
            frame.then(
                (rendered) => {
                    if( responseKind === 'png' )
                        return send(response, 200, 'image/png', rendered.png);
                    const inspector = nativeTreeInspector(rendered.tree, selectedIr);
                    return send(response, 200, 'application/json', JSON.stringify({
                        viewport: inspector.viewport,
                        boxes: inspector.boxes,
                    }));
                },
                (error) => send(response, 500, 'text/plain', error.message));
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
            const png = contentDir ? spritePng(contentDir, sprites.get(source), id) : null;
            if( !png ) return send(response, 404, 'text/plain', 'no such sprite');
            return send(response, 200, 'image/png', png);
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
            const id = Number.parseInt(basename(leaf, '.model'), 10);
            const bytes = rawModel(sources.get(source), models.get(source), id);
            if( !bytes ) return send(response, 404, 'text/plain', 'no such model');
            return proxyModel(renderer, response, { method: 'POST', path: '/api/modelfile', body: bytes });
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
function view(project, current, selected, stateJson, contentCatalog, sources) {
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
    const requestedState = { ...state };

    if( result.source === 'content' || result.source === 'dat2' )
        executeContentHooks(result, state);

    const native = nativePreviewStatus(project);
    return {
        error: null,
        warnings: [...(result.warnings || []), ...(result.source === 'authored' ? current.warnings : [])],
        interfaces: [result].map((result) => {
            /* Host reads the preview cannot answer are collected while laying out,
             * so the page can say which values it is showing as zero. */
            const unmodelled = new Set();
            const boxes = layout(result.ir, state, undefined, unmodelled);
            return {
            name: result.name,
            interfaceId: result.interfaceId,
            file: result.file,
            source: result.source || 'authored',
            spriteSource: result.source === 'authored' ? project.contentSource : result.source,
            modelSource: result.source === 'authored' ? project.contentSource : result.source,
            /* Dat2 records go to the production client verbatim; an unpacked
             * content record is compiled into a keyed cache overlay by the HTTP
             * route. Authored dry-run TSX has no file to overlay until build. */
            ...nativePreviewUrls(native, result, requestedState),
            boxes,
            unmodelled: [...unmodelled],
            inputs: stateInputs(result.ir),
            interfaceText: result.interfaceText,
            compackText: result.compackText,
            scripts: result.scripts,
            reactSource: result.reactSource || null,
            }; }),
    };
}

function nativePreviewUrls(native, result, state) {
    if( !native.available || (result.source !== 'dat2' && result.source !== 'content') )
        return { nativeFrame: null, nativeTree: null };
    const query = '?width=512&height=334&source=' + encodeURIComponent(result.source) +
        '&name=' + encodeURIComponent(result.name) +
        '&state=' + encodeURIComponent(JSON.stringify(state));
    const root = `/native/interface/${result.interfaceId}`;
    return {
        nativeFrame: `${root}.png${query}`,
        nativeTree: `${root}.tree.json${query}`,
    };
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

function spritePng(contentDir, index, id) {
    const name = index.get(id);
    if( !name ) return null;
    const dir = join(contentDir, 'sprites', name);
    if( !existsSync(dir) ) return null;
    /* A pack holds several sprites; the preview shows the first, which is the one
     * a component with a plain graphic id gets. */
    const bitmap = readdirSync(dir).filter((f) => extname(f) === '.bmp').sort()[0];
    if( !bitmap ) return null;
    try {
        return bmpToPng(readFileSync(join(dir, bitmap)));
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
