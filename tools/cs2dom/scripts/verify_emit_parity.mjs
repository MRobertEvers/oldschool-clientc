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
    createHostKernel, HostState, HostClock, StoreAssetSource, UnimplementedHostOp,
} from '../src/host_kernel.js';
import { FontStore, SpriteStore } from '../src/assets.js';
import { createContentAssets } from '../src/content_assets.js';
import { createDriver, ScriptRegistry } from '../src/cs2_driver.js';
import { attachLayout } from '../src/layout.js';
import { createHitTester } from '../src/hit_test.js';
import { attachTransmitPump } from '../src/transmit_pump.js';
import { createEmitter } from '../src/emit.js';
import { compareEmit, normalizeJsCommands } from '../src/emit_parity.js';
import { createDbState, parseDbTextData } from '../src/host_db.js';
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
const astDir = resolve(flag('--ast') ?? join(HERE, '..', 'build', 'ast'));
const verbose = args.includes('--verbose');

/* The C client renders at this size; laying out against anything else moves
 * every proportional box and makes the comparison meaningless. */
const ROOT = { x: 0, y: 0, width: 765, height: 503 };

/*
 * Where the pointer is, and it is NOT nowhere.
 *
 * A headless SDL client still has a mouse, and it sits at the origin. That is
 * observable: `cr_ui`'s eight resize handles arm
 * `cc_setonmouserepeat("cc_settrans(event_com, event_comsubid, 200, -1)")`,
 * the top-left one covers (-1,-1)..(4,4), and the reference fades it in every
 * frame. Comparing against a run with no pointer at all left that handle at
 * its authored trans=255 and drew nothing where the reference draws one
 * command.
 */
const MOUSE = { x: 0, y: 0 };

/* The captured reference is frame 60 of the C client; see `run`. */
const TICKS = flag('--ticks') ? Number(flag('--ticks')) : null;
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
 * The DBTABLE / DBROW pair, because ten list panels are BUILT from a query.
 *
 * `music_init` opens with `db_find_with_count(music:hidden, 0)` and creates one
 * row per result; against an empty database that is zero rows, and the panel
 * the reference draws with 914 commands drew with 57. The quest list, the
 * hiscores, the minigame list and the recipe books are all the same shape.
 */
