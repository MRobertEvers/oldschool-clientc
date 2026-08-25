import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import { IF_TYPE } from '../src/components.js';
import { createHostRuntime, HostRuntimeError } from '../src/host_runtime.js';
import { createWasmCS2Runtime, __wasmRuntimeTest } from '../src/wasm_runtime.js';
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

function lowGroupHost(options = {}) {
    return createHostRuntime({
        interfaceId: 12,
        components: [layer(0, 'root')],
    }, { viewport: { width: 128, height: 96 }, ...options });
}

const DB_HOST_DATA = Object.freeze({
    dbTables: {
        3: {
            id: 3,
            columns: {
                0: { types: ['int'], defaults: [[7]], tupleCount: 1 },
                1: { types: ['string'], defaults: [['fallback']], tupleCount: 1 },
                2: { types: ['int', 'int'], defaults: [[1, 2]], tupleCount: 1 },
                3: { types: ['int'], defaults: [[77]], tupleCount: 1 },
                4: { types: ['int'], defaults: [], tupleCount: 0 },
            },
        },
    },
    dbRows: {
        12: {
            id: 12,
            tableId: 3,
            columns: {
                0: { types: ['int'], values: [[995]], tupleCount: 2 },
                1: { types: ['string'], values: [['Coins']], tupleCount: 1 },
                2: { types: ['int', 'int'], values: [[41, 42]], tupleCount: 1 },
            },
        },
        13: {
            id: 13,
            tableId: 3,
            columns: {
                0: { types: ['int'], values: [[995]], tupleCount: 1 },
            },
        },
        14: {
            id: 14,
            tableId: 3,
            columns: {
                0: { types: ['int'], values: [[7]], tupleCount: 1 },
            },
        },
    },
});

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

/* Production-only fresh-create fusion must remain exactly equivalent to the
 * ordinary packed replay for every dynamic widget family, including rejected
 * type-specific setters, repeated/no-op transitions, operation removal, and
 * a clear of an absent hook. Repeat after delete-all to exercise the recycled
 * component/resource path rather than only first-use allocation. */
{
    const makeBatch = (rootWire) => {
        const encoder = new TextEncoder();
        const arenaBytes = [];
        const records = [];
        const addString = (text) => {
            const offset = arenaBytes.length;
            const bytes = encoder.encode(text);
            arenaBytes.push(...bytes);
            return [offset, bytes.length];
        };
        const add = (kind) => {
            const words = new Int32Array(RECORD_WORDS);
            words[0] = kind;
            records.push(words);
            return words;
        };
        const addSetter = (kind, token, values = []) => {
            const words = add(kind);
            words[1] = token;
            words[11] = 1;
            for( let index = 0; index < values.length; index++ )
                words[index + 2] = values[index];
            return words;
        };
        const addStringSetter = (kind, token, value, text) => {
            const [offset, length] = addString(text);
            return addSetter(kind, token, [value, offset, length]);
        };
        const types = [0, 2, IF_TYPE.rectangle, IF_TYPE.text, IF_TYPE.graphic,
            IF_TYPE.model, 8, IF_TYPE.line, 10, 255];
        let previous = rootWire;
        let previousIsToken = 0;
        for( let subId = 0; subId < types.length; subId++ ) {
            /* Match the C bridge's dense INT_MAX-serial token sequence. */
            const token = 0x7fffffff - (subId + 1);
            const create = add(100);
            create[1] = rootWire;
            create[2] = types[subId];
            create[3] = subId;
            create[7] = token;
            create[8] = previous;
            create[9] = previousIsToken;
            addSetter(1000, token, [3, 4, 1, 2]);
            addSetter(1000, token, [3, 4, 1, 2]); // exact no-op
            addSetter(1001, token, [31, 17, 0, 0]);
            addSetter(1003, token, [1]);
            addSetter(1003, token, [0]); // queues widgets-loaded
            addSetter(1101, token, [0x123456]);
            addSetter(1102, token, [1]);
            addSetter(1103, token, [73]);
            addSetter(1105, token, [4151]);
            addStringSetter(1112, token, 0, `child-${subId}`);
            addSetter(1113, token, [494]);
            addSetter(1114, token, [1, 2, 3]);
            addSetter(1115, token, [1]);
            addStringSetter(1300, token, 1, 'Use');
            addStringSetter(1300, token, 1, 'Use'); // exact no-op
            addStringSetter(1300, token, 1, ''); // remove
            addSetter(1307, token); // clear already-empty ops
            addStringSetter(1300, token, 11, 'ignored'); // invalid slot still decodes
            addStringSetter(1305, token, 0, `base-${subId}`);
            addSetter(1403, token, [-1]); // clear absent hook creates {}
            previous = token;
            previousIsToken = 1;
        }
        const words = new Int32Array(records.length * RECORD_WORDS);
        for( let index = 0; index < records.length; index++ )
            words.set(records[index], index * RECORD_WORDS);
        return { words, arena: Uint8Array.from(arenaBytes), count: records.length };
    };
    const view = (host) => ({
        version: host.version,
        widgetsLoaded: host.pendingTransmits.widgetsLoaded,
        activeSubId: host.activeRef()?.subId,
        children: host.children('root', { startIndex: -0x80000000 }).map((ref) => {
            const component = host.component(ref);
            const raw = host.ir.components.find((entry) => entry.name === component.name);
            return {
                subId: component.subId, type: component.type, props: component.props,
                ops: component.ops, hooks: raw.hooks, runtime: component.runtime,
            };
        }),
    });
    const fused = lowGroupHost({ recordChanges: false });
    const replayed = lowGroupHost({ recordChanges: false });
    replayed._fastApplyFreshPackedRun = (_component, _token, _records, start) => start - 1;
    for( let pass = 0; pass < 2; pass++ ) {
        if( pass ) {
            fused.deleteAll('root');
            replayed.deleteAll('root');
        }
        for( const host of [fused, replayed] ) {
            const batch = makeBatch(host.ref('root').componentId | 0);
            host.requestFastPackedBatch(batch.words, batch.count, batch.arena);
        }
        assert.deepEqual(view(fused), view(replayed),
            `fresh packed fusion diverged on pass ${pass + 1}`);
    }
}

