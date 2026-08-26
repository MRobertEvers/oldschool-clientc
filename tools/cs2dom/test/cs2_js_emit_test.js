/*
 * The AOT back end's own tests.
 *
 * Two kinds. The first are hand-built syntax trees exercising one lowering
 * decision each, because a corpus run tells you something failed and not what.
 * The second is the cross-check between the emitter and the generated host
 * surface: they are produced by different tools from different sources, and
 * every method one emits the other must declare, or generated code calls a
 * method nobody knew to implement.
 */

import assert from 'node:assert/strict';

import { emitScript, scriptFunctionName, Cs2EmitError } from '../src/cs2_js_emit.js';
import { HOST_SURFACE } from '../src/generated/cs2_host_surface.js';
import { PARK_CLASS_BY_OPCODE, HOST_PARK } from '../src/generated/cs2_host_park.js';
import * as K from '../src/cs2_intrinsics.js';

const tests = [];
function test(name, fn) { tests.push([name, fn]); }

/* -------------------------------------------------------------------------
 * Tree builders
 * ---------------------------------------------------------------------- */

function script({ id = 1, name = '[clientscript,t]', args = [], returns = [], body }) {
    return {
        schema: 'rscache-cs2-ast/1',
        id, name,
        arguments: args,
        returns,
        frame: { localInts: 0, localStrings: 0, intArguments: 0, stringArguments: 0 },
        body,
    };
}

function seq(instructions, next = null) {
    return { kind: 'seq', instructions, next };
}

function local(kind, id, identifier = kind) {
    return { kind, id, local: true, type: 'int', identifier,
             stackType: kind === 'string' || kind === 'array' ? 'string' : 'int' };
}

function access(variable) { return { kind: 'access', variable }; }

function global_(kind, id) {
    return { kind, id, local: false, type: 'int', identifier: 'int', stackType: 'int' };
}

function konst(value, extra = {}) {
    return { kind: 'constant', stackType: 'int', value, type: 'int',
             identifier: 'int', literal: String(value), ...extra };
}

function op(opcode, name, args, stackTypes = ['int'], extra = {}) {
    return { kind: 'operation', opcode, name, dot: false, calcInfix: null,
             branchInfix: null, arguments: args, stackTypes, ...extra };
}

function assign(definitions, expression) {
    return { kind: 'assignment', definitions, expression };
}

/* -------------------------------------------------------------------------
 * Lowering decisions
 * ---------------------------------------------------------------------- */

test('arithmetic never becomes a JavaScript operator', () => {
    const result = emitScript(script({
        body: seq([assign([access(local('int', 0))],
            op(4002, 'multiply', [konst(70000), konst(70000)], ['int'],
               { calcInfix: '*' }))]),
    }));
    assert.match(result.code, /K\.multiply\(70000, 70000\)/);
    assert.doesNotMatch(result.code, /70000 \* 70000/);
    /* And the intrinsic it routes to is the one that agrees with C: the
     * product overflows int32, which `*` would not reproduce. */
    assert.equal(K.multiply(70000, 70000), Math.imul(70000, 70000));
    assert.notEqual(70000 * 70000, K.multiply(70000, 70000));
});

test('comparisons do become operators, and === not ==', () => {
    const result = emitScript(script({
        body: { kind: 'if', branches: [{
            condition: op(8, 'branch_equals', [access(local('int', 0)), konst(3)], [],
                          { branchInfix: '=' }),
            body: seq([]),
        }], otherwise: null, next: null },
    }));
    assert.match(result.code, /\(\$int0 === 3\)/);
});

test('a parking call becomes a retry loop, a non-parking one does not', () => {
    const parking = emitScript(script({
        body: seq([assign([access(local('int', 0))],
            op(200, 'cc_find', [konst(5), konst(1)], ['bool']))]),
    }));
    assert.match(parking.code, /while \(\(t0 = H\.cc_find\(5, 1\)\) === PARK\) yield;/);
    assert.deepEqual(parking.parksOn, ['component']);

    const plain = emitScript(script({
        body: seq([assign([], op(1101, 'cc_setcolour', [konst(255)], []))]),
    }));
    assert.match(plain.code, /^\s*H\.cc_setcolour\(255\);$/m);
    assert.doesNotMatch(plain.code, /PARK/);
});

test('an array and the string of the same index are one slot', () => {
    /*
     * A rev-239 array handle lives in a string local, so the decompiler prints
     * the handle as `$string1` and its elements as `$xarray1`. Emitting two
     * variables declares one and reads the other.
     */
    const result = emitScript(script({
        body: seq([
            assign([], op(44, 'define_array',
                [access(local('array', 1)), konst(10)], [])),
            assign([], op(9999, 'sort_it', [access(local('string', 1))], [])),
        ]),
    }));
    const names = [...result.code.matchAll(/\$\w+/g)].map((m) => m[0]);
    assert.equal(new Set(names).size, 1, `expected one name, got ${[...new Set(names)]}`);
});

