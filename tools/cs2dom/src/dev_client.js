/*
 * The dev server, with the real client as the preview.
 *
 * WHAT CHANGED, AND WHY. This tool used to carry its own port of the client's
 * interface runtime -- a UITree, the IF3 layout, the emit walk, the hit tests
 * and a canvas painter -- and serve it to the browser as JavaScript. It was
 * checked against the C client command for command and reached 881 interfaces
 * out of 881, and it still drew the wrong picture: an emit list cannot see a
 * painter, so the device-pixel transform, the model clip, and the assets only
 * the painter knew it needed were all invisible to the gate that said it was
 * right. CS2_DOM_CLIENT_PLAN.md is the argument in full.
 *
 * So the preview is the client. `make -C src web` builds the same C sources the
 * native client is built from into build-web/torirs.wasm; this server hands the
 * browser that, an io_server to answer its cache reads, and the dev page puts it
 * in an iframe. Nothing here renders anything.
 *
 * WHAT THIS SERVER IS FOR, then, is the half the client does not do: reading
 * the content tree, and showing what an interface COMPILES TO. The catalogue,
 * the `.if`, the `.cs2`, and the JavaScript that cache CS2 lowers to are this
 * tool's own subject, and none of them need a renderer.
 *
 *   GET /                     the dev page
 *   GET /dev-client.js        its script
 *   GET /events               SSE: hello (boot id), changed, reload
 *   GET /api/catalogue        every interface in the content tree
 *   GET /api/records?key=     one interface's .if / .cs2 / JavaScript
 *   GET /client/*             build-web: index.html, torirs.js, torirs.wasm
 *   POST /io, GET /boot/*     proxied to the io_server child
 *
 * The client is served from THIS origin rather than from the io_server's, so
 * the iframe is same-origin and a stack trace out of it is readable. Its cache
 * traffic is proxied on to the process that owns the cache.
 */

import { spawn } from 'node:child_process';
import { cpSync, existsSync, mkdirSync, readFileSync, readdirSync, statSync, watch } from 'node:fs';
import { createServer, request as httpRequest } from 'node:http';
import { extname, join, relative as relativePath, resolve } from 'node:path';

import { lowerClosure, hookScriptIds } from './dev_records.js';
import { packCache } from './export.js';
import { parseIf } from './if_record.js';

const MIME = {
    '.html': 'text/html; charset=utf-8',
    '.js': 'text/javascript; charset=utf-8',
    '.json': 'application/json',
    '.wasm': 'application/wasm',
    '.css': 'text/css; charset=utf-8',
    '.map': 'application/json',
    '.ini': 'text/plain; charset=utf-8',
};

/** Where `make -C src web` puts the client, and what it must contain. */
const CLIENT_DIR = ['build-web'];
const CLIENT_REQUIRED = ['torirs.js', 'torirs.wasm', 'index.html', 'torirs_host.js'];