/* Packed temporary ids are an internal dense INT_MAX-serial namespace. Reject
 * malformed/out-of-chunk ids before creating a child, distinguish an unknown
 * slot from a known missing-parent create, and preserve missing-target result
 * propagation through a later create. */
{
    const host = lowGroupHost({ recordChanges: false });
    const rootWire = host.ref('root').componentId | 0;
    const malformedCreate = new Int32Array(RECORD_WORDS);
    malformedCreate[0] = 100;
    malformedCreate[1] = rootWire;
    malformedCreate[2] = IF_TYPE.text;
    malformedCreate[3] = 1;
    malformedCreate[7] = 123;
    malformedCreate[8] = rootWire;
    assert.throws(
        () => host.requestFastPackedBatch(malformedCreate, 1, new Uint8Array(0)),
        (error) => error instanceof HostRuntimeError && error.code === 'BAD_REQUEST');
    assert.equal(host.findChild('root', 1, false), null,
        'malformed packed token partially created a child');

    const unknownPrevious = malformedCreate.slice();
    unknownPrevious[7] = 0x7ffffffe;
    unknownPrevious[8] = 0x7ffffffd;
    unknownPrevious[9] = 1;
    assert.throws(
        () => host.requestFastPackedBatch(unknownPrevious, 1, new Uint8Array(0)),
        (error) => error instanceof HostRuntimeError && error.code === 'BAD_REQUEST');
    assert.equal(host.findChild('root', 1, false), null,
        'unknown previous packed token partially created a child');

    const malformedSetter = new Int32Array(RECORD_WORDS);
    malformedSetter[0] = IF_SETHIDE;
    malformedSetter[1] = 123;
    malformedSetter[2] = 1;
    malformedSetter[11] = 1;
    assert.throws(
        () => host.requestFastPackedBatch(malformedSetter, 1, new Uint8Array(0)),
        (error) => error instanceof HostRuntimeError && error.code === 'BAD_REQUEST');

    const missing = new Int32Array(3 * RECORD_WORDS);
    missing[0] = 100;
    missing[1] = 12345;
    missing[2] = IF_TYPE.text;
    missing[3] = 2;
    missing[7] = 0x7ffffffe;
    missing[8] = rootWire;
    missing[RECORD_WORDS] = IF_SETHIDE;
    missing[RECORD_WORDS + 1] = 0x7ffffffe;
    missing[RECORD_WORDS + 2] = 1;
    missing[RECORD_WORDS + 11] = 1;
    missing[2 * RECORD_WORDS] = 100;
    missing[2 * RECORD_WORDS + 1] = 12345;
    missing[2 * RECORD_WORDS + 2] = IF_TYPE.text;
    missing[2 * RECORD_WORDS + 3] = 3;
    missing[2 * RECORD_WORDS + 7] = 0x7ffffffd;
    missing[2 * RECORD_WORDS + 8] = 0x7ffffffe;
    missing[2 * RECORD_WORDS + 9] = 1;
    host.requestFastPackedBatch(missing, 3, new Uint8Array(0));
    assert.equal(missing[6], rootWire);
    assert.equal(missing[2 * RECORD_WORDS + 6], rootWire);
}