test('define_array stores into its first argument, not into nothing', () => {
    const result = emitScript(script({
        body: seq([assign([], op(44, 'define_array',
            [access(local('array', 2)), konst(7)], []))]),
    }));
    assert.match(result.code, /\$\w+ = K\.defineArray\(7, "string"\);/);
});

test('a multi-valued expression fills several argument slots', () => {
    /*
     * `scale(~script5787, 32)` is real: the proc returns two ints, so two
     * argument nodes fill scale's three slots. Counting nodes rejects it;
     * counting stack slots lowers it with a spread.
     */
    const proc = { kind: 'proc', scriptId: 5787, name: null, arguments: [],
                   stackTypes: ['int', 'int'] };
    const result = emitScript(script({
        body: seq([assign([access(local('int', 0))],
            op(4018, 'scale', [proc, konst(32)], ['int']))]),
    }));
    assert.match(result.code, /K\.scale\(\.\.\.\(\(yield\* cs2_5787\(H\)\)\), 32\)/);
});

test('the frame is declared zeroed, as the VM hands it over', () => {
    const result = emitScript(script({
        args: [local('int', 0, 'component')],
        body: seq([assign([access(local('string', 3))], konst(0))]),
    }));
    assert.match(result.code, /function\* cs2_1\(H, \$component0\)/);
    assert.match(result.code, /let \$string3 = '';/);
    /* An argument is already bound; re-declaring it would shadow the value. */
    assert.doesNotMatch(result.code, /let \$component0/);
});

test('globals read and write through the host, never as variables', () => {
    const result = emitScript(script({
        body: seq([assign([access(global_('varbit', 698))],
            access(global_('varp', 300)))]),
    }));
    assert.match(result.code, /H\.setVarbit\(698, H\.varp\(300\)\)/);
});

test('a proc call is yield*, so a callee parking suspends this frame too', () => {
    const result = emitScript(script({
        body: seq([assign([access(local('int', 0))],
            { kind: 'proc', scriptId: 42, name: 'thing', arguments: [konst(1)],
              stackTypes: ['int'] })]),
    }));
    assert.match(result.code, /yield\* cs2_42\(H, 1\)/);
    assert.deepEqual(result.procs, [42]);
});

test('a hook registration is a binding record, not a rebuilt string', () => {
    const result = emitScript(script({
        body: seq([assign([], {
            kind: 'clientscript', opcode: 2407, name: 'if_setonvartransmit', dot: false,
            scriptId: 5256, scriptName: null,
            arguments: [konst(7)], triggers: [konst(300)],
            component: konst(0x30001),
        })]),
    }));
    assert.match(result.code, /H\.if_setonvartransmit\(K\.hook\(5256, \[7\], \[300\]\), 196609\)/);
    assert.deepEqual(result.hooks, [5256]);
});

test('a wrong intrinsic arity is refused, not silently miscalled', () => {
    assert.throws(
        () => emitScript(script({
            body: seq([assign([], op(4003, 'div', [konst(1)], ['int']))]),
        })),
        (error) => error instanceof Cs2EmitError && /div takes 2 values/.test(error.message));
});

test('a returned tuple is destructured through a temporary', () => {
    const result = emitScript(script({
        body: seq([assign(
            [access(local('int', 0)), access(global_('varp', 5))],
            { kind: 'proc', scriptId: 9, name: 'pair', arguments: [],
              stackTypes: ['int', 'int'] })]),
    }));
    assert.match(result.code, /const t0 = \(yield\* cs2_9\(H\)\);/);
    assert.match(result.code, /\$int0 = t0\[0\];/);
    assert.match(result.code, /H\.setVarp\(5, t0\[1\]\);/);
});

/* -------------------------------------------------------------------------
 * Cross-checks between generated artifacts
 * ---------------------------------------------------------------------- */

test('every park class the table names is a real load class', () => {
    const classes = new Set(PARK_CLASS_BY_OPCODE.values());
    assert.ok(classes.size > 0);
    for( const name of classes )
        assert.match(name, /^[a-z]+$/);
    assert.ok(PARK_CLASS_BY_OPCODE.has(200), 'cc_find must be able to park on its group');
    assert.equal(PARK_CLASS_BY_OPCODE.get(1105), 'sprite');
    assert.ok(!PARK_CLASS_BY_OPCODE.has(1101), 'cc_setcolour needs nothing loaded');
});

test('HOST_PARK is a value no host answer could collide with', () => {
    assert.equal(typeof HOST_PARK, 'symbol');
});

