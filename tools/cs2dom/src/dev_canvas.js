/*
 * The dev server.
 *
 * Small on purpose. The old one had to marshal a whole runtime's state to the
 * page because the VM lived on the server side of a bridge; this one serves
 * three things — a catalogue, an interface's records, and the modules the
 * browser imports — because the runtime IS the browser.
 *
 * The one piece of real work here is turning a cache interface into a mount
 * payload: decompile its scripts to syntax trees with the C tool, lower them
 * to JavaScript with the emitter, and hand the browser the source. Everything
 * after that happens client-side.
 */

import { createServer } from 'node:http';
import { readFileSync, existsSync, readdirSync, statSync, watch } from 'node:fs';
import { join, extname, resolve } from 'node:path';
import { execFileSync } from 'node:child_process';

import { emitScript } from './cs2_js_emit.js';
import { parseIf, parseCompack } from './if_record.js';
import { createContentAssets } from './content_assets.js';
import { createContentConfigs } from './content_configs.js';
import {
    modelAssets, modelIndex, rawModel, requestModel, startModelServer,
} from './model.js';
import { encodePng } from './png.js';

const MIME = {
    '.html': 'text/html; charset=utf-8',
    '.js': 'text/javascript; charset=utf-8',
    '.mjs': 'text/javascript; charset=utf-8',
    '.json': 'application/json; charset=utf-8',
    '.css': 'text/css; charset=utf-8',
};

export function serveCanvas({
    root, contentDir, cache = null, revision = null, names = null,
    cs2 = null, port = 4173, onListen = null, onAddressInUse = null,
} = {}) {
    const state = {
        root: resolve(root),
        contentDir: contentDir ? resolve(contentDir) : null,
        cache, revision, names,
        cs2: cs2 ?? defaultCs2(root),
        catalogue: null,
        /* Assets are read on demand and remembered: a font is ~200 glyph
         * files, and a page that remounts on every save must not re-read them. */
        assets: contentDir ? createContentAssets(resolve(contentDir)) : null,
        assetCache: new Map(),
        /* Syntax trees are expensive to produce and immutable per cache, so
         * they are remembered for the life of the process. */
        asts: new Map(),
        listeners: new Set(),
        /* Identifies THIS process to a page that reconnects; see the event
         * stream. `process.pid` alone repeats after a wrap, and the start
         * time makes the pair unique for as long as anyone cares. */
        boot: `${process.pid}:${Date.now()}`,
        /*
         * The MODEL half.
         *
         * A widget model is toridraw's, not the emitter's -- the draw list
         * says "model 7748 at this pose" and something has to raster it. The
         * entity viewer already owns that renderer, so the dev server starts
         * it on a private port and hands the browser the same WASM the entity
         * viewer's page uses. Without it, 442 of the 878 interfaces with a
         * draw list are missing part of their picture and 20 of them are
         * missing all of it -- `pirate_combilock` is 15 models and one line
         * of text, so it drew the text and nothing else.
         */
        renderer: null,
        rendererAssets: modelAssets(),
        modelIndex: null,
    };

    const server = createServer((request, response) => {
        const url = new URL(request.url, 'http://localhost');
        try { route(state, url, request, response); }
        catch( error ) { send(response, 500, 'text/plain', String(error.stack ?? error)); }
    });

    if( state.contentDir ) watchContent(state);
    watchSource(state);

    /* One port up, exactly as the older dev server does it. */
    state.renderer = startModelServer(
        { cache, revision, content: contentDir, unpackedContent: contentDir },
        port + 1,
        (line) => process.stderr.write(`${line}\n`));

    /*
     * A port already taken is a QUESTION, not a crash.
     *
     * The thing holding it is almost always this same server from an earlier
     * run, and the unhandled EADDRINUSE stack that came out instead said
     * nothing about what to do next. The decision belongs to the caller --
     * asking is only sensible on a terminal -- so the condition is handed
     * out and the listen is retried once if the caller says it cleared it.
     */
    server.on('error', (error) => {
        if( error.code !== 'EADDRINUSE' || !onAddressInUse ) throw error;
        Promise.resolve(onAddressInUse(port, error)).then((cleared) => {
            if( !cleared ) process.exit(1);
            server.listen(port, () => onListen?.(`http://localhost:${port}`));
        });
    });
    server.listen(port, () => onListen?.(`http://localhost:${port}`));
    return server;
}

