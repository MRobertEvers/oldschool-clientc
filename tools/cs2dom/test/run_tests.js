/*
 * Tests.
 *
 * Three kinds, and the middle one is the one that matters.
 *
 *   1. The compiler's own behaviour: what a tree lowers to, which props become
 *      fields, which become scripts, what an operator prints as.
 *   2. The dialect gate: every command in the vocabulary, and every generated
 *      script in the fixtures, handed to 3rd/rscache/tools/cs2 — the real compiler
 *      cachepack calls. An argument order this file gets wrong fails here rather
 *      than in the client, which is the only reason to trust the emitter at all.
 *   3. Refusals: the things that must be errors, because each of them is a way a
 *      component could otherwise compile into something silently dead.
 *
 * Run: node tools/cs2dom/test/run_tests.js
 */

import { mkdtempSync, writeFileSync, mkdirSync, rmSync, existsSync, readFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import { build, loadProject } from '../src/build.js';
import { ModuleGraph, renderModule } from '../src/loader.js';
import { lower } from '../src/ir.js';
import { emitInterface, emitCompack } from '../src/emit_if.js';
import { emitScript } from '../src/emit_cs2.js';
import { PackFile, Ledger } from '../src/ledger.js';
import { OPS } from '../src/ops.js';
import { ELEMENTS, EVENTS } from '../src/components.js';
import { compileScripts, findRepoRoot, CS2_TOOL } from '../src/verify.js';
import { checkRange, rangeContext, SLICES } from '../src/host.js';
import { evaluate, resolveProps, stateInputs } from '../src/eval.js';
import { layout, axisFromPositionMode, dimFromParentMode } from '../src/preview.js';
import { decodeBmp, encodePng } from '../src/png.js';

const HERE = dirname(fileURLToPath(import.meta.url));
const REPO = findRepoRoot(HERE);

let passed = 0;
const failures = [];

function test(name, fn) {
    try {
        fn();
        passed++;
    } catch( error ) {
        failures.push({ name, error });
    }
}

function assert(condition, message) {
    if( !condition ) throw new Error(message || 'assertion failed');
}

function assertIncludes(haystack, needle, what) {
    if( !haystack.includes(needle) )
        throw new Error(`${what || 'output'} does not contain:\n  ${needle}\n--- got ---\n${haystack}`);
}

function assertThrows(fn, fragment) {
    let threw = null;
    try { fn(); } catch( error ) { threw = error; }
    if( !threw ) throw new Error(`expected a failure mentioning "${fragment}", but it succeeded`);
    if( !threw.message.includes(fragment) )
        throw new Error(`expected a failure mentioning "${fragment}", got: ${threw.message}`);
}

/* ---- a scratch project --------------------------------------------------- */

const scratch = mkdtempSync(join(tmpdir(), 'cs2dom-test-'));

/** A content tree with just the pack files a build reads, so no real tree is touched. */
function makeContent(name = 'content') {
    const dir = join(scratch, name);
    mkdirSync(join(dir, 'pack'), { recursive: true });
    mkdirSync(join(dir, 'interfaces'), { recursive: true });
    mkdirSync(join(dir, 'scripts'), { recursive: true });
    mkdirSync(join(dir, 'configs'), { recursive: true });
    writeFileSync(join(dir, 'pack', '3_interfaces.pack'), '0=first\n1=second\n');
    writeFileSync(join(dir, 'pack', '12_clientscripts.pack'), '0=script_0\n41=script_41\n');
    writeFileSync(join(dir, 'configs', 'all.varbit'), '[energy_bit]\nbasevar=sa_energy\nstartbit=0\nendbit=7\n');
    writeFileSync(join(dir, 'configs', 'all.varbit.compack'), '9=energy_bit\n');
    writeFileSync(join(dir, 'configs', 'all.varp.compack'), '300=sa_energy\n');
    return dir;
}

/** Render one .tsx written inline, and lower it. */
function compileSource(source, { name = 'fixture', varcPool = [1400, 1499], ranges = null } = {}) {
    const dir = mkdtempSync(join(scratch, 'src-'));
    const file = join(dir, `${name}.tsx`);
    writeFileSync(file, source);

    const graph = new ModuleGraph();
    const ids = new Map();
    let next = 100;
    const rendered = renderModule(graph, file, {
        varcPool,
        varbitVarp: { 9: 300 },
    });
    const ir = lower({
        tree: rendered.tree,
        states: rendered.states,
        name,
        interfaceId: 700,
        scriptId: (scriptName) => {
            if( !ids.has(scriptName) ) ids.set(scriptName, next++);
            return ids.get(scriptName);
        },
        ranges,
    });
    return {
        ir,
        interfaceText: emitInterface(ir),
        compackText: emitCompack(ir),
        scripts: ir.scripts.map((s) => ({ ...emitScript(s, 700), name: s.name, id: s.id })),
    };
}

/** Compile with a declared set of id ceilings, for the range checks. */
function compileSourceWithRanges(source, ranges) {
    return compileSource(source, { ranges });
}

/* ---- 1. lowering --------------------------------------------------------- */

test('a static tree becomes .if blocks and no scripts', () => {
    const built = compileSource(`
        import { Layer, Text } from 'cs2dom';
        export default function Panel() {
            return (
                <Layer id="root" width={100} height={40}>
                    <Text id="label" x={4} y={4} font={495} color={0xffffff} halign="centre">Hello</Text>
                </Layer>
            );
        }
    `);
    assert(built.scripts.length === 0, 'a tree with no state should generate no scripts');
    assertIncludes(built.interfaceText, '[root]');
    assertIncludes(built.interfaceText, 'type=0');
    assertIncludes(built.interfaceText, 'width=100');
    assertIncludes(built.interfaceText, '[label]');
    assertIncludes(built.interfaceText, 'layer=0');
    assertIncludes(built.interfaceText, 'text=Hello');
    assertIncludes(built.interfaceText, 'halign=1');
    assert(built.compackText === '0=root\n1=label\n', `compack was: ${JSON.stringify(built.compackText)}`);
});

test('composition, props and loops are resolved at build time', () => {
    const built = compileSource(`
        import { Layer, Rect } from 'cs2dom';
        function Row({ index }: { index: number }) {
            return <Rect id={"row" + index} y={index * 12} width={80} height={10} color={0x222222} fill />;
        }
        export default function List() {
            return (
                <Layer id="root" width={80} height={40}>
                    {[0, 1, 2].map((i) => <Row index={i} />)}
                </Layer>
            );
        }
    `);
    assert(built.ir.components.length === 4, `expected 4 components, got ${built.ir.components.length}`);
    assertIncludes(built.interfaceText, '[row2]');
    assertIncludes(built.interfaceText, 'y=24');
    assertIncludes(built.interfaceText, 'fill=yes');
});

test('a prop reading a varp becomes a script bound to that varp', () => {
    const built = compileSource(`
        import { Layer, Text, useVarp } from 'cs2dom';
        export default function Energy() {
            const energy = useVarp(300);
            return (
                <Layer id="root" width={60} height={20}>
                    <Text id="readout" font={495}>{\`\${energy / 100}%\`}</Text>
                </Layer>
            );
        }
    `);
    assert(built.scripts.length === 1, `expected 1 script, got ${built.scripts.length}`);
    assertIncludes(built.interfaceText, 'onvarptransmit=i:100');
    assertIncludes(built.interfaceText, 'varptriggers=300');
    assertIncludes(built.interfaceText, 'onload=i:100');
    assertIncludes(built.scripts[0].source, 'if_settext("<tostring(calc(%var300 / 100))>%", interface_700:1);');
});

test('a varbit binds to the varp that carries it, not to its own id', () => {
    const built = compileSource(`
        import { Layer, Text, useVarbit } from 'cs2dom';
        export default function Bits() {
            const bit = useVarbit(9);
            return <Layer id="root"><Text id="t" font={495}>{\`\${bit}\`}</Text></Layer>;
        }
    `);
    assertIncludes(built.interfaceText, 'varptriggers=300');
    assertIncludes(built.scripts[0].source, '%varbit9');
});

test('a ternary becomes an if/else into a local', () => {
    const built = compileSource(`
        import { Layer, Text, useVarp } from 'cs2dom';
        export default function Colour() {
            const energy = useVarp(300);
            return (
                <Layer id="root">
                    <Text id="t" font={495} color={energy <= 2000 ? 0xff0000 : 0x00ff00}>x</Text>
                </Layer>
            );
        }
    `);
    const source = built.scripts[0].source;
    assertIncludes(source, 'def_int $int0 = 0;');
    assertIncludes(source, 'if (%var300 <= 2000) {');
    assertIncludes(source, '$int0 = 16711680;');
    assertIncludes(source, '} else {');
    assertIncludes(source, 'if_setcolour($int0, interface_700:1);');
});

test('grouped commands re-send the props that did not move', () => {
    const built = compileSource(`
        import { Layer, Rect, useVarp } from 'cs2dom';
        export default function Bar() {
            const value = useVarp(300);
            return <Layer id="root"><Rect id="fill" x={4} y={8} width={value / 10} height={12} color={1} /></Layer>;
        }
    `);
    /* width moved; height, and both modes, ride along because if_setsize takes four. */
    assertIncludes(built.scripts[0].source, 'if_setsize(calc(%var300 / 10), 12, 0, 0, interface_700:1);');
});

test('constant arithmetic never reaches the emitter', () => {
    const built = compileSource(`
        import { Layer, Rect } from 'cs2dom';
        const GAP = 4;
        export default function Fixed() {
            return <Layer id="root"><Rect id="r" x={GAP * 2 + 1} width={10} height={10} color={2} /></Layer>;
        }
    `);
    assert(built.scripts.length === 0, 'constant maths should not produce a script');
    assertIncludes(built.interfaceText, 'x=9');
});

test('local state is written by its handler, which carries the dependent updates', () => {
    const built = compileSource(`
        import { Layer, Text, useState, actions } from 'cs2dom';
        export default function Toggle() {
            const [open, setOpen] = useState(0);
            return (
                <Layer id="root" ops={['Toggle']} onOp={() => setOpen(1)}>
                    <Text id="label" font={495} color={open === 1 ? 0xffffff : 0x666666}>state</Text>
                </Layer>
            );
        }
    `);
    const handler = built.scripts.find((s) => s.name.endsWith('_onop'));
    assert(handler, 'expected a handler script');
    assertIncludes(handler.source, '%varcint1400 = 1;');
    /* The write is followed by the update of the component that reads it — there is
     * no varc transmit hook to do that for us. The local is $int1 because $int0 is
     * the handler's own op parameter. */
    assertIncludes(handler.source, 'if (%varcint1400 = 1) {');
    assertIncludes(handler.source, 'if_setcolour($int1, interface_700:1);');
    assert(!built.interfaceText.includes('varctriggers'), 'a varc must not claim a trigger list');
});

test('an op handler receives the op index as a script parameter', () => {
    const built = compileSource(`
        import { Layer, actions } from 'cs2dom';
        export default function Menu() {
            return <Layer id="root" ops={[[1, 'Open'], [2, 'Close']]} onOp={(op) => actions.set('root', 'transparency', op)} />;
        }
    `);
    const handler = built.scripts.find((s) => s.name.endsWith('_onop'));
    assertIncludes(handler.source, '[clientscript,cs2dom_fixture_root_onop](int $int0)');
    assertIncludes(handler.source, 'if_settrans($int0, interface_700:0);');
    assertIncludes(built.interfaceText, 'onop=i:100,i:-2147483644');
    assertIncludes(built.interfaceText, 'op1=Open');
    assertIncludes(built.interfaceText, 'op2=Close');
});

/* ---- 2. the dialect gate ------------------------------------------------- */

test('every command in the vocabulary compiles', () => {
    if( !REPO || !existsSync(join(REPO, CS2_TOOL)) ) {
        process.stderr.write(`  (skipped: ${CS2_TOOL} is not built)\n`);
        return;
    }

    /* One script per command, with an argument for every slot the table declares. */
    const scripts = [];
    let id = 9000;
    for( const [command, signature] of Object.entries(OPS) ) {
        if( !signature.args ) continue;
        const args = signature.args.map((prop) => sampleArgument(command, prop));
        scripts.push({
            id: id++,
            name: command,
            source: `// ${id}\n[clientscript,probe_${command}]\n${command}(${args.join(', ')}, interface_700:1);\n`,
        });
    }

    const result = compileScripts(scripts, { repoRoot: REPO, scratch });
    if( result.failures.length ) {
        const detail = result.failures.map((f) => `${f.name}: ${f.message}`).join('\n  ');
        throw new Error(`commands the real compiler rejected:\n  ${detail}`);
    }
    assert(result.compiled === scripts.length,
           `compiled ${result.compiled} of ${scripts.length} command probes`);
});

function sampleArgument(command, prop) {
    const booleans = new Set(['hidden', 'tiled', 'hFlip', 'vFlip', 'fill', 'lineDirection',
                              'noClickThrough', 'orthographic']);
    if( command === 'if_settextshadow' && prop === 'shadow' ) return 'true';
    if( command === 'if_setgraphicshadow' && prop === 'shadow' ) return '255';
    if( booleans.has(prop) ) return 'true';
    if( prop === 'text' || prop === 'targetVerb' ) return '"probe"';
    return '1';
}

test('the generated scripts of every fixture compile', () => {
    if( !REPO || !existsSync(join(REPO, CS2_TOOL)) ) {
        process.stderr.write(`  (skipped: ${CS2_TOOL} is not built)\n`);
        return;
    }

    const fixtures = [
        `import { Layer, Text, Graphic, useVarp, useStat, useState, actions } from 'cs2dom';
         export default function All() {
             const energy = useVarp(300);
             const level = useStat(3);
             const [mode, setMode] = useState(0);
             const percent = energy / 100;
             return (
                 <Layer id="root" width={200} height={80} ops={['Toggle']} onOp={() => setMode(1)}>
                     <Text id="readout" font={495} halign="centre"
                           color={percent <= 20 ? 0xff981f : 0x00ff00}
                           hidden={mode === 1}>
                         {\`\${percent}% and \${level} hp\`}
                     </Text>
                     <Graphic id="icon" x={40} width={26} height={26}
                              sprite={percent <= 20 ? 1058 : 1067}
                              transparency={percent * 2} />
                     <Layer id="row" y={40} width={100} height={20}
                            onMouseOver={() => actions.set('readout', 'color', 0xffffff)}
                            onMouseLeave={() => actions.set('readout', 'color', 0x00ff00)} />
                 </Layer>
             );
         }`,
        `import { Layer, Model, Line, Rect, useVarp } from 'cs2dom';
         export default function Shapes() {
             const spin = useVarp(300);
             return (
                 <Layer id="root" width={120} height={120}>
                     <Model id="doll" model={1234} zoom={800} yAngle={spin * 8} seq={808} />
                     <Line id="rule" y={100} width={120} color={0x333333} lineWidth={2} />
                     <Rect id="bg" width={120} height={120} color={0x101010} fill transparency={spin / 40} />
                 </Layer>
             );
         }`,
    ];

    const scripts = [];
    let id = 9500;
    fixtures.forEach((source, i) => {
        const built = compileSource(source, { name: `fixture${i}` });
        for( const script of built.scripts )
            scripts.push({ id: id++, name: script.name, source: script.source.replace(/^\/\/ \d+/, `// ${id}`) });
    });

    assert(scripts.length > 0, 'the fixtures should have generated scripts');
    const result = compileScripts(scripts, { repoRoot: REPO, scratch });
    if( result.failures.length ) {
        const detail = result.failures
            .map((f) => `${f.name}: ${f.message}\n${f.source}`).join('\n');
        throw new Error(`generated CS2 the real compiler rejected:\n${detail}`);
    }
});

test('the op table and the component schema name the same commands', () => {
    for( const [element, definition] of Object.entries(ELEMENTS) ) {
        for( const [prop, schema] of Object.entries(definition.props) ) {
            if( !schema.op ) continue;
            const signature = OPS[schema.op];
            assert(signature, `${element}.${prop} names ${schema.op}, which ops.js does not know`);
            assert(signature.args === null || signature.args.includes(prop),
                   `${schema.op} does not list '${prop}' among its arguments`);
        }
    }
});

/* ---- 3. refusals --------------------------------------------------------- */

test('binding state to a prop with no runtime command is an error', () => {
    assertThrows(() => compileSource(`
        import { Layer, Rect, useVarp } from 'cs2dom';
        export default function Bad() {
            const v = useVarp(300);
            return <Layer id="root"><Rect id="r" width={10} height={10} color={1} clickMask={v} /></Layer>;
        }
    `), 'can only be given a fixed value');
});

test('an unknown prop is an error that lists the real ones', () => {
    assertThrows(() => compileSource(`
        import { Layer, Text } from 'cs2dom';
        export default function Bad() {
            return <Layer id="root"><Text id="t" font={495} colour={0xffffff}>x</Text></Layer>;
        }
    `), "has no prop 'colour'");
});

test('two components with the same id is an error', () => {
    assertThrows(() => compileSource(`
        import { Layer, Rect } from 'cs2dom';
        export default function Bad() {
            return (
                <Layer id="root">
                    <Rect id="same" width={1} height={1} color={1} />
                    <Rect id="same" width={1} height={1} color={2} />
                </Layer>
            );
        }
    `), "two components are called 'same'");
});

test('comparing an int with a string is an error', () => {
    assertThrows(() => compileSource(`
        import { Layer, Text, useVarp } from 'cs2dom';
        export default function Bad() {
            const v = useVarp(300);
            return <Layer id="root"><Text id="t" font={495} hidden={v === ('x' as any)}>y</Text></Layer>;
        }
    `), 'cannot compare');
});

test('a comparison used as a value is an error, not a wrong number', () => {
    assertThrows(() => compileSource(`
        import { Layer, Text, useVarp } from 'cs2dom';
        export default function Bad() {
            const v = useVarp(300);
            return <Layer id="root"><Text id="t" font={495} x={(v <= 5) as any}>y</Text></Layer>;
        }
    `), 'is int, but the expression is boolean');
});

test('markup-opening text is refused rather than silently rendered as a tag', () => {
    assertThrows(() => compileSource(`
        import { Layer, Text, useVarp } from 'cs2dom';
        export default function Bad() {
            const v = useVarp(300);
            return <Layer id="root"><Text id="t" font={495}>{\`<col=ff0000>\${v}\`}</Text></Layer>;
        }
    `), "'<' cannot appear");
});

test('a handler that returns something other than actions is an error', () => {
    assertThrows(() => compileSource(`
        import { Layer } from 'cs2dom';
        export default function Bad() {
            return <Layer id="root" ops={['Go']} onOp={() => 42 as any} />;
        }
    `), 'a handler returns actions');
});

test('an import that is not cs2dom or a local file is refused', () => {
    assertThrows(() => compileSource(`
        import { readFileSync } from 'node:fs';
        import { Layer } from 'cs2dom';
        const stolen = readFileSync;
        export default function Bad() { return <Layer id="root" name={String(stolen)} />; }
    `), "cannot import 'node:fs'");
});

/* ---- 3b. host state ------------------------------------------------------ */

test('an id outside its slice is a build error naming the range', () => {
    assertThrows(() => compileSourceWithRanges(`
        import { Layer, Text, useStat } from 'cs2dom';
        export default function Bad() {
            const level = useStat(40);
            return <Layer id="root"><Text id="t" font={495}>{\`\${level}\`}</Text></Layer>;
        }
    `, { varp: 5746, inv: 1025 }), 'outside the range this cache defines (0..22)');

    assertThrows(() => compileSourceWithRanges(`
        import { Layer, Text, useVarp } from 'cs2dom';
        export default function Bad() {
            const v = useVarp(99999);
            return <Layer id="root"><Text id="t" font={495}>{\`\${v}\`}</Text></Layer>;
        }
    `, { varp: 5746 }), 'outside the range this cache defines (0..5746)');
});

test('a varc outside the cache table is allowed — an interface allocates its own', () => {
    const built = compileSourceWithRanges(`
        import { Layer, Text, useState } from 'cs2dom';
        export default function Local() {
            const [n] = useState(0, { varc: 99999 });
            return <Layer id="root"><Text id="t" font={495} color={n === 1 ? 1 : 2}>x</Text></Layer>;
        }
    `, { varc: 1506 });
    assert(built.scripts.length === 1, 'expected the update script');
});

test('the preview answers host reads out of the state it is given', () => {
    const built = compileSource(`
        import { Layer, Text, useVarp, useStat, useInvCount } from 'cs2dom';
        export default function Panel() {
            const energy = useVarp(300);
            const hp = useStat(3);
            const coins = useInvCount(93, 995);
            return (
                <Layer id="root" width={200} height={40}>
                    <Text id="t" font={495}>{\`\${energy / 100} \${hp} \${coins}\`}</Text>
                </Layer>
            );
        }
    `);
    const state = { 'varp:300': 4300, 'stat:3': 55, 'invobj:93': { 995: 250 } };
    const boxes = layout(built.ir, state);
    const text = boxes.find((b) => b.name === 't').props.text;
    assert(text === '43 55 250', `preview text was "${text}"`);
});

test('a host read with no model is reported, not invented', () => {
    const built = compileSource(`
        import { Layer, Text, useVarp, cs2 } from 'cs2dom';
        export default function Panel() {
            const v = useVarp(300);
            const named = cs2.enumLookup('int', 'string', 680, v);
            return <Layer id="root"><Text id="t" font={495}>{\`\${named}\`}</Text></Layer>;
        }
    `);
    const unmodelled = new Set();
    layout(built.ir, {}, undefined, unmodelled);
    assert([...unmodelled].some((u) => u.startsWith('enum:')),
           `expected the enum read to be reported, got ${[...unmodelled]}`);
});

test('every state slice offers a control, so nothing is untestable', () => {
    for( const [kind, slice] of Object.entries(SLICES) ) {
        assert(slice.control, `${kind} has no control`);
        assert(slice.request, `${kind} does not name a host request`);
        assert(typeof slice.read === 'function', `${kind} cannot be read`);
    }
});

test('state inputs carry the slice a control is built from', () => {
    const built = compileSource(`
        import { Layer, Text, useVarp, useStat } from 'cs2dom';
        export default function Panel() {
            const v = useVarp(300);
            const hp = useStat(3);
            return <Layer id="root"><Text id="t" font={495}>{\`\${v}\${hp}\`}</Text></Layer>;
        }
    `);
    const inputs = stateInputs(built.ir);
    const stat = inputs.find((i) => i.kind === 'stat');
    assert(stat.control.max === 99, 'a stat control should stop at 99');
    assert(stat.request === 'STAT', 'a stat should name its host request');
    assert(inputs.find((i) => i.kind === 'varp').readBy.includes('t'), 'the reader should be named');
});

/* ---- 3c. the preview's geometry ------------------------------------------ */

test('layout matches the client formulas, including the fixed-point modes', () => {
    /* src/ui/ui_if3_layout.h: centre truncates, proportional is a 14-bit shift. */
    assert(axisFromPositionMode(1, 0, 0, 100, 27) === 36, 'centre mode');
    assert(axisFromPositionMode(2, 4, 0, 100, 20) === 76, 'right mode');
    assert(axisFromPositionMode(3, 8192, 0, 100, 10) === 50, 'proportional mode');
    assert(dimFromParentMode(1, 20, 100) === 80, 'minus mode');
    assert(dimFromParentMode(2, 8192, 100) === 50, 'proportional size');
});

test('nested components lay out against their parent, not the viewport', () => {
    const built = compileSource(`
        import { Layer, Rect } from 'cs2dom';
        export default function Nested() {
            return (
                <Layer id="root" x={10} y={10} width={100} height={100}>
                    <Layer id="inner" x={5} y={5} width={50} height={50}>
                        <Rect id="dot" xMode="abs_centre" yMode="abs_centre"
                              width={10} height={10} color={1} fill />
                    </Layer>
                </Layer>
            );
        }
    `);
    const boxes = layout(built.ir, {});
    const dot = boxes.find((b) => b.name === 'dot');
    /* root at 10,10; inner at 15,15; centred 10x10 in a 50x50 is +20. */
    assert(dot.x === 35 && dot.y === 35, `dot landed at ${dot.x},${dot.y}`);
});

/* ---- 3d. sprites --------------------------------------------------------- */

test('a cachepack sprite bitmap becomes a PNG', () => {
    /* A 2x1 BGRA bitmap, bottom-up, the shape cachepack writes. */
    const header = Buffer.alloc(54);
    header.write('BM', 0, 'ascii');
    header.writeUInt32LE(54 + 8, 2);
    header.writeUInt32LE(54, 10);
    header.writeUInt32LE(40, 14);
    header.writeInt32LE(2, 18);
    header.writeInt32LE(1, 22);
    header.writeUInt16LE(1, 26);
    header.writeUInt16LE(32, 28);
    const pixels = Buffer.from([0x10, 0x20, 0x30, 0xff, 0x40, 0x50, 0x60, 0x80]);
    const decoded = decodeBmp(Buffer.concat([header, pixels]));

    assert(decoded.width === 2 && decoded.height === 1, 'dimensions');
    /* BGRA in, RGBA out. */
    assert(decoded.rgba[0] === 0x30 && decoded.rgba[2] === 0x10, 'channel order');
    assert(decoded.rgba[7] === 0x80, 'alpha survives');

    const png = encodePng(decoded);
    assert(png.subarray(0, 8).equals(Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a])),
           'PNG signature');
    assert(png.readUInt32BE(16) === 2 && png.readUInt32BE(20) === 1, 'PNG dimensions');
});