test('the host surface declares every command the emitter can call', () => {
    /*
     * The two are generated by different tools from different tables. If the
     * emitter's intrinsic list and the surface generator's disagree, one side
     * emits `H.add(...)` while the other never declares it — or the surface
     * advertises a method no generated code ever calls. Both are silent.
     */
    for( const [name, row] of HOST_SURFACE )
    {
        assert.match(name, /^[a-z][a-z0-9_]*$/, `odd method name '${name}'`);
        assert.ok(Array.isArray(row.params));
        assert.ok(Array.isArray(row.results));
        if( row.park !== null )
            assert.equal(row.park, PARK_CLASS_BY_OPCODE.get(row.opcode),
                `${name} disagrees with the park table`);
    }
    /* Nothing the emitter turns into an intrinsic may also be a host method. */
    for( const opcode of [4000, 4002, 4003, 4117, 8003, 44, 45, 46, 37] )
        for( const [name, row] of HOST_SURFACE )
            assert.notEqual(row.opcode, opcode,
                `opcode ${opcode} is an intrinsic but the surface declares '${name}'`);
});

test('the surface covers what the emitter actually emits, not just itself', () => {
    /*
     * The direction that matters, and the one a self-consistency check misses.
     * Walking the surface proves nothing about the emitter; the hook setters
     * were absent from the surface for exactly as long as nothing asked this
     * question, because their command kind is named `clientscript` and reads
     * structural. Emit one call of each shape and demand the surface knows it.
     */
    const shapes = [
        op(1101, 'cc_setcolour', [konst(1)], []),
        op(2105, 'if_setgraphic', [konst(1), konst(2)], []),
        op(200, 'cc_find', [konst(1), konst(2)], ['bool']),
        { kind: 'clientscript', opcode: 2400, name: 'if_setonclick', dot: false,
          scriptId: 5, scriptName: null, arguments: [], triggers: [], component: konst(1) },
        { kind: 'clientscript', opcode: 1409, name: 'cc_setonop', dot: false,
          scriptId: 6, scriptName: null, arguments: [], triggers: [], component: null },
        { kind: 'clientscript', opcode: 2407, name: 'if_setonvartransmit', dot: false,
          scriptId: 7, scriptName: null, arguments: [], triggers: [konst(300)],
          component: konst(1) },
    ];
    for( const shape of shapes )
    {
        const result = emitScript(script({
            body: seq([assign([], shape)]),
        }));
        for( const method of result.hostOps )
        {
            if( method === 'event' || /^(set)?[Vv]ar/.test(method) ) continue;
            const base = method.startsWith('dot_') ? method.slice(4) : method;
            assert.ok(HOST_SURFACE.has(base),
                `the emitter calls H.${method} and the surface does not declare it`);
        }
    }
});

/* -------------------------------------------------------------------------
 * Intrinsics against the C handlers
 * ---------------------------------------------------------------------- */

test('integer intrinsics agree with the C VM at the edges', () => {
    assert.equal(K.add(2147483647, 1), -2147483648);
    assert.equal(K.sub(-2147483648, 1), 2147483647);
    assert.equal(K.multiply(65536, 65536), 0);
    /* C division truncates toward zero; JS `/` does not divide. */
    assert.equal(K.div(-7, 2), -3);
    assert.equal(K.mod(-7, 2), -1);
    assert.throws(() => K.div(1, 0), /div by zero/);
    /* Scale's intermediate is 64-bit in C, so a large product must not round. */
    assert.equal(K.scale(2000000000, 1000, 3), 6000000);
    /* Interpolate answers `a` on a zero span rather than dividing. */
    assert.equal(K.interpolate(5, 9, 3, 3, 7), 5);
});

test('bit-range writes clamp, they do not wrap', () => {
    /* A masked write would turn 255 into 7; the client saturates at 7 too, but
     * by clamping — and the two differ for a value that masks to a small
     * number, e.g. 8 masks to 0 and clamps to 7. */
    assert.equal(K.setbitRangeValue(0, 8, 0, 2), 7);
    assert.equal(K.getbitRange(0b1011000, 3, 6), 0b1011);
});

test('escape emits the renderer escapes, not HTML entities', () => {
    assert.equal(K.escape('<col=f>'), '<lt>col=f<gt>');
});

test('a fresh int array is -1, which is null for every reference type', () => {
    /*
     * Not cosmetic. A script that fills an array and guards each slot with
     * `= null` is testing a sentinel, and 0 is a perfectly good dbrow,
     * component or obj id — script1090, behind all three Slayer Rewards
     * catalogue tabs, takes its error branch on iteration zero against a
     * zeroed array and draws nothing, with nothing logged.
     */
    assert.deepEqual(K.defineArray(3, 'int'), [-1, -1, -1]);
    assert.deepEqual(K.defineArray(2, 'string'), ['', '']);
});

