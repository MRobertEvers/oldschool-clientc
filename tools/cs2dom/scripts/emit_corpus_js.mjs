/*
 * Lower a directory of `cs2 decompile --emit ast-json` trees to JavaScript.
 *
 * The standing gate for the AOT back end: every script the decompiler can
 * describe must lower, and every lowering must be a program the JavaScript
 * engine will accept. Parsing is done by the engine itself (`new Function`
 * over the module body) rather than by a parser of our own, because the only
 * opinion that matters is the one that will run it.
 *
 * Usage:
 *   node scripts/emit_corpus_js.mjs <ast-dir> [--out DIR] [--limit N] [--quiet]
 */

import { readdirSync, readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { join, basename } from 'node:path';

import { emitScript, Cs2EmitError } from '../src/cs2_js_emit.js';

const args = process.argv.slice(2);
const astDir = args.find((a) => !a.startsWith('--'));
if( !astDir )
{
    console.error('usage: node scripts/emit_corpus_js.mjs <ast-dir> [--out DIR] [--limit N]');
    process.exit(2);
}
const outDir = flagValue('--out');
const limit = Number(flagValue('--limit') || 0);
const quiet = args.includes('--quiet');

function flagValue(name) {
    const index = args.indexOf(name);
    return index >= 0 && index + 1 < args.length ? args[index + 1] : null;
}

if( outDir ) mkdirSync(outDir, { recursive: true });

const files = readdirSync(astDir).filter((f) => f.endsWith('.json')).sort();
const selected = limit > 0 ? files.slice(0, limit) : files;

let emitted = 0;
let parsed = 0;
const emitFailures = [];
const parseFailures = [];
const scopeFailures = [];
const hostOps = new Map();
const parkClasses = new Map();
let totalBytes = 0;

for( const file of selected )
{
    const path = join(astDir, file);
    let ast;
    try { ast = JSON.parse(readFileSync(path, 'utf8')); }
    catch( error ) { emitFailures.push([file, `unreadable tree: ${error.message}`]); continue; }

    let result;
    try { result = emitScript(ast); }
    catch( error )
    {
        emitFailures.push([file, error instanceof Cs2EmitError ? error.message : String(error)]);
        continue;
    }
    emitted++;
    totalBytes += result.code.length;
    for( const op of result.hostOps ) hostOps.set(op, (hostOps.get(op) || 0) + 1);
    for( const cls of result.parksOn ) parkClasses.set(cls, (parkClasses.get(cls) || 0) + 1);

    /*
     * Parse, do not run. The generated module references H, K and PARK, which
     * only exist at runtime; wrapping the source in a function with those as
     * parameters gives the engine a complete program to accept or reject
     * without needing a host.
     */
    try
    {
        const source = result.code.replace(/^export function\*/m, 'function*');
        // eslint-disable-next-line no-new-func
        new Function('H', 'K', 'PARK', `${source}\nreturn ${result.functionName};`);
        parsed++;
    }
    catch( error )
    {
        parseFailures.push([file, error.message]);
    }

    /*
     * Every local a function reads must be one it declared or was passed.
     *
     * Parsing cannot catch this — an undeclared `$string1` is a perfectly legal
     * global reference to JavaScript, and would only fail at run time, on the
     * one branch that reaches it. It is also exactly the mistake that is easy
     * to make here: a rev-239 array handle lives in a string local, so an
     * array and the string of the same index are one slot, and treating them
     * as two declares one name and reads another.
     */
    const undeclared = undeclaredLocals(result.code);
    if( undeclared.length )
        scopeFailures.push([file, `undeclared: ${undeclared.join(', ')}`]);

    if( outDir )
        writeFileSync(join(outDir, `${basename(file, '.json')}.mjs`), `${result.code}\n`);
}

if( !quiet )
{
    for( const [file, reason] of emitFailures.slice(0, 20) )
        console.error(`EMIT-FAIL ${file}: ${reason}`);
    for( const [file, reason] of parseFailures.slice(0, 20) )
        console.error(`PARSE-FAIL ${file}: ${reason}`);
    for( const [file, reason] of scopeFailures.slice(0, 20) )
        console.error(`SCOPE-FAIL ${file}: ${reason}`);
}

console.log(JSON.stringify({
    trees: selected.length,
    emitted,
    parsed,
    emitFailures: emitFailures.length,
    parseFailures: parseFailures.length,
    scopeFailures: scopeFailures.length,
    generatedKiB: Math.round(totalBytes / 1024),
    distinctHostOps: hostOps.size,
    parkClasses: Object.fromEntries([...parkClasses].sort()),
}, null, 2));

if( outDir )
{
    writeFileSync(join(outDir, 'host_ops.json'),
        JSON.stringify(Object.fromEntries([...hostOps].sort((a, b) => b[1] - a[1])), null, 1));
}

/**
 * Locals a function reads without declaring, in source order.
 *
 * A generated local is always spelled `$name` and always introduced either in
 * the parameter list or by a `let`, so the two sets are recoverable without a
 * parser. Nothing else in the generated vocabulary starts with `$`.
 */
function undeclaredLocals(source) {
    /*
     * String literals first. Cache copy contains `$`-prefixed words — script
     * 9487 builds the message "Unexpected $hotspotID: ..." — and scanning them
     * as code reports eighteen variables that do not exist. Comments go too,
     * since a constant's source spelling rides in one.
     */
    const code = source
        .replace(/"(?:[^"\\]|\\.)*"/g, '""')
        .replace(/\/\*[\s\S]*?\*\//g, '');
    const declared = new Set();
    const header = code.match(/^export function\* \w+\(([^)]*)\)/m);
    if( header )
        for( const match of header[1].matchAll(/\$[A-Za-z0-9_]+/g) ) declared.add(match[0]);
    /* The frame is one `let $a = 0, $b = '';`, so a declaration is any name
     * introduced by `let` or continuing that list after a comma. */
    for( const match of code.matchAll(/(?:\blet |,\s*)(\$[A-Za-z0-9_]+) = /g) )
        declared.add(match[1]);

    const missing = new Set();
    for( const match of code.matchAll(/\$[A-Za-z0-9_]+/g) )
        if( !declared.has(match[0]) ) missing.add(match[0]);
    return [...missing];
}

process.exit(emitFailures.length || parseFailures.length || scopeFailures.length ? 1 : 0);