export function serveClient({
    root, contentDir, cache = null, revision = null, names = null,
    cs2 = null, port = 8099, ioPort = null, onListen = null, onAddressInUse = null,
    log = (line) => process.stderr.write(`${line}\n`),
} = {}) {
    const repoRoot = resolve(root, '..', '..');
    const state = {
        root: resolve(root),
        repoRoot,
        clientDir: resolve(repoRoot, ...CLIENT_DIR),
        contentDir: contentDir ? resolve(contentDir) : null,
        cache: cache ? resolve(cache) : null,
        revision,
        names,
        cs2: cs2 ?? join(repoRoot, '3rd', 'rscache', 'tools', 'cs2', 'cs2'),
        catalogue: null,
        /* Set by startIoServer: the cache dir spelled the way both the client
         * and io_server must name it. Null until then, and null means no cache. */
        cacheArg: null,
        asts: new Map(),
        listeners: new Set(),
        /*
         * Identifies THIS process to a page that reconnects. A tab holding
         * modules from a server that has since restarted is running code that
         * no longer exists on disk, and every fix landed while it stayed open
         * was invisible in it. A changed boot id means reload.
         */
        boot: `${process.pid}:${Date.now()}`,
        io: null,
        ioPort: ioPort ?? port + 1,
        cachepack: join(repoRoot, '3rd', 'rscache', 'tools', 'cachepack', 'cachepack'),
        /* The cache the client actually reads: a copy of `cache` with this
         * content tree packed over it. Null when there is no content tree, and
         * then the real cache is read directly and nothing is written. */
        previewCache: null,
        baking: false,
        bakeAgain: false,
        bakeError: null,
        /* When the last successful bake finished. The smoke and edit-loop
         * harnesses wait on this rather than on a duration. */
        bakedAt: 0,
        log,
    };

    requireClientBuild(state);
    state.previewCache = ensurePreviewCache(state);

    const server = createServer((request, response) => {
        const url = new URL(request.url, 'http://localhost');
        try { route(state, url, request, response); }
        catch( error ) { send(response, 500, 'text/plain', String(error.stack ?? error)); }
    });

    /*
     * Bake ONCE at startup, and do it BEFORE io_server opens the cache.
     *
     * Without a startup bake the preview cache holds whatever the last session
     * packed into it -- days old, or someone's half-finished edit -- and the
     * page shows it with complete confidence. Doing it here rather than after
     * io_server starts also means no restart and no reboot: nothing has read
     * the cache yet, so there is nothing holding stale bytes.
     */
    if( state.previewCache ) packOnce(state);

    if( state.contentDir ) watchContent(state);
    watchSource(state);
    state.io = startIoServer(state);

    /*
     * A port already taken is a QUESTION, not a crash: the thing holding it is
     * almost always this same server from an earlier run, and an unhandled
     * EADDRINUSE stack says nothing about what to do next. The decision belongs
     * to the caller, because asking is only sensible on a terminal.
     */
    server.on('error', (error) => {
        if( error.code !== 'EADDRINUSE' || !onAddressInUse ) throw error;
        Promise.resolve(onAddressInUse(port, error)).then((cleared) => {
            if( !cleared ) process.exit(1);
            server.listen(port, () => onListen?.(`http://localhost:${port}`));
        });
    });
    server.listen(port, () => onListen?.(`http://localhost:${port}`));

    const stop = () => { try { state.io?.kill(); } catch { /* already gone */ } };
    process.on('exit', stop);
    process.on('SIGINT', () => { stop(); process.exit(0); });
    return server;
}

/**
 * Refuse to start without a client, and say which command builds one.
 *
 * A dev server that comes up and serves a blank iframe is a worse failure than
 * one that does not come up: the missing piece is a build in another directory,
 * and nothing on the page can say so.
 */
function requireClientBuild(state) {
    const missing = CLIENT_REQUIRED.filter((file) => !existsSync(join(state.clientDir, file)));
    if( missing.length === 0 ) return;
    throw new Error(
        `cs2dom: ${state.clientDir} is missing ${missing.join(', ')}.\n`
        + '        The preview IS the client, so it has to be built first:\n'
        + '            make -C src web\n');
}

/* -------------------------------------------------------------------------
 * The cache, answered by the process that owns it
 * ---------------------------------------------------------------------- */

/**
 * io_server, on a private port.
 *
 * The wasm client has no disk: every cache read it would have satisfied locally
 * becomes a POST /io, and this is the process that answers them. It is the
 * tested backend for the wire lane and it already knows how to open a cache, so
 * it is started rather than reimplemented -- the same shape the old server used
 * for the entity viewer.
 */
function startIoServer(state, { quiet = false } = {}) {
    const binary = join(state.repoRoot, 'src', 'build', 'io_server');
    if( !existsSync(binary) )
    {
        state.log(`  cache     io_server not built -- make -C src io-server`);
        return null;
    }
    /* The PREVIEW cache when there is one -- the client must read the bytes an
     * edit produces, not the ones the tree shipped with. */
    const served = state.previewCache ?? state.cache;
    if( !served || !existsSync(served) )
    {
        state.log('  cache     no cache configured; the client will have nothing to read');
        return null;
    }
    /*
     * RELATIVE cache path, from the repo root.
     *
     * io_server refuses a leading '/' outright (cache_dir_normalize in
     * src/ioserver/io_server_main.c): cache directories arrive from a page, so
     * an absolute one is a path it will not be talked into opening. That makes
     * the child's working directory part of the argument, not an incidental.
     */
    const relative = relativePath(state.repoRoot, served);
    if( relative.startsWith('..') )
    {
        state.log(`  cache     ${served} is outside ${state.repoRoot}; io_server will not open it`);
        return null;
    }
    /* The page needs this exact spelling: it is the client's first positional
     * argument, and the client and io_server have to name one cache the same
     * way or the client opens a directory that is not there. */
    state.cacheArg = relative;

    const args = [
        '--rev', state.revision ?? 'osrs239', relative,
        '--port', String(state.ioPort),
        '--root', state.clientDir,
        '--boot-root', state.repoRoot,
    ];
    const child = spawn(binary, args, { cwd: state.repoRoot, stdio: ['ignore', 'ignore', 'pipe'] });

    /*
     * Keep the last of its output and print it if it DIES, rather than
     * filtering for words that look like errors. The filter version dropped
     * "refusing cache directory" -- which was the whole explanation -- and
     * left a server that reported the child as started and then answered 502
     * to everything it proxied.
     */
    let tail = '';
    child.stderr.on('data', (chunk) => { tail = (tail + chunk).slice(-2000); });
    child.on('error', (error) => state.log(`  cache     ${error.message}`));
    child.on('exit', (code, signal) => {
        state.io = null;
        if( signal === 'SIGTERM' ) return; /* our own shutdown */
        state.log(`  cache     io_server exited (${signal ?? code})`);
        for( const line of tail.trim().split('\n').slice(-4) )
            if( line ) state.log(`            ${line}`);
    });
    if( !quiet )
        state.log(`  cache     io_server on ${state.ioPort} (${state.revision} @ ${relative})`);
    return child;
}

