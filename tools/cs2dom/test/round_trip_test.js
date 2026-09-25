/*
 * The round trip, in both directions.
 *
 * The requirement the `.if` half exists for is stated once and tested here:
 * an interface imported and exported with NO edits must come back
 * byte-identical. Not equivalent — identical, because the content tree is
 * under version control and a diff full of reformatting hides the one line
 * that mattered.
 *
 * The JavaScript half is the opposite shape. Lowering CS2 to JavaScript is
 * total; lowering back is not, so the tests there are as much about what is
 * REFUSED as about what is produced.
 */

import assert from 'node:assert/strict';

import {
    emitCompack, nextFileId, parseCompack, parseIf, IfRecordError,
} from '../src/if_record.js';
import { lowerScriptToCs2, Cs2LowerError } from '../src/js_to_cs2.js';

const tests = [];
function test(name, fn) { tests.push([name, fn]); }

/* A real-shaped record: preamble comments, blank lines, an unmodelled field. */
const SAMPLE = `// Interface 600 — 3 components.
// One block per component.

[universe]
if3=yes
type=0
width=30
height=40
widthmode=1

[frame]
if3=yes
type=0
// a hand-written note nobody should lose
layer=39321600
name=
onload=i:703,i:-2147483645,s:Kudos List,i:0

[content]
if3=yes
type=4
x=10
colour=16750623
somethingcs2domhasneverheardof=42
`;

/* -------------------------------------------------------------------------
 * The .if half
 * ---------------------------------------------------------------------- */

test('import then export with no edits is byte-identical', () => {
    /*
     * THE requirement. Parsing into a typed model and re-serialising would
     * lose the comment, the blank lines, the field order and the unmodelled
     * key — and every one of those is a diff line that hides a real change.
     */
    assert.equal(parseIf(SAMPLE).toText(), SAMPLE);
});

test('a field nobody models survives an edit to a different field', () => {
    const record = parseIf(SAMPLE);
    record.set('content', 'colour', '65280');
    const text = record.toText();
    assert.match(text, /somethingcs2domhasneverheardof=42/);
    assert.match(text, /colour=65280/);
    assert.doesNotMatch(text, /colour=16750623/);
});

test('an edited field keeps its position', () => {
    /* Moving it to the end of the block turns a one-line edit into a
     * whole-block diff, which is the noise this record exists to avoid. */
    const record = parseIf(SAMPLE);
    record.set('universe', 'width', '99');
    const lines = record.toText().split('\n');
    const start = lines.indexOf('[universe]');
    assert.equal(lines[start + 3], 'width=99');
    assert.equal(lines[start + 4], 'height=40', 'and its neighbour did not move');
});

test('a new field is appended, and a removed one leaves nothing behind', () => {
    const record = parseIf(SAMPLE);
    record.set('universe', 'trans', '128');
    assert.match(record.toText(), /widthmode=1\ntrans=128/);

    record.set('universe', 'trans', null);
    assert.doesNotMatch(record.toText(), /trans=/);
});

test('setting a field to what it already is changes nothing', () => {
    const record = parseIf(SAMPLE);
    assert.equal(record.set('universe', 'width', '30'), false);
    assert.deepEqual(record.changed(), []);
    assert.equal(record.toText(), SAMPLE);
});

test('only the blocks that changed are reported', () => {
    const record = parseIf(SAMPLE);
    record.set('content', 'x', '20');
    assert.deepEqual(record.changed(), ['content']);
});

test('a repeated key keeps every occurrence', () => {
    /* Not something to normalise away: a hand-authored file writes an op list
     * as repeated keys, and silently keeping one changes the interface. */
    const record = parseIf('[a]\nop1=Use\nop1=Drop\n');
    assert.deepEqual(record.getAll('a', 'op1'), ['Use', 'Drop']);
    assert.equal(record.get('a', 'op1'), 'Use', 'and the first one is the decoder\'s');
    assert.equal(record.toText(), '[a]\nop1=Use\nop1=Drop\n');
});

test('a block can be added and removed', () => {
    const record = parseIf(SAMPLE);
    record.addBlock('extra', { if3: 'yes', type: '3' });
    assert.match(record.toText(), /\[extra\]\nif3=yes\ntype=3/);

    assert.throws(() => record.addBlock('extra'), (e) => e instanceof IfRecordError);
    assert.equal(record.removeBlock('extra'), true);
    assert.doesNotMatch(record.toText(), /\[extra\]/);
});

test('setting a field on a block that is not there is refused', () => {
    const record = parseIf(SAMPLE);
    assert.throws(() => record.set('nope', 'x', '1'), /no block \[nope\]/);
});

/* -------------------------------------------------------------------------
 * The .compack beside it
 * ---------------------------------------------------------------------- */

