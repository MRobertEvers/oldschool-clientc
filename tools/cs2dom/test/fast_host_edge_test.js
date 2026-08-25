import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import { IF_TYPE } from '../src/components.js';
import { createHostRuntime, HostRuntimeError } from '../src/host_runtime.js';
import { createWasmCS2Runtime } from '../src/wasm_runtime.js';
import { compileScripts, findRepoRoot } from '../src/verify.js';
import moduleFactory from '../web/cs2vm_wasm.js';

const RECORD_WORDS = 12;
const CC_FIND = 200;
const IF_SETHIDE = 2003;
const IF_SETTRANS = 2103;

function layer(fileId, name) {
    const props = {
        x: 0, y: 0, width: 128, height: 96,
        xMode: 0, yMode: 0, widthMode: 0, heightMode: 0,
        hidden: false, transparency: 0,
    };
    return {
        fileId, name, kind: 'Layer', type: IF_TYPE.layer, layer: null,
        props, static: props, authoredProps: new Set(), dynamic: [],
        ops: [], events: {}, hooks: {}, triggers: {}, dependencies: [], rawFields: {},
    };
}

function highGroupHost() {
    return createHostRuntime({
        /* Packed ids for this group have bit 31 set and therefore cross the C
         * bridge as negative signed i32 values. */
        interfaceId: 40000,
        components: [layer(0, 'root')],
    }, {
        viewport: { width: 128, height: 96 },
        state: {
            'invslots:95': {
                0: { id: 4151, count: 2 },
                2: { id: 0, count: 999 },
                70000: { objectId: 995, count: 123 },
            },
        },
    });
}

function lowGroupHost() {
    return createHostRuntime({
        interfaceId: 12,
        components: [layer(0, 'root')],
    }, { viewport: { width: 128, height: 96 } });
}

function packed(records) {
    const words = new Int32Array(records.length * RECORD_WORDS);
    for( let index = 0; index < records.length; index++ ) {
        const record = records[index];
        const base = index * RECORD_WORDS;
        words[base] = record.kind;
        words[base + 1] = record.componentId;
        for( let word = 0; word < (record.args || []).length; word++ )
            words[base + 2 + word] = record.args[word];
    }
    return words;
}

/* Dynamic child slots are native signed ints, independent from the bounded
 * transient component-UID allocator. Shipped GIM scripts create at -1 before
 * replacing that exact slot. */
{
    const host = lowGroupHost();
    const first = host.createChild('root', IF_TYPE.text, -1);
    const replacement = host.createChild('root', IF_TYPE.graphic, -1);
    assert.notEqual(first.key, replacement.key);
    assert.equal(host.findChild('root', -1, false)?.key, replacement.key);
    assert.deepEqual(host.children('root', { startIndex: -0x80000000 })
        .map((ref) => ref.subId), [-1]);
    assert.throws(() => host.component(first),
        (error) => error instanceof HostRuntimeError && error.code === 'STALE_REF');
}

/* FROMDATE uses the client's fixed UTC epoch and English month table. */
{
    const host = lowGroupHost();
    assert.equal(host.request({ kind: 'FROMDATE', day: -11745 }), '1-Jan-1970');
    assert.equal(host.request({ kind: 'FROMDATE', day: 0 }), '27-Feb-2002');
    assert.equal(host.request({ kind: 'FROMDATE', day: 8037 }), '29-Feb-2024');
    assert.equal(host.request({ kind: 'FROMDATE', day: 8038 }), '1-Mar-2024');
}