function route(state, url, request, response) {
    switch( url.pathname )
    {
    case '/':
        return sendPage(state, response);
    case '/dev-client.js':
        return sendClient(response);
    case '/events':
        return openEventStream(state, response);
    case '/api/catalogue':
        return send(response, 200, MIME['.json'], JSON.stringify(catalogue(state)));
    case '/api/interface':
        return sendInterface(state, url.searchParams.get('key'), response);
    case '/api/group':
        return sendGroup(state, Number(url.searchParams.get('id')), response);
    case '/api/config':
        return sendConfig(state, url.searchParams, response);
    case '/api/model':
        return sendModel(state, Number(url.searchParams.get('id')), response);
    case '/api/seq':
        return sendSequence(state, Number(url.searchParams.get('id')), response);
    /* The worker asks for `/model/textures.bin?ids=...`; the name is its
     * own and changing it there would only move the coupling. */
    case '/model/textures.bin':
        return sendTextures(state, url.search, response);
    case '/toridraw/ev_wasm.js':
        return sendRendererFile(state.rendererAssets.javascript,
            'text/javascript; charset=utf-8', response);
    case '/toridraw/ev_wasm_module.js':
        /* The entity viewer ships a classic script; the worker imports it as
         * a MODULE, and the only difference is the export. */
        return sendRendererFile(state.rendererAssets.javascript,
            'text/javascript; charset=utf-8', response, '\nexport { EVModule };\n');
    case '/toridraw/ev_wasm.wasm':
        return sendRendererFile(state.rendererAssets.wasm, 'application/wasm', response);
    case '/api/sprite':
        return sendSprite(state, url.searchParams, response);
    case '/api/font':
        return sendFont(state, url.searchParams, response);
    default:
        return sendModule(state, url.pathname, response);
    }
}

/* -------------------------------------------------------------------------
 * The page and its modules
 * ---------------------------------------------------------------------- */

async function sendPage(state, response) {
    const { canvasDevPage } = await import('./dev_page_canvas.js');
    send(response, 200, MIME['.html'], canvasDevPage({ build: state.boot }));
}

async function sendClient(response) {
    const { canvasDevClient } = await import('./dev_page_canvas.js');
    send(response, 200, MIME['.js'], canvasDevClient());
}

/**
 * Serve the runtime's own modules straight from disk.
 *
 * No bundler. Every file under `src/` is already an ES module the browser can
 * import, and skipping the bundle means a stack trace in the console names the
 * real file and line — which is the difference between reading a bug and
 * hunting one.
 */
function sendModule(state, pathname, response) {
    if( !pathname.startsWith('/src/') ) return send(response, 404, 'text/plain', 'not found');
    /* Path traversal: resolve and confirm the result is still inside `src`. */
    const base = join(state.root, 'src');
    const path = resolve(join(state.root, pathname));
    if( !path.startsWith(base) || !existsSync(path) )
        return send(response, 404, 'text/plain', 'not found');
    const type = MIME[extname(path)] ?? 'application/octet-stream';
    send(response, 200, type, readFileSync(path));
}

/* -------------------------------------------------------------------------
 * The catalogue
 * ---------------------------------------------------------------------- */

function catalogue(state) {
    if( state.catalogue ) return state.catalogue;
    const entries = [];
    if( state.contentDir )
    {
        const dir = join(state.contentDir, 'interfaces');
        const compack = readPack(join(state.contentDir, 'pack', '3_interfaces.pack'));
        if( existsSync(dir) )
            for( const file of readdirSync(dir).sort() )
            {
                if( !file.endsWith('.if') ) continue;
                const name = file.slice(0, -3);
                const id = compack.get(name);
                entries.push({
                    key: `content:${name}`, name, source: 'OSRS-Content',
                    label: id === undefined ? name : `${name} · ${id}`,
                    interfaceId: id ?? -1,
                });
            }
    }
    state.catalogue = entries;
    return entries;
}

