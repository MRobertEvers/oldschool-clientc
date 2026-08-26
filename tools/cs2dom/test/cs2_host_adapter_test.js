import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { pathToFileURL, fileURLToPath } from 'node:url';

import { IF_TYPE } from '../src/components.js';
import { createHostRuntime } from '../src/host_runtime.js';

const HERE = dirname(fileURLToPath(import.meta.url));
const CS2DOM = resolve(HERE, '..');
const TSC = join(CS2DOM, 'node_modules', '.bin', 'tsc');
const OPCODE_BY_KIND = Object.freeze({
    PUSH_VARBIT: 25, PUSH_VARC_INT: 42, CC_CREATE: 100, CC_DELETEALL: 102,
    CC_FIND: 200, CC_SETPOSITION: 1000, CC_SETSIZE: 1001, CC_SETHIDE: 1003,
    CC_SETNOCLICKTHROUGH: 1005, CC_SETSCROLLPOS: 1100, CC_SETCOLOUR: 1101,
    CC_SETFILL: 1102, CC_SETTRANS: 1103, CC_SETGRAPHIC: 1105, CC_SETTILING: 1107,
    CC_SETTEXT: 1112, CC_SETTEXTFONT: 1113, CC_SETTEXTALIGN: 1114,
    CC_SETTEXTSHADOW: 1115, CC_SETOP: 1300, CC_SETDRAGGABLE: 1301,
    CC_SETDRAGGABLEBEHAVIOR: 1302, CC_SETONCLICK: 1400, CC_SETONHOLD: 1401,
    CC_SETONMOUSEOVER: 1403, CC_SETONMOUSELEAVE: 1404, CC_SETONDRAG: 1405,
    CC_SETONVARTRANSMIT: 1407, CC_SETONTIMER: 1408, CC_SETONOP: 1409,
    CC_SETONDRAGCOMPLETE: 1410, CC_SETONMOUSEREPEAT: 1412,
    CC_SETONSCROLLWHEEL: 1417, CC_SETONKEY: 1419, CC_GETY: 1501,
    CC_GETHEIGHT: 1503, CC_GETID: 1702, CC_SETCOMPONENTPARAM: 1704,
    IF_SETPOSITION: 2000, IF_SETSIZE: 2001, IF_SETHIDE: 2003,
    IF_SETSCROLLPOS: 2100, IF_SETCOLOUR: 2101, IF_SETOP: 2300,
    IF_SETOPBASE: 2305, IF_SETONVARTRANSMIT: 2407, IF_SETONTIMER: 2408,
    IF_SETONOP: 2409, IF_SETONSCROLLWHEEL: 2417, IF_GETWIDTH: 2502,
    IF_GETHEIGHT: 2503, IF_GETSCROLLHEIGHT: 2604, IF_SETPARAM: 2704,
    CLIENTCLOCK: 3300, ENUM: 3408, ENUM_GETOUTPUTCOUNT: 3411, STRUCT_PARAM: 6516,
});
const temporary = mkdtempSync(join(tmpdir(), 'cs2dom-direct-host-'));
let adapter;
try {
    const compiled = join(temporary, 'compiled');
    writeFileSync(join(temporary, 'package.json'), '{"type":"module"}\n');
    const build = spawnSync(TSC, [
        '--strict', '--target', 'ES2020', '--module', 'NodeNext',
        '--moduleResolution', 'NodeNext', '--skipLibCheck',
        '--rootDir', join(CS2DOM, 'src'), '--outDir', compiled,
        join(CS2DOM, 'src', 'generated', 'cs2_host.ts'),
        join(CS2DOM, 'src', 'cs2_host_adapter.ts'),
    ], { cwd: CS2DOM, encoding: 'utf8' });
    assert.equal(build.status, 0, `direct Host adapter TypeScript build failed:\n${build.stdout}${build.stderr}`);
    adapter = await import(pathToFileURL(join(compiled, 'cs2_host_adapter.js')).href);

    const manifest = JSON.parse(readFileSync(
        join(CS2DOM, 'wasm', 'cs2_host_executable_semantics.json'), 'utf8'));
    assert.deepEqual(
        [...adapter.CS2_DIRECT_HOST_EXECUTABLE_OPCODES],
        manifest.reviewed.map(({ opcode }) => opcode),
        'adapter and reviewed executable manifest diverged',
    );

    exerciseEveryReviewedHandler(adapter);
    exerciseHookABI(adapter);
    exerciseTargetsAndPolymorphicResults(adapter);
    exerciseAsyncVoidRejection(adapter);
    exerciseHostRuntimeMutations(adapter);
} finally {
    rmSync(temporary, { recursive: true, force: true });
}