/* INV_GETOBJ/NUM are native empty reads for invalid signed inputs, rather than
 * request validation failures. Positive slots are full C ints, not uint16s. */
{
    const host = highGroupHost();
    for( const [invId, slot] of [[-1, 0], [95, -1], [0x80000000, 0], [95, 0x80000000]] ) {
        assert.equal(host.request({ kind: 'INV_GETOBJ', inv_id: invId, slot }), -1);
        assert.equal(host.request({ kind: 'INV_GETNUM', inv_id: invId, slot }), 0);
    }
    assert.equal(host.request({ kind: 'INV_GETOBJ', inv_id: 95, slot: 70000 }), 995);
    assert.equal(host.request({ kind: 'INV_GETNUM', inv_id: 95, slot: 70000 }), 123);
    assert.equal(host.request({ kind: 'INV_GETOBJ', inv_id: 95, slot: 2 }), -1);
    assert.equal(host.request({ kind: 'INV_GETNUM', inv_id: 95, slot: 2 }), 0);
    assert.deepEqual([...host.fastHostInventorySnapshot(-1)], []);
    assert.deepEqual([...host.fastHostInventorySnapshot(95)],
        [0, 4151, 2, 2, -1, 0, 70000, 995, 123]);
}

/* Generic, named-fast and packed-fast paths all preserve a successful target
 * and leave it untouched for invalid/not-found child indices, like exec_cc_find. */
for( const mode of ['generic', 'named-fast', 'packed-fast'] ) {
    const host = highGroupHost();
    const root = host.ref('root');
    const child = host.createChild(root, IF_TYPE.text, 7);
    const rootWire = root.componentId | 0;
    const childWire = child.componentId | 0;

    assert(rootWire < 0 && childWire < 0, 'fixture did not exercise signed packed ids');
    assert.equal(host.component(rootWire).name, 'root');
    assert.equal(host.component(childWire).name, 'root[7]');
    assert.deepEqual([...host.fastHostChildrenSnapshot(rootWire)], [7, childWire]);

    if( mode === 'generic' ) {
        host.request({ kind: 'CC_FIND', parent_id: rootWire, sub_id: 7, dot_operand: 0 });
        host.request({ kind: 'IF_SETTRANS', component_id: childWire, trans: 47 });
        assert.equal(host.request({
            kind: 'CC_FIND', parent_id: rootWire, sub_id: -1, dot_operand: 0,
        }), null);
        assert.equal(host.request({
            kind: 'CC_FIND', parent_id: rootWire, sub_id: 0x10000, dot_operand: 0,
        }), null);
    } else if( mode === 'named-fast' ) {
        host.requestFastBatch([
            { kind: 'CC_FIND', parent_id: rootWire, sub_id: 7,
                dot_operand: false, expected_component_id: childWire },
            { kind: 'IF_SETTRANS', component_id: childWire, trans: 47 },
            { kind: 'CC_FIND', parent_id: rootWire, sub_id: -1,
                dot_operand: false, expected_component_id: -1 },
            { kind: 'CC_FIND', parent_id: rootWire, sub_id: 0x10000,
                dot_operand: false, expected_component_id: -1 },
        ]);
    } else {
        host.requestFastPackedBatch(packed([
            { kind: CC_FIND, componentId: rootWire, args: [7, 0, childWire] },
            { kind: IF_SETTRANS, componentId: childWire, args: [47] },
            { kind: CC_FIND, componentId: rootWire, args: [-1, 0, -1] },
            { kind: CC_FIND, componentId: rootWire, args: [0x10000, 0, -1] },
        ]), 4, new Uint8Array(0));
    }

    assert.equal(host.component(childWire).props.transparency, 47,
        `${mode} did not resolve a signed target id`);
    assert.equal(host.activeRef().key, child.key,
        `${mode} corrupted the implicit target on CC_FIND miss`);
}

/* A hook too wide for the compact arena vocabulary remains a valid generic
 * request. The C bridge must flush earlier packed writes and use this path. */
{
    const host = highGroupHost();
    const rootWire = host.ref('root').componentId | 0;
    const signature = 'i'.repeat(66);
    host.request({
        kind: 'IF_SETONMOUSEOVER', component_id: rootWire, script_id: 991,
        signature, int_args: Array.from({ length: 64 }, (_, index) => index + 1),
        int_arg_count: 64, str_args: [], str_arg_count: 0,
        trigger_ids: [], trigger_count: 0,
    });
    const installed = host.ir.components.find((component) => component.name === 'root')
        .hooks.on_mouse_over;
    assert.equal(installed.signature, signature);
    assert.equal(installed.args.length, 64);
}

