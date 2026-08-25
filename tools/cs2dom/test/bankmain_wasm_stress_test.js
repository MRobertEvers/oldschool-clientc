/* Deterministic bankmain integration stress test.
 *
 * This is intentionally not part of the default unit suite: it consumes the
 * checked-in OSRS-Content corpus and executes the production C CS2VM WebAssembly
 * module. Run it with `npm run test:bank` from tools/cs2dom.
 *
 * The supervisor keeps the VM in a worker. A synchronous C/WASM regression can
 * otherwise block Node's event loop forever; the worker lets the parent enforce
 * a hard deadline around every named interaction and report the exact offender.
 */

import { existsSync, readFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { dirname, join, resolve } from 'node:path';
import { performance } from 'node:perf_hooks';
import {
    isMainThread, parentPort, Worker, workerData,
} from 'node:worker_threads';
import { fileURLToPath } from 'node:url';

import { compileInterfaceProgram } from '../src/bytecode.js';
import { openContentInterface } from '../src/content.js';
import { contentHostData } from '../src/host_data.js';
import { createHostRuntime } from '../src/host_runtime.js';
import { createWasmCS2Runtime } from '../src/wasm_runtime.js';
import moduleFactory from '../web/cs2vm_wasm.js';

const HERE = dirname(fileURLToPath(import.meta.url));
const REPO = resolve(HERE, '../../..');
/* Allow performance comparisons against an immutable corpus worktree without
 * touching a developer's in-progress OSRS-Content checkout. */
const CONTENT = process.env.CS2DOM_BANK_CONTENT
    ? resolve(process.env.CS2DOM_BANK_CONTENT)
    : join(REPO, 'OSRS-Content', 'osrs239-content');
const WASM = resolve(HERE, '../web/cs2vm_wasm.wasm');
const ACTION_TIMEOUT_MS = 5_000;
const BOOT_TIMEOUT_MS = 20_000;
const INTERACTION_BUDGET_MS = 10;
const ENFORCE_INTERACTION_BUDGET =
    process.env.CS2DOM_BANK_ENFORCE_INTERACTION_BUDGET !== '0';
/* Match the production worker: it does not retain change logs and hands the
 * renderer a structured-clone view. Opt into the expensive diagnostic paths
 * explicitly when profiling persistence rather than interaction latency. */
const RECORD_CHANGES = process.env.CS2DOM_BANK_RECORD_CHANGES === '1';
const DETACH_RENDER = process.env.CS2DOM_BANK_DETACH_RENDER === '1';
const TRACE_ACTION = process.env.CS2DOM_BANK_TRACE_ACTION || '';
const TRACE_REQUESTS = process.env.CS2DOM_BANK_TRACE_REQUESTS === '1';
const TRACE_SEQUENCE = process.env.CS2DOM_BANK_TRACE_SEQUENCE === '1';
const PROFILE_FAST = process.env.CS2DOM_BANK_PROFILE_FAST === '1';
const TRACE_SLOW = process.env.CS2DOM_BANK_TRACE_SLOW === '1';
const FAST_HOST = process.env.CS2DOM_WASM_FAST_HOST !== '0';
const PRELOAD_HOST_DATA = process.env.CS2DOM_WASM_PRELOAD_HOST_DATA !== '0';

if( isMainThread ) {
    const summary = await supervise();
    console.log(`bankmain C/WASM stress: ${summary.actions} actions, ` +
        `${summary.hooks} hooks, ${summary.hostRequests} HOST calls, ` +
        `${summary.elapsedMs} ms, fingerprint ${summary.fingerprint}`);
    console.log(summary.groups.map((group) =>
        `${group.name}=${group.totalMs}ms/${group.actions} (max ${group.maxMs}ms)`).join(' · '));
    console.log(formatProfile(summary.profile));
} else if( workerData?.bankmainStress ) {
    await stressWorker();
}

function supervise() {
    return new Promise((resolvePromise, rejectPromise) => {
        const worker = new Worker(new URL(import.meta.url), {
            workerData: { bankmainStress: true },
        });
        let settled = false;
        let currentAction = 'runtime boot/mount';
        let actionTimer = null;
        let bootTimer = setTimeout(() => fail(
            new Error(`bankmain ${currentAction} exceeded ${BOOT_TIMEOUT_MS} ms`)), BOOT_TIMEOUT_MS);

        const clearTimers = () => {
            clearTimeout(bootTimer);
            clearTimeout(actionTimer);
            bootTimer = null;
            actionTimer = null;
        };
        const finish = (error, value) => {
            if( settled ) return;
            settled = true;
            clearTimers();
            worker.terminate().finally(() => error ? rejectPromise(error) : resolvePromise(value));
        };
        const fail = (error) => finish(error);

        worker.on('message', (message) => {
            if( message?.type === 'ready' ) {
                clearTimeout(bootTimer);
                bootTimer = null;
                return;
            }
            if( message?.type === 'begin' ) {
                clearTimeout(actionTimer);
                currentAction = message.action;
                actionTimer = setTimeout(() => fail(new Error(
                    `bankmain action '${currentAction}' exceeded ${ACTION_TIMEOUT_MS} ms`)),
                ACTION_TIMEOUT_MS);
                return;
            }
            if( message?.type === 'end' ) {
                clearTimeout(actionTimer);
                actionTimer = null;
                if( message.ms > ACTION_TIMEOUT_MS ) fail(new Error(
                    `bankmain action '${message.action}' took ${message.ms} ms`));
                return;
            }
            if( message?.type === 'dispatch-trace' ) {
                console.log(`bankmain dispatch trace: ${JSON.stringify(message.row)}`);
                return;
            }
            if( message?.type === 'failed' ) {
                const detail = [message.message,
                    message.scriptId === undefined ? '' : `script ${message.scriptId}`,
                    message.pc === undefined ? '' : `pc ${message.pc}`,
                    message.opcode === undefined ? '' : `opcode ${message.opcode}`,
                ].filter(Boolean).join(' · ');
                fail(new Error(`bankmain action '${message.action || currentAction}' failed: ${detail}`));
                return;
            }
            if( message?.type === 'done' ) finish(null, message.summary);
        });
        worker.on('error', fail);
        worker.on('exit', (code) => {
            if( !settled ) fail(new Error(
                `bankmain stress worker exited ${code} during '${currentAction}'`));
        });
    });
}

async function stressWorker() {
    let wasm = null;
    let action = 'runtime boot/mount';
    const started = performance.now();
    try {
        assert(existsSync(join(CONTENT, 'interfaces', 'bankmain.if')),
            `OSRS-Content bankmain is unavailable at ${CONTENT}`);
        assert(existsSync(WASM), `C CS2VM WebAssembly is unavailable at ${WASM}`);

        const imported = openContentInterface(CONTENT, 'bankmain', { source: 'content' });
        const program = compileInterfaceProgram({
            content: CONTENT, unpackedContent: CONTENT, revision: 'osrs239',
        }, imported);
        assert(program.available && program.scripts.length > 0,
            `bankmain bytecode is unavailable: ${(program.warnings || []).join('; ')}`);

        const session = { host: null, wasm: null };
        let hooks = 0;
        let hostRequests = 0;
        session.host = createHostRuntime(imported.ir, {
            viewport: { width: 512, height: 334 },
            hostData: contentHostData(CONTENT),
            recordChanges: RECORD_CHANGES,
            invoke(intent) {
                hooks++;
                const result = session.wasm.invokeIntent(intent);
                hostRequests += result.hostRequests;
                return result;
            },
        });
        const wasmUrl = `data:application/wasm;base64,${readFileSync(WASM).toString('base64')}`;
        wasm = session.wasm = await createWasmCS2Runtime({
            program, host: session.host, moduleFactory, wasmUrl, fastHost: FAST_HOST,
            preloadHostData: PRELOAD_HOST_DATA,
        });
        const fastProfile = PROFILE_FAST ? installFastProfile(session) : null;
        session.host.mount();
        parentPort.postMessage({ type: 'ready' });

        let requestTrace = null;
        let requestSequence = null;
        if( TRACE_REQUESTS ) {
            const hostRequest = session.host.request.bind(session.host);
            session.host.request = (...args) => {
                if( !requestTrace ) return hostRequest(...args);
                const supplied = args[0];
                const kind = String(typeof supplied === 'object' ? supplied?.kind : supplied);
                if( requestSequence ) requestSequence.push(kind);
                const before = performance.now();
                try { return hostRequest(...args); }
                finally {
                    const elapsed = performance.now() - before;
                    const row = requestTrace.get(kind) || { kind, calls: 0, ms: 0, maxMs: 0 };
                    row.calls++;
                    row.ms += elapsed;
                    row.maxMs = Math.max(row.maxMs, elapsed);
                    requestTrace.set(kind, row);
                }
            };
        }

        const profile = createProfile(RECORD_CHANGES, DETACH_RENDER);
        const resolveLayout = session.host.layout.bind(session.host);
        session.host.layout = () => {
            const cold = session.host.layoutVersion !== session.host.version;
            profile.layoutCalls++;
            if( !cold ) return resolveLayout();
            const before = performance.now();
            const boxes = resolveLayout();
            profile.layoutMs += performance.now() - before;
            profile.layoutResolves++;
            return boxes;
        };

        const rows = [];
        const run = (label, operation) => {
            action = label;
            parentPort.postMessage({ type: 'begin', action });
            const beforeHooks = hooks;
            const beforeRequests = hostRequests;
            const beforeVersion = session.host.version;
            const before = performance.now();
            operation();

            /* changes() is not part of the current dev-page repaint path, but
             * it is the public incremental-state transport. Serialize one
             * real action-sized delta so its retention and clone costs stay
             * visible beside snapshot/render work. */
            const changesBefore = performance.now();
            const changes = session.host.changes(beforeVersion);
            profile.changesMs += performance.now() - changesBefore;
            const changesJsonBefore = performance.now();
            const changesJson = JSON.stringify(changes);
            profile.changesJsonMs += performance.now() - changesJsonBefore;
            profile.changesBytes += changesJson.length;
            profile.changeSamples++;

            /* Full snapshots are persistence/checkpoint payloads. The browser
             * renderer only needs the smaller payload sampled by dispatch(). */
            const snapshotBefore = performance.now();
            const snapshot = session.host.snapshot();
            profile.snapshotMs += performance.now() - snapshotBefore;
            const snapshotJsonBefore = performance.now();
            const snapshotJson = JSON.stringify(snapshot);
            profile.snapshotJsonMs += performance.now() - snapshotJsonBefore;
            profile.snapshotBytes += snapshotJson.length;
            profile.snapshotSamples++;

            const ms = Math.round(performance.now() - before);
            assert(ms <= ACTION_TIMEOUT_MS,
                `${label} took ${ms} ms (limit ${ACTION_TIMEOUT_MS} ms)`);
            assert(hooks > beforeHooks, `${label} dispatched no C hook`);
            assert(hostRequests > beforeRequests, `${label} made no C HOST request`);
            assert(Object.values(session.host.pendingDeferred).every((queue) => queue.length === 0),
                `${label} left deferred component work queued`);
            assert(session.host.services.deferredDrops.length === 0,
                `${label} overflowed a deferred component queue`);
            rows.push({ action: label, ms, hooks: hooks - beforeHooks,
                hostRequests: hostRequests - beforeRequests });
            parentPort.postMessage({ type: 'end', action: label, ms });
        };

        const component = (name) => {
            const value = session.host.byName.get(name);
            assert(value, `bankmain component '${name}' is missing`);
            return value;
        };
        const point = (name) => {
            const box = session.host._box(component(name));
            const left = Math.max(box.x, box.clip.left);
            const right = Math.min(box.x + box.w, box.clip.right);
            const top = Math.max(box.y, box.clip.top);
            const bottom = Math.min(box.y + box.h, box.clip.bottom);
            assert(right > left && bottom > top, `bankmain component '${name}' is not hittable`);
            return {
                x: left + Math.floor((right - left) / 2),
                y: top + Math.floor((bottom - top) / 2),
            };
        };
        const dispatch = (event) => {
            const beforeVersion = session.host.version;
            const beforeHooks = hooks;
            const beforeRequests = hostRequests;
            if( TRACE_REQUESTS && TRACE_ACTION && action === TRACE_ACTION ) {
                requestTrace = new Map();
                if( TRACE_SEQUENCE ) requestSequence = [];
            }
            const before = performance.now();
            if( fastProfile ) fastProfile.begin();
            const result = session.host.dispatch(event);
            const elapsed = performance.now() - before;
            const fast = fastProfile?.end();
            profile.dispatchMs += elapsed;
            profile.dispatches++;
            profile.dispatchMaxMs = Math.max(profile.dispatchMaxMs, elapsed);
            if( elapsed >= INTERACTION_BUDGET_MS ) profile.dispatchOver10++;
            const type = String(event.type || 'unknown');
            const typeProfile = profile.dispatchByType[type] ||= {
                count: 0, totalMs: 0, maxMs: 0, over10: 0,
            };
            typeProfile.count++;
            typeProfile.totalMs += elapsed;
            typeProfile.maxMs = Math.max(typeProfile.maxMs, elapsed);
            if( elapsed >= INTERACTION_BUDGET_MS ) typeProfile.over10++;
            if( (TRACE_ACTION && action === TRACE_ACTION) ||
                (TRACE_SLOW && elapsed >= INTERACTION_BUDGET_MS) ) {
                const requests = requestTrace ? [...requestTrace.values()]
                    .sort((left, right) => right.ms - left.ms) : [];
                requestTrace = null;
                const sequence = requestSequence ? runLengthRequestSequence(requestSequence) : undefined;
                requestSequence = null;
                parentPort.postMessage({ type: 'dispatch-trace', row: {
                    action, event: event.type, ms: elapsed,
                    hooks: hooks - beforeHooks,
                    hostRequests: hostRequests - beforeRequests,
                    versions: session.host.version - beforeVersion,
                    components: session.host.ir.components.length,
                    requests,
                    sequence,
                    fast,
                } });
            }
            if( session.host.version === beforeVersion ) return result;

            /* This mirrors applyRuntimeSnapshot(): old runtimes fall back to
             * the full persistence snapshot, while optimized runtimes expose
             * a detached renderer-only payload. JSON encoding is measured
             * independently as a proxy for worker/network transport. */
            const renderBefore = performance.now();
            const render = typeof session.host.renderSnapshot === 'function'
                ? session.host.renderSnapshot({ detached: DETACH_RENDER })
                : session.host.snapshot();
            profile.renderSnapshotMs += performance.now() - renderBefore;
            const renderJsonBefore = performance.now();
            const renderJson = JSON.stringify({
                version: render.version, viewport: render.viewport, boxes: render.boxes,
            });
            profile.renderJsonMs += performance.now() - renderJsonBefore;
            profile.renderBytes += renderJson.length;
            profile.renderRefreshes++;
            return result;
        };
        const ownsHit = (ownerName, hitRef) => {
            const owner = component(ownerName);
            for( let hit = session.host._component(hitRef, false); hit;
                 hit = session.host.byFileId.get(hit.layer) || null )
                if( hit === owner ) return true;
            return false;
        };
        const pointerCycle = (name) => {
            const p = point(name);
            for( const event of [
                { type: 'pointer_move', ...p }, { type: 'tick' },
                { type: 'pointer_down', ...p, button: 0 }, { type: 'tick' },
                { type: 'pointer_up', ...p, button: 0 }, { type: 'tick' },
                { type: 'pointer_move', x: 0, y: 0 }, { type: 'tick' },
            ] ) {
                const result = dispatch(event);
                /* A cache button may own a decorative child at the sampled
                 * point. Rev-239's scrollbar track and draggable thumb are
                 * siblings, and the centre of scrollbar[0] is deliberately
                 * covered by scrollbar[1], so pin that one audited overlap. */
                const auditedScrollbarOverlap = name === 'scrollbar[0]' &&
                    result.hit?.name === 'scrollbar[1]';
                if( event.type === 'pointer_down' ) assert(
                    auditedScrollbarOverlap || ownsHit(name, result.hit),
                    `${name} pointer escaped to ${result.hit?.name || 'no component'}`);
            }
        };

        const buttons = [
            'quantity1', 'quantity5', 'quantity10', 'quantityx', 'quantityall',
            'swap_insert', 'note', 'search',
            /* Rev-239 calls the familiar/container control "depositcontainers". */
            'depositcontainers', 'depositinv', 'depositworn',
        ];
        for( const name of buttons ) for( let repeat = 1; repeat <= 3; repeat++ )
            run(`${name}#${repeat}`, () => pointerCycle(name));

        for( const name of ['scrollbar[4]', 'scrollbar[5]', 'scrollbar[0]'] )
            for( let repeat = 1; repeat <= 3; repeat++ )
                run(`${name}#${repeat}`, () => pointerCycle(name));

        for( let repeat = 1; repeat <= 3; repeat++ ) run(`scrollbar-wheel#${repeat}`, () => {
            const p = point('scrollbar[0]');
            dispatch({ type: 'wheel', ...p, wheel: 1 });
            dispatch({ type: 'tick' });
            dispatch({ type: 'wheel', ...p, wheel: -1 });
            dispatch({ type: 'tick' });
        });

        for( let repeat = 1; repeat <= 3; repeat++ ) run(`scrollbar-drag#${repeat}`, () => {
            const p = point('scrollbar[1]');
            const moved = { x: p.x, y: Math.min(270, p.y + 24) };
            for( const event of [
                { type: 'pointer_move', ...p },
                { type: 'pointer_down', ...p, button: 0 }, { type: 'tick' },
                { type: 'pointer_move', ...moved }, { type: 'tick' },
                { type: 'pointer_up', ...moved, button: 0 },
                { type: 'pointer_move', x: 0, y: 0 }, { type: 'tick' },
            ] ) dispatch(event);
        });

        run('swap-insert-context-menu-op1', () => {
            const p = point('swap_insert');
            dispatch({ type: 'pointer_move', ...p });
            const opened = dispatch({ type: 'pointer_down', ...p, button: 2 });
            const entry = opened.menu?.find((candidate) => candidate.opIndex === 1);
            assert(entry, 'swap/insert context menu did not expose operation 1');
            dispatch({ type: 'op', target: entry.component, opIndex: entry.opIndex });
            dispatch({ type: 'tick' });
            dispatch({ type: 'menu_close' });
            dispatch({ type: 'pointer_move', x: 0, y: 0 });
            dispatch({ type: 'tick' });
        });

        /* Close is last because its service intent represents dismissal. */
        for( let repeat = 1; repeat <= 3; repeat++ )
            run(`close#${repeat}`, () => pointerCycle('frame[11]'));

        assert(session.host.services.closeModalRequested,
            'bank close did not reach the native close-modal HOST service');
        assert(rows.length === 52, `stress matrix ran ${rows.length} actions instead of 52`);
        assert(hostRequests > 100_000,
            `only ${hostRequests} HOST calls ran; the native bank rebuild path was not exercised`);
        assert(session.host.layout().length > imported.componentCount,
            'bankmain native scripts did not create their dynamic React children');
        assert(!ENFORCE_INTERACTION_BUDGET || profile.dispatchOver10 === 0,
            `bankmain had ${profile.dispatchOver10} dispatches at or above ` +
            `${INTERACTION_BUDGET_MS}ms (max ${profile.dispatchMaxMs.toFixed(3)}ms)`);

        wasm.destroy();
        wasm = null;
        const fingerprint = createHash('sha256')
            .update(JSON.stringify(session.host.snapshot())).digest('hex');
        parentPort.postMessage({ type: 'done', summary: {
            actions: rows.length,
            hooks,
            hostRequests,
            elapsedMs: Math.round(performance.now() - started),
            fingerprint,
            slowest: rows.reduce((left, right) => right.ms > left.ms ? right : left, rows[0]),
            groups: groupTimings(rows),
            profile,
        } });
    } catch( error ) {
        parentPort.postMessage({
            type: 'failed', action, message: error?.message || String(error),
            code: error?.code, scriptId: error?.scriptId, pc: error?.pc, opcode: error?.opcode,
        });
    } finally {
        wasm?.destroy();
    }
}

function installFastProfile(session) {
    let current = null;
    const wrap = (owner, name, measure) => {
        const original = owner[name].bind(owner);
        owner[name] = (...args) => {
            const before = performance.now();
            try { return original(...args); }
            finally {
                if( current ) measure(current, performance.now() - before, args);
            }
        };
    };
    wrap(session.wasm, 'invokeIntent', (row, ms, [intent]) => {
        row.invokes.push({ scriptId: intent?.hook?.scriptId, ms });
        row.invokeMs += ms;
    });
    wrap(session.host, 'requestFastBatch', (row, ms, [requests]) => {
        row.fastBatches++;
        row.fastRecords += requests?.length || 0;
        row.fastBatchMs += ms;
        for( const request of requests || [] )
            row.fastKinds[request.kind] = (row.fastKinds[request.kind] || 0) + 1;
    });
    wrap(session.host, 'requestFastPackedBatch', (row, ms, [records, count]) => {
        row.fastBatches++;
        row.fastRecords += count || 0;
        row.fastBatchMs += ms;
        for( let index = 0; index < count; index++ ) {
            const kind = records[index * 12];
            row.fastKinds[kind] = (row.fastKinds[kind] || 0) + 1;
        }
    });
    wrap(session.host, '_retireInvisibleInteraction', (row, ms) => {
        row.retireCalls++;
        row.retireMs += ms;
    });
    for( const name of ['layout', '_box', '_geometry', '_pointerDown', '_hover', '_hit', 'menuAt', '_click', '_emit',
        '_publishArmedButton', '_publishButtonService', '_resolveAncestorHook',
        '_defaultButtonComponent', '_result', '_interactionView', '_drainDeferredComponents'] )
        wrap(session.host, name, (row, ms) => {
            const metric = row.hostMethods[name] ||= { calls: 0, ms: 0 };
            metric.calls++;
            metric.ms += ms;
        });
    return {
        begin() {
            current = {
                invokes: [], invokeMs: 0, fastBatches: 0, fastRecords: 0,
                fastBatchMs: 0, fastKinds: {}, retireCalls: 0, retireMs: 0,
                hostMethods: {},
            };
        },
        end() {
            const result = current;
            current = null;
            return result;
        },
    };
}

function assert(condition, message) {
    if( !condition ) throw new Error(message || 'assertion failed');
}

function createProfile(recordChanges, detachedRender) {
    return {
        recordChanges,
        detachedRender,
        dispatches: 0, dispatchMs: 0,
        dispatchMaxMs: 0, dispatchOver10: 0, dispatchByType: {},
        renderRefreshes: 0, renderSnapshotMs: 0, renderJsonMs: 0, renderBytes: 0,
        snapshotSamples: 0, snapshotMs: 0, snapshotJsonMs: 0, snapshotBytes: 0,
        changeSamples: 0, changesMs: 0, changesJsonMs: 0, changesBytes: 0,
        layoutCalls: 0, layoutResolves: 0, layoutMs: 0,
    };
}

function formatProfile(profile) {
    const duration = (value) => `${value.toFixed(1)}ms`;
    const size = (bytes, count) => `${Math.round(bytes / Math.max(1, count) / 1024)}KiB avg`;
    return [
        `dispatch ${duration(profile.dispatchMs)}/${profile.dispatches} ` +
            `(max ${duration(profile.dispatchMaxMs)}, ${profile.dispatchOver10} >=10ms; ` +
            Object.entries(profile.dispatchByType).map(([type, value]) =>
                `${type} ${duration(value.totalMs)}/${value.count} max ${duration(value.maxMs)}`
            ).join(', ') + ')',
        `render snapshot (${profile.detachedRender ? 'detached' : 'worker view'}) ` +
            `${duration(profile.renderSnapshotMs)} + encode ` +
            `${duration(profile.renderJsonMs)}/${profile.renderRefreshes} (${size(profile.renderBytes, profile.renderRefreshes)})`,
        `full snapshot ${duration(profile.snapshotMs)} + encode ` +
            `${duration(profile.snapshotJsonMs)}/${profile.snapshotSamples} (${size(profile.snapshotBytes, profile.snapshotSamples)})`,
        `changes (${profile.recordChanges ? 'retained' : 'disabled'}) ` +
            `${duration(profile.changesMs)} + encode ` +
            `${duration(profile.changesJsonMs)}/${profile.changeSamples} (${size(profile.changesBytes, profile.changeSamples)})`,
        `layout resolve ${duration(profile.layoutMs)}/${profile.layoutResolves} cold ` +
            `(${profile.layoutCalls} total calls)`,
    ].join('\n');
}

function groupTimings(rows) {
    const groups = new Map();
    for( const row of rows ) {
        const name = row.action.includes('context-menu') ? 'menu'
            : row.action.startsWith('quantity') ? 'quantity'
                : row.action.startsWith('swap_') || row.action.startsWith('note') ? 'mode-buttons'
                    : row.action.startsWith('search') ? 'search'
                        : row.action.startsWith('deposit') ? 'deposit'
                            : row.action.startsWith('scrollbar') ? 'scrollbar'
                                : row.action.startsWith('close') ? 'close' : 'other';
        const group = groups.get(name) || { name, actions: 0, totalMs: 0, maxMs: 0 };
        group.actions++;
        group.totalMs += row.ms;
        group.maxMs = Math.max(group.maxMs, row.ms);
        groups.set(name, group);
    }
    return [...groups.values()];
}

function runLengthRequestSequence(sequence) {
    const runs = [];
    for( const kind of sequence ) {
        const last = runs.at(-1);
        if( last?.[0] === kind ) last[1]++;
        else runs.push([kind, 1]);
    }
    return runs;
}