function readPack(path) {
    const out = new Map();
    if( !existsSync(path) ) return out;
    for( const raw of readFileSync(path, 'utf8').split('\n') )
    {
        const line = raw.replace(/\/\/.*$/, '').trim();
        const split = line.indexOf('=');
        if( split < 1 ) continue;
        const id = Number(line.slice(0, split));
        const name = line.slice(split + 1).trim();
        if( Number.isInteger(id) && name ) out.set(name, id);
    }
    return out;
}

/* -------------------------------------------------------------------------
 * One interface, ready to mount
 * ---------------------------------------------------------------------- */

function sendInterface(state, key, response) {
    if( !key ) return send(response, 400, MIME['.json'], '{"error":"no key"}');
    const [source, name] = splitKey(key);
    if( source !== 'content' || !state.contentDir )
        return send(response, 404, MIME['.json'], '{"error":"unknown source"}');

    const ifPath = join(state.contentDir, 'interfaces', `${name}.if`);
    const compackPath = join(state.contentDir, 'interfaces', `${name}.compack`);
    if( !existsSync(ifPath) )
        return send(response, 404, MIME['.json'], `{"error":"no ${name}.if"}`);

    const ifText = readFileSync(ifPath, 'utf8');
    const record = parseIf(ifText);
    const compack = existsSync(compackPath)
        ? parseCompack(readFileSync(compackPath, 'utf8')) : { byName: new Map(), order: [] };

    /*
     * The scripts an interface needs are the ones its blocks name in a hook
     * field. Gathering them from the RECORD rather than from a dependency
     * graph keeps the payload to what this interface actually installs; the
     * closure beneath them is pulled in by the emitter's `procs` list.
     */
    const roots = hookScriptIds(record);
    const { scripts, cs2Source, errors } = lowerClosure(state, roots);

    send(response, 200, MIME['.json'], JSON.stringify({
        name,
        interfaceId: groupIds(state).get(name) ?? -1,
        width: 765, height: 503,
        records: { if: ifText, cs2: cs2Source, js: Object.values(scripts).join('\n\n') },
        /* The page BAKES: an interface is its `.if` plus the `.compack` that
         * gives every block a component id, and without the second the tree
         * cannot be built at all. */
        ifText,
        compackText: existsSync(compackPath) ? readFileSync(compackPath, 'utf8') : '',
        scripts,
        onLoad: onLoadEntries(record, compack, name),
        state: stateSlices(record),
        errors,
    }));
}

/**
 * One interface by GROUP ID, for a mount the running scripts asked for.
 *
 * A script reaching a component in a group that is not in the tree is not an
 * error -- it is a mount, and the client answers it by loading that interface.
 * The page cannot read the content tree, so the bake inputs and the new
 * group's own script closure come from here, in one round trip.
 */
function sendGroup(state, id, response) {
    if( !Number.isInteger(id) || id < 0 )
        return send(response, 400, MIME['.json'], '{"error":"no id"}');
    const name = groupNames(state).get(id);
    if( !name || !state.contentDir )
        return send(response, 404, MIME['.json'], `{"error":"no interface ${id}"}`);

    const ifPath = join(state.contentDir, 'interfaces', `${name}.if`);
    if( !existsSync(ifPath) )
        return send(response, 404, MIME['.json'], `{"error":"no ${name}.if"}`);
    const compackPath = join(state.contentDir, 'interfaces', `${name}.compack`);
    const ifText = readFileSync(ifPath, 'utf8');
    const { scripts, errors } = lowerClosure(state, hookScriptIds(parseIf(ifText)));

    send(response, 200, MIME['.json'], JSON.stringify({
        id, name, ifText,
        compackText: existsSync(compackPath) ? readFileSync(compackPath, 'utf8') : '',
        scripts, errors,
    }));
}

/*
 * One config record.
 *
 * The tables are read whole on first use -- they are one text file each and
 * the process keeps them -- but they are SENT one record at a time, because
 * the object table alone is tens of megabytes and an interface touches a
 * handful of rows. A park names exactly the row it wants, so the round trip
 * carries no more than that.
 */
const CONFIG_TABLES = {
    enum: 'enums', struct: 'structs', obj: 'objects', npc: 'npcs',
    loc: 'locs', inv: 'inventories', mapelement: 'mapElements',
    param: 'params',
};