/* The other compact-shape rejection arms are valid generic HOST shapes. A
 * negative malformed count becomes no triggers; a defensive 4,097-entry
 * direct request is bounded to HostRuntime's native-sized trigger limit. (The
 * latter cannot originate in CS2VM2 because its entire int stack is 1,024.) */
for( const [triggerCount, expectedCount] of [[-1, 0], [4097, 4096]] ) {
    const host = highGroupHost();
    const rootWire = host.ref('root').componentId | 0;
    host.request({
        kind: 'IF_SETONMOUSEOVER', component_id: rootWire, script_id: 992,
        signature: 'Y', int_args: [], int_arg_count: 0,
        str_args: [], str_arg_count: 0,
        trigger_ids: Array.from({ length: Math.max(0, triggerCount) }, (_, index) => index),
        trigger_count: triggerCount,
    });
    const installed = host.ir.components.find((component) => component.name === 'root')
        .hooks.on_mouse_over;
    assert.equal(installed.triggerIds.length, expectedCount,
        `generic trigger_count ${triggerCount} did not preserve its bounded semantics`);
}

/* HostRuntime accepts the largest borrowed chunk. The C producer flushes at
 * this boundary before appending record 65,537, so its next callback cannot be
 * rejected for exceeding the packed-view contract. */
{
    const host = highGroupHost();
    const count = 65536;
    const records = new Int32Array(count * RECORD_WORDS);
    for( let index = 0; index < count; index++ ) {
        const base = index * RECORD_WORDS;
        records[base] = IF_SETHIDE;
        records[base + 1] = 12345; // proven missing target: deterministic no-op
    }
    host.requestFastPackedBatch(records, count, new Uint8Array(0));
    assert.throws(
        () => host.requestFastPackedBatch(records, count + 1, new Uint8Array(0)),
        (error) => error instanceof HostRuntimeError && error.code === 'BAD_REQUEST');
}

