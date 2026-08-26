/*
 * The parity run: this runtime's draw list against the C client's.
 *
 * Bake the same interface from the same content tree, run its onload closure,
 * settle, resolve layout, walk emit — then diff against the reference
 * `capture_emit_reference.mjs` took from the headless C client.
 *
 * ------------------------------------------------------------------
 * What a difference means
 * ------------------------------------------------------------------
 *
 * Not every difference is a defect, and reporting them as though they were is
 * how a comparison gets muted. Three kinds are expected and are reported
 * separately from the rest:
 *
 *   ABSENT SCRIPT   the C client ran a closure this run could not lower or
 *                   could not find. Its widgets keep their authored values
 *                   instead of the ones the script set.
 *   ABSENT ASSET    a sprite or font the preview has not decoded. The widget
 *                   is laid out but not drawn, so it is missing from the list
 *                   rather than misplaced in it.
 *   UNHOSTED        content the browser preview does not host at all.
 *
 * Everything else is a real disagreement about layout, pruning, clipping or
 * draw order, and that is what this exists to find.
 *
 *   node scripts/verify_emit_parity.mjs [--content DIR] [--reference DIR]
 *        [--interface N] [--verbose]
 */

import { existsSync, readdirSync, readFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { execFileSync } from 'node:child_process';

import { bakeInterface } from '../src/if_to_tree.js';
import { createUITree } from '../src/uitree.js';
import { createContentConfigs } from '../src/content_configs.js';
import {
    createHostKernel, HostState, StoreAssetSource, UnimplementedHostOp,
} from '../src/host_kernel.js';
import { FontStore, SpriteStore } from '../src/assets.js';
import { createContentAssets } from '../src/content_assets.js';
import { createDriver, ScriptRegistry } from '../src/cs2_driver.js';
import { attachLayout } from '../src/layout.js';
import { attachTransmitPump } from '../src/transmit_pump.js';
import { createEmitter } from '../src/emit.js';
import { compareEmit, normalizeJsCommands } from '../src/emit_parity.js';
import { emitScript } from '../src/cs2_js_emit.js';
import * as K from '../src/cs2_intrinsics.js';
import { HOST_PARK } from '../src/generated/cs2_host_park.js';

const HERE = dirname(fileURLToPath(import.meta.url));
const REPO = resolve(join(HERE, '..', '..', '..'));

const args = process.argv.slice(2);
const contentDir = resolve(flag('--content') ?? join(REPO, 'OSRS-Content', 'osrs239-content'));
const referenceDir = resolve(flag('--reference') ?? join(HERE, '..', 'test', 'fixtures', 'emit'));
const cache = flag('--cache') ?? join(REPO, 'cache.osrs239');
const revision = flag('--rev') ?? 'osrs239';
const names = flag('--names') ?? join(REPO, '..', 'cs2', 'src', 'main', 'resources', 'org', 'runestar', 'cs2');
const cs2Tool = flag('--cs2') ?? join(REPO, '3rd', 'rscache', 'tools', 'cs2', 'cs2');
const only = flag('--interface') ? Number(flag('--interface')) : null;
const verbose = args.includes('--verbose');

/* The C client renders at this size; laying out against anything else moves
 * every proportional box and makes the comparison meaningless. */
const ROOT = { x: 0, y: 0, width: 765, height: 503 };

/* The captured reference is frame 60 of the C client; see `run`. */
const TICKS = Number(flag('--ticks') ?? 60);
const MOUNT_TRANSMIT = !args.includes('--no-mount-transmit');

/*
 * The cache tables, read once for the whole run.
 *
 * The reference client answered `inv_size`, `struct_param` and `oc_name` from
 * a real table; a runtime answering the miss default disagrees about layout
 * for a reason that has nothing to do with layout. What the tree cannot give
 * is reported (`configs.notes`), not filled in.
 */
const configs = createContentConfigs(contentDir);

/*
 * Real fonts, because a tooltip's width IS its measured text.
 *
 * `parawidth` with no metrics answers 0, and the spellbook's tooltip then
 * lays out 14 pixels wide against the reference's 46 — a layout difference
 * whose cause is not layout. The content tree's fonts are files on disk, so
 * they decode synchronously and the park is serviced without an await.
 */
const contentAssets = createContentAssets(contentDir);
const fonts = new FontStore({ decode: async (id) => contentAssets.font(id) });
const sprites = new SpriteStore({ decode: async (id) => contentAssets.sprite(id) });

/**
 * The loader, which also BAKES.
 *
 * A script reaching a component in a group that is not in the tree is not an
 * error — it is a mount, and the client answers it by loading that interface.
 * The chatbox's own var-transmit hook (script 924) reaches interface 163 on
 * its first run; refusing it left the driver parking on a group nothing would
 * ever supply, which its spin guard correctly reported as a hang.
 *
 * Baking on demand is what makes the comparison follow the same mount graph
 * the reference walked. The onload bindings of a newly baked group are queued
 * on the driver rather than run here, for the same reason the target
 * interface's are: they must not execute against a half-built tree.
 */
function createLoader(tree, driver) {
    return {
    loadSync(kind, id) {
        if( kind === 'component' )
        {
            if( tree.hasGroup(id) ) return true;
            const group = bakeGroup(tree, id);
            if( !group ) return false;
            for( const entry of group.onLoad )
                driver.dispatch(entry.scriptId, entry.args,
                    { reason: 'onload', componentId: entry.componentId });
            return true;
        }
        try
        {
            if( kind === 'font' )
            {
                const font = contentAssets.font(id);
                if( font ) fonts.put(id, font);
                return true;
            }
            if( kind === 'sprite' )
            {
                const sprite = contentAssets.sprite(id);
                if( sprite ) sprites.put(id, sprite);
                return true;
            }
        }
        catch { /* a font the tree does not carry is absent, not fatal */ }
        /*
         * Everything else is a config table, already read in whole. The load
         * is genuinely DONE; a record still missing afterwards is missing from
         * the source, and the host's spent-await rule completes the call with
         * its documented miss answer.
         */
        return true;
    },
    async load(kind, id) { return this.loadSync(kind, id); },
    };
}

function flag(name) {
    const index = args.indexOf(name);
    return index >= 0 && index + 1 < args.length ? args[index + 1] : null;
}

/* -------------------------------------------------------------------------
 * The interface pack, by id
 * ---------------------------------------------------------------------- */

const interfaceNames = readPack(join(contentDir, 'pack', '3_interfaces.pack'));

function readPack(path) {
    const out = new Map();
    if( !existsSync(path) ) return out;
    for( const raw of readFileSync(path, 'utf8').split('\n') )
    {
        const line = raw.replace(/\/\/.*$/, '').trim();
        const split = line.indexOf('=');
        if( split < 1 ) continue;
        const id = Number(line.slice(0, split));
        const name = line.slice(split + 1).trim().split(/\s+/)[0];
        if( Number.isInteger(id) && name ) out.set(id, name);
    }
    return out;
}

/* -------------------------------------------------------------------------
 * Lowering a closure, cached for the process
 * ---------------------------------------------------------------------- */

const astCache = new Map();

function syntaxTree(id) {
    if( astCache.has(id) ) return astCache.get(id);
    let tree = null;
    if( existsSync(cs2Tool) && existsSync(cache) )
    {
        try
        {
            const stdout = execFileSync(cs2Tool, [
                'decompile', '--cache', cache, '--rev', revision,
                ...(existsSync(names) ? ['--names', names] : []),
                '--emit', 'ast-json', '--quiet', String(id),
            ], { encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'] });
            const start = stdout.indexOf('{');
            if( start >= 0 ) tree = JSON.parse(stdout.slice(start));
        }
        catch { tree = null; }
    }
    astCache.set(id, tree);
    return tree;
}

/** Lower every script reachable from `roots` into one registry. */
function lowerClosure(roots) {
    const registry = new ScriptRegistry();
    const sources = [];
    const missing = [];
    const seen = new Set();
    const queue = [...roots];

    while( queue.length )
    {
        const id = queue.shift();
        if( seen.has(id) ) continue;
        seen.add(id);
        const ast = syntaxTree(id);
        if( !ast ) { missing.push(id); continue; }
        try
        {
            const result = emitScript(ast);
            sources.push(result.code);
            for( const dependency of [...result.procs, ...result.hooks] )
                if( !seen.has(dependency) ) queue.push(dependency);
        }
        catch { missing.push(id); }
    }

    if( sources.length )
    {
        const body = sources.map((source) => source.replace(/^export function\*/m, 'function*'))
            .join('\n');
        const exported = [...body.matchAll(/function\* (cs2_\d+)/g)].map((match) => match[1]);
        // eslint-disable-next-line no-new-func
        const factory = new Function('K', 'PARK', `${body}\nreturn { ${exported.join(', ')} };`);
        registry.addModule(factory(K, HOST_PARK));
    }
    return { registry, missing };
}

/* -------------------------------------------------------------------------
 * One interface
 * ---------------------------------------------------------------------- */

/**
 * Bake one interface group into `tree`, if the content tree has it.
 *
 * Returns its onload bindings; the CALLER dispatches them, because running
 * them here would execute scripts against a tree that is only half built when
 * the first of them fires.
 */
function bakeGroup(tree, groupId) {
    const name = interfaceNames.get(groupId);
    if( !name ) return null;
    const ifPath = join(contentDir, 'interfaces', `${name}.if`);
    if( !existsSync(ifPath) ) return null;
    const compackPath = join(contentDir, 'interfaces', `${name}.compack`);
    return bakeInterface({
        tree,
        ifText: readFileSync(ifPath, 'utf8'),
        compackText: existsSync(compackPath) ? readFileSync(compackPath, 'utf8') : '',
        interfaceId: groupId,
    });
}

async function run(reference) {
    const id = reference.interface;
    const name = interfaceNames.get(id);
    if( !name ) return { id, skipped: 'not in the interface pack' };

    const tree = createUITree();
    const baked = bakeGroup(tree, id);
    if( !baked ) return { id, name, skipped: 'no .if in the content tree' };
    const { onLoad } = baked;

    const { registry, missing } = lowerClosure(onLoad.map((entry) => entry.scriptId));
    /*
     * Operations neither this host nor the reference implements are FAKED,
     * because that is what the reference did during the capture: its stack
     * stub balances the call, pushes zeros and empty strings, and announces
     * the opcode once. Throwing instead would compare a full C draw list
     * against a JS script that aborted at the first clan operation, and every
     * difference after that point would be an artefact of the comparison.
     */
    const fakedOps = new Set();
    const host = createHostKernel({
        tree, state: new HostState(), config: configs, fonts,
        assets: new StoreAssetSource({ sprites, fonts, config: configs }),
        fakeUnimplemented: true,
        onUnimplemented: (method) => fakedOps.add(method),
    });
    /*
     * A script that reaches an operation this host has not transcribed is
     * ABORTED and named, not faked. The reference VM aborts the offending
     * script too, so the rest of the interface still builds — and one run then
     * names the whole gap instead of one operation per crash.
     */
    const hostGaps = new Set();
    const failedScripts = [];
    /*
     * `PARITY_TRACE=meth,meth` logs each call to those host methods. The
     * question a difference usually raises is "did this script reach this
     * operation at all", and that is not answerable from the draw list.
     */
    for( const method of (process.env.PARITY_TRACE ?? '').split(',').filter(Boolean) )
    {
        const original = host[method].bind(host);
        host[method] = (...args) => {
            const answer = original(...args);
            const where = process.env.PARITY_TRACE_WHERE
                ? `\n${new Error().stack.split('\n').slice(2, 6).join('\n')}` : '';
            console.error(`${method}(${args.map((a) => JSON.stringify(a)).join(', ')})`
                + ` = ${String(answer)}${where}`);
            return answer;
        };
    }

    if( process.env.PARITY_TRACE_TEXT )
    {
        for( const method of ['parawidth', 'paraheight'] )
        {
            const original = host[method].bind(host);
            /* eslint-disable-next-line no-loop-func */
            host[method] = (text, fontId, width) => {
                const answer = original(text, fontId, width);
                console.error(`${method}(text=${JSON.stringify(text)}, width=${fontId}, `
                    + `font=${width}) = ${String(answer)}`);
                return answer;
            };
        }
    }

    const driver = createDriver({
        host, scripts: registry, onWarning: () => {},
        /*
         * Everything the content tree can give is already in `configs`, so a
         * park is answered "loaded" and the retry completes with whatever
         * actually arrived. That is the honest shape: the load is DONE, and a
         * record still absent afterwards is absent from the source.
         */
        /* Filled in below: the loader queues onload bindings on the driver,
         * and the driver needs the loader to service a park. */
        loader: null,
        onScriptError: (error, request) => {
            if( error instanceof UnimplementedHostOp ) hostGaps.add(error.op);
            failedScripts.push({ script: request.scriptId, error: error.message });
            if( process.env.PARITY_SHOW_ERRORS ) console.error(`abort in ${request.scriptId}: ${error.stack}`);
        },
    });
    driver.loader = createLoader(tree, driver);
    const layout = attachLayout(host, { root: ROOT });
    driver.resolveLayout = () => layout.resolve();
    const emitter = createEmitter({ tree, layout });

    /*
     * The pump, because the REFERENCE ran one.
     *
     * The captured draw list is frame 60 of a real client: its transmit hooks
     * had fired, its timers had ticked, and every `if_callonresize` a script
     * queued had been drained. Dispatching onload once and settling reproduces
     * frame 1, and comparing frame 1 against frame 60 reports every hook that
     * has run since as a difference — which is how the chatbox's tab strip
     * came to sit two pixels left of the reference's, and why interface 600's
     * scrollbar was sized from a scroll extent nothing had recomputed.
     */
    const pump = attachTransmitPump(driver, tree);

    for( const entry of onLoad )
    {
        if( !registry.has(entry.scriptId) ) continue;
        /* The component the hook is bound to: `event_com` and its friends in
         * the argument list are resolved against it, and it is the cursor the
         * script starts with. */
        driver.dispatch(entry.scriptId, entry.args,
            { reason: 'onload', componentId: entry.componentId });
    }
    await driver.settle({ wait: false });

    /*
     * The MOUNT transmit pass, which the reference runs and a bake-then-settle
     * does not. A cache-authored transmit hook is the only thing that ever
     * paints some widgets, and at mount nothing has changed yet — so the
     * trigger filter would dispatch none of them.
     */
    if( MOUNT_TRANSMIT ) pump.dispatchAll();
    await driver.settle({ wait: false });

    /*
     * Then run ticks until the tree stops changing, bounded.
     *
     * Bounded rather than fixed at 60: a tree that has stopped changing is
     * settled by definition, and a run that never stops is a script re-arming
     * itself — which is a defect to report, not to out-wait. The captured
     * reference is 60 frames, so the cap matches it.
     */
    let ticks = 0;
    for( ; ticks < TICKS; ticks++ )
    {
        const before = tree.dirtyGeneration;
        pump.tick();
        /* `if_callonresize` is queued from inside a running script — there is
         * no runner to nest a second one on — so the queue is drained here,
         * which is where the C host drains its own. */
        for( const componentId of host.pendingResize.splice(0) )
        {
            const node = tree.findByComponentId(componentId);
            const binding = node?.hooks?.onResize;
            if( binding ) driver.dispatchHook(binding, { reason: 'resize', componentId });
        }
        // eslint-disable-next-line no-await-in-loop
        await driver.settle({ wait: false });
        if( tree.dirtyGeneration === before ) break;
    }

    layout.resolve();
    emitter.walk({ force: true });

    if( process.env.PARITY_DUMP_NODES )
    {
        for( const uid of process.env.PARITY_DUMP_NODES.split(',') )
        {
            const node = tree.findByComponentId(Number(uid));
            if( !node ) { console.error(`${uid}: absent`); continue; }
            const parent = node.parent >= 0 ? tree.at(node.parent) : null;
            console.error(`${uid} type=${node.type} layout=${JSON.stringify(node.layout)}`
                + ` parent=${parent ? parent.componentId : -1}`
                + ` parentLayout=${JSON.stringify(parent?.layout)}`
                + ` props=${JSON.stringify(node.props)}`);
        }
    }

    if( process.env.PARITY_DUMP_FROM )
    {
        const from = Number(process.env.PARITY_DUMP_FROM);
        emitter.commands.slice(from, from + 12).forEach((command, offset) => {
            const node = tree.at(command.node);
            console.error(`[${from + offset}] ${command.kind} com=${command.componentId} `
                + `box=${command.x},${command.y} ${command.width}x${command.height} `
                + `sub=${node.subId} dyn=${node.dynamic} `
                + `props=${JSON.stringify(node.props)}`);
        });
    }

    /* Which interface GROUPS the draw list came from. The reference's list for
     * every captured interface holds exactly one group, so a second group
     * appearing here means this run mounted something the reference did not —
     * a difference in what was built, not in how it was laid out. */
    const byGroup = {};
    for( const command of emitter.commands )
    {
        const group = command.componentId >= 0 ? (command.componentId >>> 16) & 0xffff : -1;
        byGroup[group] = (byGroup[group] ?? 0) + 1;
    }

    let visible = 0;
    const byType = {};
    let dynamic = 0;
    for( const node of tree.nodes )
    {
        if( node.freed ) continue;
        if( !node.hidden ) visible++;
        if( node.dynamic ) dynamic++;
        const key = `${node.type}${node.hidden ? '-hidden' : ''}`;
        byType[key] = (byType[key] ?? 0) + 1;
    }

    const result = compareEmit(reference.commands, normalizeJsCommands(emitter.commands));
    return {
        id, name,
        /* Counted because "no commands" has two very different causes: a tree
         * that was never built, and a tree whose nodes are all hidden. */
        nodes: tree.liveCount, visible, dynamic, byType, byGroup,
        onLoad: onLoad.length,
        ran: driver.stats.invocations,
        ticks,
        cCommands: result.expectedCount,
        jsCommands: result.actualCount,
        /* `prefix` commands identical from the top; `matched` identical once
         * the lists are slid `offset` apart — a big matched at a non-zero
         * offset means commands are missing, not misplaced. */
        alignment: result.alignment,
        scriptsMissing: missing.length,
        scriptsAborted: failedScripts.length,
        hostGaps: [...hostGaps].sort(),
        /* Balanced and answered with zeros, exactly as the reference did. */
        hostFaked: [...fakedOps].sort(),
        differences: result.differences.length,
        summary: result.summary.slice(0, 6).map((entry) => ({
            field: entry.field, count: entry.count,
            example: entry.first.field === 'present'
                ? { expected: entry.first.expected, actual: entry.first.actual }
                : { at: entry.first.at, expected: entry.first.expected, actual: entry.first.actual },
        })),
        matches: result.matches,
        _differences: result.differences,
    };
}

/* -------------------------------------------------------------------------
 * Run
 * ---------------------------------------------------------------------- */

if( !existsSync(referenceDir) )
{
    console.error(`no references at ${referenceDir}; run scripts/capture_emit_reference.mjs`);
    process.exit(2);
}

const references = readdirSync(referenceDir)
    .filter((file) => file.endsWith('.json'))
    .map((file) => JSON.parse(readFileSync(join(referenceDir, file), 'utf8')))
    .filter((reference) => only === null || reference.interface === only)
    .sort((a, b) => a.interface - b.interface);

const results = [];
for( const reference of references ) results.push(await run(reference));

if( verbose )
    for( const result of results )
        for( const difference of (result._differences ?? []).slice(0, 40) )
            console.error(`${result.id} [${difference.at}] ${difference.field}: `
                + `C=${difference.expected} JS=${difference.actual}`);

console.log(JSON.stringify({
    references: references.length,
    results: results.map(({ _differences, ...rest }) => rest),
    matching: results.filter((result) => result.matches).length,
}, null, 2));