/* ---- 4. ids -------------------------------------------------------------- */

test('ids are allocated past the highest in the pack file and never reused', () => {
    const dir = makeContent('ids');
    const ledger = new Ledger(dir);

    assert(ledger.interfaceId('first') === 0, 'an existing name keeps its id');
    assert(ledger.interfaceId('brand_new') === 2, 'a new name goes after the highest');
    assert(ledger.interfaceId('brand_new') === 2, 'the same name is stable within a run');
    assert(ledger.scriptId('cs2dom_x') === 42, 'script ids continue past the highest, not the count');

    ledger.write();

    const reread = new Ledger(dir);
    assert(reread.interfaceId('brand_new') === 2, 'an allocated id survives a rebuild');
    assert(reread.interfaceId('another') === 3, 'the next allocation follows it');
});

test('a pack file keeps its comment header when rewritten', () => {
    const dir = makeContent('header');
    writeFileSync(join(dir, 'pack', '3_interfaces.pack'), '// the interface namespace\n0=first\n');
    const pack = new PackFile(join(dir, 'pack', '3_interfaces.pack'));
    pack.idFor('second');
    pack.write();
    const text = readFileSync(join(dir, 'pack', '3_interfaces.pack'), 'utf8');
    assertIncludes(text, '// the interface namespace');
    assertIncludes(text, '1=second');
});

