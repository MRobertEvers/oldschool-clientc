/*
 * Saving, against a scratch content tree.
 *
 * Two properties matter and both are about restraint. A save that changes
 * nothing must WRITE nothing — touching a file's mtime trips every watcher
 * downstream and makes a no-op look like an edit. And a save that changes one
 * field must produce a one-line diff, or the round trip is correct and useless.
 */

import assert from 'node:assert/strict';
import {
    chmodSync, existsSync, mkdirSync, mkdtempSync, readFileSync, statSync, writeFileSync,
} from 'node:fs';
import { join } from 'node:path';
import { tmpdir } from 'node:os';

import { saveInterface, addComponent, ExportError } from '../src/export.js';

const tests = [];
function test(name, fn) { tests.push([name, fn]); }

const IF_TEXT = `// Interface 600 — 2 components.

[universe]
if3=yes
type=0
width=30
height=40

[content]
if3=yes
type=4
colour=16750623
somethingunmodelled=42
`;

const COMPACK_TEXT = '0=universe\n5=content\n';

/** A throwaway content tree holding one interface. */
function scratch() {
    const root = mkdtempSync(join(tmpdir(), 'cs2dom-export-'));
    mkdirSync(join(root, 'interfaces'), { recursive: true });
    mkdirSync(join(root, 'scripts'), { recursive: true });
    writeFileSync(join(root, 'interfaces', 'sample.if'), IF_TEXT);
    writeFileSync(join(root, 'interfaces', 'sample.compack'), COMPACK_TEXT);
    return root;
}

const ifPath = (root) => join(root, 'interfaces', 'sample.if');

/* -------------------------------------------------------------------------
 * Restraint
 * ---------------------------------------------------------------------- */

test('a save that changes nothing writes nothing', () => {
    /*
     * Not merely "writes the same bytes": writing at all touches the mtime,
     * and the dev server watches this directory. A no-op save would remount
     * the interface the developer is looking at.
     */
    const root = scratch();
    const before = statSync(ifPath(root)).mtimeMs;
    const result = saveInterface({ contentDir: root, name: 'sample', edits: {} });
    assert.deepEqual(result.written, []);
    assert.equal(result.unchanged, true);
    assert.equal(statSync(ifPath(root)).mtimeMs, before);
});

test('setting a field to what it already holds is also nothing', () => {
    const root = scratch();
    const result = saveInterface({
        contentDir: root, name: 'sample', edits: { universe: { width: '30' } },
    });
    assert.deepEqual(result.written, []);
    assert.equal(readFileSync(ifPath(root), 'utf8'), IF_TEXT);
});

test('one edited field is one changed line', () => {
    const root = scratch();
    saveInterface({
        contentDir: root, name: 'sample', edits: { content: { colour: '65280' } },
    });
    const after = readFileSync(ifPath(root), 'utf8');

    const before = IF_TEXT.split('\n');
    const now = after.split('\n');
    assert.equal(before.length, now.length, 'no lines added or removed');
    const differing = before.filter((line, index) => line !== now[index]);
    assert.deepEqual(differing, ['colour=16750623']);
    assert.match(after, /colour=65280/);
    assert.match(after, /somethingunmodelled=42/, 'and the unmodelled field is untouched');
});

test('a dry run reports what it would write and writes nothing', () => {
    const root = scratch();
    const result = saveInterface({
        contentDir: root, name: 'sample', edits: { content: { colour: '1' } }, dryRun: true,
    });
    assert.equal(result.written.length, 1);
    assert.equal(readFileSync(ifPath(root), 'utf8'), IF_TEXT);
});

test('editing an interface that is not there is refused', () => {
    const root = scratch();
    assert.throws(
        () => saveInterface({ contentDir: root, name: 'nope', edits: {} }),
        (error) => error instanceof ExportError && /no nope\.if/.test(error.message));
});

/* -------------------------------------------------------------------------
 * Scripts
 * ---------------------------------------------------------------------- */

test('a script is written only once its bytes differ', () => {
    const root = scratch();
    const source = '[clientscript,thing]\ncc_setcolour(1);\n';
    const first = saveInterface({ contentDir: root, name: 'sample', scripts: { thing: source } });
    assert.equal(first.written.length, 1);
    assert.ok(existsSync(join(root, 'scripts', 'thing.cs2')));

    const second = saveInterface({ contentDir: root, name: 'sample', scripts: { thing: source } });
    assert.deepEqual(second.written, []);
});

test('a script the compiler refuses is not written, and says why', () => {
    /*
     * The tree must never hold source that will not bake — a build that fails
     * hours later on another machine is the failure this prevents. A stub
     * compiler stands in for the real one; what is being tested is that a
     * refusal stops the write.
     */
    const root = scratch();
    const refusingTool = join(root, 'always-fails.sh');
    writeFileSync(refusingTool, '#!/bin/sh\necho "line 2: no such command" >&2\nexit 1\n');
    chmodSync(refusingTool, 0o755);

    assert.throws(
        () => saveInterface({
            contentDir: root, name: 'sample',
            scripts: { bad: 'nonsense' }, cs2Tool: refusingTool,
        }),
        (error) => error instanceof ExportError && /refused 1 script/.test(error.message));
    assert.equal(existsSync(join(root, 'scripts', 'bad.cs2')), false,
        'nothing reaches the tree');
});

/* -------------------------------------------------------------------------
 * New components
 * ---------------------------------------------------------------------- */

test('a new component gets an id past the highest, never a gap', () => {
    /*
     * A recycled file id hands one component's uid to a different component,
     * and every script that referenced the old one silently addresses the new
     * one. The gap at 1..4 stays a gap.
     */
    const root = scratch();
    const result = addComponent({
        contentDir: root, name: 'sample', blockName: 'added', fields: { type: '3' },
    });
    assert.equal(result.fileId, 6, 'past 5, not into the gap');

    const compack = readFileSync(join(root, 'interfaces', 'sample.compack'), 'utf8');
    assert.match(compack, /6=added/);
    assert.match(readFileSync(ifPath(root), 'utf8'), /\[added\]\nif3=yes\ntype=3/);
});

test('adding a component that already exists is refused', () => {
    const root = scratch();
    assert.throws(
        () => addComponent({ contentDir: root, name: 'sample', blockName: 'content' }),
        (error) => error instanceof ExportError && /already has file id 5/.test(error.message));
});

test('the rest of the interface survives an addition', () => {
    const root = scratch();
    addComponent({ contentDir: root, name: 'sample', blockName: 'added' });
    const after = readFileSync(ifPath(root), 'utf8');
    assert.ok(after.startsWith(IF_TEXT.split('\n[content]')[0]),
        'everything before the new block is byte-identical');
    assert.match(after, /somethingunmodelled=42/);
});

let failed = 0;
for( const [name, fn] of tests )
{
    try { fn(); console.log(`ok   ${name}`); }
    catch( error ) { failed++; console.error(`FAIL ${name}\n     ${error.message}`); }
}
console.log(`\n${tests.length - failed}/${tests.length} passed`);
process.exit(failed ? 1 : 0);
