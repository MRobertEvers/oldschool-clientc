/*
 * Every implemented host method takes the arguments the surface says it does.
 *
 * ------------------------------------------------------------------
 * Why this exists
 * ------------------------------------------------------------------
 *
 * `parawidth` was written `(text, fontId, width)` against a surface that says
 * `(string, width, fontmetrics)`. The transposition is invisible: the call
 * succeeds, the font id lands in the wrap width, font 190 does not exist, so
 * the measurement answers 0 and every widget sized from one lays out at its
 * padding. Nothing throws and nothing logs. The C request STRUCT does put the
 * font first — its field order is C's convenience — which is exactly how a
 * transposition gets written down with a straight face.
 *
 * The surface is generated from the decompiler's prototype pool, which is PUSH
 * order, and push order is what the emitter hands a method. So comparing the
 * two is comparing a call against its own call site.
 *
 * ------------------------------------------------------------------
 * What it deliberately does not check
 * ------------------------------------------------------------------
 *
 * A row with NO declared parameters proves nothing: the `seton*` family
 * carries its argument as a script binding and the `*_param` and `enum` forms
 * carry theirs in the opcode operand, so the generator has nothing to list and
 * an empty list is not a claim that the method takes none.
 *
 * A method declared with a rest parameter is skipped too — the intent
 * recorders spread whatever they are given, so their arity says nothing about
 * their order. Those are the rows this gate cannot speak for, and saying so is
 * better than a check that passes because it looked at nothing.
 */

import assert from 'node:assert/strict';

import { HostKernel } from '../src/host_kernel.js';
import { HOST_SURFACE } from '../src/generated/cs2_host_surface.js';

const tests = [];
function test(name, fn) { tests.push([name, fn]); }

/** The declared parameter list of a function, as source text. */
function parameters(fn) {
    const match = /^[^(]*\(([^)]*)\)/.exec(String(fn));
    return (match ? match[1] : '').split(',').map((part) => part.trim()).filter(Boolean);
}

function isImplemented(fn) {
    return !!fn && !/UnimplementedHostOp|fakeUnimplemented/.test(String(fn));
}

function surveyed() {
    const rows = [];
    for( const [name, row] of HOST_SURFACE )
    {
        if( row.params.length === 0 ) continue;
        for( const method of row.dotCapable ? [name, `dot_${name}`] : [name] )
        {
            const fn = HostKernel.prototype[method];
            if( !isImplemented(fn) ) continue;
            const declared = parameters(fn);
            if( declared.some((part) => part.startsWith('...')) ) continue;
            rows.push({ method, declared, expected: row.params });
        }
    }
    return rows;
}

test('an implemented method takes exactly the arguments its call site pushes', () => {
    const wrong = [];
    for( const { method, declared, expected } of surveyed() )
    {
        /* A trailing parameter with a default is allowed to go unsupplied —
         * that is how `nested = 0` sits on a two-argument command — but a
         * REQUIRED parameter past the declared count, or a signature shorter
         * than the call, means arguments are landing in the wrong slots. */
        const required = declared.filter((part) => !part.includes('=')).length;
        if( required > expected.length || declared.length < expected.length )
            wrong.push(`${method}(${declared.join(', ')}) against (${expected.join(', ')})`);
    }
    assert.deepEqual(wrong, [], `signatures disagree with the surface:\n  ${wrong.join('\n  ')}`);
});

test('the gate is looking at something', () => {
    /*
     * A count, because every failure mode of the check above is silence: a
     * broken `isImplemented`, a regex that stops matching, an empty surface —
     * each one turns this file into a test that passes by inspecting nothing.
     */
    assert.ok(surveyed().length > 150,
        `only ${surveyed().length} methods were checked; the survey has stopped finding them`);
});

let failed = 0;
for( const [name, fn] of tests )
{
    try { fn(); console.log(`ok   ${name}`); }
    catch( error ) { failed++; console.error(`FAIL ${name}\n     ${error.message}`); }
}
console.log(`\n${tests.length - failed}/${tests.length} passed`);
process.exit(failed ? 1 : 0);
