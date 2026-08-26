/*
 * The parity oracle itself.
 *
 * A comparison that reports a false difference is worse than no comparison:
 * it gets muted, and then it never reports a real one. So most of what is
 * tested here is what the oracle must NOT complain about — a node index that
 * differs because two trees allocate slots differently, a kind the browser
 * preview does not host, a colour on a command that carries none.
 */

import assert from 'node:assert/strict';

import { existsSync, readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

import {
    C_KIND_NAMES, compareEmit, emitFingerprint, normalizeJsCommands, parseCEmitDump,
    UNHOSTED_KINDS,
} from '../src/emit_parity.js';
import { EMIT_KIND } from '../src/emit.js';

const HERE = dirname(fileURLToPath(import.meta.url));
const EMIT_HEADER = join(HERE, '..', '..', '..', 'src', 'ui', 'uitree_emit.h');

const tests = [];
function test(name, fn) { tests.push([name, fn]); }

/** A line in exactly the shape src/main.c prints. */
function cLine(index, fields = {}) {
    const f = {
        kind: 3, com: 0x02580001, x: 10, y: 20, w: 100, h: 50,
        scene: -1, model: -1, color: 0xff981f, filled: 1, trans: 0, tiled: 0,
        clipX: 0, clipY: 0, clipW: 765, clipH: 503, ...fields,
    };
    const group = (f.com >>> 16) & 0xffff;
    return `EMIT_EXIT[${index}] kind=${f.kind} com=0x${f.com.toString(16).padStart(8, '0')} `
        + `(${group}|${f.com & 0xffff}) x=${f.x} y=${f.y} w=${f.w} h=${f.h} `
        + `scene=${f.scene} model=${f.model} color=0x${f.color.toString(16).padStart(6, '0')} `
        + `filled=${f.filled} trans=${f.trans} tiled=${f.tiled} `
        + `clip=${f.clipX},${f.clipY} ${f.clipW}x${f.clipH}`;
}

function jsCommand(fields = {}) {
    const f = {
        kind: EMIT_KIND.RECT, componentId: 0x02580001, x: 10, y: 20,
        width: 100, height: 50, trans: 0,
        props: { colour: 0xff981f, filled: 1 },
        clip: { x: 0, y: 0, width: 765, height: 503 }, ...fields,
    };
    return f;
}

/* -------------------------------------------------------------------------
 * Reading the C dump
 * ---------------------------------------------------------------------- */

test('a real dump line parses into every field it carries', () => {
    const [command] = parseCEmitDump(cLine(0));
    assert.equal(command.kind, EMIT_KIND.RECT);
    assert.equal(command.componentId, 0x02580001);
    assert.deepEqual([command.x, command.y, command.width, command.height], [10, 20, 100, 50]);
    assert.equal(command.colour, 0xff981f);
    assert.deepEqual(command.clip, { x: 0, y: 0, width: 765, height: 503 });
});

test('surrounding log noise is ignored', () => {
    /* The dump shares stderr with everything else the client prints; a parser
     * that choked on a neighbouring line would be unusable in practice. */
    const text = [
        'some unrelated warning',
        cLine(0),
        'Task_CS2Run: something',
        cLine(1, { kind: 2, x: 30 }),
    ].join('\n');
    const commands = parseCEmitDump(text);
    assert.equal(commands.length, 2);
    assert.equal(commands[1].kind, EMIT_KIND.TEXT);
});

test('negative coordinates parse', () => {
    /* A widget scrolled above its window has a negative y, and it is real. */
    const [command] = parseCEmitDump(cLine(0, { x: -40, y: -12, clipX: -1 }));
    assert.equal(command.x, -40);
    assert.equal(command.y, -12);
    assert.equal(command.clip.x, -1);
});

/* -------------------------------------------------------------------------
 * What must NOT be reported
 * ---------------------------------------------------------------------- */

test('the kind numbers agree with the C enum they came from', () => {
    /*
     * Read the authority, do not restate it. A guessed number makes the oracle
     * report a false difference on EVERY command of that kind, which is the
     * failure mode that gets a comparison muted — and MODEL being 6 rather
     * than 5 was guessed wrong the first time.
     */
    if( !existsSync(EMIT_HEADER) ) { console.log('     (no C header; skipped)'); return; }
    const header = readFileSync(EMIT_HEADER, 'utf8');
    const block = /enum UITreeEmitKind\s*\{([\s\S]*?)\}/.exec(header);
    assert.ok(block, 'found enum UITreeEmitKind');

    const names = [...block[1].matchAll(/UITREE_EMIT_([A-Z_0-9]+)/g)].map((m) => m[1]);
    assert.deepEqual(names, [...C_KIND_NAMES],
        'the C enum and this table have drifted');

    /* And every number is accounted for: mapped, or explicitly unhosted. */
    for( let kind = 0; kind < names.length; kind++ )
    {
        const mapped = parseCEmitDump(cLine(0, { kind }))[0].kind !== null;
        const unhosted = UNHOSTED_KINDS.has(kind);
        assert.ok(mapped !== unhosted,
            `${names[kind]} (${kind}) is neither mapped nor declared unhosted`);
    }
});

test('a kind the preview does not host is filtered, not mismatched', () => {
    /*
     * The world viewport, the minimap, the IF1 scrollbar chrome: content the
     * browser preview does not host. Reporting them would bury every real
     * difference under a permanent wall of noise, and a muted oracle reports
     * nothing at all.
     */
    const c = parseCEmitDump([cLine(0, { kind: 10 }), cLine(1)].join('\n'));
    const result = compareEmit(c, normalizeJsCommands([jsCommand()]));
    assert.equal(result.matches, true);
    assert.equal(result.expectedCount, 1, 'only the hosted command is expected');
});

test('node indexes are never compared', () => {
    /*
     * An index is storage. The C tree's free list hands slots out in a
     * different order than a fresh JavaScript tree does, and demanding they
     * agree would fail on a difference that means nothing.
     */
    const c = parseCEmitDump(cLine(0));
    const js = normalizeJsCommands([jsCommand({ node: 999 })]);
    assert.equal(compareEmit(c, js).matches, true);
});

test('a colour is compared only where both sides carry one', () => {
    /* A command whose kind has no colour reports -1; comparing that against a
     * real colour would fail every sprite. */
    const c = parseCEmitDump(cLine(0, { kind: 1, color: 0 }));
    const js = normalizeJsCommands([jsCommand({
        kind: EMIT_KIND.SPRITE, props: { sprite: 7 },
    })]);
    const result = compareEmit(c, js);
    assert.equal(result.differences.filter((d) => d.field === 'colour').length, 0);
});

/* -------------------------------------------------------------------------
 * What must be reported
 * ---------------------------------------------------------------------- */

test('a moved box is reported with both values', () => {
    const c = parseCEmitDump(cLine(0, { y: 93 }));
    const js = normalizeJsCommands([jsCommand({ y: 80 })]);
    const result = compareEmit(c, js);
    assert.equal(result.matches, false);
    const moved = result.differences.find((d) => d.field === 'y');
    assert.deepEqual([moved.expected, moved.actual], [93, 80]);
    assert.equal(moved.component, 0x02580001);
});

test('a wrong clip is reported — it is how an overflow is diagnosed', () => {
    const c = parseCEmitDump(cLine(0, { clipW: 200 }));
    const js = normalizeJsCommands([jsCommand({
        clip: { x: 0, y: 0, width: 765, height: 503 },
    })]);
    const result = compareEmit(c, js);
    assert.ok(result.differences.some((d) => d.field === 'clip.width'));
});

test('a command drawn by one side and not the other is reported', () => {
    const c = parseCEmitDump([cLine(0), cLine(1, { com: 0x02580002 })].join('\n'));
    const result = compareEmit(c, normalizeJsCommands([jsCommand()]));
    const absent = result.differences.find((d) => d.field === 'present');
    assert.equal(absent.actual, null);
    assert.match(absent.expected, /rect@/);
});

test('every difference is reported, not the first', () => {
    /*
     * A layout change moves many commands at once, and stopping at one turns a
     * single cause into as many runs of the comparison as there are rows.
     */
    const c = parseCEmitDump([
        cLine(0, { y: 0 }), cLine(1, { y: 16 }), cLine(2, { y: 32 }),
    ].join('\n'));
    const js = normalizeJsCommands([
        jsCommand({ y: 1 }), jsCommand({ y: 17 }), jsCommand({ y: 33 }),
    ]);
    const result = compareEmit(c, js);
    assert.equal(result.differences.length, 3);
    assert.deepEqual(result.summary[0], { field: 'y', count: 3, first: result.differences[0] });
});

test('the summary groups by kind of difference', () => {
    /* So a report can say "eleven boxes moved" rather than listing eleven
     * near-identical rows. */
    const c = parseCEmitDump([cLine(0, { y: 5, w: 90 }), cLine(1, { y: 21 })].join('\n'));
    const js = normalizeJsCommands([jsCommand(), jsCommand({ y: 20 })]);
    const fields = compareEmit(c, js).summary.map((entry) => entry.field);
    assert.deepEqual(fields.sort(), ['width', 'y']);
});

test('an ignore list keeps a known gap from muting the whole oracle', () => {
    const c = parseCEmitDump([cLine(0, { com: 0x02580009, y: 999 }), cLine(1)].join('\n'));
    const js = normalizeJsCommands([jsCommand()]);
    assert.equal(compareEmit(c, js, { ignore: [0x02580009] }).matches, true);
});

/* -------------------------------------------------------------------------
 * Fingerprint
 * ---------------------------------------------------------------------- */

test('a fingerprint is stable and notices a move', () => {
    const a = normalizeJsCommands([jsCommand(), jsCommand({ y: 40 })]);
    const b = normalizeJsCommands([jsCommand(), jsCommand({ y: 40 })]);
    assert.equal(emitFingerprint(a), emitFingerprint(b));

    const moved = normalizeJsCommands([jsCommand(), jsCommand({ y: 41 })]);
    assert.notEqual(emitFingerprint(a), emitFingerprint(moved));
});

test('a fingerprint notices a reorder', () => {
    /* Draw ORDER decides what covers what; two lists with the same commands in
     * a different sequence are different pictures. */
    const a = normalizeJsCommands([jsCommand({ y: 0 }), jsCommand({ y: 40 })]);
    const b = normalizeJsCommands([jsCommand({ y: 40 }), jsCommand({ y: 0 })]);
    assert.notEqual(emitFingerprint(a), emitFingerprint(b));
});

let failed = 0;
for( const [name, fn] of tests )
{
    try { fn(); console.log(`ok   ${name}`); }
    catch( error ) { failed++; console.error(`FAIL ${name}\n     ${error.message}`); }
}
console.log(`\n${tests.length - failed}/${tests.length} passed`);
process.exit(failed ? 1 : 0);
