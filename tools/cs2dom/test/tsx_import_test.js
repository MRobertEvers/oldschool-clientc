/*
 * Importing a cache interface as something you can actually edit.
 *
 * The gate is the same one the record has: import, export, and the file must
 * be untouched. What is added here is the reason a prop-typed view usually
 * cannot do that — an IF3 component has more fields than the vocabulary
 * models, so anything unnamed has to survive in `raw`, and a hook has to stay
 * a binding rather than become a callback that loses its sentinels.
 */

import assert from 'node:assert/strict';
import { existsSync, readdirSync, readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

import { parseCompack } from '../src/if_record.js';
import {
    applyModel, formatBinding, importInterface, parseBinding, toTsx, TsxImportError,
} from '../src/tsx_import.js';

const HERE = dirname(fileURLToPath(import.meta.url));
const CONTENT = process.env.CS2DOM_CONTENT
    ?? join(HERE, '..', '..', '..', 'OSRS-Content', 'osrs239-content');

const tests = [];
function test(name, fn) { tests.push([name, fn]); }

const IF_TEXT = `// Interface 600 — 3 components.

[universe]
if3=yes
type=0
width=30
height=40
widthmode=1

[frame]
if3=yes
type=0
layer=39321600
onload=i:703,i:-2147483645,s:Kudos List,i:0
somethingunmodelled=42

[readout]
if3=yes
type=4
layer=39321601
colour=16750623
text=Kudos
op1=Use
op1=Drop
`;

const COMPACK = parseCompack('0=universe\n1=frame\n2=readout\n');
const model = () => importInterface({ ifText: IF_TEXT, compack: COMPACK, name: 'sample' });

/* -------------------------------------------------------------------------
 * The gate
 * ---------------------------------------------------------------------- */

test('import then export with no edits changes nothing', () => {
    /*
     * The property that makes an edit's diff one line long. Rebuilding the
     * file from the model would be simpler and would reformat every record in
     * the tree, burying every real change.
     */
    const imported = model();
    const { text, changed } = applyModel(imported);
    assert.equal(text, IF_TEXT);
    assert.deepEqual(changed, []);
});

test('one edited prop is one changed line', () => {
    const imported = model();
    imported.components.find((c) => c.block === 'readout').props.color = 65280;
    const { text, changed } = applyModel(imported);

    assert.deepEqual(changed, ['readout']);
    const before = IF_TEXT.split('\n');
    const after = text.split('\n');
    assert.equal(before.length, after.length);
    assert.deepEqual(before.filter((line, i) => line !== after[i]), ['colour=16750623']);
});

/* -------------------------------------------------------------------------
 * What must survive
 * ---------------------------------------------------------------------- */

test('a field the vocabulary does not model rides raw', () => {
    const imported = model();
    const frame = imported.components.find((c) => c.block === 'frame');
    assert.equal(frame.raw.somethingunmodelled, '42');
    assert.equal('somethingunmodelled' in frame.props, false);
});

test('a repeated key is kept whole, not reduced to one', () => {
    /* An op list is written as repeated keys and the encoder reads all of
     * them; keeping one would change the interface. */
    const imported = model();
    const readout = imported.components.find((c) => c.block === 'readout');
    assert.deepEqual(readout.raw.op1, ['Use', 'Drop']);
});

test('editing one entry of a repeated key is refused rather than guessed', () => {
    const imported = model();
    imported.components.find((c) => c.block === 'readout').raw.op1 = ['Use'];
    assert.throws(() => applyModel(imported),
        (error) => error instanceof TsxImportError && /repeated key/.test(error.message));
});

test('a prop with no field is refused, not silently dropped', () => {
    const imported = model();
    imported.components.find((c) => c.block === 'universe').props.invented = 1;
    assert.throws(() => applyModel(imported),
        (error) => error instanceof TsxImportError && /no field for prop 'invented'/
            .test(error.message));
});

/* -------------------------------------------------------------------------
 * Hooks
 * ---------------------------------------------------------------------- */

test('a hook is a binding, and its sentinels survive', () => {
    /*
     * `-2147483645` is `event_com`, substituted by the client at dispatch.
     * Turning the hook into a JavaScript callback would lose it and invent a
     * call that never happens.
     */
    const binding = parseBinding('i:703,i:-2147483645,s:Kudos List,i:0');
    assert.equal(binding.scriptId, 703);
    assert.deepEqual(binding.args, [-2147483645, 'Kudos List', 0]);
    assert.equal(formatBinding(binding), 'i:703,i:-2147483645,s:Kudos List,i:0');
});

test('a binding that cannot be parsed round-trips as its own text', () => {
    const binding = parseBinding('something,unexpected');
    assert.equal(formatBinding(binding), 'something,unexpected');
});

test('an edited hook writes back in the cache\'s own form', () => {
    const imported = model();
    const frame = imported.components.find((c) => c.block === 'frame');
    frame.hooks.onload[0].args[1] = 'Different Title';
    const { text } = applyModel(imported);
    assert.match(text, /onload=i:703,i:-2147483645,s:Different Title,i:0/);
});

/* -------------------------------------------------------------------------
 * The TSX itself
 * ---------------------------------------------------------------------- */

test('the TSX nests by the cache\'s layer links', () => {
    const source = toTsx(model());
    /* `frame` and `readout` both hang off interface 600; `universe` is the
     * root because nothing names it as a layer. */
    assert.match(source, /<Layer\s+id="universe"/s);
    assert.match(source, /export default function Sample\(\)/);
    assert.match(source, /import \{ Layer, Rect, Text, Graphic, Model, Line \} from 'cs2dom';/);
});

test('the TSX carries raw and bindings so it is editable, not decorative', () => {
    const source = toTsx(model());
    assert.match(source, /raw=\{\{"somethingunmodelled":"42"\}\}/);
    assert.match(source, /"scriptId":703/);
    assert.match(source, /-2147483645/, 'the sentinel is visible in the file');
});

test('a component type outside the vocabulary still presents as something', () => {
    /* An unknown type has to render — refusing would make the whole interface
     * unopenable because of one component. */
    const imported = importInterface({ ifText: '[odd]\nif3=yes\ntype=99\n' });
    assert.equal(imported.components[0].element, 'Layer');
    assert.match(toTsx(imported), /<Layer\s+id="odd"/s);
});

/* -------------------------------------------------------------------------
 * On the real tree
 * ---------------------------------------------------------------------- */

test('every interface in the content tree imports and exports unchanged', () => {
    const dir = join(CONTENT, 'interfaces');
    if( !existsSync(dir) ) { console.log('     (no content tree; skipped)'); return; }

    const files = readdirSync(dir).filter((file) => file.endsWith('.if')).sort();
    const differing = [];
    let checked = 0;

    for( const file of files )
    {
        const name = file.slice(0, -3);
        const ifText = readFileSync(join(dir, file), 'utf8');
        const compackPath = join(dir, `${name}.compack`);
        const compack = existsSync(compackPath)
            ? parseCompack(readFileSync(compackPath, 'utf8')) : null;
        checked++;
        try
        {
            const imported = importInterface({ ifText, compack, name });
            /* And it must be renderable — a model that exports cleanly but
             * cannot be written as TSX is not an editable import. */
            toTsx(imported);
            const { text, changed } = applyModel(imported);
            if( text !== ifText || changed.length ) differing.push(name);
        }
        catch( error ) { differing.push(`${name}: ${error.message}`); }
    }

    assert.equal(differing.length, 0,
        `${differing.length}/${checked} differ: ${differing.slice(0, 5).join(', ')}`);
    console.log(`     ${checked} interfaces imported and exported unchanged`);
});

let failed = 0;
for( const [name, fn] of tests )
{
    try { fn(); console.log(`ok   ${name}`); }
    catch( error ) { failed++; console.error(`FAIL ${name}\n     ${error.message}`); }
}
console.log(`\n${tests.length - failed}/${tests.length} passed`);
process.exit(failed ? 1 : 0);
