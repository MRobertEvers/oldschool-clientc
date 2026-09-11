/*
 * The round trip's standing gate, on the real content tree.
 *
 * Every `.if` in OSRS-Content is parsed and written back with no edits, and
 * the result must be byte-identical. A unit test proves the mechanism on one
 * hand-written sample; this proves it on 968 files a person actually wrote,
 * with whatever comments, spacing, duplicate keys and unmodelled fields they
 * happen to contain.
 *
 * That is the property the exporter rests on: if an untouched interface
 * survives exactly, then a diff after an edit shows only the edit.
 *
 *   node scripts/verify_if_roundtrip.mjs [content-dir]
 */

import { readdirSync, readFileSync } from 'node:fs';
import { join } from 'node:path';

import { parseIf, parseCompack, emitCompack } from '../src/if_record.js';

const contentDir = process.argv[2]
    ?? join(process.cwd(), '..', '..', 'OSRS-Content', 'osrs239-content');
const interfaces = join(contentDir, 'interfaces');

let files = [];
try { files = readdirSync(interfaces); }
catch( error )
{
    console.error(`no interfaces at ${interfaces}: ${error.message}`);
    process.exit(2);
}

let checked = 0;
let compacks = 0;
const differing = [];

for( const file of files.sort() )
{
    const path = join(interfaces, file);
    if( file.endsWith('.if') )
    {
        const original = readFileSync(path, 'utf8');
        const rewritten = parseIf(original).toText();
        checked++;
        if( rewritten !== original ) differing.push([file, firstDifference(original, rewritten)]);
        continue;
    }
    if( file.endsWith('.compack') )
    {
        const original = readFileSync(path, 'utf8');
        const rewritten = emitCompack(parseCompack(original));
        compacks++;
        /* A compack is a plain id=name list; comments and blank lines are not
         * part of it, so the comparison ignores them rather than pretending
         * the emitter should reproduce them. */
        if( normalise(rewritten) !== normalise(original) )
            differing.push([file, firstDifference(normalise(original), normalise(rewritten))]);
    }
}

function normalise(text) {
    return text.split('\n')
        .map((line) => line.replace(/\/\/.*$/, '').trim())
        .filter(Boolean)
        .join('\n');
}

function firstDifference(a, b) {
    const limit = Math.min(a.length, b.length);
    for( let i = 0; i < limit; i++ )
        if( a[i] !== b[i] )
            return `at ${i}: ${JSON.stringify(a.slice(i, i + 40))} vs ` +
                JSON.stringify(b.slice(i, i + 40));
    return `lengths differ: ${a.length} vs ${b.length}`;
}

for( const [file, detail] of differing.slice(0, 10) )
    console.error(`DIFFERS ${file}: ${detail}`);

console.log(JSON.stringify({
    interfaces: checked,
    compacks,
    identical: checked + compacks - differing.length,
    differing: differing.length,
}, null, 2));

process.exit(differing.length ? 1 : 0);