/**
 * Replace the io_server child with one that has not read the cache yet.
 *
 * Synchronous in effect rather than in fact: the old child is signalled and the
 * new one spawned immediately, and the first request the new one gets will be
 * from a client that is still booting. The port is the same, so nothing that
 * points at it has to be told.
 */
async function restartIoServer(state) {
    const previous = state.io;
    state.io = null;
    if( previous )
    {
        previous.removeAllListeners('exit');
        try { previous.kill(); } catch { /* already gone */ }
    }
    state.io = startIoServer(state, { quiet: true });

    /*
     * Wait for it to bind before anyone is told the bake is done. The page
     * reboots the client the moment it hears, and a client whose first cache
     * read lands in the gap between the old process dying and the new one
     * listening fails its boot for a reason that has nothing to do with the
     * edit.
     */
    for( let attempt = 0; attempt < 100; attempt++ )
    {
        if( await ioAnswers(state) ) return;
        await new Promise((wake) => setTimeout(wake, 50));
    }
    state.log('  cache     io_server did not come back after the bake');
}

/** Does the child answer on its port yet? */
function ioAnswers(state) {
    return new Promise((resolve) => {
        const probe = httpRequest(
            { host: '127.0.0.1', port: state.ioPort, method: 'GET', path: '/stats' },
            (response) => { response.resume(); resolve(true); });
        probe.on('error', () => resolve(false));
        probe.end();
    });
}

/**
 * Hand a request on to io_server and stream the answer back.
 *
 * Proxied rather than redirected so the iframe stays on this origin: a redirect
 * to another port makes the client cross-origin, and then a console error from
 * inside it is unreadable and every message needs an explicit target origin.
 */
function proxyToIo(state, url, request, response) {
    if( !state.io ) return send(response, 503, 'text/plain', 'no io_server');
    const proxied = httpRequest(
        {
            host: '127.0.0.1', port: state.ioPort,
            method: request.method, path: url.pathname + url.search,
            headers: { ...request.headers, host: `127.0.0.1:${state.ioPort}` },
        },
        (upstream) => {
            response.writeHead(upstream.statusCode ?? 502, upstream.headers);
            upstream.pipe(response);
        });
    proxied.on('error', (error) => send(response, 502, 'text/plain', String(error.message)));
    request.pipe(proxied);
}

/* -------------------------------------------------------------------------
 * Routing
 * ---------------------------------------------------------------------- */

function route(state, url, request, response) {
    const path = url.pathname;

    if( path === '/io' || path.startsWith('/boot/') )
        return proxyToIo(state, url, request, response);
    if( path.startsWith('/client/') )
        return sendClientFile(state, path.slice('/client/'.length), response);

    switch( path )
    {
    case '/': return sendPage(state, response);
    case '/dev-client.js': return sendPageScript(response);
    case '/events': return openEventStream(state, response);
    case '/api/catalogue':
        return send(response, 200, MIME['.json'], JSON.stringify(catalogue(state)));
    case '/api/records':
        return sendRecords(state, url.searchParams.get('key'), response);
    case '/api/bake':
        return send(response, 200, MIME['.json'], JSON.stringify({
            baking: state.baking,
            bakedAt: state.bakedAt,
            error: state.bakeError,
        }));
    case '/api/project':
        return send(response, 200, MIME['.json'], JSON.stringify({
            revision: state.revision,
            /* The cache dir the CLIENT must name, which is io_server's
             * spelling of it: relative to the repo root, because that is the
             * only spelling io_server accepts (see startIoServer). */
            cacheDir: state.cacheArg,
            hasCache: Boolean(state.io),
            build: state.boot,
        }));
    default:
        if( path.startsWith('/src/') ) return sendModule(state, path, response);
        return send(response, 404, 'text/plain', 'not found');
    }
}