/* An outer dispatch snapshots interaction once, after all synchronous hooks
 * and visibility retirement. A React-authored hook making a nested public Host
 * call still receives its immediate interaction view rather than the outer
 * boundary's deferred placeholder. */
{
    const root = layer(0, 'root');
    root.ops = [{ index: 1, text: 'Use' }];
    root.hooks = { on_timer: { script: { id: 64990 }, args: [] } };
    let nestedResult = null;
    const host = createHostRuntime({ interfaceId: 12, components: [root] }, {
        viewport: { width: 128, height: 96 },
        invoke: (intent, runtime) => {
            nestedResult = runtime.writeState('varp', 1, 7);
            runtime.mutate('if_sethide', intent.component, true);
        },
    });
    host.dispatch({ type: 'pointer_move', x: 8, y: 8 });
    assert.equal(host.activeRef(), null);
    assert.equal(host.snapshot().interaction.hover?.name, 'root');

    const interactionView = host._interactionView.bind(host);
    let interactionViews = 0;
    host._interactionView = () => {
        interactionViews++;
        return interactionView();
    };
    const result = host.dispatch({ type: 'tick', cycle: 1 });
    assert(nestedResult?.interaction && nestedResult.interaction.hover?.name === 'root',
        'nested React Host result lost its immediate interaction view');
    assert.equal(result.interaction.hover, null,
        'outer result was not refreshed after synchronous visibility changes');
    assert.equal(interactionViews, 2,
        'outer dispatch built a discarded pre-reconciliation interaction view');
}

/* FROMDATE uses the client's fixed UTC epoch and English month table. */
{
    const host = lowGroupHost();
    assert.equal(host.request({ kind: 'FROMDATE', day: -11745 }), '1-Jan-1970');
    assert.equal(host.request({ kind: 'FROMDATE', day: 0 }), '27-Feb-2002');
    assert.equal(host.request({ kind: 'FROMDATE', day: 8037 }), '29-Feb-2024');
    assert.equal(host.request({ kind: 'FROMDATE', day: 8038 }), '1-Mar-2024');
}

