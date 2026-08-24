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

import {
    chmodSync, mkdtempSync, writeFileSync, mkdirSync, rmSync, existsSync, readFileSync,
    readdirSync,
} from 'node:fs';
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
import {
    contentInterfaceCatalog, executeContentHooks, openContentInterface, parseBlocks,
} from '../src/content.js';
import { prepareDat2Project } from '../src/dat2.js';
import { page as devPage } from '../src/dev_page.js';
import { modelIndex, rawModel } from '../src/model.js';
import { nativeTreeInspector, parseNativeTree } from '../src/native_tree.js';
import { encodeNativeState, nativePreviewFingerprint } from '../src/native_preview.js';
import { collectInterfaceScripts, prepareNativeOverlay } from '../src/native_overlay.js';

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

test('the interface picker is searchable and keyboard accessible', () => {
    const html = devPage();
    assertIncludes(html, 'id="pick" type="search"');
    assertIncludes(html, 'function matchingEntries(query)');
    assertIncludes(html, "event.key === 'ArrowDown'");
    assertIncludes(html, 'role="listbox"');
});

test('the preview embeds the toridraw WASM model component', () => {
    const html = devPage();
    assertIncludes(html, '<script src="/toridraw/ev_wasm.js"></script>');
    assertIncludes(html, "wrap('ev_w_render'");
    assertIncludes(html, "'/model/' + iface.modelSource");
});

test('untouched controls do not seed false native-state defaults', () => {
    const html = devPage();
    assertIncludes(html, 'return key in state ? state[key] : fallback;');
    assertIncludes(html, 'contents = state[key] = { ...contents };');
});

test('native tree metadata replaces the diagnostic inspector without moving the framebuffer', () => {
    const html = devPage();
    assertIncludes(html, 'hydrateNativeTree(iface, epoch)');
    assertIncludes(html, 'iface.viewport = native.viewport;');
    assertIncludes(html, 'const originX = native ? 0');
    assertIncludes(html, "box.effectiveHidden ? 'hidden' : ''");
});

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

/** A complete but tiny Dat2/content pair for exercising native cache overlays. */
function makeNativeOverlayFixture(name, { fail = false } = {}) {
    const root = join(scratch, name);
    const content = join(root, 'content');
    const cache = join(root, 'cache');
    const cacheRoot = join(root, 'overlays');
    const cs2Names = join(root, 'cs2-names');
    const calls = join(root, 'cachepack-calls.jsonl');
    const tool = join(root, 'fake-cachepack');
    for( const path of [
        join(content, 'pack'), join(content, 'interfaces'), join(content, 'scripts'),
        join(content, 'configs'), cache, cs2Names,
    ] ) mkdirSync(path, { recursive: true });

    writeFileSync(join(cache, 'main_file_cache.dat2'), 'base dat2');
    writeFileSync(join(cache, 'main_file_cache.idx3'), 'interfaces index');
    writeFileSync(join(cache, 'main_file_cache.idx7'), 'models index');
    writeFileSync(join(cache, 'main_file_cache.idx12'), 'scripts index');
    writeFileSync(join(cache, 'main_file_cache.idx255'), 'reference index');
    writeFileSync(join(cs2Names, 'commands.tsv'), '0\treturn\n');

    writeFileSync(join(content, 'pack', '3_interfaces.pack'), '12=panel\n');
    writeFileSync(join(content, 'pack', '8_sprites.pack'), '40=button\n');
    writeFileSync(join(content, 'pack', '12_clientscripts.pack'), [
        '10=entry',
        '11=proc_a',
        '12=event_b',
        '13=numeric_script',
        '14=raw_only',
        '15=event_c',
        '99=unrelated',
        '',
    ].join('\n'));
    writeFileSync(join(content, 'configs', 'all.varbit.compack'), '1=example_bit\n');
    writeFileSync(join(content, 'configs', 'all.varp.compack'), '2=example_var\n');
    writeFileSync(join(content, 'configs', 'all.varp'), '[example_var]\ntype=0\n');
    writeFileSync(join(content, 'interfaces', 'panel.compack'), '0=root\n');
    writeFileSync(join(content, 'interfaces', 'panel.if'), [
        '[root]',
        'if3=yes',
        'type=0',
        'onload=i:10',
        'onop=i:14',
        '',
    ].join('\n'));

    writeFileSync(join(content, 'scripts', 'entry.cs2'), [
        '[clientscript,entry]',
        '~proc_a();',
        'if_setontimer("event_b(0)", null);',
        '~script13();',
        'return;',
    ].join('\n'));
    writeFileSync(join(content, 'scripts', 'proc_a.cs2'), [
        '[proc,proc_a]',
        "if_setonvartransmit('event_c{var1}', null);",
        'return;',
    ].join('\n'));
    writeFileSync(join(content, 'scripts', 'event_b.cs2'), [
        '[clientscript,event_b]',
        '~proc_a();',
        'return;',
    ].join('\n'));
    writeFileSync(join(content, 'scripts', 'numeric_script.cs2'),
                  '[clientscript,numeric_script]\nreturn;\n');
    writeFileSync(join(content, 'scripts', 'event_c.cs2'),
                  '[clientscript,event_c]\nreturn;\n');
    writeFileSync(join(content, 'scripts', 'unrelated.cs2'),
                  '[clientscript,unrelated]\nreturn;\n');
    /* raw_only deliberately has no .cs2: the base cache must supply it. */

    writeFileSync(tool, [
        '#!/usr/bin/env node',
        "const fs = require('node:fs');",
        "const path = require('node:path');",
        "const arg = (name) => process.argv[process.argv.indexOf(name) + 1];",
        "const list = (dir) => fs.existsSync(dir) ? fs.readdirSync(dir).sort() : [];",
        "const src = arg('--src');",
        "const out = arg('--out');",
        "const archiveList = arg('--archive-list');",
        `const calls = ${JSON.stringify(calls)};`,
        'fs.appendFileSync(calls, JSON.stringify({',
        '  argv: process.argv.slice(2),',
        "  archives: fs.readFileSync(archiveList, 'utf8').trim().split(/\\r?\\n/),",
        "  pack: list(path.join(src, 'pack')),",
        "  configs: list(path.join(src, 'configs')),",
        "  interfaces: list(path.join(src, 'interfaces')),",
        "  scripts: list(path.join(src, 'scripts')),",
        '  cs2Names: process.env.CACHEPACK_CS2_NAMES || null,',
        "}) + '\\n');",
        ...(fail ? [
            "process.stderr.write('selected archive(s) were codec-declined\\n');",
            'process.exit(9);',
        ] : [
            "fs.appendFileSync(path.join(out, 'main_file_cache.dat2'), '|overlay');",
        ]),
    ].join('\n'));
    chmodSync(tool, 0o755);

    return {
        root, content, cache, cacheRoot, cs2Names, calls, tool,
        project: { cache, content, revision: 'osrs239' },
    };
}

