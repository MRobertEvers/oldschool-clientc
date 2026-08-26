/*
 * The dev server's own logic, without a server.
 *
 * What is worth testing here is the part that reads cache records and decides
 * what the browser needs: which scripts an interface installs, what its
 * onload hooks are, which state a developer must be able to move. The HTTP
 * around it is plumbing.
 *
 * The closure walk is the one with a failure mode worth naming: fetching too
 * little means a hook fires at run time and finds no script, which presents as
 * a panel that half-builds.
 */

import assert from 'node:assert/strict';

import { parseIf, parseCompack } from '../src/if_record.js';
import {
    hookScriptIds, lowerClosure, onLoadEntries, stateSlices,
} from '../src/dev_canvas.js';
import { canvasDevPage, canvasDevClient } from '../src/dev_page_canvas.js';

const tests = [];
function test(name, fn) { tests.push([name, fn]); }

const RECORD = parseIf(`// Interface 600
[universe]
if3=yes
type=0
width=30

[frame]
if3=yes
type=0
onload=i:703,i:-2147483645,s:Kudos List,i:0
onvarptransmit=i:9727
varptriggers=300,301

[readout]
if3=yes
type=4
onmouseover=i:5501
varbittriggers=698
`);

/* -------------------------------------------------------------------------
 * Reading the record
 * ---------------------------------------------------------------------- */

test('every hook field names a script the browser will need', () => {
    assert.deepEqual(hookScriptIds(RECORD).sort((a, b) => a - b), [703, 5501, 9727]);
});

test('a hook whose script id is negative is not a script', () => {
    /* `-1` is the clear-the-hook form; fetching it would ask the decompiler
     * for a script that does not exist. */
    const record = parseIf('[a]\nonclick=i:-1\n');
    assert.deepEqual(hookScriptIds(record), []);
});

test('onload entries keep their arguments, ints and strings alike', () => {
    const compack = parseCompack('0=universe\n1=frame\n2=readout\n');
    const entries = onLoadEntries(RECORD, compack, 'test');
    assert.equal(entries.length, 1);
    assert.equal(entries[0].scriptId, 703);
    assert.deepEqual(entries[0].args, [-2147483645, 'Kudos List', 0]);
    assert.equal(entries[0].component, 1, 'and the block\'s file id comes from the compack');
});

test('the state controls are the ids this interface actually watches', () => {
    /* A slice nobody can move is a slice that cannot be tested — but offering
     * every var in the cache is a list nobody can use. The trigger lists are
     * exactly the ones this interface reacts to. */
    const slices = stateSlices(RECORD).map((slice) => slice.id).sort();
    assert.deepEqual(slices, ['varbit:698', 'varp:300', 'varp:301']);
});

/* -------------------------------------------------------------------------
 * The closure
 * ---------------------------------------------------------------------- */

/** A fake decompiler: script N calls N+1, up to a depth. */
function chainState(depth) {
    return {
        asts: new Map(),
        cs2: null, cache: null,
        _chain: depth,
    };
}

function chainTree(id, last) {
    return {
        schema: 'rscache-cs2-ast/1', id, name: `[clientscript,s${id}]`,
        arguments: [], returns: [],
        frame: { localInts: 0, localStrings: 0, intArguments: 0, stringArguments: 0 },
        body: { kind: 'seq', next: null, instructions: id >= last ? [] : [{
            kind: 'assignment', definitions: [], expression: {
                kind: 'proc', scriptId: id + 1, name: null, arguments: [], stackTypes: [],
            },
        }] },
    };
}

test('the closure follows what the scripts call, not just the roots', () => {
    /*
     * Fetching only the roots means the first `~proc` at run time finds no
     * script — a panel that half-builds, with a warning far from the cause.
     */
    const state = chainState(4);
    for( let id = 1; id <= 4; id++ ) state.asts.set(id, chainTree(id, 4));

    const { scripts, errors } = lowerClosure(state, [1]);
    assert.deepEqual(Object.keys(scripts).map(Number).sort((a, b) => a - b), [1, 2, 3, 4]);
    assert.deepEqual(errors, []);
});

test('a cycle in the closure terminates', () => {
    const state = { asts: new Map(), cs2: null, cache: null };
    state.asts.set(1, chainTree(1, 99));
    state.asts.set(2, {
        ...chainTree(2, 99),
        body: { kind: 'seq', next: null, instructions: [{
            kind: 'assignment', definitions: [], expression: {
                kind: 'proc', scriptId: 1, name: null, arguments: [], stackTypes: [],
            },
        }] },
    });
    const { scripts } = lowerClosure(state, [1]);
    assert.deepEqual(Object.keys(scripts).map(Number).sort(), [1, 2]);
});

test('a script that cannot be decompiled is reported, not silently dropped', () => {
    const state = { asts: new Map([[1, null]]), cs2: null, cache: null };
    const { scripts, errors } = lowerClosure(state, [1]);
    assert.deepEqual(scripts, {});
    assert.match(errors[0], /script 1: could not decompile/);
});

/* -------------------------------------------------------------------------
 * The page
 * ---------------------------------------------------------------------- */

test('the page has one canvas and no per-widget markup', () => {
    /* The whole point of the renderer: there is no element per widget, so a
     * three-thousand-component rebuild costs draw calls and no DOM. */
    const html = canvasDevPage({});
    assert.equal((html.match(/<canvas/g) || []).length, 1);
    assert.match(html, /id="surface"/);
    assert.match(html, /image-rendering: pixelated/, 'pixel art must not be smoothed');
});

test('the page loads its client as a module rather than inlining it', () => {
    const html = canvasDevPage({});
    assert.match(html, /<script type="module" src="\/dev-client\.js\?v=[^"]*">/);
    assert.doesNotMatch(html, /<script>[^<]{200,}/, 'no giant inline script');
});

test('the page carries the serving build, so a stale tab is identifiable', () => {
    /*
     * The stamp is the only thing that tells a screenshot of an old tab from
     * a screenshot of a broken fix: both show the same wrong picture, and
     * the client reads it back out of its own module URL.
     */
    assert.match(canvasDevPage({ build: '1234:5678' }),
        /<script type="module" src="\/dev-client\.js\?v=1234:5678">/);
});

test('the client keeps selection, drafts and focus outside the mount', () => {
    /*
     * The one hot-reload rule worth keeping: a source save replaces the
     * session, and the picker selection, the state drafts and the caret must
     * all survive it.
     */
    const client = canvasDevClient();
    assert.match(client, /const chrome = \{/);
    assert.match(client, /runtime\?\.dispose\(\)/, 'the old runtime is detached on remount');
    assert.match(client, /for\( const \[id, value\] of chrome\.drafts \)/,
        'and the drafts are re-applied before onLoad runs');
});

test('a page-side var write arms the transmit pump', () => {
    /*
     * A script's own var write deliberately does not re-trigger its own hook.
     * A write from the state panel is standing in for the SERVER, so it must
     * arm the pump or nothing re-reads it — which reads as a dead control.
     */
    assert.match(canvasDevClient(), /pump\.noteVarChanged\(number\)/);
});

let failed = 0;
for( const [name, fn] of tests )
{
    try { fn(); console.log(`ok   ${name}`); }
    catch( error ) { failed++; console.error(`FAIL ${name}\n     ${error.message}`); }
}
console.log(`\n${tests.length - failed}/${tests.length} passed`);
process.exit(failed ? 1 : 0);