console.log('direct TypeScript CS2 Host adapter tests passed');

function exerciseEveryReviewedHandler(api) {
    const seen = new Set();
    const calls = [];
    const results = {
        PUSH_VARBIT: 71, PUSH_VARC_INT: 72,
        CC_GETY: 73, CC_GETHEIGHT: 74, CC_GETID: -7,
        IF_GETWIDTH: 75, IF_GETHEIGHT: 76, IF_GETSCROLLHEIGHT: 77,
        CLIENTCLOCK: 78, ENUM: 79, ENUM_GETOUTPUTCOUNT: 80, STRUCT_PARAM: 81,
    };
    const host = new Proxy({}, {
        get(_target, kind) {
            return (...args) => {
                seen.add(Number(OPCODE_BY_KIND[kind]));
                calls.push({ kind, args });
                return results[kind];
            };
        },
    });
    const ref = (componentId) => ({ componentId });

    const cases = [
        [25, [], [], 9, 'PUSH_VARBIT', [9], [71], []],
        [42, [], [], 12, 'PUSH_VARC_INT', [12], [72], []],
        [102, [901], [], 0, 'CC_DELETEALL', [901], [], []],
        [1000, [10, 20, 1, 2], [], 0, 'CC_SETPOSITION', [111, 10, 20, 1, 2], [], []],
        [1001, [30, 40, 3, 4], [], 1, 'CC_SETSIZE', [222, 30, 40, 3, 4], [], []],
        [1003, [3], [], 0, 'CC_SETHIDE', [111, true], [], []],
        [1005, [4], [], 0, 'CC_SETNOCLICKTHROUGH', [111, 4], [], []],
        [1100, [5, 6], [], 0, 'CC_SETSCROLLPOS', [111, 5, 6], [], []],
        [1101, [0x123456], [], 0, 'CC_SETCOLOUR', [111, 0x123456], [], []],
        [1102, [1], [], 0, 'CC_SETFILL', [111, 1], [], []],
        [1103, [87], [], 0, 'CC_SETTRANS', [111, 87], [], []],
        [1105, [999], [], 0, 'CC_SETGRAPHIC', [111, 999], [], []],
        [1107, [1], [], 0, 'CC_SETTILING', [111, 1], [], []],
        [1112, [], ['hello'], 0, 'CC_SETTEXT', [111, 'hello'], [], []],
        [1113, [494], [], 0, 'CC_SETTEXTFONT', [111, 494], [], []],
        [1114, [1, 2, 13], [], 0, 'CC_SETTEXTALIGN', [111, 1, 2, 13], [], []],
        [1115, [1], [], 0, 'CC_SETTEXTSHADOW', [111, 1], [], []],
        [1300, [4], ['Use'], 0, 'CC_SETOP', [111, 4, 'Use'], [], []],
        [1301, [700, 8], [], 0, 'CC_SETDRAGGABLE', [111, 700, 8], [], []],
        [1302, [2], [], 0, 'CC_SETDRAGGABLEBEHAVIOR', [111, 2], [], []],
        [1501, [], [], 0, 'CC_GETY', [111], [73], []],
        [1503, [], [], 1, 'CC_GETHEIGHT', [222], [74], []],
        [1702, [], [], 0, 'CC_GETID', [111], [-7], []],
        [1704, [2365, 44, 1], [], 0, 'CC_SETCOMPONENTPARAM',
            [111, 2365, 44, null, 1], [], []],
        [2000, [10, 20, 1, 2, 900], [], 0, 'IF_SETPOSITION',
            [900, 10, 20, 1, 2], [], []],
        [2001, [30, 40, 3, 4, 901], [], 0, 'IF_SETSIZE',
            [901, 30, 40, 3, 4], [], []],
        [2003, [1, 902], [], 0, 'IF_SETHIDE', [902, true], [], []],
        [2100, [5, 6, 903], [], 0, 'IF_SETSCROLLPOS', [903, 5, 6], [], []],
        [2101, [0x345678, 904], [], 0, 'IF_SETCOLOUR', [904, 0x345678], [], []],
        [2300, [3, 905], ['Take'], 0, 'IF_SETOP', [905, 3, 'Take'], [], []],
        [2305, [906], ['Base'], 0, 'IF_SETOPBASE', [906, 'Base'], [], []],
        [2502, [907], [], 0, 'IF_GETWIDTH', [907], [75], []],
        [2503, [908], [], 0, 'IF_GETHEIGHT', [908], [76], []],
        [2604, [909], [], 0, 'IF_GETSCROLLHEIGHT', [909], [77], []],
        [2704, [2366, 55, 910, -1, 1], [], 0, 'IF_SETPARAM',
            [910, 2366, 55, null, 1], [], []],
        [3300, [], [], 0, 'CLIENTCLOCK', [0], [78], []],
        [3408, [105, 105, 12, 99], [], 0, 'ENUM', [105, 105, 12, 99], [79], []],
        [3411, [12], [], 0, 'ENUM_GETOUTPUTCOUNT', [12], [80], []],
        [6516, [60, 61], [], 0, 'STRUCT_PARAM', [60, 61], [81], []],
    ];

    for( const [opcode, ints, strings, operand, kind, expectedArgs,
        expectedInts, expectedStrings] of cases ) {
        const state = makeState(ints, strings, ref(111), ref(222));
        const before = calls.length;
        api.executeCS2DirectHostInstruction(state, { opcode, intOperand: operand }, host);
        assert.equal(calls.length, before + 1, `${opcode} did not make one Host call`);
        assert.deepEqual(calls.at(-1), { kind, args: expectedArgs }, `${opcode} payload/pop order`);
        assert.deepEqual(state.intStack, expectedInts, `${opcode} integer result`);
        assert.deepEqual(state.stringStack, expectedStrings, `${opcode} string result`);
    }

    /* Create/find are the only rows whose logical Host result also changes an
     * implicit target. Exercise both canonical and RS2 create arities. */
    results.CC_CREATE = ref(333);
    let state = makeState([700, IF_TYPE.text, 9, 1], [], ref(111), ref(222));
    api.executeCS2DirectHostInstruction(state, { opcode: 100, intOperand: 1 }, host);
    assert.deepEqual(calls.at(-1), {
        kind: 'CC_CREATE', args: [700, IF_TYPE.text, 9, 1, 1, 0],
    });
    assert.deepEqual(state.dotComponent, ref(333));
    seen.add(100);

    state = makeState([701, IF_TYPE.graphic, 10], [], ref(111), ref(222));
    state.dialect = 'rs2-dat2';
    api.executeCS2DirectHostInstruction(state, { opcode: 100 }, host);
    assert.deepEqual(calls.at(-1), {
        kind: 'CC_CREATE', args: [701, IF_TYPE.graphic, 10, 0, 0, 0],
    });

    results.CC_FIND = ref(444);
    state = makeState([700, 9], [], ref(111), ref(222));
    api.executeCS2DirectHostInstruction(state, { opcode: 200 }, host);
    assert.deepEqual(calls.at(-1), { kind: 'CC_FIND', args: [700, 9, 0] });
    assert.deepEqual(state.activeComponent, ref(444));
    assert.deepEqual(state.intStack, [1]);
    seen.add(200);

    const hookOpcodes = [
        1400, 1401, 1403, 1404, 1405, 1407, 1408, 1409,
        1410, 1412, 1417, 1419,
    ];
    for( const opcode of hookOpcodes ) {
        const hookState = makeState([1234], [''], ref(111), ref(222));
        api.executeCS2DirectHostInstruction(hookState, { opcode }, host);
        assert.equal(calls.at(-1).args[0], 111);
        seen.add(opcode);
    }
    const ifHookOpcodes = [2407, 2408, 2409, 2417];
    for( const opcode of ifHookOpcodes ) {
        const hookState = makeState([1234, 987], [''], ref(111), ref(222));
        api.executeCS2DirectHostInstruction(hookState, { opcode }, host);
        assert.equal(calls.at(-1).args[0], 987);
        seen.add(opcode);
    }

    assert.deepEqual([...seen].sort((a, b) => a - b),
        [...api.CS2_DIRECT_HOST_EXECUTABLE_OPCODES].sort((a, b) => a - b),
        'one or more reviewed handlers were not executable');
}