function nativeOverlayCalls(fixture) {
    if( !existsSync(fixture.calls) ) return [];
    return readFileSync(fixture.calls, 'utf8').trim().split('\n').filter(Boolean)
        .map((line) => JSON.parse(line));
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
    assert(axisFromPositionMode(3, -1, 0, 1, 10) === -1,
           'proportional position must use signed >> 14, not truncating division');
    assert(dimFromParentMode(1, 20, 100) === 80, 'minus mode');
    assert(dimFromParentMode(2, 8192, 100) === 50, 'proportional size');
    assert(dimFromParentMode(2, -1, 1) === -1,
           'proportional size must round a negative product toward minus infinity');
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

test('layout links forward parents before resolving and paints depth first', () => {
    const built = compileSource(`
        import { Layer, Rect } from 'cs2dom';
        export default function ForwardTree() {
            return (
                <Layer id="root" x={10} y={20} width={100} height={80}>
                    <Layer id="first" x={3} y={4} width={50} height={40}>
                        <Rect id="grandchild" x={5} y={6} width={7} height={8} color={1} fill />
                    </Layer>
                    <Rect id="second" x={70} y={4} width={10} height={10} color={2} fill />
                </Layer>
            );
        }
    `);
    const named = (name) => built.ir.components.find((component) => component.name === name);
    /* Put both children before their parent and the grandchild after its parent's
     * sibling. A flat archive walk gets both topology and paint order wrong. */
    built.ir.components = [named('first'), named('second'), named('grandchild'), named('root')];

    const boxes = layout(built.ir, {});
    assert(boxes.map((box) => box.name).join(',') === 'root,first,grandchild,second',
           `paint order was ${boxes.map((box) => box.name).join(',')}`);
    const grandchild = boxes.find((box) => box.name === 'grandchild');
    assert(grandchild.x === 18 && grandchild.y === 30,
           `forward-parent grandchild landed at ${grandchild.x},${grandchild.y}`);
    assert(grandchild.depth === 2, `forward-parent depth was ${grandchild.depth}`);
});

test('layout propagates hidden and collapsed-layer state through whole subtrees', () => {
    const built = compileSource(`
        import { Layer, Rect } from 'cs2dom';
        export default function Visibility() {
            return (
                <Layer id="root" width={100} height={100} hidden>
                    <Layer id="collapsed" width={0} height={20}>
                        <Rect id="child" width={10} height={10} color={1} fill />
                    </Layer>
                </Layer>
            );
        }
    `);
    const boxes = layout(built.ir, {});
    const root = boxes.find((box) => box.name === 'root');
    const collapsed = boxes.find((box) => box.name === 'collapsed');
    const child = boxes.find((box) => box.name === 'child');
    assert(root.effectiveHidden && collapsed.effectiveHidden && child.effectiveHidden,
           'an ancestor hide did not suppress the complete subtree');
    assert(collapsed.culled && child.culled,
           'a zero-width clipping layer did not structurally cull its subtree');
    assert(!root.emitted && !collapsed.emitted && !child.emitted,
           'a hidden/collapsed subtree remained in the paint list');
});

test('scroll extents drive child layout while clamped offsets drive screen position', () => {
    const built = compileSource(`
        import { Layer, Rect } from 'cs2dom';
        export default function Scroller() {
            return (
                <Layer id="root" x={10} y={20} width={100} height={80}
                       scrollWidth={200} scrollHeight={300}>
                    <Rect id="content" widthMode="minus" heightMode="minus"
                          width={0} height={0} color={1} fill />
                </Layer>
            );
        }
    `);
    const rootComponent = built.ir.components.find((component) => component.name === 'root');
    rootComponent.static.scrollX = 30;
    rootComponent.static.scrollY = 40;

    const boxes = layout(built.ir, {});
    const root = boxes.find((box) => box.name === 'root');
    const content = boxes.find((box) => box.name === 'content');
    assert(content.w === 200 && content.h === 300,
           `content used ${content.w}x${content.h} instead of the scroll extent`);
    assert(content.absX === 10 && content.absY === 20, 'scroll changed logical layout coordinates');
    assert(content.x === -20 && content.y === -20,
           `scroll offset produced screen position ${content.x},${content.y}`);
    assert(root.scrollX === 30 && root.scrollY === 40, 'valid scroll offsets were not retained');
    assert(content.clip.left === 10 && content.clip.top === 20 &&
           content.clip.right === 110 && content.clip.bottom === 100,
           `scroll viewport clip was ${JSON.stringify(content.clip)}`);

    rootComponent.static.scrollX = 999;
    rootComponent.static.scrollY = -20;
    const clamped = layout(built.ir, {}).find((box) => box.name === 'root');
    assert(clamped.scrollX === 100 && clamped.scrollY === 0,
           `scroll clamp was ${clamped.scrollX},${clamped.scrollY}`);
});

test('ordinary nested layers replace clips while scroll layers establish surfaces', () => {
    const built = compileSource(`
        import { Layer, Rect } from 'cs2dom';
        export default function Clips() {
            return (
                <Layer id="root" width={100} height={100}>
                    <Layer id="inner" x={80} width={100} height={100}>
                        <Rect id="leaf" x={70} width={20} height={20} color={1} fill />
                    </Layer>
                </Layer>
            );
        }
    `);
    const boxes = layout(built.ir, {}, { width: 200, height: 100 });
    const inner = boxes.find((box) => box.name === 'inner');
    const leaf = boxes.find((box) => box.name === 'leaf');
    assert(inner.clip.left === 0 && inner.clip.right === 100,
           'the outer layer did not clip the inner layer itself');
    assert(leaf.clip.left === 80 && leaf.clip.right === 180,
           `ordinary layer clip compounded instead of replacing: ${JSON.stringify(leaf.clip)}`);

    /* Once the outer layer scrolls, its 100px viewport is the enclosing surface;
     * the inner layer can no longer reset clipping past that viewport. */
    const root = built.ir.components.find((component) => component.name === 'root');
    root.static.scrollWidth = 200;
    const scrolled = layout(built.ir, {}, { width: 200, height: 100 });
    const clippedLeaf = scrolled.find((box) => box.name === 'leaf');
    assert(clippedLeaf.clip.left === 80 && clippedLeaf.clip.right === 100,
           `scroll surface leaked: ${JSON.stringify(clippedLeaf.clip)}`);
});

test('native UITree snapshots drive inspector topology, runtime boxes, visibility and hooks', () => {
    const node = ({
        id, uid, parent = -1, firstChild = -1, nextSibling = -1,
        dynamic = false, childIndex = -1, kind = 'layer', type = 0,
        x = 0, y = 0, width = 10, height = 10, hidden = false,
        culled = false, walked = true, hooks = [], draw = null,
    }) => ({
        node: id, uid, group: uid < 0 ? -1 : uid >> 16, file: uid < 0 ? -1 : uid & 0xffff,
        parent, first_child: firstChild, next_sibling: nextSibling, depth: parent < 0 ? 0 : 1,
        dynamic, child_index: childIndex, kind, type, widget_type: type, if3: true,
        transparency: 0, client_code: 0, item_id: -1, item_count: 0,
        raw: { x, y, width, height, x_mode: 0, y_mode: 0, width_mode: 0, height_mode: 0 },
        box: { x, y, width, height, resolved: true },
        scroll: { x: 0, y: 0, width: 0, height: 0 },
        visibility: {
            own_hidden: hidden, frame_hidden: false, replacement_hidden: false,
            effective_hidden: hidden, culled, walked, displayable: !hidden && !culled,
        },
        hooks,
        draw,
    });
    const document = {
        schema: 1, interface: 12, viewport: { width: 512, height: 334 }, root: 3,
        component_count: 10, live_count: 3, exported_count: 3, emit_count: 1, truncated: false,
        /* Storage order is deliberately unrelated to the sibling links. */
        nodes: [
            node({
                id: 7, uid: 12 << 16, parent: 3, dynamic: true, childIndex: 42,
                kind: 'rectangle', type: 3, x: 18, y: 24, width: 96, height: 31,
                hidden: true,
                hooks: [{ name: 'on_timer', script: 99, argc: 3, string_argc: 0 }],
            }),
            node({ id: 3, uid: 12 << 16, firstChild: 9, width: 512, height: 334 }),
            node({
                id: 9, uid: (12 << 16) | 4, parent: 3, nextSibling: 7,
                kind: 'text', type: 4, x: 12, y: 8, width: 120, height: 20,
                draw: {
                    count: 1, kind: 2, x: 12, y: 8, width: 120, height: 20,
                    clip: { x: 0, y: 0, width: 512, height: 334 }, scroll_x: 0, scroll_y: 0,
                },
            }),
        ],
    };

    const parsed = parseNativeTree(JSON.stringify(document));
    assert(parsed.tree.map((entry) => entry.node).join(',') === '3,9,7',
           `native sibling order was ${parsed.tree.map((entry) => entry.node).join(',')}`);
    const ir = {
        interfaceId: 12,
        components: [
            { fileId: 0, name: 'infinite' },
            { fileId: 4, name: 'title' },
        ],
    };
    const inspector = nativeTreeInspector(parsed, ir);
    assert(inspector.viewport.width === 512 && inspector.viewport.height === 334,
           'native framebuffer dimensions were not preserved');
    assert(inspector.boxes.map((box) => box.name).join(',') === 'infinite,title,infinite[42]',
           `native inspector names/order were ${inspector.boxes.map((box) => box.name).join(',')}`);
    const dynamic = inspector.boxes.find((box) => box.native.node === 7);
    assert(dynamic.w === 96 && dynamic.h === 31, 'post-script native size was replaced by imported IR');
    assert(dynamic.effectiveHidden && !dynamic.emitted, 'native hidden state was not authoritative');
    assert(dynamic.hooks[0] === 'on_timer' && dynamic.native.hooks[0].script === 99,
           'runtime hook metadata was lost');
    const title = inspector.boxes.find((box) => box.name === 'title');
    assert(title.clip.right === 512 && title.native.drawCount === 1,
           'native emit/clip metadata was lost');
});

test('native UITree parser enforces its machine-output bounds and links', () => {
    const empty = JSON.stringify({
        schema: 1, interface: 12, viewport: { width: 512, height: 334 }, root: -1,
        component_count: 0, live_count: 0, exported_count: 0, emit_count: 0,
        truncated: false, nodes: [],
    });
    assertThrows(() => parseNativeTree(empty, { maxBytes: 10 }), 'exceeds 10 bytes');
    const invalid = JSON.parse(empty);
    invalid.root = 4;
    invalid.component_count = 1;
    invalid.live_count = 1;
    invalid.exported_count = 1;
    invalid.nodes = [{
        node: 4, uid: 1, group: 0, file: 1,
        parent: 99, first_child: -1, next_sibling: -1, depth: 0,
        dynamic: false, child_index: -1, kind: 'layer', type: 1, widget_type: 0,
        if3: true, transparency: 0, client_code: 0, item_id: -1, item_count: 0,
        raw: {
            x: 0, y: 0, width: 1, height: 1,
            x_mode: 0, y_mode: 0, width_mode: 0, height_mode: 0,
        },
        box: { x: 0, y: 0, width: 1, height: 1, resolved: true },
        scroll: { x: 0, y: 0, width: 0, height: 0 },
        visibility: {
            own_hidden: false, frame_hidden: false, replacement_hidden: false,
            effective_hidden: false, culled: false, walked: true, displayable: true,
        },
        hooks: [], draw: null,
    }];
    assertThrows(() => parseNativeTree(invalid), 'missing parent 99');
});

test('native preview state transport is canonical, typed and bounded', () => {
    const first = encodeNativeState({
        'varcstr:359': 'dragon', 'stat:3': 77, 'varbit:3958': 1,
        'varc:5': 11, 'varp:115': 4, 'inventory:93': { 0: 995 },
    });
    const reordered = encodeNativeState({
        'varp:115': 4, 'varc:5': 11, 'varbit:3958': 1,
        'stat:3': 77, 'varcstr:359': 'dragon',
    });
    assert(first.equals(reordered), 'state object insertion order changed the native packet');
    assert(first.subarray(0, 8).toString('ascii') === 'C2STATE1', 'state packet magic changed');
    assert(first.readUInt32LE(8) === 5, 'unsupported state slices reached the native packet');
    assertThrows(() => encodeNativeState({ 'stat:25': 1 }), 'out of range');
    assertThrows(() => encodeNativeState({ 'varcstr:1': 2 }), 'must be a string');
    assertThrows(() => encodeNativeState({ 'varp:1': 1.5 }), 'signed 32-bit integer');
    assertThrows(() => encodeNativeState({ 'varp:1': 1, 'varp:01': 2 }), 'duplicate canonical');
});

test('native preview fingerprint covers state and every Dat2 index', () => {
    const root = mkdtempSync(join(scratch, 'native-fingerprint-'));
    const cache = join(root, 'cache.osrs239');
    const binary = join(root, 'torirs');
    mkdirSync(cache);
    writeFileSync(binary, 'fixture');
    writeFileSync(join(cache, 'main_file_cache.dat2'), 'dat2');
    writeFileSync(join(cache, 'main_file_cache.idx0'), 'zero');
    writeFileSync(join(cache, 'main_file_cache.idx17'), 'seventeen');
    const project = { cache, nativeClient: binary, revision: 'osrs239' };
    const baseline = nativePreviewFingerprint(project, 12, 512, 334, {});
    const state = nativePreviewFingerprint(project, 12, 512, 334, { 'varbit:3958': 1 });
    assert(baseline !== state, 'state did not invalidate the native frame');
    writeFileSync(join(cache, 'main_file_cache.idx17'), 'changed index bytes');
    const changed = nativePreviewFingerprint(project, 12, 512, 334, {});
    assert(baseline !== changed, 'an idx-only cache change did not invalidate the frame');
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

/* ---- 3e. opening unpacked content --------------------------------------- */

test('an unpacked .if is rebuilt as React-style preview IR with linked scripts', () => {
    const content = makeContent('open-content');
    writeFileSync(join(content, 'pack', '3_interfaces.pack'), '10=existing_panel\n');
    writeFileSync(join(content, 'pack', '12_clientscripts.pack'), '77=script_77\n');
    writeFileSync(join(content, 'interfaces', 'existing_panel.compack'),
                  '0=universe\n4=backing\n9=label\n');
    writeFileSync(join(content, 'interfaces', 'existing_panel.if'), `
        // existing cache interface
        [universe]
        if3=yes
        type=0
        width=120
        height=40
        onload=i:77,i:-2147483645,s:a,b

        [backing]
        if3=yes
        type=3
        width=120
        height=40
        layer=655360
        colour=2236962
        fill=yes

        [label]
        if3=yes
        type=4
        x=4
        y=4
        width=112
        height=20
        layer=655360
        text=From content
        font=495
        colour=16777215
    `);
    writeFileSync(join(content, 'scripts', 'script_77.cs2'),
                  '// 77\n[clientscript,existing_init](component $component0, string $string0)\nreturn;\n');

    const catalog = contentInterfaceCatalog(content);
    assert(catalog.length === 1 && catalog[0].key === 'content:existing_panel',
           'the content interface was not catalogued');

    const opened = openContentInterface(content, 'existing_panel');
    assert(opened.interfaceId === 10, 'interface id');
    assert(opened.ir.components.length === 3, 'component count');
    assert(opened.ir.components[1].fileId === 4, 'compack file id was not preserved');
    assert(opened.ir.components[1].layer === 0, 'full parent uid was not reduced to its file id');
    assert(opened.ir.components[2].static.text === 'From content', 'text field');
    assert(opened.scripts.length === 1 && opened.scripts[0].id === 77, 'hook script was not loaded');
    assertIncludes(opened.scripts[0].source, '[clientscript,existing_init]');
    assertIncludes(opened.reactSource, 'export default function ExistingPanel()');
    assertIncludes(opened.reactSource, '<Rect id={"backing"}');
    assertIncludes(opened.reactSource, 'universe cache hooks: onload=i:77');
});

test('interface parsing keeps equals signs and commas inside field values', () => {
    const blocks = parseBlocks('[root]\ntext=a=b,c\nonload=i:1,s:x,y\n');
    assert(blocks[0].fields.text === 'a=b,c', 'equals/comma text was truncated');
    assert(blocks[0].fields.onload === 'i:1,s:x,y', 'hook string was truncated');
});

test('cache onload scripts apply conditional component state and resolve model assets', () => {
    const content = makeContent('runtime-content');
    mkdirSync(join(content, 'models'), { recursive: true });
    writeFileSync(join(content, 'pack', '3_interfaces.pack'), '10=runtime_panel\n');
    writeFileSync(join(content, 'pack', '12_clientscripts.pack'), '77=runtime_init\n');
    writeFileSync(join(content, 'pack', '7_models.pack'), '55=model_55\n');
    writeFileSync(join(content, 'pack', '8_sprites.pack'), '170=miscgraphics_0\n179=miscgraphics_9\n');
    writeFileSync(join(content, 'models', 'model_55.model'), Buffer.from([1, 2, 3]));
    writeFileSync(join(content, 'interfaces', 'runtime_panel.compack'), '0=root\n2=panel\n3=avatar\n');
    writeFileSync(join(content, 'interfaces', 'runtime_panel.if'), [
        '[root]', 'if3=yes', 'type=0', 'width=100', 'height=100',
        'onload=i:77,i:-2147483645,i:655362', '',
        '[panel]', 'if3=yes', 'type=5', 'width=20', 'height=20', 'layer=655360', '',
        '[avatar]', 'if3=yes', 'type=6', 'width=20', 'height=20', 'layer=655360',
        'clientcode=328', 'modelzoom=550', '',
    ].join('\n'));
    writeFileSync(join(content, 'scripts', 'runtime_init.cs2'), [
        '[clientscript,runtime_init](component $self, component $panel)',
        'if (%varbit9 = 0) {',
        '  if_sethide(true, $panel);',
        '} else {',
        '  if_sethide(false, $panel);',
        '  if_setgraphic("miscgraphics,9", $panel);',
        '}',
    ].join('\n'));

    const hidden = openContentInterface(content, 'runtime_panel');
    executeContentHooks(hidden, { 'varbit:9': 0 });
    assert(hidden.ir.components.find((component) => component.name === 'panel').static.hidden,
           'false branch state did not hide the panel');
    assert(hidden.ir.components.find((component) => component.name === 'avatar').static.clientCode === 328,
           'client-code model selector was lost');

    const visible = openContentInterface(content, 'runtime_panel');
    executeContentHooks(visible, { 'varbit:9': 1 });
    const panel = visible.ir.components.find((component) => component.name === 'panel');
    assert(!panel.static.hidden && panel.static.sprite === 179,
           'true branch state did not update visibility and sprite');
    assert(stateInputs(visible.ir).some((input) => input.key === 'varbit:9'),
           'script state read did not become a preview control');
    assert(rawModel(content, modelIndex(content), 55).equals(Buffer.from([1, 2, 3])),
           'content model id did not resolve to its raw cache record');
});

test('a Dat2 project is selectively decoded once and invalidated when the cache changes', () => {
    const root = join(scratch, 'dat2-project');
    const cache = join(root, 'cache');
    const derived = join(root, 'derived');
    mkdirSync(join(root, 'ui'), { recursive: true });
    mkdirSync(cache, { recursive: true });
    writeFileSync(join(cache, 'main_file_cache.dat2'), 'first');
    writeFileSync(join(cache, 'main_file_cache.idx255'), 'reference');
    writeFileSync(join(root, 'cs2dom.json'), JSON.stringify({
        cache: 'cache', revision: 'osrs239', sources: 'ui',
    }));

    /* A tiny cachepack stand-in: this test is about staging, reuse and invalidation;
     * the real decoder is exercised by the repository-cache smoke test. */
    const tool = join(root, 'fake-cachepack');
    writeFileSync(tool, [
        '#!/usr/bin/env node',
        "const fs = require('node:fs');",
        "const path = require('node:path');",
        "const arg = (name) => process.argv[process.argv.indexOf(name) + 1];",
        "const out = arg('--src'), cache = arg('--cache');",
        "for (const dir of ['pack', 'interfaces', 'scripts', 'sprites'])",
        "  fs.mkdirSync(path.join(out, dir), { recursive: true });",
        "fs.writeFileSync(path.join(out, 'pack', '3_interfaces.pack'), '0=panel\\n');",
        "fs.writeFileSync(path.join(out, 'pack', '8_sprites.pack'), '');",
        "fs.writeFileSync(path.join(out, 'pack', '12_clientscripts.pack'), '');",
        "fs.writeFileSync(path.join(out, 'interfaces', 'panel.if'), '[root]\\nif3=yes\\ntype=0\\n');",
        "fs.writeFileSync(path.join(out, 'interfaces', 'panel.compack'), '0=root\\n');",
        "fs.appendFileSync(path.join(cache, 'invocations'), '1\\n');",
    ].join('\n'));
    chmodSync(tool, 0o755);

    const project = loadProject(root);
    assert(project.content === null && project.cache === cache, 'cache-only project config');
    const first = prepareDat2Project(project, { tool, cacheRoot: derived });
    assert(first.contentSource === 'dat2' && first.derivedContent, 'Dat2 source marker');
    assert(contentInterfaceCatalog(first.content, { source: 'dat2' }).length === 1,
           'derived interface was not readable');

    const again = prepareDat2Project(project, { tool, cacheRoot: derived });
    assert(again.content === first.content && again.reusedDat2Decode, 'decode was not reused');
    assert(readFileSync(join(cache, 'invocations'), 'utf8').trim().split('\n').length === 1,
           'cachepack ran for an unchanged cache');

    writeFileSync(join(cache, 'main_file_cache.dat2'), 'changed cache bytes');
    const changed = prepareDat2Project(project, { tool, cacheRoot: derived });
    assert(changed.content !== first.content, 'a changed Dat2 cache reused stale output');
    assert(readFileSync(join(cache, 'invocations'), 'utf8').trim().split('\n').length === 2,
           'changed cache was not decoded again');

    const unpacked = join(root, 'content');
    mkdirSync(unpacked);
    const combined = prepareDat2Project({
        ...project, content: unpacked, unpackedContent: unpacked, contentSource: 'content',
    }, { tool, cacheRoot: derived });
    assert(combined.content === unpacked, 'Dat2 replaced the authored content ledger');
    assert(combined.dat2Content === changed.content, 'combined project lost its Dat2 source');
});

test('native overlay closure follows procedures and deferred events but keeps base-only scripts', () => {
    const fixture = makeNativeOverlayFixture('native-overlay-closure');
    const scripts = collectInterfaceScripts(fixture.content, 'panel');
    const actual = scripts.map(({ id, name }) => [id, name]);
    const expected = [
        [10, 'entry'],
        [11, 'proc_a'],
        [12, 'event_b'],
        [13, 'numeric_script'],
        [15, 'event_c'],
    ];
    assert(JSON.stringify(actual) === JSON.stringify(expected),
           `unexpected native script closure: ${JSON.stringify(actual)}`);
    assert(!scripts.some(({ id }) => id === 14),
           'a hook without editable source displaced its base-cache script');
    assert(!scripts.some(({ id }) => id === 99),
           'an unreachable content script was pulled into the overlay');
});

test('native overlay stages a sparse source set and invalidates cache and source keys', () => {
    const fixture = makeNativeOverlayFixture('native-overlay-selection');
    const options = {
        tool: fixture.tool,
        cacheRoot: fixture.cacheRoot,
        cs2Names: fixture.cs2Names,
    };
    const first = prepareNativeOverlay(fixture.project, 'panel', options);
    assert(!first.nativeOverlay.reused, 'a new overlay was reported as reused');
    assert(first.nativeOverlay.interfaceId === 12, 'the selected interface id was lost');
    assert(JSON.stringify(first.nativeOverlay.scriptIds) === JSON.stringify([10, 11, 12, 13, 15]),
           `unexpected overlay script ids: ${JSON.stringify(first.nativeOverlay.scriptIds)}`);
    assert(first.cache !== fixture.cache, 'the overlay still points at the mutable base cache');
    assert(existsSync(join(first.cache, '.cs2dom-native-overlay.json')),
           'the reusable overlay marker was not published');
    assert(readFileSync(join(first.cache, 'main_file_cache.dat2'), 'utf8') === 'base dat2|overlay',
           'the fake packer did not receive and update the cloned cache');
    assert(readFileSync(join(fixture.cache, 'main_file_cache.dat2'), 'utf8') === 'base dat2',
           'overlay composition modified the base cache');

    let calls = nativeOverlayCalls(fixture);
    assert(calls.length === 1, `cachepack ran ${calls.length} times for the first overlay`);
    assert(calls[0].argv.includes('--asset-only') &&
           calls[0].argv.includes('--assets=interfaces,scripts'),
           `cachepack did not receive sparse asset flags: ${JSON.stringify(calls[0].argv)}`);
    assert(JSON.stringify(calls[0].archives) === JSON.stringify([
        'interfaces=12',
        'scripts=10',
        'scripts=11',
        'scripts=12',
        'scripts=13',
        'scripts=15',
    ]), `unexpected archive selection: ${JSON.stringify(calls[0].archives)}`);
    assert(JSON.stringify(calls[0].interfaces) === JSON.stringify(['panel.compack', 'panel.if']),
           `unexpected staged interfaces: ${JSON.stringify(calls[0].interfaces)}`);
    assert(JSON.stringify(calls[0].scripts) === JSON.stringify([
        'entry.cs2', 'event_b.cs2', 'event_c.cs2', 'numeric_script.cs2', 'proc_a.cs2',
    ]), `unexpected staged scripts: ${JSON.stringify(calls[0].scripts)}`);
    assert(JSON.stringify(calls[0].pack) === JSON.stringify([
        '12_clientscripts.pack', '3_interfaces.pack', '8_sprites.pack',
    ]), `the lookup ledgers were not staged: ${JSON.stringify(calls[0].pack)}`);
    assert(JSON.stringify(calls[0].configs) === JSON.stringify([
        'all.varbit.compack', 'all.varp.compack',
    ]), `non-compack config source was staged: ${JSON.stringify(calls[0].configs)}`);
    assert(calls[0].cs2Names === resolve(fixture.cs2Names),
           'the selected CS2 name table was not passed to cachepack');

    const reused = prepareNativeOverlay(fixture.project, 12, options);
    assert(reused.cache === first.cache && reused.nativeOverlay.reused,
           'an identical numeric selection did not reuse its overlay key');
    assert(nativeOverlayCalls(fixture).length === 1,
           'cachepack reran for unchanged compiler inputs');

    writeFileSync(join(fixture.cache, 'main_file_cache.idx7'), 'expanded models index bytes');
    const indexChanged = prepareNativeOverlay(fixture.project, 'panel', options);
    assert(indexChanged.nativeOverlay.key !== first.nativeOverlay.key &&
           indexChanged.cache !== first.cache && !indexChanged.nativeOverlay.reused,
           'an idx-only base-cache change reused a stale overlay');

    const entry = join(fixture.content, 'scripts', 'entry.cs2');
    writeFileSync(entry, `${readFileSync(entry, 'utf8')}\n// edited source\n`);
    const sourceChanged = prepareNativeOverlay(fixture.project, 'panel', options);
    assert(sourceChanged.nativeOverlay.key !== indexChanged.nativeOverlay.key &&
           !sourceChanged.nativeOverlay.reused,
           'a reachable source edit reused a stale overlay');

    const unrelated = join(fixture.content, 'scripts', 'unrelated.cs2');
    writeFileSync(unrelated, `${readFileSync(unrelated, 'utf8')}\n// unrelated edit\n`);
    const unrelatedChanged = prepareNativeOverlay(fixture.project, 'panel', options);
    assert(unrelatedChanged.nativeOverlay.key === sourceChanged.nativeOverlay.key &&
           unrelatedChanged.nativeOverlay.reused,
           'an unreachable source edit invalidated the selected overlay');
    calls = nativeOverlayCalls(fixture);
    assert(calls.length === 3,
           `cachepack should run once per relevant key, but ran ${calls.length} times`);
});

test('native overlay reports cachepack failure and removes its private staging cache', () => {
    const fixture = makeNativeOverlayFixture('native-overlay-failure', { fail: true });
    assertThrows(() => prepareNativeOverlay(fixture.project, 'panel', {
        tool: fixture.tool,
        cacheRoot: fixture.cacheRoot,
        cs2Names: fixture.cs2Names,
    }), 'selected archive(s) were codec-declined');
    assert(existsSync(fixture.cacheRoot) && readdirSync(fixture.cacheRoot).length === 0,
           'a failed cachepack run left a partial overlay behind');
    assert(nativeOverlayCalls(fixture).length === 1,
           'the failing fake cachepack did not receive exactly one attempt');
    assert(readFileSync(join(fixture.cache, 'main_file_cache.dat2'), 'utf8') === 'base dat2',
           'a failed overlay modified the base cache');
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
