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
 * cache's bitmap font and a model is a labelled box. The fidelity path is unchanged
 * — `cs2dom build` then a bake, into the real client. What this buys is the twenty
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

const DEBOUNCE_MS = 40;

export function serve(project, { port = 8099, open = true, log = console.log } = {}) {
    let current = compile(project);
    const clients = new Set();
    const sprites = spriteIndex(project.content);

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
            return send(response, 200, 'application/json', JSON.stringify(view(current, state)));
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
            const id = Number.parseInt(basename(url.pathname, '.png'), 10);
            const png = spritePng(project.content, sprites, id);
            if( !png ) return send(response, 404, 'text/plain', 'no such sprite');
            return send(response, 200, 'image/png', png);
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
        log(`  content  ${project.content}`);
        if( current.error ) log(`  ✗ ${current.error.message.split('\n')[0]}`);
        else log(`  ✓ ${current.results.map((r) => r.name).join(', ') || 'no components yet'}`);
        if( open ) openBrowser(address);
    });

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
function view(current, stateJson) {
    if( current.error )
        return { error: current.error.message, interfaces: [] };

    let state = {};
    try { if( stateJson ) state = JSON.parse(stateJson); } catch { /* fall back to defaults */ }

    return {
        error: null,
        warnings: current.warnings,
        interfaces: current.results.map((result) => {
            /* Host reads the preview cannot answer are collected while laying out,
             * so the page can say which values it is showing as zero. */
            const unmodelled = new Set();
            const boxes = layout(result.ir, state, undefined, unmodelled);
            return {
            name: result.name,
            interfaceId: result.interfaceId,
            file: result.file,
            boxes,
            unmodelled: [...unmodelled],
            inputs: stateInputs(result.ir),
            interfaceText: result.interfaceText,
            compackText: result.compackText,
            scripts: result.scripts,
            }; }),
    };
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