test('an array index past the end reads 0 and drops the write', () => {
    /* Zero on an out-of-range READ, which is the reference's own answer and a
     * different question from what an in-range unwritten cell holds. */
    const array = K.defineArray(2, 'int');
    K.arraySet(array, 5, 9);
    assert.equal(K.arrayGet(array, 5), 0);
    assert.deepEqual(array, [-1, -1]);
});

test('a proc call groups its arguments by STACK BANK, not source order', () => {
    /*
     * `CS2VM2_Op_GosubWithParams` pops the callee's string arguments off the
     * STRING stack and its int arguments off the INT stack, each in reverse
     * declaration order — two banks, filled independently. So a call whose
     * strings are not last still binds ints to ints, and a generated function
     * takes every int parameter and then every string one.
     *
     * `~magic_spellbook_redraw(..., $string0, $string1, $int12)` is the case:
     * eleven ints with the last one written AFTER the strings. In source
     * order `$string0` arrives in the last int parameter and `$int12` in
     * `$string0`.
     */
    const emitted = emitScript({
        schema: 'rscache-cs2-ast/1', id: 90001, name: '[proc,caller]',
        arguments: [], returns: [], frame: { localInts: 1, localStrings: 1 },
        body: { kind: 'seq', instructions: [{
            kind: 'assignment', definitions: [],
            expression: {
                kind: 'proc', scriptId: 90002, stackTypes: [],
                arguments: [
                    { kind: 'constant', stackType: 'int', value: 1, type: 'int' },
                    { kind: 'constant', stackType: 'string', value: 'a', type: 'string' },
                    { kind: 'constant', stackType: 'int', value: 2, type: 'int' },
                ],
            },
        }] },
    });
    const call = emitted.code.split('\n').find((line) => line.includes('cs2_90002'));
    assert.match(call, /cs2_90002\(H, \w+, \w+, \w+\)/);
    /* The two ints first, then the string — whatever order they were written. */
    const order = emitted.code.match(/t\d+ = [^;]+;/g).map((line) => line.split(' = ')[1]);
    assert.deepEqual(order, ['1;', '"a";', '2;'],
        'evaluation stays in SOURCE order; only the binding is regrouped');
});

test('an ARRAY handle is a string-bank value, however the tree spells it', () => {
    /*
     * An array lives on the string stack at this revision, and the tree hands
     * one over as a `pointer` node rather than an `access`. Classifying it by
     * node kind rather than by its VARIABLE called it an int, and
     * `magic_spellbook_sort`'s recursive call then passed its handle in the
     * first int slot — the callee's first `array_set` threw on a number.
     */
    const emitted = emitScript({
        schema: 'rscache-cs2-ast/1', id: 90003, name: '[proc,caller]',
        arguments: [], returns: [], frame: { localInts: 1, localStrings: 1 },
        body: { kind: 'seq', instructions: [{
            kind: 'assignment', definitions: [],
            expression: {
                kind: 'proc', scriptId: 90004, stackTypes: [],
                arguments: [
                    { kind: 'pointer',
                      variable: { kind: 'array', id: 0, local: true, type: 'int' } },
                    { kind: 'constant', stackType: 'int', value: 7, type: 'int' },
                ],
            },
        }] },
    });
    const call = emitted.code.split('\n').find((line) => line.includes('cs2_90004'));
    const [, first, second] = /cs2_90004\(H, (\w+), (\w+)\)/.exec(call);
    const assignments = Object.fromEntries(
        emitted.code.match(/t\d+ = [^;]+;/g).map((line) => line.split(' = ')));
    assert.equal(assignments[second].trim(), '$array0;', 'the handle is passed LAST');
    assert.equal(assignments[first].trim(), '7;');
});

test('array_sort_all permutes the secondary in lockstep', () => {
    const names = ['c', 'a', 'b'];
    const ids = [30, 10, 20];
    K.arraySortAll(names, ids);
    assert.deepEqual(names, ['a', 'b', 'c']);
    assert.deepEqual(ids, [10, 20, 30]);
});

test('the emitted function name is the one dependants reference', () => {
    const result = emitScript(script({ id: 77, body: seq([]) }));
    assert.equal(result.functionName, scriptFunctionName(77));
    assert.match(result.code, /export function\* cs2_77\(H\)/);
});

/* -------------------------------------------------------------------------
 * Runner
 * ---------------------------------------------------------------------- */

let failed = 0;
for( const [name, fn] of tests )
{
    try { fn(); console.log(`ok   ${name}`); }
    catch( error ) { failed++; console.error(`FAIL ${name}\n     ${error.message}`); }
}
console.log(`\n${tests.length - failed}/${tests.length} passed`);
process.exit(failed ? 1 : 0);