function exerciseHookABI(api) {
    const calls = [];
    const host = new Proxy({}, { get: (_target, kind) => (...args) => calls.push({ kind, args }) });
    let state = makeState(
        [7000, 41, 501, 502, 2],
        ['string-arg', 'isY'],
        { componentId: 111 }, { componentId: 222 },
    );
    api.executeCS2DirectHostInstruction(state, { opcode: 1400, intOperand: 1 }, host);
    assert.deepEqual(calls.at(-1), {
        kind: 'CC_SETONCLICK',
        args: [
            222, 7000, 'isY', [501, 502], 2,
            [41, 0], 2, [2, 0], 1, ['string-arg'],
        ],
    });

    /* Position 33 proves the upper half of the native uint64 string mask. */
    const signature = `${'i'.repeat(33)}s`;
    state = makeState([8000, ...Array.from({ length: 33 }, (_, index) => index + 1), 999],
        ['high-mask', signature], { componentId: 111 }, { componentId: 222 });
    api.executeCS2DirectHostInstruction(state, { opcode: 2408 }, host);
    const args = calls.at(-1).args;
    assert.equal(args[0], 999);
    assert.equal(args[1], 8000);
    assert.equal(args[6], 34);
    assert.deepEqual(args[7], [0, 2]);
    assert.deepEqual(args[9], ['high-mask']);

    let receiver = null;
    const receiverHost = {
        CC_SETONCLICK() { receiver = this; },
    };
    state = makeState([91], [''], { componentId: 111 }, { componentId: 222 });
    api.executeCS2DirectHostInstruction(state, { opcode: 1400 }, receiverHost);
    assert.strictEqual(receiver, receiverHost, 'SETON detached a Host method from its receiver');
}