/* The immutable preload contains exact scalar DB records, including typed
 * missing-tuple and table-default values, but never a polymorphic whole tuple. */
{
    const host = lowGroupHost({ hostData: DB_HOST_DATA });
    const { records, arena } = __wasmRuntimeTest.fastScalarPreload(host);
    const found = new Map();
    const rowTables = new Map();
    for( let at = 0; at < records.length; at += 9 ) {
        if( records[at] === 7505 ) {
            rowTables.set(records[at + 1], records[at + 5]);
            continue;
        }
        if( records[at] !== 7502 ) continue;
        const key = `${records[at + 1]}:${records[at + 2]}:${records[at + 3]}`;
        found.set(key, records[at + 4] === 2
            ? new TextDecoder().decode(arena.subarray(
                records[at + 6], records[at + 6] + records[at + 7]))
            : records[at + 5]);
    }
    assert.equal(found.get('12:12288:0'), 995);
    assert.equal(found.get('12:12288:1'), -1);
    assert.equal(found.get('12:12304:0'), 'Coins');
    assert.equal(found.get('12:12336:0'), 77);
    assert.equal(found.get('12:12352:0'), -1);
    assert.equal(found.has('12:12320:0'), false,
        'whole multi-field DB tuples must retain generic arity handling');
    assert.equal(found.get('12:12321:0'), 41);
    assert.equal(found.get('12:12322:0'), 42);
    assert.equal(rowTables.get(12), 3);
    assert.equal(rowTables.get(13), 3);

    host.request({ kind: 'DB_FIND_WITH_COUNT', column: 12288, typeTag: 0, value: 995 });
    const iterator = host.fastHostDbIteratorSnapshot();
    assert.deepEqual([...iterator.rows], [12, 13]);
    assert.equal(iterator.cursor, 0);
    assert.equal(host.fastHostDbIteratorCommit(iterator.revision, 1), true);
    assert.equal(host.snapshot().db.iterator.cursor, 1);
    host.request({ kind: 'DB_FINDALL', tableId: 3 });
    assert.equal(host.fastHostDbIteratorCommit(iterator.revision, 2), false,
        'a stale native iterator revision overwrote a replacement query');

    const override = lowGroupHost({
        hostData: DB_HOST_DATA,
        db: { dbTables: DB_HOST_DATA.dbTables, dbRows: {
            12: { id: 12, tableId: 3, columns: {
                0: { types: ['int'], values: [[123]], tupleCount: 1 },
            } },
        } },
    });
    assert.notEqual(override.fastHostScalarDataIdentity(), DB_HOST_DATA,
        'an explicit DB override must not reuse its HostData scalar namespace');
    assert.equal(override.fastHostDbDataSnapshot(), null);
    assert.equal(override.fastHostDbIteratorSnapshot(), null);
    assert.equal(override.fastHostDbIteratorCommit(1, 0), false);
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
    const dbIntScriptId = 64996;
    const dbStringScriptId = 64995;
    const dbMissingScriptId = 64994;
    const dbDefaultScriptId = 64993;
    const dbMultiScriptId = 64992;
    const dbIteratorScriptId = 64991;
    const dbNextScriptId = 64990;
    const dbRowTableScriptId = 64989;
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
    const dbIntSource = [
        `// ${dbIntScriptId}`,
        '[clientscript,cs2dom_db_fast_int]()',
        'cc_create(interface_12:0, 4, 91, 0);',
        'cc_settext(tostring(db_getfield(12, 12288, 0)));',
    ].join('\n');
    const dbStringSource = [
        `// ${dbStringScriptId}`,
        '[clientscript,cs2dom_db_fast_string]()',
        'cc_create(interface_12:0, 4, 92, 0);',
        'cc_settext(db_getfield(12, 12304, 0));',
    ].join('\n');
    const dbMissingSource = [
        `// ${dbMissingScriptId}`,
        '[clientscript,cs2dom_db_fast_missing]()',
        'cc_create(interface_12:0, 4, 93, 0);',
        'cc_settext(tostring(db_getfield(12, 12288, 1)));',
    ].join('\n');
    const dbDefaultSource = [
        `// ${dbDefaultScriptId}`,
        '[clientscript,cs2dom_db_fast_default]()',
        'cc_create(interface_12:0, 4, 94, 0);',
        'cc_settext(tostring(db_getfield(12, 12336, 0)));',
    ].join('\n');
    const dbMultiSource = [
        `// ${dbMultiScriptId}`,
        '[clientscript,cs2dom_db_generic_multi]()',
        'def_int $int0 = 0;',
        'def_int $int1 = 0;',
        '$int0, $int1 = db_getfield(12, 12320, 0);',
        'cc_create(interface_12:0, 4, 95, 0);',
        'cc_settext(tostring($int0));',
    ].join('\n');
    const dbIteratorSource = [
        `// ${dbIteratorScriptId}`,
        '[clientscript,cs2dom_db_fast_iterator]()',
        'def_int $count0 = db_find_with_count(12288, 995, 0);',
        'def_dbrow $first1 = db_findnext;',
        'def_dbrow $second2 = db_findnext;',
        'def_dbrow $end3 = db_findnext;',
        'cc_create(interface_12:0, 4, 96, 0);',
        'cc_settext(tostring($second2));',
    ].join('\n');
    const dbNextSource = [
        `// ${dbNextScriptId}`,
        '[clientscript,cs2dom_db_fast_next]()',
        'def_dbrow $first0 = db_findnext;',
        'def_dbrow $second1 = db_findnext;',
        'cc_create(interface_12:0, 4, 97, 0);',
        'cc_settext(tostring($second1));',
    ].join('\n');
    const dbRowTableSource = [
        `// ${dbRowTableScriptId}`,
        '[clientscript,cs2dom_db_fast_rowtable]()',
        'cc_create(interface_12:0, 4, 98, 0);',
        'cc_settext(tostring(db_getrowtable(12)));',
    ].join('\n');
    const compiled = compileScripts([
        { id: scriptId, name: 'cs2dom_fast_boundary', source },
        { id: wideScriptId, name: 'cs2dom_fast_wide_hook', source: wideSource },
        { id: nestedScriptId, name: 'cs2dom_fast_nested_create', source: nestedSource },
        { id: fromDateScriptId, name: 'cs2dom_fromdate', source: fromDateSource },
        { id: dbIntScriptId, name: 'cs2dom_db_fast_int', source: dbIntSource },
        { id: dbStringScriptId, name: 'cs2dom_db_fast_string', source: dbStringSource },
        { id: dbMissingScriptId, name: 'cs2dom_db_fast_missing', source: dbMissingSource },
        { id: dbDefaultScriptId, name: 'cs2dom_db_fast_default', source: dbDefaultSource },
        { id: dbMultiScriptId, name: 'cs2dom_db_generic_multi', source: dbMultiSource },
        { id: dbIteratorScriptId, name: 'cs2dom_db_fast_iterator', source: dbIteratorSource },
        { id: dbNextScriptId, name: 'cs2dom_db_fast_next', source: dbNextSource },
        { id: dbRowTableScriptId, name: 'cs2dom_db_fast_rowtable', source: dbRowTableSource },
    ], { repoRoot: repo, revision: 'osrs239', returnBytecode: true });
    assert(compiled.ok && compiled.bytecode.length === 12,
        `could not compile fast boundary script: ${compiled.output}`);

    const host = lowGroupHost({ hostData: DB_HOST_DATA });
    const chunks = [];
    const commitPacked = host.requestFastPackedBatch.bind(host);
    host.requestFastPackedBatch = (records, count, arena) => {
        chunks.push({ count, first: records[0], last: records[(count - 1) * RECORD_WORDS] });
        return commitPacked(records, count, arena);
    };
    const wasmPath = resolve(here, '../web/cs2vm_wasm.wasm');
    const program = {
        available: true, dialect: 'osrs', revision: 'osrs239',
        scripts: compiled.bytecode.map((script) => ({
            id: script.id, data: Buffer.from(script.bytes).toString('base64'),
        })),
    };
    const runtime = await createWasmCS2Runtime({
        host,
        moduleFactory,
        wasmUrl: `data:application/wasm;base64,${readFileSync(wasmPath).toString('base64')}`,
        fastHost: true,
        program,
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

        let genericDbCalls = 0;
        let genericDbFindCalls = 0;
        let genericDbNextCalls = 0;
        let genericDbRowTableCalls = 0;
        const dbGenericRequest = host.request.bind(host);
        host.request = (request, ...rest) => {
            if( request?.kind === 'DB_GETFIELD' ) genericDbCalls++;
            if( request?.kind === 'DB_FIND_WITH_COUNT' ) genericDbFindCalls++;
            if( request?.kind === 'DB_FINDNEXT' ) genericDbNextCalls++;
            if( request?.kind === 'DB_GETROWTABLE' ) genericDbRowTableCalls++;
            return dbGenericRequest(request, ...rest);
        };
        let firstDbIntResult = null;
        for( const [dbScriptId, childIndex, expected] of [
            [dbIntScriptId, 91, '995'],
            [dbStringScriptId, 92, 'Coins'],
            [dbMissingScriptId, 93, '-1'],
            [dbDefaultScriptId, 94, '77'],
        ] ) {
            const dbResult = runtime.invokeIntent({
                component: host.ref('root'), hook: { scriptId: dbScriptId, args: [] }, locals: {},
            });
            if( dbScriptId === dbIntScriptId ) firstDbIntResult = dbResult;
            assert.equal(dbResult.scriptId, dbScriptId);
            assert.equal(host.component(host.findChild('root', childIndex, false)).props.text,
                expected);
        }
        assert.equal(genericDbCalls, 0,
            'preloaded scalar DB_GETFIELD calls crossed the generic JS bridge');
        assert.equal(firstDbIntResult.fastScalarL1Hits, 0);
        assert.equal(firstDbIntResult.fastScalarL1Misses, 1);
        const repeatedDbInt = runtime.invokeIntent({
            component: host.ref('root'), hook: { scriptId: dbIntScriptId, args: [] }, locals: {},
        });
        assert.equal(repeatedDbInt.fastScalarL1Hits, 1);
        assert.equal(repeatedDbInt.fastScalarL1Misses, 0);
        assert.equal(genericDbCalls, 0);

        const multiResult = runtime.invokeIntent({
            component: host.ref('root'), hook: { scriptId: dbMultiScriptId, args: [] }, locals: {},
        });
        assert.equal(multiResult.scriptId, dbMultiScriptId);
        assert.equal(genericDbCalls, 1,
            'a polymorphic whole DB tuple did not retain generic arity handling');
        assert.equal(host.component(host.findChild('root', 95, false)).props.text, '41');

        const iteratorResult = runtime.invokeIntent({
            component: host.ref('root'), hook: { scriptId: dbIteratorScriptId, args: [] }, locals: {},
        });
        assert.equal(iteratorResult.scriptId, dbIteratorScriptId);
        assert.equal(genericDbFindCalls, 1,
            'the stateful DB query must still execute once in the React Host');
        assert.equal(genericDbNextCalls, 0,
            'a bounded DB iterator snapshot did not keep DB_FINDNEXT inside C');
        assert.equal(host.component(host.findChild('root', 96, false)).props.text, '13');
        assert.deepEqual(host.snapshot().db.iterator, { rows: [12, 13], cursor: 2 },
            'native DB_FINDNEXT progress was not committed to the React Host');

        host.request({ kind: 'DB_FIND_WITH_COUNT', column: 12288, typeTag: 0, value: 995 });
        const nextCallsBefore = genericDbNextCalls;
        runtime.invokeIntent({
            component: host.ref('root'), hook: { scriptId: dbNextScriptId, args: [] }, locals: {},
        });
        assert.equal(genericDbNextCalls - nextCallsBefore, 1,
            'a generic first DB_FINDNEXT did not seed the remaining native iterator');
        assert.equal(host.component(host.findChild('root', 97, false)).props.text, '13');
        assert.deepEqual(host.snapshot().db.iterator, { rows: [12, 13], cursor: 2 });

        runtime.invokeIntent({
            component: host.ref('root'),
            hook: { scriptId: dbRowTableScriptId, args: [] }, locals: {},
        });
        assert.equal(genericDbRowTableCalls, 0,
            'preloaded DB_GETROWTABLE crossed the generic JS bridge');
        assert.equal(host.component(host.findChild('root', 98, false)).props.text, '3');
    } finally { runtime.destroy(); }

    /* Fast cache hits and the fully generic bridge must produce the same live
     * component presentation for every scalar type and the multi-value case. */
    const genericHost = lowGroupHost({ hostData: DB_HOST_DATA });
    const genericRuntime = await createWasmCS2Runtime({
        host: genericHost,
        moduleFactory,
        wasmUrl: `data:application/wasm;base64,${readFileSync(wasmPath).toString('base64')}`,
        fastHost: false,
        program,
    });
    try {
        for( const [dbScriptId, childIndex, expected] of [
            [dbIntScriptId, 91, '995'],
            [dbStringScriptId, 92, 'Coins'],
            [dbMissingScriptId, 93, '-1'],
            [dbDefaultScriptId, 94, '77'],
            [dbMultiScriptId, 95, '41'],
        ] ) {
            genericRuntime.invokeIntent({
                component: genericHost.ref('root'),
                hook: { scriptId: dbScriptId, args: [] },
                locals: {},
            });
            assert.equal(genericHost.component(
                genericHost.findChild('root', childIndex, false)).props.text, expected);
        }
        genericRuntime.invokeIntent({
            component: genericHost.ref('root'),
            hook: { scriptId: dbIteratorScriptId, args: [] },
            locals: {},
        });
        assert.equal(genericHost.component(
            genericHost.findChild('root', 96, false)).props.text, '13');
        assert.deepEqual(genericHost.snapshot().db.iterator, { rows: [12, 13], cursor: 2 });
        genericRuntime.invokeIntent({
            component: genericHost.ref('root'),
            hook: { scriptId: dbRowTableScriptId, args: [] }, locals: {},
        });
        assert.equal(genericHost.component(
            genericHost.findChild('root', 98, false)).props.text, '3');
    } finally { genericRuntime.destroy(); }

    /* A caller-supplied DB state must not see DB entries already cached under
     * the shared HostData identity. Its isolated namespace falls through with
     * all three operands restored and returns the override's exact value. */
    const overrideHost = lowGroupHost({
        hostData: DB_HOST_DATA,
        db: { dbTables: DB_HOST_DATA.dbTables, dbRows: {
            12: { id: 12, tableId: 3, columns: {
                0: { types: ['int'], values: [[123]], tupleCount: 1 },
            } },
        } },
    });
    let overrideDbCalls = 0;
    let overrideDbNextCalls = 0;
    let overrideDbRowTableCalls = 0;
    const overrideRequest = overrideHost.request.bind(overrideHost);
    overrideHost.request = (request, ...rest) => {
        if( request?.kind === 'DB_GETFIELD' ) overrideDbCalls++;
        if( request?.kind === 'DB_FINDNEXT' ) overrideDbNextCalls++;
        if( request?.kind === 'DB_GETROWTABLE' ) overrideDbRowTableCalls++;
        return overrideRequest(request, ...rest);
    };
    const overrideRuntime = await createWasmCS2Runtime({
        host: overrideHost,
        moduleFactory,
        wasmUrl: `data:application/wasm;base64,${readFileSync(wasmPath).toString('base64')}`,
        fastHost: true,
        program,
    });
    try {
        overrideRuntime.invokeIntent({
            component: overrideHost.ref('root'),
            hook: { scriptId: dbIntScriptId, args: [] },
            locals: {},
        });
        assert.equal(overrideHost.component(
            overrideHost.findChild('root', 91, false)).props.text, '123');
        assert.equal(overrideDbCalls, 1);
        overrideRuntime.invokeIntent({
            component: overrideHost.ref('root'),
            hook: { scriptId: dbIteratorScriptId, args: [] },
            locals: {},
        });
        assert.equal(overrideDbNextCalls, 3,
            'an explicit DB override leaked into the native iterator path');
        assert.equal(overrideHost.component(
            overrideHost.findChild('root', 96, false)).props.text, '-1');
        overrideRuntime.invokeIntent({
            component: overrideHost.ref('root'),
            hook: { scriptId: dbRowTableScriptId, args: [] }, locals: {},
        });
        assert.equal(overrideDbRowTableCalls, 1,
            'an explicit DB override reused a preloaded DB_GETROWTABLE answer');
    } finally { overrideRuntime.destroy(); }
}

console.log('fast HOST edge and boundary tests passed');