/* Production C/WASM boundary regression: records 1..65,536 are setters and
 * record 65,537 carries an arena-backed hook. The bridge must flush before it
 * reserves that hook payload, producing two valid callbacks in exact order. */
{
    const here = dirname(fileURLToPath(import.meta.url));
    const repo = findRepoRoot(here);
    const scriptId = 65000;
    const wideScriptId = 64999;
    const nestedScriptId = 64998;
    const fromDateScriptId = 64997;
    const source = [
        `// ${scriptId}`,
        '[clientscript,cs2dom_fast_boundary]()',
        'def_int $int0 = 0;',
        'while ($int0 < 65536) {',
        '  if_sethide(false, interface_12:0);',
        '  $int0 = calc($int0 + 1);',
        '}',
        'if_setonmouseover(null, interface_12:0);',
    ].join('\n');
    const wideArgs = Array.from({ length: 66 }, () => '1').join(', ');
    const wideSource = [
        `// ${wideScriptId}`,
        '[clientscript,cs2dom_fast_wide_hook]()',
        'if_sethide(true, interface_12:0);',
        `if_setonmouseover("script1(${wideArgs})", interface_12:0);`,
    ].join('\n');
    const nestedSource = [
        `// ${nestedScriptId}`,
        '[clientscript,cs2dom_fast_nested_create]()',
        'cc_create(interface_12:0, 0, 1, 0);',
        'cc_createchild(4, 2);',
        'cc_settext("nested");',
        'cc_createsibling(4, 3);',
        'cc_settext("sibling");',
    ].join('\n');
    const fromDateSource = [
        `// ${fromDateScriptId}`,
        '[clientscript,cs2dom_fromdate]()',
        'cc_create(interface_12:0, 4, 90, 0);',
        'cc_settext(fromdate(8037));',
    ].join('\n');
    const compiled = compileScripts([
        { id: scriptId, name: 'cs2dom_fast_boundary', source },
        { id: wideScriptId, name: 'cs2dom_fast_wide_hook', source: wideSource },
        { id: nestedScriptId, name: 'cs2dom_fast_nested_create', source: nestedSource },
        { id: fromDateScriptId, name: 'cs2dom_fromdate', source: fromDateSource },
    ], { repoRoot: repo, revision: 'osrs239', returnBytecode: true });
    assert(compiled.ok && compiled.bytecode.length === 4,
        `could not compile fast boundary script: ${compiled.output}`);

    const host = lowGroupHost();
    const chunks = [];
    const commitPacked = host.requestFastPackedBatch.bind(host);
    host.requestFastPackedBatch = (records, count, arena) => {
        chunks.push({ count, first: records[0], last: records[(count - 1) * RECORD_WORDS] });
        return commitPacked(records, count, arena);
    };
    const wasmPath = resolve(here, '../web/cs2vm_wasm.wasm');
    const runtime = await createWasmCS2Runtime({
        host,
        moduleFactory,
        wasmUrl: `data:application/wasm;base64,${readFileSync(wasmPath).toString('base64')}`,
        fastHost: true,
        program: {
            available: true, dialect: 'osrs', revision: 'osrs239',
            scripts: compiled.bytecode.map((script) => ({
                id: script.id, data: Buffer.from(script.bytes).toString('base64'),
            })),
        },
    });
    try {
        const result = runtime.invokeIntent({
            component: host.ref('root'), hook: { scriptId, args: [] }, locals: {},
        });
        assert.equal(result.hostRequests, 65537);
        assert.deepEqual(chunks, [
            { count: 65536, first: IF_SETHIDE, last: IF_SETHIDE },
            { count: 1, first: 2403, last: 2403 },
        ]);

        /* Shape rejection is a generic fallback barrier, not a script error:
         * the packed write must commit before JS receives the wide hook. */
        chunks.length = 0;
        const genericHooks = [];
        const genericRequest = host.request.bind(host);
        host.request = (request, ...rest) => {
            if( request?.kind === 'IF_SETONMOUSEOVER' ) genericHooks.push({
                signatureLength: request.signature.length,
                hiddenAtCall: host.component(rootWire).props.hidden,
            });
            return genericRequest(request, ...rest);
        };
        const rootWire = host.ref('root').componentId | 0;
        const wideResult = runtime.invokeIntent({
            component: host.ref('root'), hook: { scriptId: wideScriptId, args: [] }, locals: {},
        });
        assert.equal(wideResult.hostRequests, 2);
        assert.deepEqual(chunks, [{ count: 1, first: IF_SETHIDE, last: IF_SETHIDE }]);
        assert.deepEqual(genericHooks, [{ signatureLength: 66, hiddenAtCall: true }]);
        const installed = host.ir.components.find((component) => component.name === 'root')
            .hooks.on_mouse_over;
        assert.equal(installed.signature.length, 66);
        assert.equal(installed.args.length, 64);

        /* Pending CC_CREATE targets must remain acceptable to the core VM's
         * non-negative CC_CREATECHILD/CC_CREATESIBLING precheck. The bridge
         * flushes at the generic nested-create barrier, patches the real id,
         * and preserves both implicit-target updates in script order. */
        const nestedResult = runtime.invokeIntent({
            component: host.ref('root'), hook: { scriptId: nestedScriptId, args: [] }, locals: {},
        });
        assert.equal(nestedResult.scriptId, nestedScriptId);
        const parent = host.findChild('root', 1, false);
        const nested = host.findChild(parent, 2, false);
        const sibling = host.findChild(parent, 3, false);
        assert(parent && nested && sibling, 'nested fast create hierarchy is incomplete');
        assert.equal(host.component(nested).props.text, 'nested');
        assert.equal(host.component(sibling).props.text, 'sibling');
        assert.equal(host.activeRef().key, sibling.key);

        const dateResult = runtime.invokeIntent({
            component: host.ref('root'), hook: { scriptId: fromDateScriptId, args: [] }, locals: {},
        });
        assert.equal(dateResult.scriptId, fromDateScriptId);
        assert.equal(host.component(host.findChild('root', 90, false)).props.text,
            '29-Feb-2024');
    } finally { runtime.destroy(); }
}

console.log('fast HOST edge and boundary tests passed');