function exerciseTargetsAndPolymorphicResults(api) {
    const calls = [];
    let findResult = null;
    let enumResult = 'enum-string';
    const host = new Proxy({}, {
        get(_target, kind) {
            return (...args) => {
                calls.push({ kind, args });
                if( kind === 'CC_FIND' ) return findResult;
                if( kind === 'ENUM' ) return enumResult;
                if( kind === 'STRUCT_PARAM' ) return -9;
                return undefined;
            };
        },
    });
    const original = { componentId: 123, generation: 4 };
    let state = makeState([700, 4], [], original, { componentId: 222 });
    api.executeCS2DirectHostInstruction(state, { opcode: 200 }, host);
    assert.strictEqual(state.activeComponent, original, 'failed find changed active target');
    assert.deepEqual(state.intStack, [0]);

    findResult = { componentId: 456, generation: 8 };
    state.intStack.push(700, 4);
    api.executeCS2DirectHostInstruction(state, { opcode: 200 }, host);
    assert.strictEqual(state.activeComponent, findResult);
    assert.deepEqual(state.intStack, [0, 1]);

    state.intStack.push(105, 115, 90, 3);
    api.executeCS2DirectHostInstruction(state, { opcode: 3408 }, host);
    assert.deepEqual(state.stringStack, ['enum-string']);
    enumResult = 123;
    state.intStack.push(105, 115, 90, 4);
    api.executeCS2DirectHostInstruction(state, { opcode: 3408 }, host);
    assert.deepEqual(state.stringStack, ['enum-string', '123'],
        'ENUM output descriptor, not Host primitive, selects the string stack');
    enumResult = '456';
    state.intStack.push(105, 105, 90, 5);
    api.executeCS2DirectHostInstruction(state, { opcode: 3408 }, host);
    assert.equal(state.intStack.at(-1), 456,
        'ENUM output descriptor, not Host primitive, selects the integer stack');
    state.intStack.push(12, 34);
    api.executeCS2DirectHostInstruction(state, { opcode: 6516 }, host);
    assert.deepEqual(state.intStack, [0, 1, 456, -9]);

    state = makeState([2365, 2], ['tag'], original, null);
    api.executeCS2DirectHostInstruction(state, { opcode: 1704 }, host);
    assert.deepEqual(calls.at(-1), {
        kind: 'CC_SETCOMPONENTPARAM', args: [123, 2365, 0, 'tag', 2],
    });
    state = makeState([2366, 910, -1, 115], ['named'], original, null);
    api.executeCS2DirectHostInstruction(state, { opcode: 2704 }, host);
    assert.deepEqual(calls.at(-1), {
        kind: 'IF_SETPARAM', args: [910, 2366, 0, 'named', 2],
    });

    assert.throws(
        () => api.executeCS2DirectHostInstruction(
            makeState([], [], original, null), { opcode: 1000 }, host),
        (error) => error?.code === 'INT_STACK_UNDERFLOW' && error.opcode === 1000,
    );
    assert.throws(
        () => api.executeCS2DirectHostInstruction(
            makeState([], [], original, null), { opcode: 999999 }, host),
        (error) => error?.code === 'UNSUPPORTED_OPCODE',
    );
    const handleState = makeState([], [], original, null);
    handleState.stringStack.push({ kind: 'cs2-array-handle', slot: 0 });
    assert.throws(
        () => api.executeCS2DirectHostInstruction(handleState, { opcode: 1112 }, host),
        (error) => error?.code === 'BAD_STRING_VALUE',
        'a core array handle was silently treated as a Host string',
    );
}