function sendConfig(state, params, response) {
    const kind = params.get('kind');
    const id = Number(params.get('id'));
    const table = CONFIG_TABLES[kind];
    if( !table || !Number.isInteger(id) )
        return send(response, 400, MIME['.json'], '{"error":"bad kind or id"}');
    if( !state.contentDir )
        return send(response, 404, MIME['.json'], '{"error":"no content"}');

    if( !state.configs ) state.configs = createContentConfigs(state.contentDir);
    const record = state.configs.get(table, id);
    /* A row that is genuinely absent is a 200 with no record, not a 404: the
     * host has a documented miss answer for every config op and needs the
     * load to have COMPLETED to reach it. A 404 would be retried. */
    send(response, 200, MIME['.json'], JSON.stringify({ kind, table, id, record: record ?? null }));
}

/* -------------------------------------------------------------------------
 * Models
 * ---------------------------------------------------------------------- */

/*
 * One widget model, prepared.
 *
 * The bytes go to the renderer's `/api/widgetmodel`, which decodes the model
 * archive and applies the UI asset profile -- classic model, non-SD textures
 * dropped, scene lighting with the reference adjustments. That is the exact
 * model the C client draws, which is the only reason a comparison against it
 * means anything.
 */
function sendModel(state, id, response) {
    if( !Number.isInteger(id) || !state.contentDir )
        return send(response, 400, 'text/plain', 'no id');
    if( !state.modelIndex ) state.modelIndex = modelIndex(state.contentDir);
    const body = rawModel(state.contentDir, state.modelIndex, id);
    if( !body ) return send(response, 404, 'text/plain', `no model ${id}`);
    relay(state, { method: 'POST', path: '/api/widgetmodel', body }, response);
}

function sendSequence(state, id, response) {
    if( !Number.isInteger(id) || id < 0 ) return send(response, 404, 'text/plain', 'no sequence');
    relay(state, { path: `/api/seq/${id}.anim` }, response);
}

function sendTextures(state, search, response) {
    relay(state, { path: `/api/textures.bin${search}` }, response);
}

function relay(state, options, response) {
    requestModel(state.renderer, options)
        .then((result) => {
            response.writeHead(result.status, {
                'content-type': result.headers['content-type'] || 'application/octet-stream',
                'cache-control': 'no-store',
            });
            response.end(result.body);
        })
        .catch((error) => send(response, 502, 'text/plain',
            `model renderer: ${error.message}`));
}

/* A file from the entity viewer's web directory, optionally with a tail. */
function sendRendererFile(path, type, response, tail = '') {
    if( !path || !existsSync(path) )
        return send(response, 404, 'text/plain', 'the model renderer is not built');
    const body = readFileSync(path);
    send(response, 200, type, tail ? Buffer.concat([body, Buffer.from(tail)]) : body);
}

/** name -> group id, from the interface pack. */
function groupIds(state) {
    if( !state.groupIds )
        state.groupIds = state.contentDir
            ? readPack(join(state.contentDir, 'pack', '3_interfaces.pack')) : new Map();
    return state.groupIds;
}

/** group id -> name, the same pack read the other way. */
function groupNames(state) {
    if( !state.groupNames )
    {
        state.groupNames = new Map();
        for( const [name, id] of groupIds(state) ) state.groupNames.set(id, name);
    }
    return state.groupNames;
}

function splitKey(key) {
    const split = key.indexOf(':');
    return split < 0 ? ['content', key] : [key.slice(0, split), key.slice(split + 1)];
}

/** Every script id a block's hook fields name. */
function hookScriptIds(record) {
    const ids = new Set();
    for( const block of record.blocks )
        for( const [key, entries] of block.fields )
        {
            if( !/^on[a-z]+$/.test(key) ) continue;
            for( const entry of entries )
            {
                const first = entry.value.split(',')[0];
                const match = /^i:(-?\d+)$/.exec(first.trim());
                if( match && Number(match[1]) >= 0 ) ids.add(Number(match[1]));
            }
        }
    return [...ids];
}

/** The onload hooks, so the page can dispatch them after mounting. */
function onLoadEntries(record, compack, interfaceName) {
    const entries = [];
    for( const block of record.blocks )
    {
        const value = record.get(block.name, 'onload');
        if( !value ) continue;
        const parts = value.split(',').map((part) => part.trim());
        const first = /^i:(-?\d+)$/.exec(parts[0] ?? '');
        if( !first ) continue;
        entries.push({
            scriptId: Number(first[1]),
            args: parts.slice(1).map(hookArgument),
            component: compack.byName.get(block.name) ?? -1,
        });
    }
    return entries;
}