async function sendPage(state, response) {
    const { clientDevPage } = await import('./dev_page_client.js');
    send(response, 200, MIME['.html'], clientDevPage({ build: state.boot }));
}

async function sendPageScript(response) {
    const { clientDevScript } = await import('./dev_page_client.js');
    send(response, 200, MIME['.js'], clientDevScript());
}

/** build-web, as served to the iframe. Path-traversal checked. */
function sendClientFile(state, name, response) {
    const path = resolve(join(state.clientDir, name));
    if( !path.startsWith(state.clientDir) || !existsSync(path) )
        return send(response, 404, 'text/plain', 'not found');
    send(response, 200, MIME[extname(path)] ?? 'application/octet-stream', readFileSync(path));
}

/**
 * This tool's own modules, straight from disk.
 *
 * No bundler: every file under src/ is already an ES module a browser can
 * import, and skipping the bundle means a stack trace names the real file and
 * line, which is the difference between reading a bug and hunting one.
 */
function sendModule(state, pathname, response) {
    const base = join(state.root, 'src');
    const path = resolve(join(state.root, pathname));
    if( !path.startsWith(base) || !existsSync(path) )
        return send(response, 404, 'text/plain', 'not found');
    send(response, 200, MIME[extname(path)] ?? 'application/octet-stream', readFileSync(path));
}

/* -------------------------------------------------------------------------
 * The catalogue and the records
 * ---------------------------------------------------------------------- */

