/* Real-wall-clock interaction latency corpus for cache interfaces.
 *
 * This diagnostic deliberately measures HostRuntime.dispatch() with
 * performance.now(). It does not use the deterministic/static projector and it
 * does not include rendering, snapshot encoding, compilation, or mount time in
 * an interaction sample.
 *
 * By default every readable OSRS-Content .if record is compiled and mounted.
 * Useful narrower invocations:
 *
 *   CS2DOM_CORPUS_FILTER=bankmain,pirate_combilock \
 *     node test/interface_latency_corpus_test.js
 *   CS2DOM_CORPUS_SHARD=0/8 node test/interface_latency_corpus_test.js
 *   CS2DOM_CORPUS_HARD_FAIL=1 node test/interface_latency_corpus_test.js
 *
 * Target sampling deduplicates components which expose the same hook scripts,
 * operation indexes, and interaction traits. Set CS2DOM_CORPUS_TARGET_LIMIT=0
 * to exercise every live target instead of the representative corpus.
 */

import { existsSync, readFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { performance } from 'node:perf_hooks';
import { isMainThread, parentPort, Worker, workerData } from 'node:worker_threads';
import { fileURLToPath } from 'node:url';

import { compileInterfaceProgram } from '../src/bytecode.js';
import { contentInterfaceCatalog, openContentInterface } from '../src/content.js';
import { prepareDat2Project } from '../src/dat2.js';
import { contentHostData } from '../src/host_data.js';
import { createHostRuntime } from '../src/host_runtime.js';
import { createWasmCS2Runtime } from '../src/wasm_runtime.js';

const HERE = dirname(fileURLToPath(import.meta.url));
const REPO = resolve(HERE, '../../..');
const CONTENT = process.env.CS2DOM_CORPUS_CONTENT
    ? resolve(process.env.CS2DOM_CORPUS_CONTENT)
    : join(REPO, 'OSRS-Content', 'osrs239-content');
const CACHE = process.env.CS2DOM_CORPUS_CACHE
    ? resolve(process.env.CS2DOM_CORPUS_CACHE) : join(REPO, 'cache.osrs239');
const WASM = resolve(HERE, '../web/cs2vm_wasm.wasm');
const MODULE_URL = new URL('../web/cs2vm_wasm.js', import.meta.url).href;
const EXPECTED_INTERFACE_COUNT = 968;
const THRESHOLD_MS = positiveNumber(process.env.CS2DOM_CORPUS_THRESHOLD_MS, 10);
const TARGET_LIMIT = nonnegativeInteger(process.env.CS2DOM_CORPUS_TARGET_LIMIT, 24);
const HARD_FAIL = process.env.CS2DOM_CORPUS_HARD_FAIL === '1';
const FAST_HOST = process.env.CS2DOM_WASM_FAST_HOST !== '0';
const PROFILE = process.env.CS2DOM_CORPUS_PROFILE === '1';
const INTERFACE_TIMEOUT_MS = positiveNumber(
    process.env.CS2DOM_CORPUS_INTERFACE_TIMEOUT_MS, 120_000);
const PROGRESS_EVERY = positiveInteger(process.env.CS2DOM_CORPUS_PROGRESS_EVERY, 25);
const MAX_PRINTED_ROWS = positiveInteger(process.env.CS2DOM_CORPUS_MAX_PRINT, 100);
const EVENT_FILTER = wordSet(process.env.CS2DOM_CORPUS_EVENTS);

const POINTER_HOOKS = new Set([
    'onmouseover', 'onmouseleave', 'onmouseenter', 'onmouseexit',
    'onmousedown', 'onmouseup', 'onclick', 'onclickrepeat', 'onhold', 'onrelease',
    'onop', 'ondrag', 'ondragcomplete', 'ondragstart', 'ondragrelease',
]);
const WHEEL_HOOKS = new Set(['onscrollwheel', 'onwheel']);
const DRAG_HOOKS = new Set(['ondrag', 'ondragcomplete', 'ondragstart', 'ondragrelease']);
const KEY_HOOKS = new Set([
    'onkey', 'onkeypress', 'onkeydown', 'onkeyup', 'onkeyheld', 'onkeyrelease',
]);
const TIMER_HOOKS = new Set(['ontimer']);
const PRIORITY_TARGET = /(?:search|quantity5|unlock|combi|confirm|submit|continue)/i;

if( isMainThread ) {
    const summary = await supervise();
    printSummary(summary);
    if( HARD_FAIL && (summary.outliers.length || summary.dispatchErrors.length ||
                      summary.mountFailures.length || summary.unavailablePrograms.length) )
        process.exitCode = 1;
} else if( workerData?.interfaceLatencyCorpus ) {
    await corpusWorker();
}

function supervise() {
    return new Promise((resolvePromise, rejectPromise) => {
        const worker = new Worker(new URL(import.meta.url), {
            workerData: { interfaceLatencyCorpus: true },
        });
        let settled = false;
        let current = 'corpus boot';
        let timer = setTimeout(() => fail(new Error(
            `${current} exceeded ${INTERFACE_TIMEOUT_MS}ms`)), INTERFACE_TIMEOUT_MS);

        function arm(label) {
            current = label;
            clearTimeout(timer);
            timer = setTimeout(() => fail(new Error(
                `${current} exceeded ${INTERFACE_TIMEOUT_MS}ms`)), INTERFACE_TIMEOUT_MS);
        }
        function finish(error, value) {
            if( settled ) return;
            settled = true;
            clearTimeout(timer);
            worker.terminate().finally(() => error ? rejectPromise(error) : resolvePromise(value));
        }
        function fail(error) { finish(error); }

        worker.on('message', (message) => {
            if( message?.type === 'activity' ) {
                arm(message.label);
                if( message.progress ) console.log(message.progress);
            } else if( message?.type === 'done' ) finish(null, message.summary);
            else if( message?.type === 'failed' ) fail(new Error(
                `interface latency corpus failed during ${message.label || current}: ${message.message}`));
        });
        worker.on('error', fail);
        worker.on('exit', (code) => {
            if( !settled ) fail(new Error(
                `interface latency corpus worker exited ${code} during ${current}`));
        });
    });
}

async function corpusWorker() {
    const started = performance.now();
    let activeLabel = 'corpus boot';
    try {
        assert(existsSync(CONTENT), `OSRS-Content is unavailable at ${CONTENT}`);
        assert(existsSync(WASM), `C CS2VM WebAssembly is unavailable at ${WASM}`);
        const catalog = contentInterfaceCatalog(CONTENT, { source: 'content' })
            .sort((left, right) => left.interfaceId - right.interfaceId);
        assert(catalog.length === EXPECTED_INTERFACE_COUNT,
            `expected ${EXPECTED_INTERFACE_COUNT} readable .if interfaces, found ${catalog.length}`);
        const selected = selectCatalog(catalog);
        const compileProject = existsSync(join(CACHE, 'main_file_cache.dat2'))
            ? prepareDat2Project({
                cache: CACHE,
                content: CONTENT,
                unpackedContent: CONTENT,
                revision: 'osrs239',
            })
            : { content: CONTENT, unpackedContent: CONTENT, revision: 'osrs239' };
        const hostDataBefore = performance.now();
        const sharedHostData = deepFreeze(contentHostData(CONTENT));
        const hostDataMs = performance.now() - hostDataBefore;
        const wasmUrl = `data:application/wasm;base64,${readFileSync(WASM).toString('base64')}`;
        const summary = {
            schema: 'cs2dom-interface-latency-corpus-v1',
            clock: 'performance.now/raw HostRuntime.dispatch wall time',
            instrumented: PROFILE,
            thresholdMs: THRESHOLD_MS,
            fastHost: FAST_HOST,
            catalogCount: catalog.length,
            selectedCount: selected.length,
            filter: process.env.CS2DOM_CORPUS_FILTER || '',
            shard: process.env.CS2DOM_CORPUS_SHARD || '0/1',
            targetLimit: TARGET_LIMIT,
            availablePrograms: 0,
            unavailablePrograms: [],
            fallbackInterfaces: [],
            fallbackRecords: 0,
            mountedInterfaces: 0,
            mountFailures: [],
            discovered: {
                components: 0, liveComponents: 0, interactiveComponents: 0,
                hooks: 0, ops: 0, pointerTargets: 0, wheelTargets: 0,
                dragTargets: 0, sampledPointerTargets: 0,
                sampledWheelTargets: 0, sampledDragTargets: 0,
            },
            sessions: 0,
            hooks: 0,
            hostRequests: 0,
            dispatches: 0,
            dispatchMs: 0,
            rawMax: null,
            outliers: [],
            dispatchErrors: [],
            skippedTargets: [],
            compileMs: 0,
            mountMs: 0,
            hostDataMs,
            elapsedMs: 0,
        };

        for( let index = 0; index < selected.length; index++ ) {
            const record = selected[index];
            activeLabel = `${record.name} (${index + 1}/${selected.length})`;
            const progress = index === 0 || (index + 1) % PROGRESS_EVERY === 0
                ? `corpus ${index + 1}/${selected.length}: ${record.name}` : '';
            parentPort.postMessage({ type: 'activity', label: activeLabel, progress });
            await auditInterface(record, compileProject, sharedHostData, wasmUrl, summary);
        }

        summary.outliers.sort((left, right) => right.ms - left.ms);
        summary.dispatchErrors.sort(compareIdentity);
        summary.mountFailures.sort(compareIdentity);
        summary.elapsedMs = performance.now() - started;
        parentPort.postMessage({ type: 'done', summary });
    } catch( error ) {
        parentPort.postMessage({
            type: 'failed', label: activeLabel, message: error?.stack || error?.message || String(error),
        });
    }
}

async function auditInterface(record, compileProject, hostData, wasmUrl, summary) {
    let imported;
    let program;
    const compileBefore = performance.now();
    try {
        imported = openContentInterface(CONTENT, record.name, { source: 'content' });
        program = compileInterfaceProgram(compileProject, imported);
    } catch( error ) {
        summary.compileMs += performance.now() - compileBefore;
        summary.unavailablePrograms.push(identity(record, {
            error: error?.message || String(error),
        }));
        return;
    }
    summary.compileMs += performance.now() - compileBefore;
    if( !program.available ) {
        summary.unavailablePrograms.push(identity(record, {
            warnings: program.warnings || [],
        }));
        return;
    }
    summary.availablePrograms++;
    if( program.fallbacks?.length ) {
        summary.fallbackRecords += program.fallbacks.length;
        summary.fallbackInterfaces.push(identity(record, {
            records: program.fallbacks.map(({ id, name, reason }) => ({ id, name, reason })),
        }));
    }

    let current = null;
    let invalidated = false;
    const makeSession = async () => {
        const mountBefore = performance.now();
        const metrics = { hooks: 0, hostRequests: 0 };
        const session = { host: null, wasm: null, metrics };
        try {
            session.host = createHostRuntime(imported.ir, {
                viewport: { width: 512, height: 334 },
                hostData,
                recordChanges: false,
                invoke(intent) {
                    metrics.hooks++;
                    const result = session.wasm.invokeIntent(intent);
                    metrics.hostRequests += result.hostRequests;
                    return result;
                },
            });
            /* Omitting moduleFactory intentionally uses wasm_runtime's cached
             * module bundle. Every fresh C session below therefore shares one
             * compiled Emscripten/WASM module and immutable host-data graph. */
            session.wasm = await createWasmCS2Runtime({
                program,
                host: session.host,
                moduleUrl: MODULE_URL,
                wasmUrl,
                fastHost: FAST_HOST,
            });
            exposeInvocationErrorCode(session.wasm);
            session.profile = PROFILE ? installDispatchProfile(session) : null;
            session.host.mount();
            summary.sessions++;
            summary.mountMs += performance.now() - mountBefore;
            return session;
        } catch( error ) {
            session.wasm?.destroy();
            summary.mountMs += performance.now() - mountBefore;
            throw error;
        }
    };
    const destroyCurrent = () => {
        if( !current ) return;
        summary.hooks += current.metrics.hooks;
        summary.hostRequests += current.metrics.hostRequests;
        current.wasm?.destroy();
        current = null;
    };
    const ensureCurrent = async (fresh = false) => {
        if( fresh || invalidated ) destroyCurrent();
        if( !current ) current = await makeSession();
        invalidated = false;
        return current;
    };

    try {
        current = await makeSession();
    } catch( error ) {
        summary.mountFailures.push(identity(record, errorDetail(error)));
        return;
    }
    summary.mountedInterfaces++;

    try {
        const discovery = discover(current.host);
        mergeDiscovery(summary.discovered, discovery);
        const pointerTargets = sampleTargets(discovery.pointerTargets, 'pointer');
        const wheelTargets = sampleTargets(discovery.wheelTargets, 'wheel');
        const dragTargets = sampleTargets(discovery.dragTargets, 'drag');
        summary.discovered.sampledPointerTargets += pointerTargets.length;
        summary.discovered.sampledWheelTargets += wheelTargets.length;
        summary.discovered.sampledDragTargets += dragTargets.length;

        const timed = (session, componentLabel, scenario, event, eventLabel = event.type) => {
            const beforeHooks = session.metrics.hooks;
            const beforeHostRequests = session.metrics.hostRequests;
            const beforeVersion = session.host.version;
            session.profile?.begin();
            const before = performance.now();
            let result = null;
            let error = null;
            try {
                result = session.host.dispatch(event);
                if( result && typeof result.then === 'function' )
                    throw new Error('HostRuntime.dispatch returned a Promise; sample is not synchronous');
            } catch( caught ) { error = caught; }
            const ms = performance.now() - before;
            const dispatchProfile = session.profile?.end() || undefined;
            const row = identity(record, {
                component: componentLabel,
                scenario,
                event: eventLabel,
                ms,
                hooks: session.metrics.hooks - beforeHooks,
                hostRequests: session.metrics.hostRequests - beforeHostRequests,
                versions: session.host.version - beforeVersion,
                profile: dispatchProfile,
            });
            summary.dispatches++;
            summary.dispatchMs += ms;
            if( !summary.rawMax || ms > summary.rawMax.ms ) summary.rawMax = row;
            if( ms >= THRESHOLD_MS ) summary.outliers.push(row);
            if( error ) {
                summary.dispatchErrors.push({ ...row, ...errorDetail(error) });
                invalidated = true;
            }
            return { result, error, row };
        };

        const targetScenario = async (locator, scenario, operation) => {
            let session;
            try { session = await ensureCurrent(); }
            catch( error ) {
                summary.mountFailures.push(identity(record, {
                    scenario, ...errorDetail(error),
                }));
                invalidated = true;
                return;
            }
            let component = session.host._component(locator.componentId, false);
            let point = component && pointFor(session.host, component);
            if( !component || !point ) {
                try {
                    session = await ensureCurrent(true);
                    component = session.host._component(locator.componentId, false);
                    point = component && pointFor(session.host, component);
                } catch( error ) {
                    summary.mountFailures.push(identity(record, {
                        scenario, ...errorDetail(error),
                    }));
                    invalidated = true;
                    return;
                }
            }
            if( !component || !point ) {
                summary.skippedTargets.push(identity(record, {
                    component: locator.label, scenario,
                    reason: component ? 'not hittable after fresh mount' : 'missing after fresh mount',
                }));
                return;
            }
            const beforeVersion = session.host.version;
            await operation(session, component, point, (event, label = event.type) =>
                timed(session, locator.label, scenario, event, label));
            if( session.host.version !== beforeVersion ) invalidated = true;
        };

        if( eventEnabled('tick') ) {
            const session = await ensureCurrent();
            const beforeVersion = session.host.version;
            timed(session, discovery.globalLabel, 'idle-tick', { type: 'tick', cycle: 1 });
            if( session.host.version !== beforeVersion ) invalidated = true;
        }

        if( eventEnabled('pointer') ) for( const locator of pointerTargets ) {
            await targetScenario(locator, 'pointer-left', async (session, component, point) => {
                const run = (event, label = event.type) =>
                    timed(session, locator.label, 'pointer-left', event, label);
                run({ type: 'pointer_move', ...point }, 'pointer_enter');
                run({ type: 'pointer_down', ...point, button: 0 }, 'left_down');
                run({ type: 'tick', cycle: 2 }, 'left_tick');
                run({ type: 'pointer_up', ...point, button: 0 }, 'left_up');
                run({ type: 'pointer_move', x: -1, y: -1 }, 'pointer_leave');
            });
        }

        if( eventEnabled('pointer') ) for( const locator of pointerTargets ) {
            await targetScenario(locator, 'pointer-middle', async (session, component, point) => {
                timed(session, locator.label, 'pointer-middle',
                    { type: 'pointer_move', ...point });
                timed(session, locator.label, 'pointer-middle',
                    { type: 'pointer_down', ...point, button: 1 });
                timed(session, locator.label, 'pointer-middle',
                    { type: 'pointer_up', ...point, button: 1 });
                timed(session, locator.label, 'pointer-middle',
                    { type: 'pointer_move', x: -1, y: -1 });
            });
        }

        if( eventEnabled('focus') && pointerTargets.length ) {
            const locator = pointerTargets[0];
            await targetScenario(locator, 'pointer-cancel', async (session, component, point) => {
                timed(session, locator.label, 'pointer-cancel',
                    { type: 'pointer_move', ...point });
                timed(session, locator.label, 'pointer-cancel',
                    { type: 'pointer_down', ...point, button: 0 });
                /* Browser blur/cancel is normalized to focus_lost by dev_page. */
                timed(session, locator.label, 'pointer-cancel', { type: 'focus_lost' });
            });
        }

        if( eventEnabled('menu') ) for( const locator of pointerTargets.filter((target) =>
            target.hasOps || target.hasOpHook) ) {
            const exposed = [];
            await targetScenario(locator, 'pointer-right-menu-discover', async (session, component, point) => {
                timed(session, locator.label, 'pointer-right-menu',
                    { type: 'pointer_move', ...point });
                const opened = timed(session, locator.label, 'pointer-right-menu',
                    { type: 'pointer_down', ...point, button: 2 });
                const entries = opened.result?.menu || session.host.menuAt(point.x, point.y);
                for( const entry of entries ) {
                    const componentId = Number(entry.component?.componentId ?? entry.component);
                    if( Number.isInteger(componentId) && Number.isInteger(entry.opIndex) &&
                        entry.opIndex > 0 && !exposed.some((candidate) =>
                            candidate.componentId === componentId &&
                            candidate.opIndex === entry.opIndex) ) exposed.push({
                        componentId, opIndex: entry.opIndex,
                    });
                }
                if( !exposed.length ) for( const opIndex of locator.opIndexes )
                    exposed.push({ componentId: locator.componentId, opIndex });
                timed(session, locator.label, 'pointer-right-menu-discover',
                    { type: 'menu_close' });
                timed(session, locator.label, 'pointer-right-menu',
                    { type: 'pointer_up', ...point, button: 2 });
            });
            for( const exposedOp of exposed ) await targetScenario(
                locator, `menu-op-${exposedOp.opIndex}`, async (session, component, point) => {
                    timed(session, locator.label, `menu-op-${exposedOp.opIndex}`,
                        { type: 'pointer_move', ...point });
                    const opened = timed(session, locator.label, `menu-op-${exposedOp.opIndex}`,
                        { type: 'pointer_down', ...point, button: 2 });
                    const entries = opened.result?.menu || session.host.menuAt(point.x, point.y);
                    const entry = entries.find((candidate) =>
                        Number(candidate.component?.componentId ?? candidate.component) ===
                            exposedOp.componentId && candidate.opIndex === exposedOp.opIndex);
                    timed(session, locator.label, `menu-op-${exposedOp.opIndex}`, {
                        type: 'op',
                        target: entry?.component ?? exposedOp.componentId,
                        opIndex: exposedOp.opIndex,
                    });
                    timed(session, locator.label, `menu-op-${exposedOp.opIndex}`,
                        { type: 'menu_close' });
                    timed(session, locator.label, `menu-op-${exposedOp.opIndex}`,
                        { type: 'pointer_up', ...point, button: 2 });
                });
        }

        if( eventEnabled('wheel') ) for( const locator of wheelTargets ) {
            await targetScenario(locator, 'wheel', async (session, component, point) => {
                timed(session, locator.label, 'wheel', { type: 'pointer_move', ...point });
                timed(session, locator.label, 'wheel',
                    { type: 'wheel', ...point, wheel: -120 }, 'wheel_negative');
                timed(session, locator.label, 'wheel',
                    { type: 'wheel', ...point, wheel: 120 }, 'wheel_positive');
                timed(session, locator.label, 'wheel', { type: 'tick', cycle: 3 });
            });
        }

        if( eventEnabled('drag') ) for( const locator of dragTargets ) {
            await targetScenario(locator, 'drag', async (session, component, point) => {
                const moved = {
                    x: Math.max(0, Math.min(511, point.x + 12)),
                    y: Math.max(0, Math.min(333, point.y + 12)),
                };
                timed(session, locator.label, 'drag', { type: 'pointer_move', ...point });
                timed(session, locator.label, 'drag',
                    { type: 'pointer_down', ...point, button: 0 });
                timed(session, locator.label, 'drag', { type: 'tick', cycle: 4 });
                timed(session, locator.label, 'drag', { type: 'pointer_move', ...moved });
                timed(session, locator.label, 'drag', { type: 'tick', cycle: 5 });
                timed(session, locator.label, 'drag',
                    { type: 'pointer_up', ...moved, button: 0 });
                timed(session, locator.label, 'drag',
                    { type: 'pointer_move', x: -1, y: -1 });
            });
        }

        if( eventEnabled('key') && discovery.keyHooks > 0 ) {
            const session = await ensureCurrent();
            const beforeVersion = session.host.version;
            timed(session, discovery.globalLabel, 'keyboard',
                { type: 'key', keyTyped: 65, keyPressed: 65 });
            timed(session, discovery.globalLabel, 'keyboard',
                { type: 'key_down', keyTyped: 65, keyPressed: 65 });
            timed(session, discovery.globalLabel, 'keyboard',
                { type: 'key_down', keyTyped: 65, keyPressed: 65, repeat: true });
            timed(session, discovery.globalLabel, 'keyboard',
                { type: 'key_up', keyTyped: 65, keyPressed: 0 });
            timed(session, discovery.globalLabel, 'keyboard', { type: 'focus_lost' });
            timed(session, discovery.globalLabel, 'keyboard', { type: 'tick', cycle: 6 });
            if( session.host.version !== beforeVersion ) invalidated = true;
        }
    } finally { destroyCurrent(); }
}

function discover(host) {
    const boxes = host.layout();
    const live = [];
    let hookCount = 0;
    let opCount = 0;
    let keyHooks = 0;
    let timerHooks = 0;
    for( const box of boxes ) {
        if( box.effectiveHidden || box.culled || box.w <= 0 || box.h <= 0 ) continue;
        const component = host._component(box.ref, false);
        if( !component ) continue;
        const hooks = hookRecords(component);
        const ops = (component.ops || []).filter((op) => Number.isInteger(op.index));
        hookCount += hooks.length;
        opCount += ops.length;
        keyHooks += hooks.filter((hook) => KEY_HOOKS.has(hook.name)).length;
        timerHooks += hooks.filter((hook) => TIMER_HOOKS.has(hook.name)).length;
        const point = pointFor(host, component);
        if( !point ) continue;
        const meta = host.meta.get(component);
        const opIndexes = ops.map((op) => op.index).sort((a, b) => a - b);
        const names = new Set(hooks.map((hook) => hook.name));
        const pointer = hooks.some((hook) => POINTER_HOOKS.has(hook.name)) ||
            opIndexes.length > 0 || Number(component.static?.clickMask || 0) !== 0 ||
            Boolean(meta?.draggable || component.static?.draggable);
        const wheel = hooks.some((hook) => WHEEL_HOOKS.has(hook.name));
        const drag = hooks.some((hook) => DRAG_HOOKS.has(hook.name)) ||
            Boolean(meta?.draggable || component.static?.draggable);
        live.push({
            componentId: meta?.componentId,
            label: componentLabel(component, meta),
            hooks,
            opIndexes,
            hasOps: opIndexes.length > 0,
            hasOpHook: names.has('onop') || names.has('onclick'),
            draggable: drag,
            pointer,
            wheel,
            drag,
            kind: component.kind,
        });
    }
    const interactive = live.filter((target) => target.pointer || target.wheel || target.drag ||
        target.hooks.some((hook) => KEY_HOOKS.has(hook.name) || TIMER_HOOKS.has(hook.name)));
    return {
        components: host.ir.components.length,
        liveComponents: live.length,
        interactiveComponents: interactive.length,
        hooks: hookCount,
        ops: opCount,
        pointerTargets: live.filter((target) => target.pointer),
        wheelTargets: live.filter((target) => target.wheel),
        dragTargets: live.filter((target) => target.drag),
        keyHooks,
        timerHooks,
        globalLabel: `<global key=${keyHooks} timer=${timerHooks}>`,
    };
}

function sampleTargets(targets, family) {
    if( TARGET_LIMIT === 0 || targets.length <= TARGET_LIMIT ) return targets;
    const sorted = [...targets].sort((left, right) =>
        Number(PRIORITY_TARGET.test(right.label)) - Number(PRIORITY_TARGET.test(left.label)) ||
        left.componentId - right.componentId);
    const result = [];
    const fingerprints = new Set();
    for( const target of sorted ) {
        const relevant = target.hooks.filter((hook) => family === 'wheel'
            ? WHEEL_HOOKS.has(hook.name) : family === 'drag'
                ? DRAG_HOOKS.has(hook.name) : POINTER_HOOKS.has(hook.name));
        const fingerprint = [target.kind, target.draggable ? 1 : 0,
            target.opIndexes.join(','),
            relevant.map((hook) => `${hook.name}:${hook.scriptId}`).sort().join('|')].join(';');
        const priority = PRIORITY_TARGET.test(target.label);
        if( !priority && fingerprints.has(fingerprint) ) continue;
        fingerprints.add(fingerprint);
        result.push(target);
        if( result.length >= TARGET_LIMIT ) break;
    }
    return result;
}

function hookRecords(component) {
    const result = [];
    for( const [rawName, binding] of Object.entries(component.hooks || {})) {
        const scriptId = Number(binding?.script?.id ?? binding?.scriptId ?? binding?.script ?? -1);
        if( !Number.isInteger(scriptId) || scriptId <= 0 ) continue;
        result.push({ name: normalizeHookName(rawName), scriptId });
    }
    return result;
}

function pointFor(host, component) {
    const box = host._box(component);
    if( !box || box.effectiveHidden || box.culled || box.w <= 0 || box.h <= 0 ) return null;
    const left = Math.max(0, box.x, Number(box.clip?.left ?? box.x));
    const top = Math.max(0, box.y, Number(box.clip?.top ?? box.y));
    const right = Math.min(host.viewport.width, box.x + box.w,
        Number(box.clip?.right ?? box.x + box.w));
    const bottom = Math.min(host.viewport.height, box.y + box.h,
        Number(box.clip?.bottom ?? box.y + box.h));
    if( right <= left || bottom <= top ) return null;
    const fractions = [0.5, 0.2, 0.8];
    for( const yPart of fractions ) for( const xPart of fractions ) {
        const point = {
            x: Math.max(0, Math.min(host.viewport.width - 1,
                Math.floor(left + (right - left - 1) * xPart))),
            y: Math.max(0, Math.min(host.viewport.height - 1,
                Math.floor(top + (bottom - top - 1) * yPart))),
        };
        const hit = host._hit(point.x, point.y);
        if( sameComponentOrAncestor(host, component, hit) ) return point;
    }
    return null;
}

function sameComponentOrAncestor(host, owner, candidateValue) {
    const candidate = host._component(candidateValue, false);
    for( let current = candidate; current;
         current = host.byFileId.get(current.layer) || null )
        if( current === owner ) return true;
    return false;
}

function componentLabel(component, meta) {
    const suffix = meta?.dynamic ? `[sub=${meta.subId}]` : `[file=${component.fileId}]`;
    return `${component.name || component.kind || 'component'}${suffix}#${meta?.componentId ?? '?'}`;
}

function mergeDiscovery(total, discovery) {
    for( const key of ['components', 'liveComponents', 'interactiveComponents', 'hooks', 'ops'] )
        total[key] += discovery[key];
    total.pointerTargets += discovery.pointerTargets.length;
    total.wheelTargets += discovery.wheelTargets.length;
    total.dragTargets += discovery.dragTargets.length;
}

function selectCatalog(catalog) {
    const matcher = interfaceMatcher(process.env.CS2DOM_CORPUS_FILTER || '');
    let selected = catalog.filter((record) => matcher(record.name));
    const shard = parseShard(process.env.CS2DOM_CORPUS_SHARD || '0/1');
    selected = selected.filter((record) => record.interfaceId % shard.count === shard.index);
    const limit = nonnegativeInteger(process.env.CS2DOM_CORPUS_LIMIT, 0);
    if( limit > 0 ) selected = selected.slice(0, limit);
    return selected;
}

function interfaceMatcher(spec) {
    if( !spec.trim() ) return () => true;
    const slash = /^\/(.*)\/([a-z]*)$/.exec(spec.trim());
    if( slash ) {
        const regex = new RegExp(slash[1], slash[2]);
        return (name) => regex.test(name);
    }
    const patterns = spec.split(',').map((part) => part.trim()).filter(Boolean)
        .map((part) => new RegExp(`^${part.split('*').map(escapeRegex).join('.*')}$`, 'i'));
    return (name) => patterns.some((pattern) => pattern.test(name));
}

function parseShard(value) {
    const match = /^(\d+)\/(\d+)$/.exec(String(value).trim());
    assert(match, `CS2DOM_CORPUS_SHARD must look like 0/8, got '${value}'`);
    const index = Number(match[1]);
    const count = Number(match[2]);
    assert(count > 0 && index >= 0 && index < count,
        `invalid corpus shard ${index}/${count}`);
    return { index, count };
}

function eventEnabled(name) {
    return !EVENT_FILTER.size || EVENT_FILTER.has(name);
}

function wordSet(value) {
    return new Set(String(value || '').split(',').map((part) => part.trim().toLowerCase())
        .filter(Boolean));
}

function normalizeHookName(value) {
    return String(value || '').replace(/[^a-z0-9]/gi, '').toLowerCase();
}

function deepFreeze(value, seen = new WeakSet()) {
    if( !value || typeof value !== 'object' || seen.has(value) ) return value;
    seen.add(value);
    if( value instanceof Map || value instanceof Set )
        for( const entry of value.values() ) deepFreeze(entry, seen);
    else for( const entry of Object.values(value) ) deepFreeze(entry, seen);
    return Object.freeze(value);
}

function installDispatchProfile(session) {
    let current = null;
    const wrap = (owner, name, measure) => {
        if( typeof owner?.[name] !== 'function' ) return;
        const original = owner[name];
        owner[name] = function(...args) {
            const before = performance.now();
            try { return original.apply(this, args); }
            finally {
                if( current ) measure(current, performance.now() - before, args);
            }
        };
    };
    wrap(session.wasm, 'invokeIntent', (row, ms, [intent]) => {
        row.invokeMs += ms;
        row.invokes.push({ scriptId: intent?.hook?.scriptId, ms });
    });
    wrap(session.host, 'request', (row, ms, [request]) => {
        const kind = String(typeof request === 'object' ? request?.kind : request);
        const metric = row.requests[kind] ||= { calls: 0, ms: 0, maxMs: 0 };
        metric.calls++;
        metric.ms += ms;
        metric.maxMs = Math.max(metric.maxMs, ms);
    });
    wrap(session.host, 'requestFastBatch', (row, ms, [requests]) => {
        row.fastBatchMs += ms;
        row.fastBatches++;
        row.fastRecords += requests?.length || 0;
    });
    wrap(session.host, 'requestFastPackedBatch', (row, ms, [, count]) => {
        row.fastBatchMs += ms;
        row.fastBatches++;
        row.fastRecords += count || 0;
    });
    for( const name of ['layout', '_box', '_geometry', '_emit', '_retireInvisibleInteraction',
        '_hookTargets', '_tick'] ) wrap(session.host, name, (row, ms) => {
        const metric = row.methods[name] ||= { calls: 0, ms: 0 };
        metric.calls++;
        metric.ms += ms;
    });
    return {
        begin() {
            current = {
                invokeMs: 0,
                invokes: [],
                fastBatchMs: 0,
                fastBatches: 0,
                fastRecords: 0,
                requests: {},
                methods: {},
            };
        },
        end() {
            const result = current;
            current = null;
            if( result ) result.topRequests = Object.entries(result.requests)
                .map(([kind, metric]) => ({ kind, ...metric }))
                .sort((left, right) => right.ms - left.ms)
                .slice(0, 12);
            return result;
        },
    };
}

function exposeInvocationErrorCode(runtime) {
    if( typeof runtime?.invocationError !== 'function' ||
        typeof runtime.api?._cs2w_invocation_last_error !== 'function' ) return;
    const original = runtime.invocationError.bind(runtime);
    runtime.invocationError = (invocation) => {
        const vmError = runtime.api._cs2w_invocation_last_error(invocation) | 0;
        const error = original(invocation);
        error.vmError = vmError;
        return error;
    };
}

function identity(record, detail = {}) {
    return { interfaceId: record.interfaceId, interface: record.name, ...detail };
}

function errorDetail(error) {
    return {
        error: error?.message || String(error),
        code: error?.code,
        scriptId: error?.scriptId,
        pc: error?.pc,
        opcode: error?.opcode,
        vmError: error?.vmError,
    };
}

function compareIdentity(left, right) {
    return left.interfaceId - right.interfaceId ||
        String(left.component || '').localeCompare(String(right.component || ''));
}

function printSummary(summary) {
    const selected = summary.selectedCount;
    const actualFull = selected === summary.catalogCount && !summary.filter && summary.shard === '0/1';
    const projectedMs = selected
        ? summary.elapsedMs * summary.catalogCount / selected : 0;
    console.log(`interface latency corpus: ${summary.mountedInterfaces}/${selected} selected mounted ` +
        `(${summary.availablePrograms}/${selected} selected programs available; ` +
        `${summary.catalogCount} interfaces in catalog), ` +
        `${summary.dispatches} raw dispatches in ${formatMs(summary.elapsedMs)}`);
    console.log(`clock=${summary.clock}; threshold=${summary.thresholdMs}ms; fastHost=${summary.fastHost}; ` +
        `shared sessions=${summary.sessions}; target limit=${summary.targetLimit || 'all'}; ` +
        `instrumented=${summary.instrumented}`);
    console.log(`compile ${formatMs(summary.compileMs)} · mount ${formatMs(summary.mountMs)} · ` +
        `dispatch ${formatMs(summary.dispatchMs)} · immutable hostData ${formatMs(summary.hostDataMs)}`);
    console.log(`discovered ${summary.discovered.components} components, ` +
        `${summary.discovered.liveComponents} live, ${summary.discovered.interactiveComponents} interactive, ` +
        `${summary.discovered.hooks} hooks, ${summary.discovered.ops} ops`);
    console.log(`targets pointer ${summary.discovered.sampledPointerTargets}/` +
        `${summary.discovered.pointerTargets}, wheel ${summary.discovered.sampledWheelTargets}/` +
        `${summary.discovered.wheelTargets}, drag ${summary.discovered.sampledDragTargets}/` +
        `${summary.discovered.dragTargets}`);
    console.log(`raw max ${formatRow(summary.rawMax)}; ${summary.outliers.length} >= ` +
        `${summary.thresholdMs}ms; ${summary.dispatchErrors.length} dispatch errors; ` +
        `${summary.mountFailures.length} mount failures; ` +
        `${summary.unavailablePrograms.length} unavailable programs`);
    console.log(`exact Dat2 source fallback: ${summary.fallbackRecords} records across ` +
        `${summary.fallbackInterfaces.length} interfaces`);
    console.log(actualFull
        ? `actual full-corpus wall time: ${formatMs(summary.elapsedMs)}`
        : `projected full-corpus diagnostic cost: ${formatMs(projectedMs)} ` +
          `(projection only; not a latency certification)`);

    printRows('outliers', summary.outliers, formatRow);
    printRows('dispatch errors', summary.dispatchErrors, (row) =>
        `${formatRow(row)} · ${row.error}`);
    printRows('mount failures', summary.mountFailures, (row) =>
        `${row.interfaceId}:${row.interface} · ${row.scenario ? `${row.scenario} · ` : ''}` +
        `${row.error}${row.vmError === undefined ? '' : ` · vmError=${row.vmError}`}`);
    if( summary.unavailablePrograms.length ) console.log(
        `unavailable sample: ${summary.unavailablePrograms.slice(0, 10)
            .map((row) => `${row.interfaceId}:${row.interface}`).join(', ')}`);
}

function printRows(label, rows, formatter) {
    if( !rows.length ) return;
    console.log(`${label}:`);
    for( const row of rows.slice(0, MAX_PRINTED_ROWS) ) console.log(`  ${formatter(row)}`);
    if( rows.length > MAX_PRINTED_ROWS )
        console.log(`  ... ${rows.length - MAX_PRINTED_ROWS} more`);
}

function formatRow(row) {
    if( !row ) return 'none';
    const base = `${row.ms.toFixed(3)}ms · ${row.interfaceId}:${row.interface} · ` +
        `${row.component || '<global>'} · ${row.scenario}/${row.event} · ` +
        `${row.hooks} hooks/${row.hostRequests} HOST/${row.versions} versions`;
    if( !row.profile ) return base;
    const methods = row.profile.methods || {};
    const top = (row.profile.topRequests || []).slice(0, 4)
        .map((request) => `${request.kind}:${request.calls}/${request.ms.toFixed(2)}ms`).join(',');
    return `${base} · profile invoke=${row.profile.invokeMs.toFixed(2)}ms ` +
        `fast=${row.profile.fastBatchMs.toFixed(2)}ms/${row.profile.fastRecords} ` +
        `layout=${(methods.layout?.ms || 0).toFixed(2)}ms ` +
        `emit=${(methods._emit?.ms || 0).toFixed(2)}ms requests=[${top}]`;
}

function formatMs(value) {
    if( value < 1000 ) return `${value.toFixed(1)}ms`;
    return `${(value / 1000).toFixed(2)}s`;
}

function escapeRegex(value) {
    return value.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

function positiveNumber(value, fallback) {
    const result = Number(value);
    return Number.isFinite(result) && result > 0 ? result : fallback;
}

function positiveInteger(value, fallback) {
    const result = Math.trunc(Number(value));
    return Number.isFinite(result) && result > 0 ? result : fallback;
}

function nonnegativeInteger(value, fallback) {
    const result = Math.trunc(Number(value));
    return Number.isFinite(result) && result >= 0 ? result : fallback;
}

function assert(condition, message) {
    if( !condition ) throw new Error(message || 'assertion failed');
}