function hookArgument(part) {
    const match = /^([is]):(.*)$/.exec(part);
    if( !match ) return 0;
    return match[1] === 'i' ? Number(match[2]) : match[2];
}

/** The var/varbit ids this interface's hooks watch, as controls. */
function stateSlices(record) {
    const slices = new Map();
    for( const block of record.blocks )
    {
        for( const value of record.getAll(block.name, 'varptriggers') )
            for( const id of value.split(',') )
                if( id.trim() ) slices.set(`varp:${id.trim()}`,
                    { id: `varp:${id.trim()}`, label: `varp ${id.trim()}`, value: 0 });
        for( const value of record.getAll(block.name, 'varbittriggers') )
            for( const id of value.split(',') )
                if( id.trim() ) slices.set(`varbit:${id.trim()}`,
                    { id: `varbit:${id.trim()}`, label: `varbit ${id.trim()}`, value: 0 });
    }
    return [...slices.values()];
}

/* -------------------------------------------------------------------------
 * Assets
 *
 * Served as self-describing JSON rather than as bare images, because a bare
 * image is only half a sprite: the canvas size and the offset within it live
 * in `pack.meta`, and a browser handed the bitmap alone would draw every
 * trimmed icon and every offset glyph in the wrong place.
 * ---------------------------------------------------------------------- */

function sendSprite(state, params, response) {
    if( !state.assets ) return send(response, 404, MIME['.json'], '{"error":"no content tree"}');
    const id = Number(params.get('id'));
    const frame = Number(params.get('frame') ?? 0);
    const key = `sprite:${id}:${frame}`;
    if( !state.assetCache.has(key) )
    {
        const sprite = Number.isInteger(id) ? state.assets.sprite(id, frame) : null;
        state.assetCache.set(key, sprite ? JSON.stringify(spritePayload(sprite)) : null);
    }
    const body = state.assetCache.get(key);
    if( !body ) return send(response, 404, MIME['.json'], '{"error":"no such sprite"}');
    send(response, 200, MIME['.json'], body);
}

function sendFont(state, params, response) {
    if( !state.assets ) return send(response, 404, MIME['.json'], '{"error":"no content tree"}');
    const id = Number(params.get('id'));
    const key = `font:${id}`;
    if( !state.assetCache.has(key) )
    {
        const font = Number.isInteger(id) ? state.assets.font(id) : null;
        state.assetCache.set(key, font ? JSON.stringify(fontPayload(font)) : null);
    }
    const body = state.assetCache.get(key);
    if( !body ) return send(response, 404, MIME['.json'], '{"error":"no such font"}');
    send(response, 200, MIME['.json'], body);
}

function spritePayload(sprite) {
    return {
        width: sprite.width, height: sprite.height,
        offsetX: sprite.offsetX, offsetY: sprite.offsetY,
        png: dataUrl(sprite.bitmap),
    };
}

function fontPayload(font) {
    const glyphs = {};
    for( const [code, glyph] of font.glyphs )
        glyphs[code] = {
            width: glyph.width, height: glyph.height,
            offsetX: glyph.offsetX, offsetY: glyph.offsetY,
            png: dataUrl(glyph.bitmap),
        };
    return {
        id: font.id, ascent: font.ascent, lineHeight: font.lineHeight,
        /* Sparse: 256 advances of which most are zero would triple the
         * payload for nothing. */
        advances: Object.fromEntries(
            font.advances.map((value, code) => [code, value]).filter(([, value]) => value)),
        glyphs,
    };
}

function dataUrl(image) {
    return `data:image/png;base64,${encodePng(image).toString('base64')}`;
}

/* -------------------------------------------------------------------------
 * Lowering a closure
 * ---------------------------------------------------------------------- */

/**
 * Decompile and lower every script reachable from `roots`.
 *
 * Breadth-first over the emitter's own `procs` and `hooks` lists, which is why
 * the emitter reports them: a closure is exactly what the scripts call, and
 * anything else is either over-fetching or a missing script at run time.
 */