function exerciseAsyncVoidRejection(api) {
    const active = { componentId: 123 };
    assert.throws(() => api.executeCS2DirectHostInstruction(
        makeState([1], [], active, null),
        { opcode: 1003 },
        { CC_SETHIDE() { return Promise.resolve(); } },
    ), (error) => error?.code === 'ASYNC_HOST_RESULT' && error.opcode === 1003 &&
        /Promise\/thenable/.test(error.message),
    'a Promise-returning void Host setter crossed the synchronous boundary');

    assert.throws(() => api.executeCS2DirectHostInstruction(
        makeState([91], [''], active, null),
        { opcode: 1400 },
        { CC_SETONCLICK() { return { then() {} }; } },
    ), (error) => error?.code === 'ASYNC_HOST_RESULT' && error.opcode === 1400,
    'a thenable-returning SETON Host method crossed the synchronous boundary');
}

function exerciseHostRuntimeMutations(api) {
    const rootProps = {
        x: 0, y: 0, width: 200, height: 120,
        xMode: 0, yMode: 0, widthMode: 0, heightMode: 0,
        hidden: false, transparency: 0,
    };
    const labelProps = { ...rootProps, x: 4, y: 5, width: 80, height: 20, text: 'before' };
    const hostRuntime = createHostRuntime({
        interfaceId: 700,
        components: [
            component(0, 'root', IF_TYPE.layer, null, rootProps),
            component(1, 'label', IF_TYPE.text, 0, labelProps),
        ],
    }, { recordChanges: false });
    const host = api.createCS2RequestHostAdapter(hostRuntime);
    const root = hostRuntime.ref('root');
    const label = hostRuntime.ref('label');

    let state = makeState([], ['after'], label, root);
    api.executeCS2DirectHostInstruction(state, { opcode: 1112 }, host);
    assert.equal(hostRuntime.read('if_gettext', label), 'after');

    state.intStack.push(0x112233);
    api.executeCS2DirectHostInstruction(state, { opcode: 1101 }, host);
    assert.equal(hostRuntime.resolve(label).props.color, 0x112233);

    state = makeState([root.componentId, IF_TYPE.text, -3, 0], [], label, root);
    api.executeCS2DirectHostInstruction(state, { opcode: 100, intOperand: 1 }, host);
    const child = state.dotComponent;
    assert(child && child.componentId !== root.componentId);
    state.intStack.push(12, 13, 0, 0);
    api.executeCS2DirectHostInstruction(state, { opcode: 1000, intOperand: 1 }, host);
    assert.equal(hostRuntime.resolve(child).props.x, 12);
    assert.equal(hostRuntime.resolve(child).props.y, 13);

    state.intStack.push(9001, 8, 44, 45, 2);
    state.stringStack.push('argument', 'isY');
    api.executeCS2DirectHostInstruction(state, { opcode: 1400, intOperand: 1 }, host);
    const view = hostRuntime.resolve(child);
    assert(view.hooks.includes('on_click'));
}

function component(fileId, name, type, layer, props) {
    return {
        fileId, name, kind: type === IF_TYPE.text ? 'Text' : 'Layer', type, layer,
        static: props, props, authoredProps: new Set(), dynamic: [], ops: [],
        events: {}, hooks: {}, triggers: {}, dependencies: [], rawFields: {}, runtime: {},
    };
}

function makeState(intStack, stringStack, activeComponent, dotComponent) {
    return {
        intStack: [...intStack], stringStack: [...stringStack],
        activeComponent, dotComponent, dialect: 'canonical',
    };
}