test('a compack round-trips and allocates only past the highest', () => {
    /*
     * A recycled file id hands one component's uid to a different component,
     * and every script that referenced the old one then addresses the new one.
     * Ids are spent, never reclaimed.
     */
    const compack = parseCompack('0=universe\n1=frame\n5=content\n');
    assert.equal(compack.byName.get('content'), 5);
    assert.equal(emitCompack(compack), '0=universe\n1=frame\n5=content\n');
    assert.equal(nextFileId(compack), 6, 'past the highest, not into the gap at 2');
});

/* -------------------------------------------------------------------------
 * JavaScript back to CS2
 * ---------------------------------------------------------------------- */

/**
 * A tiny ESTree builder.
 *
 * Hand-built rather than parsed so each test states exactly one shape; a
 * parser would make the test about the parser.
 */
const js = {
    id: (name) => ({ type: 'Identifier', name }),
    lit: (value) => ({ type: 'Literal', value }),
    call: (object, method, args) => ({
        type: 'CallExpression',
        callee: { type: 'MemberExpression', object: js.id(object), property: js.id(method) },
        arguments: args,
    }),
    assign: (left, right) => ({
        type: 'ExpressionStatement',
        expression: { type: 'AssignmentExpression', operator: '=', left, right },
    }),
    exprStatement: (expression) => ({ type: 'ExpressionStatement', expression }),
    block: (body) => ({ type: 'BlockStatement', body }),
    binary: (operator, left, right) => ({ type: 'BinaryExpression', operator, left, right }),
};

function lower(body, options = {}) {
    return lowerScriptToCs2(js.block(body), { name: '[clientscript,t]', ...options });
}

test('a host call becomes the command it came from', () => {
    const source = lower([js.exprStatement(js.call('H', 'cc_setcolour', [js.lit(255)]))]);
    assert.match(source, /cc_setcolour\(255\);/);
});

test('the dot form comes back as a dot', () => {
    const source = lower([js.exprStatement(js.call('H', 'dot_cc_setcolour', [js.lit(1)]))]);
    assert.match(source, /\.cc_setcolour\(1\);/);
});