function lowerClosure(state, roots) {
    const scripts = {};
    const cs2Parts = [];
    const errors = [];
    const seen = new Set();
    const queue = [...roots];

    while( queue.length )
    {
        const id = queue.shift();
        if( seen.has(id) ) continue;
        seen.add(id);

        const ast = syntaxTree(state, id);
        if( !ast ) { errors.push(`script ${id}: could not decompile`); continue; }
        try
        {
            const result = emitScript(ast);
            scripts[id] = result.code;
            cs2Parts.push(`// ${result.name} — ${id}`);
            for( const dependency of [...result.procs, ...result.hooks] )
                if( !seen.has(dependency) ) queue.push(dependency);
        }
        catch( error ) { errors.push(`script ${id}: ${error.message}`); }
    }
    return { scripts, cs2Source: cs2Parts.join('\n'), errors };
}

/** One script's syntax tree, from the C decompiler, cached for the process. */
function syntaxTree(state, id) {
    if( state.asts.has(id) ) return state.asts.get(id);
    let tree = null;
    if( state.cs2 && state.cache && existsSync(state.cs2) )
    {
        try
        {
            const args = ['decompile', '--cache', state.cache, '--emit', 'ast-json', '--quiet'];
            if( state.revision ) args.push('--rev', state.revision);
            if( state.names ) args.push('--names', state.names);
            args.push(String(id));
            const stdout = execFileSync(state.cs2, args, { encoding: 'utf8' });
            /* The tool prints its summary to stderr and the document to
             * stdout, so the first `{` is the start of the tree. */
            const start = stdout.indexOf('{');
            if( start >= 0 ) tree = JSON.parse(stdout.slice(start));
        }
        catch { tree = null; }
    }
    state.asts.set(id, tree);
    return tree;
}

function defaultCs2(root) {
    const path = join(root, '..', '..', '3rd', 'rscache', 'tools', 'cs2', 'cs2');
    return existsSync(path) ? path : null;
}

/* -------------------------------------------------------------------------
 * Live reload
 * ---------------------------------------------------------------------- */

function watchContent(state) {
    const dir = join(state.contentDir, 'interfaces');
    if( !existsSync(dir) ) return;
    watchDir(dir, true, () => {
        state.catalogue = null;
        announce(state, 'changed');
    });
}

/*
 * The RUNTIME's own sources reload the page, they do not remount.
 *
 * A remount builds a new session out of the modules the document already
 * imported, and an ES module is fetched once per document -- so editing the
 * painter or the tree and remounting runs the OLD painter against a new
 * tree, silently. Only a reload picks the new code up.
 */
function watchSource(state) {
    const dir = join(state.root, 'src');
    if( !existsSync(dir) ) return;
    watchDir(dir, true, () => announce(state, 'reload'));
}

/* Coalesced: an editor save fires several events for one write, and acting
 * per event would restart the interface mid-mount. */
function watchDir(dir, recursive, changed) {
    let pending = null;
    watch(dir, { recursive }, () => {
        clearTimeout(pending);
        pending = setTimeout(changed, 60);
    });
}

function announce(state, kind) {
    for( const listener of state.listeners ) listener(kind);
}

function openEventStream(state, response) {
    response.writeHead(200, {
        'content-type': 'text/event-stream',
        'cache-control': 'no-cache',
        connection: 'keep-alive',
    });
    response.write('retry: 1000\n\n');
    /*
     * The BOOT id, first thing.
     *
     * A tab reconnecting to a server that has restarted is holding modules
     * from the old process: the page imports `/src/...` once and an ES module
     * is cached for the document's life, so every runtime fix landed while a
     * tab stayed open was invisible in it. A changed boot id means new code
     * and the page reloads itself.
     */
    response.write(`event: hello\ndata: ${state.boot}\n\n`);
    const notify = (kind) => response.write(`event: ${kind}\ndata: 1\n\n`);
    state.listeners.add(notify);
    response.on('close', () => state.listeners.delete(notify));
}

function send(response, status, type, body) {
    response.writeHead(status, { 'content-type': type, 'cache-control': 'no-store' });
    response.end(body);
}

export { lowerClosure, hookScriptIds, stateSlices, onLoadEntries, spritePayload, fontPayload };
