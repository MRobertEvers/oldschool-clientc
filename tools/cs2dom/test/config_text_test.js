/*
 * Config text markers: `key=default` is absent, `key=empty` is a list with no
 * entries, `key=\default` is the string "default". readNamedConfig must read
 * the full-key text exactly as it read the text that left those keys out.
 */

import assert from 'node:assert/strict';
import { mkdtempSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { tmpdir } from 'node:os';

import { markerOf, unmark } from '../src/config_text.js';
import { readNamedConfig } from '../src/build.js';

const tests = [];
function test(name, fn) { tests.push([name, fn]); }

test('marker test is on the raw text', () => {
    assert.equal(markerOf('default'), 'default');
    assert.equal(markerOf('empty'), 'empty');
    assert.equal(markerOf('default\r'), 'default');
    assert.equal(markerOf('\\default'), null);
    assert.equal(markerOf('foo,default'), null);
    assert.equal(markerOf('Default'), null);
    assert.equal(markerOf(''), null);
});

test('unmark undoes only the marker escape', () => {
    assert.equal(unmark('\\default'), 'default');
    assert.equal(unmark('\\empty'), 'empty');
    assert.equal(unmark('\\\\default'), '\\\\default');
    assert.equal(unmark('a\\nb'), 'a\\nb');
    assert.throws(() => unmark('default'));
});

const OLD = `// varbits
[vb_a]
basevar=varp_a
startbit=0
endbit=3

[vb_b]
basevar=varp_b
startbit=4
endbit=4
`;

const NEW = `// varbits
[vb_a]
basevar=varp_a
startbit=0
endbit=3
debugname=default

[vb_b]
basevar=varp_b
startbit=4
endbit=4
debugname=default
transmit=empty
`;

test('readNamedConfig: full-key text reads as the old text did', () => {
    const dir = mkdtempSync(join(tmpdir(), 'cs2dom-config-text-'));
    const oldPath = join(dir, 'old.varbit');
    const newPath = join(dir, 'new.varbit');
    writeFileSync(oldPath, OLD);
    writeFileSync(newPath, NEW);
    assert.deepEqual(readNamedConfig(newPath), readNamedConfig(oldPath));
    assert.equal('debugname' in readNamedConfig(newPath).get('vb_a'), false);
});

test('readNamedConfig: an escaped marker is a string', () => {
    const dir = mkdtempSync(join(tmpdir(), 'cs2dom-config-text-'));
    const path = join(dir, 'x.varbit');
    writeFileSync(path, '[x]\nbasevar=\\default\nname=\\empty\n');
    const x = readNamedConfig(path).get('x');
    assert.equal(x.basevar, 'default');
    assert.equal(x.name, 'empty');
});

let failed = 0;
for( const [name, fn] of tests ) {
    try {
        fn();
        console.log(`ok   ${name}`);
    } catch( err ) {
        failed++;
        console.log(`FAIL ${name}\n${err.stack}`);
    }
}
if( failed ) process.exit(1);