test('arithmetic is wrapped in one calc, not one per operator', () => {
    /* `calc` covers a whole nested expression; a calc per operator is not
     * legal source. */
    const inner = js.call('K', 'add', [js.id('a'), js.id('b')]);
    const outer = js.call('K', 'multiply', [inner, js.lit(2)]);
    const source = lower([js.assign(js.id('x'), outer)]);
    assert.match(source, /\$x = calc\(\(\$a \+ \$b\) \* 2\);/);
    assert.equal((source.match(/calc\(/g) || []).length, 1);
});

test('an argument list resets the calc context', () => {
    /*
     * Bare arithmetic is illegal in an argument even when the surrounding
     * expression is already inside a calc. Eighteen scripts in cache.osrs239
     * decompiled to source that would not compile back for exactly this.
     */
    const procCall = {
        type: 'YieldExpression', delegate: true,
        argument: {
            type: 'CallExpression', callee: js.id('cs2_99'),
            arguments: [js.id('H'), js.call('K', 'sub', [js.id('b'), js.id('c')])],
        },
    };
    const source = lower([js.assign(js.id('x'),
        js.call('K', 'sub', [js.id('a'), procCall]))]);
    assert.match(source, /calc\(\$a - ~script99\(calc\(\$b - \$c\)\)\)/);
});

test('the retry loop collapses back to the bare call', () => {
    /*
     * `while ((t = H.op()) === PARK) yield;` is the JavaScript mechanism for
     * a park. CS2's VM re-executes the opcode instead, so the loop carries no
     * meaning there and lowering it literally would emit a real infinite loop.
     */
    const source = lower([{
        type: 'WhileStatement',
        test: js.binary('===',
            { type: 'AssignmentExpression', operator: '=', left: js.id('t0'),
              right: js.call('H', 'cc_find', [js.lit(5), js.lit(1)]) },
            js.id('PARK')),
        body: { type: 'ExpressionStatement', expression: { type: 'YieldExpression' } },
    }]);
    assert.match(source, /^cc_find\(5, 1\);$/m);
    assert.doesNotMatch(source, /while/);
});

test('a proc call drops the host argument and the yield*', () => {
    const source = lower([js.exprStatement({
        type: 'YieldExpression', delegate: true,
        argument: {
            type: 'CallExpression', callee: js.id('cs2_1412'),
            arguments: [js.id('H'), js.lit(3)],
        },
    })]);
    assert.match(source, /~script1412\(3\);/);
});

test('a hook binding becomes the quoted callback the dialect wants', () => {
    const source = lower([js.exprStatement(js.call('H', 'if_setonvartransmit', [
        js.call('K', 'hook', [
            js.lit(5256),
            { type: 'ArrayExpression', elements: [js.lit(7)] },
            { type: 'ArrayExpression', elements: [js.lit(300)] },
        ]),
        js.lit(196609),
    ]))]);
    assert.match(source, /if_setonvartransmit\("script5256\(7\)\{300\}", 196609\);/);
});

test('clearing a hook is null, not an empty callback', () => {
    const source = lower([js.exprStatement(js.call('H', 'if_setonclick', [
        js.call('K', 'hook', [js.lit(-1),
            { type: 'ArrayExpression', elements: [] },
            { type: 'ArrayExpression', elements: [] }]),
        js.lit(1),
    ]))]);
    assert.match(source, /if_setonclick\(null, 1\);/);
});

test('a template literal becomes an interpolated string', () => {
    const source = lower([js.assign(js.id('s'), {
        type: 'TemplateLiteral',
        quasis: [
            { value: { cooked: 'Total: ' } },
            { value: { cooked: '' } },
        ],
        expressions: [js.call('K', 'tostring', [js.id('n')])],
    })]);
    assert.match(source, /\$s = "Total: <tostring\(\$n\)>";/);
});

test('comparisons use the CS2 spelling, not the JavaScript one', () => {
    const source = lower([{
        type: 'IfStatement',
        test: js.binary('===', js.id('a'), js.lit(3)),
        consequent: js.block([]),
        alternate: null,
    }]);
    assert.match(source, /if \(\$a = 3\) \{/);
});

test('&& and || become the dialect\'s & and |', () => {
    const source = lower([{
        type: 'IfStatement',
        test: { type: 'LogicalExpression', operator: '&&',
                left: js.binary('===', js.id('a'), js.lit(1)),
                right: js.binary('>', js.id('b'), js.lit(2)) },
        consequent: js.block([]), alternate: null,
    }]);
    assert.match(source, /if \(\$a = 1 & \$b > 2\) \{/);
});

test('the int32 truncation the emitter adds is dropped', () => {
    /* `| 0` is how the JavaScript states a CS2 int; it is not an operation
     * and lowering it as one would emit a bitwise-or the source never had. */
    const source = lower([js.assign(js.id('x'), js.binary('|', js.id('y'), js.lit(0)))]);
    assert.match(source, /\$x = \$y;/);
});

test('a switch keeps its cases and drops the JavaScript break', () => {
    const source = lower([{
        type: 'SwitchStatement',
        discriminant: js.call('H', 'varbit', [js.lit(698)]),
        cases: [
            { test: js.lit(0),
              consequent: [{ type: 'ReturnStatement', argument: js.lit('a') },
                           { type: 'BreakStatement' }] },
            { test: null, consequent: [{ type: 'ReturnStatement', argument: js.lit('') }] },
        ],
    }]);
    assert.match(source, /switch_int \(varbit\(698\)\) \{/);
    assert.match(source, /case 0 :/);
    assert.match(source, /case default :/);
    assert.doesNotMatch(source, /break/);
});

test('a tuple return becomes a list', () => {
    const source = lower([{
        type: 'ReturnStatement',
        argument: { type: 'ArrayExpression', elements: [js.id('a'), js.id('b')] },
    }]);
    assert.match(source, /return\(\$a, \$b\);/);
});

test('a construct with no CS2 form is refused by name', () => {
    /*
     * Refusing is the feature. An approximation would produce a script that
     * runs and misbehaves; a build error costs a minute.
     */
    assert.throws(
        () => lower([{ type: 'ForStatement', init: null, test: null, update: null,
                       body: js.block([]) }]),
        (error) => error instanceof Cs2LowerError && /ForStatement/.test(error.message));

    assert.throws(
        () => lower([{ type: 'ExpressionStatement', expression: {
            type: 'AssignmentExpression', operator: '+=', left: js.id('a'), right: js.lit(1) } }]),
        (error) => error instanceof Cs2LowerError && /compound assignment/.test(error.message));

    assert.throws(
        () => lower([js.exprStatement(js.call('K', 'somethingInvented', []))]),
        (error) => error instanceof Cs2LowerError && /somethingInvented/.test(error.message));
});

test('the signature carries arguments and return types', () => {
    const source = lowerScriptToCs2(js.block([]), {
        name: '[proc,thing]',
        args: [{ name: 'component0', type: 'component' }, { name: 'int1', type: 'int' }],
        returns: ['string'],
    });
    assert.match(source, /^\[proc,thing\]\(component \$component0, int \$int1\)\(string\)$/m);
});

let failed = 0;
for( const [name, fn] of tests )
{
    try { fn(); console.log(`ok   ${name}`); }
    catch( error ) { failed++; console.error(`FAIL ${name}\n     ${error.message}`); }
}
console.log(`\n${tests.length - failed}/${tests.length} passed`);
process.exit(failed ? 1 : 0);