function catalogue(state) {
    if( state.catalogue ) return state.catalogue;
    const entries = [];
    if( state.contentDir )
    {
        const dir = join(state.contentDir, 'interfaces');
        const ids = readPack(join(state.contentDir, 'pack', '3_interfaces.pack'));
        if( existsSync(dir) )
            for( const file of readdirSync(dir).sort() )
            {
                if( !file.endsWith('.if') ) continue;
                const name = file.slice(0, -3);
                const id = ids.get(name);
                entries.push({
                    key: `content:${name}`,
                    name,
                    /* -1, not 0: interface 0 exists, and a missing id has to be
                     * distinguishable from it or the preview opens the wrong
                     * interface without ever looking wrong. */
                    interfaceId: id ?? -1,
                    label: id === undefined ? name : `${name} · ${id}`,
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

/**
 * What an interface compiles to: its record, its CS2, and that CS2 as JavaScript.
 *
 * The scripts are the ones the interface's own blocks name in a hook field --
 * gathered from the record rather than from a dependency graph, so the answer is
 * what THIS interface installs, with the closure beneath it pulled in by the
 * emitter's own procs list.
 */
function sendRecords(state, key, response) {
    if( !key ) return send(response, 400, MIME['.json'], '{"error":"no key"}');
    const [source, name] = key.split(':');
    if( source !== 'content' || !state.contentDir )
        return send(response, 404, MIME['.json'], '{"error":"unknown source"}');

    const ifPath = join(state.contentDir, 'interfaces', `${name}.if`);
    if( !existsSync(ifPath) )
        return send(response, 404, MIME['.json'], `{"error":"no ${name}.if"}`);

    const ifText = readFileSync(ifPath, 'utf8');
    const compackPath = join(state.contentDir, 'interfaces', `${name}.compack`);
    const roots = hookScriptIds(parseIf(ifText));
    const { scripts, cs2Source, errors } = lowerClosure(state, roots);

    send(response, 200, MIME['.json'], JSON.stringify({
        name,
        interfaceId: readPack(join(state.contentDir, 'pack', '3_interfaces.pack')).get(name) ?? -1,
        records: {
            if: ifText,
            compack: existsSync(compackPath) ? readFileSync(compackPath, 'utf8') : '',
            cs2: cs2Source,
            js: Object.values(scripts).join('\n\n'),
        },
        scriptIds: roots,
        errors,
    }));
}

/* -------------------------------------------------------------------------
 * Watching
 * ---------------------------------------------------------------------- */

/*
 * The edit loop.
 *
 * An edit to a `.if` or a `.cs2` is an edit to a TEXT tree, and the client
 * reads a packed cache. Something has to do the packing, and it has to be
 * cachepack -- the same tool the real bake uses -- or the preview would be
 * showing bytes no bake will ever produce, which is the whole failure this
 * rework exists to stop repeating at a different altitude.
 *
 *     edit  ->  cachepack pack --asset-only  ->  preview cache  ->  reboot
 *
 * `--asset-only` writes tables 3 and 12 into a cache that already exists and
 * leaves every other record alone, so the preview cache is a copy of the real
 * one with this tree's interfaces and scripts written over it.
 */
function watchContent(state) {
    for( const sub of ['interfaces', 'scripts'] )
    {
        const dir = join(state.contentDir, sub);
        if( !existsSync(dir) ) continue;
        watchDir(dir, true, () => {
            state.catalogue = null;
            bake(state).catch((error) => state.log(`  bake      ${error.message}`));
        });
    }
}

/**
 * The preview cache: a copy of the real one, written into rather than over.
 *
 * Copied rather than packed from scratch because a cache is 218MB of records
 * this tool has no opinion about, and copied ONCE rather than per-boot because
 * that is several seconds every time you start the server. Refreshed only when
 * the real cache is newer than the copy.
 *
 * That it is a copy is also the safety property: no edit in this tool can reach
 * the cache the rest of the repo builds against.
 */
function ensurePreviewCache(state) {
    if( !state.cache || !state.contentDir ) return null;
    const preview = join(state.root, 'build', 'preview-cache');
    const stamp = join(preview, 'main_file_cache.dat2');
    const source = join(state.cache, 'main_file_cache.dat2');
    if( !existsSync(source) ) return null;

    const fresh = existsSync(stamp)
        && statSync(stamp).mtimeMs >= statSync(source).mtimeMs;
    if( !fresh )
    {
        state.log(`  cache     copying ${state.cache} -> build/preview-cache (once)`);
        mkdirSync(preview, { recursive: true });
        cpSync(state.cache, preview, { recursive: true });
    }
    return preview;
}

/**
 * Pack the content tree into the preview cache and tell the page to reboot.
 *
 * Serialised on `state.baking`: an editor save fires several writes and a
 * second cachepack over the same output while the first is still writing it
 * produces a cache that is neither version.
 */
function packOnce(state) {
    const started = Date.now();
    let result;
    try
    {
        result = packCache({
            cachepack: state.cachepack,
            contentDir: state.contentDir,
            out: state.previewCache,
            revision: state.revision,
            assets: 'interfaces,scripts',
        });
    }
    catch( error ) { result = { status: 1, output: String(error.message) }; }

    if( result.status !== 0 )
    {
        state.bakeError = lastLines(result.output, 6);
        state.log(`  bake      failed\n${state.bakeError}`);
    }
    else
    {
        state.bakeError = null;
        state.bakedAt = Date.now();
        state.log(`  bake      ${Date.now() - started}ms`);
    }
    return result.status === 0;
}

async function bake(state) {
    if( !state.previewCache ) { announce(state, 'changed'); return; }
    if( state.baking ) { state.bakeAgain = true; return; }
    state.baking = true;

    const ok = packOnce(state);
    state.baking = false;

    if( !ok )
    {
        /* The page shows this: a bake that failed and reloaded anyway would
         * show the PREVIOUS interface and look like the edit did nothing. */
        announce(state, 'bake-failed');
    }
    else
    {
        /*
         * RESTART io_server before telling anyone.
         *
         * It opened the cache when it started and holds it open, with its own
         * decoded records behind that -- so a cachepack that rewrote the dat2
         * underneath it changes nothing that it will say. The client rebooted
         * happily against the old bytes and the edit looked like it had not
         * happened, with the bake reporting success 3.2 seconds earlier.
         *
         * Cheap: the cache is opened lazily and the process is small. Doing it
         * here, before the announcement, is what makes the page's reboot land
         * on a server that will answer with the new bytes.
         */
        await restartIoServer(state);
        announce(state, 'baked');
    }

    if( state.bakeAgain ) { state.bakeAgain = false; await bake(state); }
}

function lastLines(text, count) {
    return String(text ?? '').trim().split('\n').slice(-count).join('\n');
}

/*
 * This tool's own sources RELOAD the page rather than refreshing a pane: the
 * page imports each module once per document, so anything short of a reload
 * runs the old code against the new state, silently.
 */
function watchSource(state) {
    const dir = join(state.root, 'src');
    if( !existsSync(dir) ) return;
    watchDir(dir, true, () => announce(state, 'reload'));
}

/* Coalesced: an editor save fires several events for one write, and acting per
 * event would restart the preview mid-boot. */
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
    response.write(`event: hello\ndata: ${state.boot}\n\n`);
    const notify = (kind) => response.write(`event: ${kind}\ndata: 1\n\n`);
    state.listeners.add(notify);
    response.on('close', () => state.listeners.delete(notify));
}

function send(response, status, type, body) {
    response.writeHead(status, { 'content-type': type, 'cache-control': 'no-store' });
    response.end(body);
}

export { catalogue, readPack };