const dbData = (() => {
    const read = (name) => {
        const path = join(contentDir, 'configs', name);
        return existsSync(path) ? readFileSync(path, 'latin1') : '';
    };
    return parseDbTextData({
        tableText: read('all.dbtable'),
        rowText: read('all.dbrow'),
        tableCompackText: read('all.dbtable.compack'),
        rowCompackText: read('all.dbrow.compack'),
    });
})();

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
function createLoader(tree, driver, closure) {
    return {
    loadSync(kind, id) {
        if( kind === 'component' )
        {
            if( tree.hasGroup(id) ) return true;
            const group = bakeGroup(tree, id);
            if( !group ) return false;
            /* The new group's own hooks, before anything can dispatch one. */
            closure.extend([...group.onLoad.map((entry) => entry.scriptId),
                ...treeHookScripts(tree)]);
            /*
             * Baked, NOT opened — its onload does not run here.
             *
             * A `cc_find`/`cc_create`/`if_getlayer` that names a component in
             * another group is a cross-interface reference, and all the
             * reference does for one is bring the PACK into the tree. Opening
             * is a separate act (`if_openwidget`), and only opening runs
             * onload.
             *
             * Running it here mounted whatever the reference had merely
             * loaded: `mm_overlay` reaches into `interface_163` for the
             * private-message overlay, so this ran `pm_init`, which created 20
             * dynamic components the reference never had. The dynamic-uid
             * cursor is tree-global, so every later id in 882 came out 20
             * high and the whole draw list mismatched on componentId.
             *
             * And its roots are HIDDEN, which is `hide_unmounted_spillover`:
             * a pack the runtime baked ahead of a mount that never came is
             * spillover, and the reference sweeps every root whose group is
             * not the one being opened. Left visible, `toplevel_osrs_stretch`
             * painted its whole tab strip over seven unrelated interfaces.
             */
            for( const node of tree.nodes )
            {
                if( node.freed || node.parent >= 0 ) continue;
                if( ((node.componentId >>> 16) & 0xffff) !== (id & 0xffff) ) continue;
                tree.setHidden(node.index, true);
            }
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

/**
 * An on-disk syntax-tree directory, indexed by script id.
 *
 * `make corpus-aot` already writes every tree the decompiler can produce into
 * `build/ast`, and reading one is a thousand times cheaper than shelling out
 * to `cs2 decompile` for it. The files are named by SCRIPT NAME, so the index
 * is built once by reading each file's `"id"` — 9,724 small reads, which is
 * still far less than one decompile.
 */
const astIndex = (() => {
    const index = new Map();
    if( !existsSync(astDir) ) return index;
    for( const file of readdirSync(astDir) )
    {
        if( !file.endsWith('.json') ) continue;
        const head = readFileSync(join(astDir, file), 'utf8').slice(0, 400);
        const match = /"id"\s*:\s*(\d+)/.exec(head);
        if( match ) index.set(Number(match[1]), join(astDir, file));
    }
    return index;
})();

function syntaxTree(id) {
    if( astCache.has(id) ) return astCache.get(id);
    const cached = astIndex.get(id);
    if( cached )
    {
        try
        {
            const tree = JSON.parse(readFileSync(cached, 'utf8'));
            astCache.set(id, tree);
            return tree;
        }
        catch { /* fall through to the decompiler */ }
    }
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

/**
 * Lower every script reachable from a set of roots into one registry.
 *
 * Incremental, because the roots are not all known at the start. `extend` is
 * called again for each group the loader bakes on demand, and a group brings
 * its own cache-authored hooks with it.
 */
function createClosure() {
    const registry = new ScriptRegistry();
    const missing = [];
    const seen = new Set();
    /*
     * Every source lowered so far, and the whole set is recompiled whenever
     * the set grows.
     *
     * The generated code calls its dependencies by bare name (`cs2_900(...)`),
     * which resolves in the module scope it was compiled in. Compiling each
     * batch as its own `new Function` therefore leaves batch 2 unable to see
     * batch 1 — `ReferenceError: cs2_900 is not defined`, thrown from inside a
     * running script, which the harness reports as an aborted script and not
     * as the packaging mistake it is.
     */
    const sources = [];

    function extend(roots) {
        const before = sources.length;
        const queue = [...roots];
        while( queue.length )
        {
            const id = queue.shift();
            if( !(id > 0) || seen.has(id) ) continue;
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
        if( sources.length === before ) return;

        const body = sources.map((source) => source.replace(/^export function\*/m, 'function*'))
            .join('\n');
        const exported = [...body.matchAll(/function\* (cs2_\d+)/g)].map((match) => match[1]);
        // eslint-disable-next-line no-new-func
        const factory = new Function('K', 'PARK', `${body}\nreturn { ${exported.join(', ')} };`);
        registry.addModule(factory(K, HOST_PARK));
    }

    return { registry, missing, extend };
}

/**
 * Every script the baked tree has a hook bound to.
 *
 * The closure cannot be seeded from `onload` alone. A cache record carries
 * `ontimer`, `onvartransmit`, `onop` and the rest as well, and nothing in an
 * onload script's body mentions them — `ge_pricechecker`'s pulsing overlay is
 * `ontimer=i:811` on the component and appears nowhere else, so the timer
 * dispatched into an empty registry and the widget kept its authored
 * `trans=255` forever, which reads as one missing sprite in the draw list.
 */
function treeHookScripts(tree) {
    const ids = [];
    for( const node of tree.nodes )
    {
        if( node.freed || !node.hooks ) continue;
        for( const binding of Object.values(node.hooks) )
            if( binding && binding.scriptId > 0 ) ids.push(binding.scriptId);
    }
    return ids;
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
    /* PARITY_CREATES=1 prints the dynamic uid the tree handed out, in order.
     * Diff it against the reference's `TORIRS_CS2_TRACE=1 | grep CC_CREATE`
     * column: the first uid that differs names the extra (or missing) build,
     * and the C trace's `script=` at that line names who ran it. */
    if( process.env.PARITY_CREATES )
    {
        const allocate = tree.allocateDynamicComponentId.bind(tree);
        tree.allocateDynamicComponentId = (group) => {
            const uid = allocate(group);
            console.error(`alloc 0x${(uid >>> 0).toString(16)}`);
            return uid;
        };
    }
    const baked = bakeGroup(tree, id);
    if( !baked ) return { id, name, skipped: 'no .if in the content tree' };
    const { onLoad } = baked;

    const closure = createClosure();
    const { registry, missing } = closure;
    closure.extend([...onLoad.map((entry) => entry.scriptId), ...treeHookScripts(tree)]);
    /*
     * Operations neither this host nor the reference implements are FAKED,
     * because that is what the reference did during the capture: its stack
     * stub balances the call, pushes zeros and empty strings, and announces
     * the opcode once. Throwing instead would compare a full C draw list
     * against a JS script that aborted at the first clan operation, and every
     * difference after that point would be an artefact of the comparison.
     */
    const fakedOps = new Set();
    /*
     * Frame-for-frame with the capture; see the settle loop.
     *
     * The clock starts at 100, which is where `RS_CS2Host` starts its own
     * (`host->client_clock = 100`), and the interface opens BEFORE the first
     * tick. Three frames therefore run the timers at 101, 102, 103 — the exact
     * sequence a bounded headless client now produces, once its logic pacer
     * ticks per frame instead of per wall clock.
     *
     * Scripts read this value directly, so it is not a free choice:
     * `ge_offers_side` schedules four fades at `clientclock + 3 - 2i` and
     * compares against `clientclock` on every tick.
     */
    const frames = Math.max(1, reference.frames | 0);
    const clock = new HostClock(100);
    /*
     * The boot varbit the client seeds before any script runs.
     *
     * `App_...` sets `features->varbit_interface_resizing` (17772) to 1
     * optimistically at boot, and scripts branch on it: `script7925` returns 0
     * without it, so `league_tasks` keeps its 512-wide default instead of the
     * 350 the resizable path computes.
     */
    const state = new HostState();
    state.setVarbit(17772, 1);
    const host = createHostKernel({
        tree, state, config: configs, fonts, clock,
        db: createDbState(dbData),
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
                ? `\n${new Error().stack.split('\n').slice(2, 14).join('\n')}` : '';
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
    if( process.env.PARITY_TRACE_DISPATCH )
    {
        const original = driver.dispatch.bind(driver);
        const counts = new Map();
        driver.dispatch = (scriptId, dispatchArgs, options) => {
            counts.set(`${scriptId}:${options?.reason}`,
                (counts.get(`${scriptId}:${options?.reason}`) ?? 0) + 1);
            process.on('exit', () => {});
            return original(scriptId, dispatchArgs, options);
        };
        driver.dispatchCounts = counts;
    }

    driver.loader = createLoader(tree, driver, closure);
    const layout = attachLayout(host, { root: ROOT });
    const hits = createHitTester({ tree, layout });
    let hovered = -1;
    const dispatchMouse = (componentId, slot) => {
        if( componentId < 0 ) return;
        const node = tree.findByComponentId(componentId);
        const binding = node?.hooks?.[slot];
        if( binding ) driver.dispatchHook(binding, { reason: 'mouse', componentId });
    };
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
    /*
     * ONE mount pass, not a fixed point.
     *
     * Iterating it until nothing new appears is the tempting version and it
     * is wrong: the reference dispatches once at mount and lets the ordinary
     * filtered pump handle everything after. Repeating it re-ran hooks whose
     * serial had not moved, and interface 600 went from an exact match to 144
     * differences.
     */
    if( MOUNT_TRANSMIT ) pump.dispatchAll();
    await driver.settle({ wait: false });

    /*
     * The spillover sweep, AFTER the mount scripts and not only at bake time.
     *
     * `task_interface_open` runs `hide_unmounted_spillover` as its step 10 —
     * after onload, after the sub-change hooks, after the inv/var dispatch —
     * so a script that reached into another group and un-hid its root has that
     * undone. `toplevel_display` does exactly that to `popout`: hiding the
     * pack when the loader baked it was not enough, because the script that
     * caused the bake un-hides it two statements later, and the whole popout
     * strip painted over five toplevel interfaces.
     */
    for( const node of tree.nodes )
    {
        if( node.freed || node.parent >= 0 || node.componentId < 0 ) continue;
        if( ((node.componentId >>> 16) & 0xffff) === (id & 0xffff) ) continue;
        tree.setHidden(node.index, true);
    }
    await driver.settle({ wait: false });


    /*
     * Then exactly as many ticks as the capture ran frames.
     *
     * Not "until the tree stops changing". The reference client's frame loop
     * IS its settle loop — `TORIRS_MAX_FRAMES=3` is all it got — so running
     * longer here compares a settled tree against an unsettled one. It is not
     * a theoretical difference: an animating widget never settles at all, and
     * `ge_pricechecker`'s pulse read `clientclock` 60 where the reference read
     * 3. Frame for frame is both simpler and closer: it took the corpus from
     * 711 exact to 715.
     */
    let ticks = 0;
    for( ; ticks < (TICKS ?? frames); ticks++ )
    {
        /*
         * The clock moves with the tick. `clientclock` is not an
         * implementation detail this runtime may leave at zero: scripts read
         * it. `ge_pricechecker`'s overlay is a pulse — `script811` recomputes
         * its transparency from `clientclock % 100` on every timer tick — so a
         * clock stuck at 0 held it at trans=100 where the reference, three
         * frames in, had 109.
         */
        clock.advance();
        pump.tick();
        /*
         * Then the pointer, after the timers — the order the reference's trace
         * shows. Enter and leave fire on a change; repeat fires every frame
         * the pointer is still over the same component.
         */
        {
            const target = hits.hoverTarget(MOUSE.x, MOUSE.y,
                { hoveredComponentId: hovered });
            if( target !== hovered )
            {
                dispatchMouse(hovered, 'onMouseLeave');
                hovered = target;
                dispatchMouse(hovered, 'onMouseOver');
            }
            dispatchMouse(hovered, 'onMouseRepeat');
        }
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
    }

    layout.resolve();
    emitter.walk({ force: true, hoveredComponentId: hovered });

    if( process.env.PARITY_FIND_NODE )
    {
        const [w, h] = process.env.PARITY_FIND_NODE.split('x').map(Number);
        for( const node of tree.nodes )
        {
            if( node.freed ) continue;
            if( (node.props.width | 0) !== w || (node.props.height | 0) !== h ) continue;
            const parent = node.parent >= 0 ? tree.at(node.parent) : null;
            console.error(`com=${node.componentId} sub=${node.subId} dyn=${node.dynamic}`
                + ` hidden=${node.hidden} type=${node.type}`
                + ` layout=${JSON.stringify(node.layout)} parent=${parent?.componentId}`
                + ` props=${JSON.stringify(node.props)}`.slice(0, 160));
        }
    }

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
        const count = Number(process.env.PARITY_DUMP_COUNT ?? 12);
        emitter.commands.slice(from, from + count).forEach((command, offset) => {
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

    if( process.env.PARITY_HOOKS )
    {
        const counts = new Map();
        for( const node of tree.nodes )
        {
            if( node.freed || !node.hooks ) continue;
            for( const [slot, binding] of Object.entries(node.hooks) )
            {
                if( !binding || binding.scriptId <= 0 ) continue;
                const key = `${binding.scriptId}:${slot}`;
                counts.set(key, (counts.get(key) ?? 0) + 1);
            }
        }
        console.error([...counts].filter(([k]) => /Transmit|Timer/.test(k))
            .sort((a, b) => b[1] - a[1]).slice(0, 16)
            .map(([k, n]) => `${k} x${n}`).join('\n'));
    }

    if( driver.dispatchCounts )
        console.error([...driver.dispatchCounts].sort((a, b) => b[1] - a[1])
            .slice(0, 20).map(([key, n]) => `${key} x${n}`).join('\n'));

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

    const jsCommands = normalizeJsCommands(emitter.commands);
    /* PARITY_DUMP=1 prints both lists side by side — the whole draw, not only
     * the fields that differ, which is what you need when a command is absent
     * on one side and everything after it slides. */
    if( process.env.PARITY_DUMP )
    {
        const line = (c) => c
            ? `${c.kind}#${c.componentId} @${c.x},${c.y} ${c.width}x${c.height} `
              + `scene=${c.scene} colour=${c.colour} trans=${c.trans}`
            : '-';
        const n = Math.max(reference.commands.length, jsCommands.length);
        for( let i = 0; i < n; i++ )
            console.error(`${String(i).padStart(3)}  C ${line(reference.commands[i])}`
                + `\n     J ${line(jsCommands[i])}`);
    }
    const result = compareEmit(reference.commands, jsCommands);
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
for( const reference of references )
{
    /*
     * One interface at a time, and each in a try: a corpus run must report
     * what the run FOUND, and an exception on interface 300 that took the
     * other 967 with it reports nothing at all.
     */
    try { results.push(await run(reference)); }
    catch( error ) { results.push({ id: reference.interface, threw: error.message }); }
    if( references.length > 8 && results.length % 50 === 0 )
        process.stderr.write(`compared ${results.length}/${references.length}\n`);
}

if( verbose )
    for( const result of results )
        for( const difference of (result._differences ?? []).slice(0, 40) )
            console.error(`${result.id} [${difference.at}] ${difference.field}: `
                + `C=${difference.expected} JS=${difference.actual}`);

const matching = results.filter((result) => result.matches).length;
const summary = {
    references: references.length,
    matching,
    /* Named, because "how many matched" is the headline and "which did not"
     * is the work. A corpus run prints the second as a list of ids with their
     * difference counts rather than the full per-interface report. */
    ...(references.length > 8
        ? {
            skipped: results.filter((result) => result.skipped).length,
            threw: results.filter((result) => result.threw).map((r) => r.id),
            /* What KIND of difference, across the whole corpus. One systemic
             * cause shows up here as a field with a large count; chasing
             * interfaces one at a time hides that. */
            fields: Object.fromEntries(Object.entries(results.reduce((totals, result) => {
                for( const entry of result.summary ?? [] )
                    totals[entry.field] = (totals[entry.field] ?? 0) + entry.count;
                return totals;
            }, {})).sort((a, b) => b[1] - a[1])),
            differing: results.filter((result) => !result.matches && !result.skipped
                && !result.threw)
                .sort((a, b) => a.differences - b.differences)
                .map((result) => ({
                    id: result.id, name: result.name,
                    c: result.cCommands, js: result.jsCommands,
                    prefix: result.alignment?.prefix, differences: result.differences,
                    top: result.summary?.[0]?.field,
                    example: result.summary?.[0]?.example,
                    faked: result.hostFaked?.length ? result.hostFaked : undefined,
                })),
        }
        : { results: results.map(({ _differences, ...rest }) => rest) }),
};
console.log(JSON.stringify(summary, null, 2));