/* ---- 5. the build -------------------------------------------------------- */

test('a build writes the tree, and refuses to stand on a file it did not write', () => {
    const content = makeContent('build');
    const root = mkdtempSync(join(scratch, 'project-'));
    mkdirSync(join(root, 'ui'));
    writeFileSync(join(root, 'cs2dom.json'),
                  JSON.stringify({ content, sources: 'ui', varcPool: [1400, 1499] }));
    writeFileSync(join(root, 'ui', 'panel.tsx'), `
        import { Layer, Text, useVarp } from 'cs2dom';
        export default function Panel() {
            const v = useVarp(300);
            return <Layer id="root" width={50} height={20}><Text id="t" font={495}>{\`\${v}\`}</Text></Layer>;
        }
    `);

    const project = loadProject(root);
    const result = build(project, { dryRun: false });

    assert(existsSync(join(content, 'interfaces', 'panel.if')), 'the .if was not written');
    assert(existsSync(join(content, 'interfaces', 'panel.compack')), 'the .compack was not written');
    assert(result.results[0].scripts.length === 1, 'expected one generated script');
    const scriptName = result.results[0].scripts[0].name;
    assert(existsSync(join(content, 'scripts', `${scriptName}.cs2`)), 'the .cs2 was not written');

    const packText = readFileSync(join(content, 'pack', '3_interfaces.pack'), 'utf8');
    assertIncludes(packText, '=panel');

    /* A second build is a no-op on ids: same names, same numbers. */
    const again = build(loadProject(root), { dryRun: false });
    assert(again.results[0].interfaceId === result.results[0].interfaceId, 'the interface id moved');

    /* Someone else's file is not ours to replace. */
    writeFileSync(join(content, 'interfaces', 'panel.if'), '[handwritten]\nif3=yes\ntype=0\n');
    assertThrows(() => build(loadProject(root), { dryRun: false }), 'refusing to overwrite');
});

/* ---- done ---------------------------------------------------------------- */

rmSync(scratch, { recursive: true, force: true });

if( failures.length ) {
    process.stderr.write(`\n${failures.length} failed, ${passed} passed\n\n`);
    for( const failure of failures )
        process.stderr.write(`  ${failure.name}\n    ${failure.error.message.replace(/\n/g, '\n    ')}\n\n`);
    process.exit(1);
}
process.stdout.write(`${passed} passed\n`);
