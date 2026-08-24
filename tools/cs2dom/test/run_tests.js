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
import { ELEMENTS, EVENTS, IF_TYPE } from '../src/components.js';
import { compileScripts, findRepoRoot, CS2_TOOL } from '../src/verify.js';
import { checkRange, rangeContext, SLICES } from '../src/host.js';
import {
    createHostRuntime, HOST_REQUEST_COVERAGE, HOST_RUNTIME_SCHEMA, hostRequestCapability,
} from '../src/host_runtime.js';
import { createSourceRuntimeSession } from '../src/cache_runtime.js';
import { createWasmCS2Runtime, __wasmRuntimeTest } from '../src/wasm_runtime.js';
import { evaluate, resolveProps, stateInputs } from '../src/eval.js';
import { layout, axisFromPositionMode, dimFromParentMode } from '../src/preview.js';
import { decodeBmp, encodePng, spriteCanvas, spriteTile } from '../src/png.js';
import { spritePng } from '../src/dev.js';
import {
    contentInterfaceCatalog, executeContentHooks, openContentInterface, parseBlocks,
} from '../src/content.js';
import { prepareDat2Project } from '../src/dat2.js';
import { compileInterfaceProgram } from '../src/bytecode.js';
import { parseEnums, parseObjects, parseParams, parseStructs } from '../src/host_data.js';
import { page as devPage } from '../src/dev_page.js';
import { modelIndex, rawModel } from '../src/model.js';
import { nativeTreeInspector, parseNativeTree } from '../src/native_tree.js';
import { encodeNativeState, nativePreviewFingerprint } from '../src/native_preview.js';
import { collectInterfaceScripts, prepareNativeOverlay } from '../src/native_overlay.js';

const HERE = dirname(fileURLToPath(import.meta.url));
const REPO = findRepoRoot(HERE);

let passed = 0;
const failures = [];
const pendingTests = [];

function test(name, fn) {
    try {
        fn();
        passed++;
    } catch( error ) {
        failures.push({ name, error });
    }
}

function testAsync(name, fn) { pendingTests.push({ name, fn }); }

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
    assertIncludes(html, "wrap('ev_w_render_widget'");
    assertIncludes(html, 'function modelRenderSurface(box, stageWidth, stageHeight)');
    assertIncludes(html, 'box.props.zAngle | 0');
    assertIncludes(html, 'Boolean(source.composed), timing.frame');
    assertIncludes(html, "'/model/' + iface.modelSource");
    assert(!html.includes('element.title ='), 'component hover still opens a native browser tooltip');
});

test('untouched controls do not seed false native-state defaults', () => {
    const html = devPage();
    assertIncludes(html, 'return key in draftState ? draftState[key] : fallback;');
    assertIncludes(html, 'contents = draftState[key] = { ...contents };');
});

test('host-state edits stay focused as drafts until Save state is pressed', () => {
    const html = devPage();
    assertIncludes(html, 'id="save-state" disabled>Save state</button>');
    assertIncludes(html, 'draftState[input.key] = field.value;');
    assertIncludes(html, 'draftState[input.key] = Number(slider.value);');
    assertIncludes(html, 'replaceState(state, draftState);');
    assertIncludes(html, "setStateDirty(false, 'State saved');");
    assertIncludes(html, 'if( !force && nextKey === renderedControlsKey ) return;');
    assert(!html.includes('field.value; refresh();'), 'text edits still refresh on every key');
});

test('the generated dev-page script parses', () => {
    const html = devPage();
    const scripts = [...html.matchAll(/<script(?: [^>]*)?>([\s\S]*?)<\/script>/g)];
    const inline = scripts.find((match) => match[1].trim());
    assert(inline, 'dev page has no inline script');
    new Function(inline[1].replace(/^import .*;$/gm, ''));
});

test('the dev page paints native tiled sprites and full-geometry lines', () => {
    const html = devPage();
    assertIncludes(html, "(props.tiled ? '?tile=1' : '')");
    assertIncludes(html, "context?.createPattern(image, 'repeat')");
    assertIncludes(html, "document.createElementNS('http://www.w3.org/2000/svg', 'line')");
    assertIncludes(html, 'pad + (props.lineDirection ? box.h : 0)');
    assertIncludes(html, 'pad + (props.lineDirection ? 0 : box.h)');
    assert(!html.includes("element.style.height = Math.max(1, props.lineWidth | 0) + 'px'"),
           'line paint still collapses every line to a horizontal strip');
});

test('HOST lookup data preserves cache strings, actions and typed parameters', () => {
    const params = parseParams([
        '[count]', 'type=i', 'default=7', '[label]', 'type=s', 'defaultstr=none',
    ].join('\n'), '31=count\n32=label\n');
    const enums = parseEnums([
        '[links]', 'outputstring=yes', 'defaultstr=https://fallback.test/',
        'valstr=1,https://example.test/path',
    ].join('\n'), '44=links\n');
    const objects = parseObjects([
        '[bank_item]', 'name=Bank item', 'cost=25', 'stackable=yes',
        'model=55', '2dzoom=1337', '2dxan=111', '2dyan=222', '2dzan=333',
        '2dxof=-4', '2dyof=17', 'countobj1=stacked_item,10',
        'ifop1=Withdraw-1', 'op2=Take', 'param=count,int,12',
        'param=label,str,https://item.test/',
        '[stacked_item]', 'model=56', '2dzoom=1777',
    ].join('\n'), '100=bank_item\n101=stacked_item\n', '31=count\n32=label\n');
    const structs = parseStructs([
        '[bank_rules]', 'param=count,int,18', 'param=label,str,ready',
    ].join('\n'), '9=bank_rules\n', '31=count\n32=label\n');
    assert(params[31].defaultInt === 7 && params[32].defaultString === 'none');
    assert(enums[44].values[1] === 'https://example.test/path' &&
        enums[44].defaultString === 'https://fallback.test/');
    assert(objects[100].inventoryOps[0] === 'Withdraw-1' &&
        objects[100].groundOps[1] === 'Take' && objects[100].params[31] === 12 &&
        objects[100].params[32].string === 'https://item.test/');
    assert(objects[100].model === 55 && objects[100].zoom2d === 1337 &&
        objects[100].xan2d === 111 && objects[100].yan2d === 222 &&
        objects[100].zan2d === 333 && objects[100].offsetX2d === -4 &&
        objects[100].offsetY2d === 17 && objects[100].countVariants[0].id === 101 &&
        objects[100].countVariants[0].count === 10,
        `object model presentation data was ${JSON.stringify(objects[100])}`);
    assert(structs[9].params[31] === 18 && structs[9].params[32].string === 'ready');
});

test('unset integer varcs match the C client sentinel', () => {
    const host = createHostRuntime({ interfaceId: 1, components: [{
        fileId: 0, name: 'root', kind: 'Layer', type: 0, layer: null,
        static: { width: 1, height: 1 }, hooks: {},
    }] });
    assert(host.readState('varc', 1422) === -1, 'unset varc did not read as -1');
    host.writeState('varc', 1422, 0, { transmit: false });
    assert(host.readState('varc', 1422) === 0, 'explicit varc zero was lost');
});

test('source analysis matches the C client integer varc sentinel', () => {
    const state = {};
    const source = createSourceRuntimeSession({ interfaceId: 12, components: [] }, { state });
    assert(source.stateRead('%varcint1381') === -1,
           'unset source-analysis varc did not read as -1');
    state['varc:1381'] = 0;
    assert(source.stateRead('%varcint1381') === 0,
           'explicit source-analysis varc zero was lost');
});

test('the dev page renders and interacts with the live React-side host tree', () => {
    const html = devPage();
    assertIncludes(html, "import { createHostRuntime } from '/runtime/host_runtime.js';");
    assertIncludes(html, "import { createWasmCS2Runtime } from '/runtime/wasm_runtime.js';");
    assertIncludes(html, 'session.host = createHostRuntime(iface.runtime.ir');
    assertIncludes(html, 'if( bytecode?.available )');
    assertIncludes(html, 'session.wasm = await createWasmCS2Runtime');
    assertIncludes(html, 'const hostDataCache = new Map();');
    assertIncludes(html, 'const hostData = await loadHostData(iface.runtime);');
    assertIncludes(html, "'Original CS2 bytecode is unavailable; scripts are not executed.'");
    assert(!html.includes('createSourceRuntimeSession'), 'the browser still contains a JS CS2 fallback');
    assertIncludes(html, 'if( epoch !== refreshEpoch )');
    assertIncludes(html, 'disposeRuntimeSession(session, false);');
    assertIncludes(html, 'const result = hostRuntime.dispatch(input);');
    assertIncludes(html, 'iface.boxes = snapshot.boxes;');
    assert(!html.includes('nativeframe'), 'the native framebuffer is still the primary preview');
    assertIncludes(html, "box.effectiveHidden ? 'hidden' : ''");
    assertIncludes(html, "source.kind === 'object' ? 'obj/' + source.id + '.model'",
        'configured object models are not routed through toridraw');
    assertIncludes(html, "source.kind === 'npcHead' || source.kind === 'npcModel'",
        'NPC-backed model components are not routed through toridraw');
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

test('Dat2 programs feed original clientscript bytes to the C/WASM VM', () => {
    const content = makeContent('dat2-bytecode-content');
    const rawScripts = join(content, '.raw', 'scripts');
    mkdirSync(rawScripts, { recursive: true });
    writeFileSync(join(content, 'pack', '12_clientscripts.pack'), '7=raw_entry\n');
    const original = Buffer.from([0x43, 0x53, 0x32, 0, 7, 0xfe, 0x81]);
    writeFileSync(join(rawScripts, 'raw_entry.cs2b'), original);
    const result = {
        source: 'dat2', name: 'raw_panel', contentDir: content,
        scripts: [{ id: 7, name: 'raw_entry', source: '[clientscript,raw_entry]\nreturn;\n' }],
        ir: { components: [{ hooks: { onload: { script: { id: 7 } } } }] },
    };
    const program = compileInterfaceProgram({
        revision: 'osrs239', dat2Content: content, dat2RawScripts: rawScripts,
    }, result);
    assert(program.available && program.scripts.length === 1,
        `raw Dat2 program was unavailable: ${program.warnings.join('; ')}`);
    assert(Buffer.from(program.scripts[0].data, 'base64').equals(original),
        'Dat2 bytecode was decompiled/recompiled instead of transported verbatim');
});

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

test('React model components preserve the native fixed-zoom projection flag', () => {
    const built = compileSource(`
        import { Layer, Model } from 'cs2dom';
        export default function Models() {
            return <Layer id="root"><Model id="preview" model={55} zoom={700} fixedZoom /></Layer>;
        }
    `);
    const model = built.ir.components.find((component) => component.name === 'preview');
    assert(model.static.fixedZoom === true, 'fixed zoom was lost from React IR');
    assertIncludes(built.interfaceText, 'modelfixedzoom=yes');
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

test('dynamic CC objects inherit their layer clip instead of clipping as widget type zero', () => {
    const built = compileSource(`
        import { Layer } from 'cs2dom';
        export default function ObjectClip() {
            return <Layer id="root" width={100} height={80} />;
        }
    `);
    const host = createHostRuntime(built.ir, { viewport: { width: 100, height: 80 } });
    const object = host.createChild('root', 0, 0);
    const defaults = host.component(object).props;
    assert(defaults.xMode === -1 && defaults.yMode === -1 &&
           defaults.widthMode === -1 && defaults.heightMode === -1,
           `CC geometry modes did not retain native unset defaults: ${JSON.stringify(defaults)}`);
    host.request({ kind: 'CC_SETPOSITION', component: object,
        x: 20, y: 10, xmode: 0, ymode: 0 });
    host.request({ kind: 'CC_SETSIZE', component: object,
        width: 20, height: 20, wmode: 0, hmode: 0 });
    const graphic = host.createChild(object, IF_TYPE.graphic, 1);
    host.request({ kind: 'CC_SETPOSITION', component: graphic,
        x: 10, y: 5, xmode: 0, ymode: 0 });
    host.request({ kind: 'CC_SETSIZE', component: graphic,
        width: 30, height: 30, wmode: 0, hmode: 0 });
    host.request({ kind: 'CC_SETSCROLLSIZE', component: object,
        scrollWidth: 200, scrollHeight: 160 });

    const boxes = host.snapshot().boxes;
    const objectBox = boxes.find((box) => box.ref?.key === object.key);
    const graphicBox = boxes.find((box) => box.ref?.key === graphic.key);
    assert(objectBox.kind === 'Object' && objectBox.type === 0,
           'the HOST did not retain the CC object/widget-type distinction');
    assert(objectBox.props.scrollWidth === 0 && objectBox.props.scrollHeight === 0,
           'a CC object incorrectly accepted an RS_LAYER-only scroll extent');
    assert(graphicBox.x === 30 && graphicBox.y === 15 &&
           graphicBox.clip.left === 0 && graphicBox.clip.top === 0 &&
           graphicBox.clip.right === 100 && graphicBox.clip.bottom === 80,
           `CC object incorrectly clipped its child: ${JSON.stringify(graphicBox.clip)}`);
});

/* ---- 3d. the live React-side host --------------------------------------- */

function liveHostFixture() {
    const built = compileSource(`
        import { Graphic, Layer, Model, Rect, Text } from 'cs2dom';
        export default function LiveHost() {
            return (
                <Layer id="root" width={120} height={100}>
                    <Rect id="button" x={10} y={12} width={40} height={24} color={1} fill />
                    <Text id="first_key" x={2} y={50} width={30} height={10} font={495}>a</Text>
                    <Text id="second_key" x={34} y={50} width={30} height={10} font={495}>b</Text>
                    <Text id="hidden_key" x={66} y={50} width={30} height={10} font={495} hidden>c</Text>
                    <Graphic id="icon" x={98} y={2} width={20} height={20} sprite={10} />
                    <Model id="figure" x={98} y={24} width={20} height={24} model={20} zoom={100} />
                </Layer>
            );
        }
    `, { name: 'live_host' });
    const component = (name) => built.ir.components.find((item) => item.name === name);
    const bind = (name, key, script) => {
        component(name).hooks[key] = { script: { id: script }, args: [] };
    };
    bind('root', 'onload', 1);
    bind('button', 'onmouseover', 2);
    bind('button', 'onmouseleave', 3);
    bind('button', 'onclick', 4);
    bind('button', 'onhold', 5);
    bind('button', 'onclickrepeat', 6);
    bind('button', 'onrelease', 7);
    bind('button', 'onscrollwheel', 8);
    bind('first_key', 'onkey', 9);
    bind('second_key', 'on_key', 10);
    bind('hidden_key', 'onkey', 11);
    component('button').events.onClick = { imported: false };
    component('button').events.onMouseOver = { imported: false };
    return { built, component };
}

test('the live host owns its React IR and exposes stable component refs', () => {
    const { built } = liveHostFixture();
    const host = createHostRuntime(built.ir, { viewport: { width: 120, height: 100 } });
    const button = host.ref('button');
    const original = built.ir.components.find((component) => component.name === 'button');

    host.request({
        kind: 'IF_SETPOSITION', component: button,
        x: 20, y: 22, xmode: 0, ymode: 0,
    });
    host.mutate('if_setcolour', button, 0xabcdef);
    host.mutate('if_setgraphicshadow', 'icon', 255);
    host.request({
        kind: 'IF_SETTEXT', component: host.ref('first_key'), text: 'hosted',
    });
    const rendered = host.snapshot();
    const box = rendered.boxes.find((item) => item.name === 'button');
    assert(rendered.schema === HOST_RUNTIME_SCHEMA, 'state-tree schema');
    assert(box.x === 20 && box.y === 22 && box.props.color === 0xabcdef,
           `runtime mutation did not reach React layout: ${JSON.stringify(box)}`);
    assert(rendered.boxes.find((item) => item.name === 'icon').props.shadow === 255,
           'type-specific graphic mutation did not reach React layout');
    assert(original.static.x === 10 && original.static.color === 1,
           'host mutated the caller-owned source IR');
    assert(host.read('if_gettext', 'first_key') === 'hosted',
           'exact C request fields did not map to the component vocabulary');
    assert(host.request({ kind: 'IF_GETWIDTH', component: button }) === 40,
           'named IF_GETWIDTH did not return resolved React geometry');
    assert(host.request({ kind: 'IF_GETLAYER', component: 'root' }) === -1,
           'root IF_GETLAYER did not preserve the native -1 sentinel');
    assert(host.request({ kind: 'IF_GETLAYER', component: button }).componentId ===
           host.ref('root').componentId,
           'child IF_GETLAYER did not return its packed parent reference');
    assert(host.resolve(button).fileId === button.fileId && host.resolve(button).props.color === 0xabcdef,
           'public ref resolver did not expose the cloned runtime component view');
    assertThrows(() => host.request({ kind: 2502, component: button }), 'must be a name');
    assertThrows(() => host.request({ kind: 'IF_GETWIDTH', fields: { component: button } }), 'top-level');

    const child = host.createChild('root', IF_TYPE.text, 7);
    host.request({ kind: 'CC_SETTEXT', component: child, text: 'dynamic' });
    assert(host.component(child).props.text === 'dynamic', 'dynamic child was not addressable by ref');
    const copied = host.request({
        kind: 'CC_COPY', parent: host.ref('root'), srcSubId: 7, dstSubId: 8, dotOperand: false,
    });
    assert(host.component(copied).props.text === 'dynamic', 'CC_COPY did not clone runtime fields');
    assert(child.componentId !== copied.componentId &&
           (child.componentId & 0xffff) >= 0x8000 && (copied.componentId & 0xffff) >= 0x8000,
           `dynamic C/WASM ids aliased: ${child.componentId}, ${copied.componentId}`);
    host.request({ kind: 'IF_SETTEXT', component_id: copied.componentId, text: 'exact dynamic' });
    assert(host.component(copied).props.text === 'exact dynamic' &&
           host.component(child).props.text === 'dynamic',
           'an explicit IF request did not preserve dynamic component identity');
    const found = host.request({
        kind: 'CC_FIND', parent: host.ref('root'), subId: 8, dotOperand: true,
    });
    assert(found.key === copied.key && host.activeRef({ dot: true }).key === copied.key,
           'CC_FIND did not return and activate the exact dynamic ref');
    host.delete(child);
    assertThrows(() => host.component(child), 'stale');
});

test('the live host models sprite, text, model, object and arc presentation fields', () => {
    const { built } = liveHostFixture();
    const host = createHostRuntime(built.ir, {
        viewport: { width: 120, height: 100 },
        hostData: { objects: {
            995: { countVariants: [{ id: 996, count: 100 }] },
            996: { xan2d: 321, yan2d: 654, zoom2d: 1777, offsetY2d: -29 },
        } },
    });

    host.request({ kind: 'IF_SETGRAPHIC', component: 'icon', graphicId: 41 });
    host.request({ kind: 'IF_SETGRAPHIC2', component: 'icon', graphicId: 42 });
    host.request({ kind: 'IF_SET2DANGLE', component: 'icon', angle: 512 });
    host.request({ kind: 'IF_SETHFLIP', component: 'icon', value: 1 });
    const sprite = host.presentation('icon');
    assert(sprite.kind === 'sprite' && sprite.sprite === 41 && sprite.activeSprite === 42 &&
           sprite.angle === 512 && sprite.hFlip, `sprite presentation was ${JSON.stringify(sprite)}`);

    host.request({ kind: 'IF_SETMODELANGLE', component: 'figure',
        xOffset: 1, yOffset: 2, xAngle: 3, yAngle: 4, zAngle: 5, zoom: 600 });
    host.request({ kind: 'IF_SETMODELANGLE', component: 'figure',
        xOffset: 6, yOffset: 7, xAngle: 8, yAngle: 9, zAngle: 10, zoom: 0 });
    host.request({ kind: 'IF_SETNPCHEAD', component: 'figure', npcId: 77 });
    host.request({ kind: 'IF_SETMODELTRANSPARENT', component: 'figure', value: 1 });
    host.request({ kind: 'IF_SETMODELORTHOG', component: 'figure', value: 1 });
    let model = host.presentation('figure');
    assert(model.source.kind === 'npcHead' && model.source.id === 77 && model.transform.zoom === 600 &&
           model.transform.xOffset === 6 && model.transparent && model.orthographic,
           `model presentation was ${JSON.stringify(model)}`);
    assert(host.request({ kind: 'IF_GETMODELZOOM', component: 'figure' }) === 600 &&
           host.request({ kind: 'IF_GETMODELANGLE_X', component: 'figure' }) === 8 &&
           host.request({ kind: 'IF_GETMODELANGLE_Y', component: 'figure' }) === 9 &&
           host.request({ kind: 'IF_GETMODELANGLE_Z', component: 'figure' }) === 10 &&
           host.request({ kind: 'IF_GETMODELTRANSPARENT', component: 'figure' }) === true,
           'IF model getters did not expose the current imperative transform');
    host.setActive('figure');
    assert(host.request({ kind: 'CC_GETMODELZOOM' }) === 600 &&
           host.request({ kind: 'CC_GETMODELANGLE_X' }) === 8 &&
           host.request({ kind: 'CC_GETMODELANGLE_Y' }) === 9 &&
           host.request({ kind: 'CC_GETMODELANGLE_Z' }) === 10 &&
           host.request({ kind: 'CC_GETMODELTRANSPARENT' }) === true,
           'CC model getters did not use the active component transform');

    /* Pirate combilock's timer reads its last x angle and adds its spin speed.
     * Each invocation must therefore observe the previous setter, not the
     * cache-time value (or a synthetic zero). */
    for( let tick = 0; tick < 2; tick++ ) {
        const xAngle = host.request({ kind: 'IF_GETMODELANGLE_X', component: 'figure' });
        host.request({ kind: 'IF_SETMODELANGLE', component: 'figure',
            xOffset: 6, yOffset: 7, xAngle: xAngle + 12, yAngle: 9, zAngle: 10, zoom: 0 });
    }
    assert(host.presentation('figure').transform.xAngle === 32,
           'timer-style model angle updates did not accumulate across requests');
    host.request({ kind: 'IF_SETOBJECT_ALWAYS_NUM', component: 'figure', objectId: 995, count: 123 });
    model = host.presentation('figure');
    assert(model.source.kind === 'object' && model.source.baseId === 995 && model.source.id === 996 &&
           model.source.count === 123 && model.source.numberMode === 1 && model.source.composed &&
           model.transform.xAngle === 321 && model.transform.yAngle === 654 &&
           model.transform.zoom === 1777 && model.transform.xOffset === 0 &&
           model.transform.yOffset === -29,
           `object-backed model presentation was ${JSON.stringify(model)}`);

    host.request({ kind: 'IF_SETTEXT', component: 'first_key', text: 'Ready' });
    host.request({ kind: 'IF_SETTEXTFONT', component: 'first_key', fontId: 494 });
    host.request({ kind: 'IF_SETTEXTALIGN', component: 'first_key', xAlign: 2, yAlign: 1, lineHeight: 13 });
    host.request({ kind: 'IF_SETTEXTSHADOW', component: 'first_key', shadow: 1 });
    const textPresentation = host.presentation('first_key');
    assert(textPresentation.text === 'Ready' && textPresentation.font === 494 &&
           textPresentation.halign === 2 && textPresentation.valign === 1 &&
           textPresentation.lineHeight === 13 && textPresentation.shadow,
           `text presentation was ${JSON.stringify(textPresentation)}`);

    const arc = host.createChild('root', 10, 11);
    host.request({ kind: 'CC_SETSIZE', component: arc, width: 20, height: 20, wmode: 0, hmode: 0 });
    host.request({ kind: 'CC_SETARC', component: arc, arcStart: 32, arcEnd: 768 });
    host.request({ kind: 'CC_SETFILL', component: arc, filled: 1 });
    host.request({ kind: 'CC_SETFILLCOLOUR', component: arc, value: 0x224466 });
    host.request({ kind: 'CC_SETLINEWID', component: arc, value: 3 });
    const arcPresentation = host.presentation(arc);
    assert(arcPresentation.kind === 'arc' && arcPresentation.start === 32 &&
           arcPresentation.end === 768 && arcPresentation.fill &&
           arcPresentation.fillColor === 0x224466 && arcPresentation.lineWidth === 3,
           `arc presentation was ${JSON.stringify(arcPresentation)}`);

    const before = host.version;
    host.request({ kind: 'IF_SETGRAPHIC', component: 'button', graphicId: 99 });
    assert(host.version === before && host.component('button').props.sprite === undefined,
           'a type-mismatched sprite setter was not a deterministic no-op');
});

test('the live host owns typed input metadata, focus/caret state and entity model sources', () => {
    const { built } = liveHostFixture();
    const seen = [];
    const host = createHostRuntime(built.ir, { invoke: (intent) => seen.push(intent) });
    const setters = [
        ['SUBMITMODE', 'submitMode'], ['SELECTCOLOUR', 'selectionColor'],
        ['ACCEPTMODE', 'acceptMode'], ['WRAPMODE', 'wrapMode'],
        ['LINEWRAPPINGWIDTH', 'lineWrappingWidth'],
        ['SELECTBGCOLOUR', 'selectionBackgroundColor'],
        ['LINECOUNTLIMIT', 'lineCountLimit'], ['CURSORCOLOUR', 'cursorColor'],
        ['CURSORTRANS', 'cursorTransparency'], ['CURSORWIDTH', 'cursorWidth'],
        ['CURSORHEIGHT', 'cursorHeight'], ['CURSOROFFSET', 'cursorOffset'],
        ['LINEWIDTHLIMIT', 'lineWidthLimit'], ['CHARFILTER', 'characterFilter'],
    ];
    setters.forEach(([suffix, field], index) => {
        for( const family of ['IF', 'CC'] ) {
            host.request({ kind: `${family}_INPUT_SET${suffix}`,
                component: 'first_key', value: index + 1 });
            assert(host.inputState('first_key')[field] === index + 1,
                   `${family}_INPUT_SET${suffix} did not retain ${field}`);
            assert(hostRequestCapability(`${family}_INPUT_SET${suffix}`).supported,
                   `${family}_INPUT_SET${suffix} remained classified unsupported`);
        }
    });
    assert(host.request({ kind: 'CC_INPUT_GETFOCUS', component: 'first_key' }) === false &&
           host.request({ kind: 'CC_INPUT_GETCARETPOSITION', component: 'first_key' }) === 0,
           'unfocused input defaults were not deterministic');
    host.request({ kind: 'CC_INPUT_SETONFOCUSCHANGED', component: 'first_key',
        scriptId: 88, args: [] });
    const focused = host.setInputState('first_key', { focused: true, caretPosition: 9 });
    assert(focused.intents.length === 1 && focused.intents[0].hook.canonical === 'on_focus_changed' &&
           host.request({ kind: 'CC_INPUT_GETFOCUS', component: 'first_key' }) === true &&
           host.request({ kind: 'CC_INPUT_GETCARETPOSITION', component: 'first_key' }) === 9,
           'focus/caret state did not round-trip through input getters');
    assert(host.presentation('first_key').input.cursorHeight === 11 && seen.at(-1).hook.scriptId === 88,
           'typed input state was absent from the text presentation or focus hook');
    host.mutate('cc_input_setwrapmode', 'first_key', 77);
    assert(host.inputState('first_key').wrapMode === 77 &&
           host.read('cc_input_getfocus', 'first_key') === true,
           'source-interpreter mutate/read aliases bypassed typed input state');

    host.request({ kind: 'CC_SETLOCMODEL', component: 'figure', locId: 321 });
    assert(host.presentation('figure').source.kind === 'locModel' &&
           host.presentation('figure').source.id === 321,
           'CC_SETLOCMODEL did not select a loc presentation source');
    host.request({ kind: 'IF_SETNPCMODEL', component: 'figure', npcId: 654 });
    assert(host.presentation('figure').source.kind === 'npcModel' &&
           host.presentation('figure').source.id === 654,
           'IF_SETNPCMODEL did not select an NPC presentation source');
    host.mutate('cc_setlocmodel', 'figure', 987);
    assert(host.presentation('figure').source.kind === 'locModel' &&
           host.presentation('figure').source.id === 987,
           'source-interpreter model mutation bypassed the model presentation state');
});

test('the live host implements dynamic traversal and component metadata operations', () => {
    const { built } = liveHostFixture();
    const seen = [];
    const host = createHostRuntime(built.ir, {
        paramDefault: (id) => id === 44 ? -1 : 0,
        invoke: (intent) => seen.push(intent),
    });
    const first = host.request({ kind: 'CC_CREATECHILD', parent: 'root',
        componentType: IF_TYPE.text, childIndex: 5 });
    const replacement = host.request({ kind: 'CC_CREATECHILD', parent: 'root',
        componentType: IF_TYPE.text, childIndex: 5 });
    assert(host.resolve(first) === null && replacement.generation !== first.generation,
           're-creating a dynamic slot did not fence the stale incarnation');
    const low = host.request({ kind: 'CC_CREATECHILD', parent: 'root',
        componentType: IF_TYPE.rectangle, childIndex: 2 });
    const nested = host.request({ kind: 'CC_CREATECHILD', parent: replacement,
        componentType: IF_TYPE.text, childIndex: 1 });
    const sibling = host.request({ kind: 'CC_CREATESIBLING', parent: nested,
        componentType: IF_TYPE.graphic, childIndex: 7 });
    assert(host.resolve(sibling).parent.key === replacement.key,
           'CC_CREATESIBLING did not use the requested component\'s parent');

    const collected = host.request({ kind: 'IF_CHILDREN_COLLECT', component: 'root', startIndex: 2 });
    assert(collected.map((ref) => ref.subId).join(',') === '2,5',
           `dynamic collection order was ${collected.map((ref) => ref.subId)}`);
    assert(host.request({ kind: 'CC_CHILDREN_FIND_COUNT', parent: 'root', startIndex: 3 }) === 1,
           'dynamic child count did not honor the inclusive lower bound');
    const next = host.request({ kind: 'CC_CHILDREN_FINDNEXT' });
    assert(next.key === replacement.key && host.activeRef().key === replacement.key,
           'child iteration did not activate the next exact component');

    host.request({ kind: 'CC_SETOP', component: replacement, index: 1, text: 'Choose' });
    host.request({ kind: 'CC_SETOPBASE', component: replacement, text: 'Widget' });
    host.request({ kind: 'CC_SETOPSUBMENU', component: replacement,
        opIndex: 1, subIndex: 3, text: 'Detailed' });
    host.request({ kind: 'CC_SETTARGETPRIORITY', component: replacement, priority: 9 });
    host.request({ kind: 'CC_SETCOMPONENTPARAM', component: replacement, paramId: 12, value: 345 });
    assert(host.request({ kind: 'CC_GETCOMPONENTPARAM', component: replacement, paramId: 12 }) === 345 &&
           host.request({ kind: 'CC_GETCOMPONENTPARAM', component: replacement, paramId: 44 }) === -1,
           'component parameter value/default semantics diverged');
    const view = host.resolve(replacement);
    assert(view.runtime.opBase === 'Widget' && view.runtime.targetPriority === 9 &&
           view.runtime.submenus[1][3] === 'Detailed' && view.runtime.params[12].value === 345,
           `component runtime metadata was ${JSON.stringify(view.runtime)}`);
    assert(host.request({ kind: 'CC_GETID', component: replacement }) === 5 &&
           host.request({ kind: 'IF_GETLAYER', component: replacement }).key === host.ref('root').key &&
           host.request({ kind: 'IF_GETTOP' }) === built.ir.interfaceId,
           'tree identity getters did not preserve component topology');

    host.setHook(replacement, 'on_resize', { scriptId: 77, args: [] });
    const resize = host.request({ kind: 'CC_CALLONRESIZE', component: replacement });
    assert(resize.intents.length === 1 && resize.intents[0].hook.canonical === 'on_resize' &&
           seen.at(-1).component.key === replacement.key,
           'named resize trigger did not dispatch through the HOST hook path');
    host.request({ kind: 'CC_CLEAROPS', component: replacement });
    assert(host.resolve(replacement).ops.length === 0, 'CC_CLEAROPS did not clear component options');
    assert(host.resolve(low), 'unrelated dynamic sibling was removed by metadata changes');
});

test('the live host classifies every generated command name deterministically', () => {
    assert(HOST_REQUEST_COVERAGE.total === HOST_REQUEST_COVERAGE.entries.length &&
           HOST_REQUEST_COVERAGE.supported + HOST_REQUEST_COVERAGE.unsupported ===
               HOST_REQUEST_COVERAGE.total,
           `HOST coverage counts were ${JSON.stringify(HOST_REQUEST_COVERAGE)}`);
    assert(HOST_REQUEST_COVERAGE.uiSupported > 100 &&
           HOST_REQUEST_COVERAGE.uiSupported === HOST_REQUEST_COVERAGE.uiTotal,
           `component HOST coverage was ${HOST_REQUEST_COVERAGE.uiSupported}/` +
           `${HOST_REQUEST_COVERAGE.uiTotal}`);
    assert(hostRequestCapability('IF_SETTEXT').supported,
           'a concrete component handler was classified unsupported');
    const outside = hostRequestCapability('DB_GETFIELD');
    assert(outside.known && !outside.supported && outside.reason.includes('outside'),
           `non-UITree command classification was ${JSON.stringify(outside)}`);
    assert(!hostRequestCapability('NOT_A_COMMAND').known,
           'an unknown command was reported as generated metadata');

    const { built } = liveHostFixture();
    const host = createHostRuntime(built.ir);
    assert(host.request({ kind: 'OC_NAME', id: 1 }) === 'null',
           'missing object record did not use the C host null-name fallback');
    assertThrows(() => host.request({ kind: 'DB_GETFIELD' }), 'explicitly unsupported');
    assertThrows(() => host.request({ kind: 'NOT_A_COMMAND' }), 'unknown host request');
});

test('the complete CC/IF HOST surface has bounded C-client service and tree semantics', () => {
    const { built } = liveHostFixture();
    const seen = [];
    const host = createHostRuntime(built.ir, {
        viewport: { width: 120, height: 100 },
        invoke: (intent) => seen.push(intent),
        interfaceParents: { button: 77 },
        hostData: {
            params: {
                9: { defaultInt: -5 },
                10: { string: true, defaultString: 'fallback' },
            },
            structs: { 2: { params: { 9: 123, 10: 'configured' } } },
        },
    });
    const button = host.ref('button');

    assert(host.request({ kind: 'IF_HASSUB', component_id: button.componentId }) === 1 &&
           host.request({ kind: 'IF_HASCHILD_OVERLAY', component_id: button.componentId,
               group_id: 77 }) === 1 &&
           host.request({ kind: 'IF_HASCHILD_OVERLAY', component_id: button.componentId,
               group_id: 78 }) === 0,
           'interface-parent queries did not mirror the mounted group map');
    assert(host.request({ kind: 'CC_GETPARAM', struct_id: 2, param_id: 9 }) === 123 &&
           host.request({ kind: 'CC_GETPARAM', struct_id: 2, param_id: 10 }) === 'configured' &&
           host.request({ kind: 'CC_GETPARAM', struct_id: -1, param_id: 9 }) === -5 &&
           host.request({ kind: 'CC_GETPARAM', struct_id: -1, param_id: 10 }) === 'fallback',
           'struct parameter values/defaults diverged from the C host');

    host.request({ kind: 'CC_SETCOMPONENTPARAM', component: 'first_key', param_id: 31, value: 200 });
    host.request({ kind: 'CC_SETCOMPONENTPARAM', component: 'first_key', param_id: 32,
        str_value: 'needle' });
    const found = host.request({ kind: 'CC_FIND_PARAM', dot_operand: true,
        args: [host.ref('root'), 31, 200, 32, 'needle', 0, 2] });
    assert(found?.key === host.ref('first_key').key && host.activeRef({ dot: true }).key === found.key,
           `parameter search selected ${JSON.stringify(found)}`);

    host.request({ kind: 'CC_SETHTTPSPRITE', component: 'icon', text: 'https://example.test/a.png' });
    assert(host.presentation('icon').httpSprite === 'https://example.test/a.png',
           'HTTPS sprite state did not reach React presentation');
    assertThrows(() => host.request({ kind: 'CC_SETHTTPSPRITE', component: 'icon', text: 'http://bad' }),
        'must use https');

    host.setHook(button, 'on_drag', { scriptId: 70, args: [] });
    const dragArea = host.ref('root');
    host.request({ kind: 'CC_SETDRAGGABLE', component_id: button.componentId,
        parent_uid: dragArea.componentId, child_index: -1 });
    assert(host.resolve(button).dragParent.key === dragArea.key,
           'CC_SETDRAGGABLE rejected or discarded the C child-index -1 sentinel');
    host.dispatch({ type: 'pointer_move', x: 20, y: 20 });
    const pickup = host.request({ kind: 'CC_DRAGPICKUP', component_id: button.componentId,
        pickup_x: 3, pickup_y: 4 });
    assert(pickup.interaction.dragging && pickup.interaction.dragPickupX === 3 &&
           pickup.interaction.dragPickupY === 4 && pickup.intents[0]?.hook.scriptId === 70,
           `scripted drag pickup was ${JSON.stringify(pickup)}`);
    host.dispatch({ type: 'pointer_up', x: 20, y: 20, button: 0 });

    host.request({ kind: 'CC_RESUME_PAUSEBUTTON', component_id: button.componentId });
    host.request({ kind: 'IF_CLOSE' });
    assert(host.request({ kind: 'CC_CRMVIEW_DISMISS' }) === 0,
           'desktop CRM dismissal did not return its deterministic sentinel');
    host.request({ kind: 'CC_ASSERT' });
    host.request({ kind: 'CC_OP1309', value: 1 });
    host.request({ kind: 'IF_OP2309', component_id: button.componentId, value: 1 });
    const services = host.snapshot().services;
    assert(services.closeModalRequested && services.resumePauseButton.key === button.key &&
           services.crmViewDismissals === 1,
           `bounded service state was ${JSON.stringify(services)}`);
});

test('browser HOST cache reads and bank setter payloads match the C client', () => {
    const { built } = liveHostFixture();
    const advances = new Array(256).fill(0);
    advances[32] = 3;
    advances[65] = 5;
    advances[66] = 6;
    const host = createHostRuntime(built.ir, {
        state: { runenergy: 73, runweight: -4 },
        hostData: {
            clientType: 4,
            mapMembers: false,
            enums: {
                7: { string: true, defaultString: 'missing', values: { 2: 'two' } },
                8: { string: false, defaultInt: -9, values: { 3: 30 } },
            },
            fonts: { 494: { lineHeight: 10, advances } },
            params: {
                9: { defaultInt: -5 },
                10: { string: true, defaultString: 'fallback' },
            },
            structs: {
                2: { params: { 9: 123, 10: 'configured' } },
                3: { params: { 10: 456 } },
            },
            objects: {
                100: { name: 'Item', cost: 50, stackable: 1,
                    placeholderLink: 101, placeholderTemplate: -1,
                    params: { 9: 321, 10: 'object string' },
                    inventoryOps: ['Use', 'Wear', null, null, 'Drop'],
                    groundOps: ['Take', null, null, null, null] },
                101: { placeholderLink: 100, placeholderTemplate: 14401 },
            },
            npcs: { 4: { params: { 9: 44, 10: 'npc string' } } },
            locs: { 5: { params: { 9: 55, 10: 'loc string' } } },
        },
    });

    assert(host.request({ kind: 'CLIENTTYPE' }) === 4 &&
           host.request({ kind: 'MAP_MEMBERS' }) === 0 &&
           host.request({ kind: 'ON_MOBILE' }) === 0,
           'deterministic client-platform reads diverged');
    assert(host.request({ kind: 'RUNENERGY_VISIBLE' }) === 73 &&
           host.request({ kind: 'RUNWEIGHT_VISIBLE' }) === -4,
           'visible run stats did not use host state');
    assert(host.request({ kind: 'ENUM', args: [105, 115, 7, 2] }) === 'two' &&
           host.request({ kind: 'ENUM_STRING', args: [7, 9] }) === 'missing' &&
           host.request({ kind: 'ENUM', args: [105, 105, 8, 3] }) === 30 &&
           host.request({ kind: 'ENUM', args: [105, 105, 8, 4] }) === -9 &&
           host.request({ kind: 'ENUM_GETOUTPUTCOUNT', args: [7] }) === 1,
           'enum value/default/count semantics diverged');
    assert(host.request({ kind: 'ENUM_STRING', args: [-1, 0] }) === 'null' &&
           host.request({ kind: 'ENUM', args: [105, 105, -1, 0] }) === -1,
           'unavailable enum miss sentinels diverged');

    const tagged = '<col=ff0000>AA</col> BB';
    assert(host.request({ kind: 'PARAWIDTH', args: [tagged, 10, 494] }) === 10 &&
           host.request({ kind: 'PARAHEIGHT', args: [tagged, 10, 494] }) === 2,
           'paragraph measurement counted markup or wrapped inside a word');
    assert(host.request({ kind: 'OC_NAME', args: [100] }) === 'Item' &&
           host.request({ kind: 'OC_COST', args: [100] }) === 50 &&
           host.request({ kind: 'OC_PLACEHOLDER', args: [100] }) === 101 &&
           host.request({ kind: 'OC_UNPLACEHOLDER', args: [101] }) === 100 &&
           host.request({ kind: 'OC_UNPLACEHOLDER', args: [999] }) === 999,
           'object/placeholder cache reads diverged');
    assert(host.request({ kind: 'OC_PARAM', item_id: 100, param_id: 9 }) === 321 &&
           host.request({ kind: 'OC_PARAM', item_id: 100, param_id: 10 }) === 'object string' &&
           host.request({ kind: 'OC_PARAM', item_id: -1, param_id: 9 }) === -5 &&
           host.request({ kind: 'OC_PARAM', item_id: -1, param_id: 10 }) === 'fallback' &&
           host.request({ kind: 'STRUCT_PARAM', struct_id: 2, param_id: 9 }) === 123 &&
           host.request({ kind: 'STRUCT_PARAM', struct_id: 2, param_id: 10 }) === 'configured' &&
           host.request({ kind: 'STRUCT_PARAM', struct_id: 3, param_id: 10 }) === 'fallback' &&
           host.request({ kind: 'NC_PARAM', type_id: 4, param_id: 9 }) === 44 &&
           host.request({ kind: 'NC_PARAM', type_id: 4, param_id: 10 }) === 'npc string' &&
           host.request({ kind: 'LC_PARAM', type_id: 5, param_id: 9 }) === 55 &&
           host.request({ kind: 'LC_PARAM', type_id: -1, param_id: 10 }) === 'fallback',
           'entity/struct parameter value and default semantics diverged');
    assert(host.request({ kind: 'OC_IOP', item_id: 100, op_index: 1 }) === 'Wear' &&
           host.request({ kind: 'OC_IOP', item_id: 100, op_index: 2 }) === '' &&
           host.request({ kind: 'OC_OP', item_id: 100, op_index: 0 }) === 'Take' &&
           host.request({ kind: 'OC_IOP', item_id: -1, op_index: 0 }) === '',
           'object operation strings diverged from their zero-based C slots');

    const button = host.ref('button');
    host.request({ kind: 'CC_SETOPBASE', ref: button, values: ['Bank'] });
    host.request({ kind: 'CC_SETDRAGGABLEBEHAVIOR', ref: button, values: [1] });
    host.request({ kind: 'CC_SETDRAGDEADTIME', ref: button, values: [0x7fffffff], time: 0x7fffffff });
    const view = host.resolve(button);
    assert(view.runtime.opBase === 'Bank' && view.dragBehavior === 1 && view.dragDeadTime === 255,
           `bank setter payload/truncation was ${JSON.stringify(view)}`);

    host.request({
        kind: 'CC_SETONCLICK', ref: button, script_id: 44, signature: 'isiY',
        int_args: Int32Array.from([7, 0, 9]), int_arg_count: 3,
        str_arg_mask: 2n, str_args: ['middle'], str_arg_count: 1,
        trigger_ids: Int32Array.from([101, 202, 303]), trigger_count: 2,
    });
    const armed = host._component(button).hooks.on_click;
    assert(armed.script.id === 44 && armed.signature === 'isiY' &&
           JSON.stringify(armed.args) === JSON.stringify([7, 'middle', 9]) &&
           JSON.stringify(armed.triggerIds) === JSON.stringify([101, 202]),
           `packed C SETON payload was ${JSON.stringify(armed)}`);
});

test('pointer hooks preserve exact cache/authored identity and client event locals', () => {
    const { built, component } = liveHostFixture();
    const seen = [];
    component('root').hooks.onload.args = [
        { type: 'int', value: -2147483645 },
        { type: 'string', value: 'bank' },
    ];
    const host = createHostRuntime(built.ir, {
        viewport: { width: 120, height: 100 },
        invoke(intent, runtime) {
            seen.push(intent);
            if( intent.hook.canonical === 'on_click' )
                runtime.mutate('if_setcolour', intent.component, 99);
        },
    });

    const mounted = host.mount();
    assert(mounted.intents.length === 1 && mounted.intents[0].hook.name === 'onload',
           'onLoad did not preserve its imported binding name');
    assert(JSON.stringify(mounted.intents[0].hook.args) === JSON.stringify([
        { type: 'int', value: -2147483645 },
        { type: 'string', value: 'bank' },
    ]), 'typed imported hook arguments were mistaken for component references');
    host.dispatch({ type: 'pointer_move', x: 15, y: 18 });
    const pressed = host.dispatch({ type: 'pointer_down', x: 15, y: 18, button: 'left' });
    host.dispatch({ type: 'tick', cycle: 1 });
    host.dispatch({ type: 'pointer_up', x: 15, y: 18, button: 0 });

    const over = seen.find((intent) => intent.hook.canonical === 'on_mouse_over');
    const click = seen.find((intent) => intent.hook.canonical === 'on_click');
    assert(over.hook.name === 'onmouseover' && over.hook.authoredEvent === 'onMouseOver',
           `hover identity was ${JSON.stringify(over.hook)}`);
    assert(click.hook.name === 'onclick' && click.hook.authoredEvent === 'onClick',
           `click identity was ${JSON.stringify(click.hook)}`);
    assert(click.component.key === pressed.hit.key && click.component.generation === pressed.hit.generation,
           'hook did not carry the exact hit component incarnation');
    assert(click.locals.mouseX === 5 && click.locals.mouseY === 6 && click.locals.opIndex === 1,
           `click locals were ${JSON.stringify(click.locals)}`);
    assert(seen.some((intent) => intent.hook.canonical === 'on_hold'), 'held press did not run onHold');
    assert(seen.some((intent) => intent.hook.canonical === 'on_click_repeat'),
           'held press did not run onClickRepeat');
    assert(seen.some((intent) => intent.hook.canonical === 'on_release'),
           'release did not return to the press owner');
    assert(host.component('button').props.color === 99, 'synchronous hook mutation was not committed');
});

test('drag, wheel and direct op dispatch stay in the React host transaction', () => {
    const { built } = liveHostFixture();
    const seen = [];
    const host = createHostRuntime(built.ir, {
        viewport: { width: 120, height: 100 },
        invoke: (intent) => seen.push(intent),
    });
    const button = host.ref('button');
    host.setHook(button, 'on_drag', { scriptId: 20, args: [] });
    host.setHook(button, 'on_drag_complete', { scriptId: 21, args: [] });
    host.setHook(button, 'on_op', { scriptId: 22, args: [] });
    host.mutate('if_setdraggable', button, true);
    host.mutate('if_setdragdeadzone', button, 2);

    host.dispatch({ type: 'pointer_down', x: 15, y: 18, button: 0 });
    host.dispatch({ type: 'pointer_move', x: 25, y: 22 });
    host.dispatch({ type: 'pointer_up', x: 25, y: 22, button: 0 });
    const wheel = host.dispatch({ type: 'wheel', x: 15, y: 18, deltaY: 120 });
    host.dispatch({ type: 'op', target: button, opIndex: 3 });

    assert(seen.some((intent) => intent.hook.canonical === 'on_drag'), 'drag hook did not run');
    assert(seen.some((intent) => intent.hook.canonical === 'on_drag_complete'),
           'drag-complete hook did not run');
    assert(!seen.some((intent) => intent.hook.canonical === 'on_click'),
           'drag gesture incorrectly fired click');
    assert(wheel.intents[0].locals.wheel === 1 && wheel.intents[0].locals.eventMouseY === 1,
           `wheel locals were ${JSON.stringify(wheel.intents[0]?.locals)}`);
    const op = seen.findLast((intent) => intent.hook.canonical === 'on_op');
    assert(op.locals.opIndex === 3, `direct op index was ${op.locals.opIndex}`);
});

test('keyboard is a visible-hook broadcast with generation fencing, not a focus route', () => {
    const { built } = liveHostFixture();
    const seen = [];
    let hideSecond = false;
    const host = createHostRuntime(built.ir, {
        viewport: { width: 120, height: 100 },
        invoke(intent, runtime) {
            seen.push(intent);
            if( hideSecond && intent.component.name === 'first_key' )
                runtime.mutate('if_sethide', 'second_key', true);
        },
    });

    host.dispatch({ type: 'key', keyTyped: 65, keyPressed: 97 });
    const firstPass = seen.filter((intent) => intent.hook.canonical === 'on_key');
    assert(firstPass.map((intent) => intent.component.name).join(',') === 'first_key,second_key',
           `keyboard targets were ${firstPass.map((intent) => intent.component.name)}`);
    assert(firstPass.every((intent) => intent.locals.keyTyped === 65 && intent.locals.keyPressed === 97),
           'keyboard event locals were not broadcast intact');
    assert(firstPass[0].hook.name === 'onkey' && firstPass[1].hook.name === 'on_key',
           'imported and canonical key hook names were collapsed');

    seen.length = 0;
    hideSecond = true;
    host.dispatch({ type: 'key', keyTyped: 66, keyPressed: 98 });
    assert(seen.map((intent) => intent.component.name).join(',') === 'first_key',
           'later keyboard target was not revalidated after a synchronous mutation');
});

test('the browser source session executes hooks inside the live React host', () => {
    const { built, component } = liveHostFixture();
    component('root').hooks = {};
    component('first_key').hooks = {
        onload: { script: { id: 70 }, args: [] },
    };
    component('button').hooks = { onclick: {
        script: { id: 71 }, args: [{ type: 'int', value: -2147483645 }],
    } };
    component('button').events = {};
    const scripts = [{
        id: 70, name: 'source_mount',
        source: [
            '[clientscript,source_mount]()',
            'if_settext("mounted", interface_700:2);',
            'if_setonclick("script72(event_com)", interface_700:2);',
        ].join('\n'),
    }, {
        id: 71, name: 'source_click',
        source: [
            '[clientscript,source_click](component $self)',
            'if_setcolour(77, $self);',
            '%varcint5 = 9;',
        ].join('\n'),
    }, {
        id: 72, name: 'source_dynamic',
        source: [
            '[clientscript,source_dynamic](component $self)',
            'if_setcolour(88, $self);',
        ].join('\n'),
    }];
    const warnings = [];
    let source;
    const host = createHostRuntime(built.ir, {
        viewport: { width: 120, height: 100 },
        invoke: (intent) => source.invokeIntent(intent),
    });
    source = createSourceRuntimeSession(built.ir, {
        host, scripts, warnings,
        scriptNames: { 70: 'source_mount', 71: 'source_click', 72: 'source_dynamic' },
    });

    host.mount();
    assert(host.read('if_gettext', 'first_key') === 'mounted',
           'source onLoad did not mutate the React tree');
    host.dispatch({ type: 'pointer_down', x: 5, y: 55, button: 0 });
    host.dispatch({ type: 'pointer_up', x: 5, y: 55, button: 0 });
    assert(host.component('first_key').props.color === 88,
           'numeric deferred source hook did not execute in the React tree');
    host.dispatch({ type: 'pointer_down', x: 15, y: 18, button: 0 });
    assert(host.component('button').props.color === 77,
           'source click did not mutate its live component');
    assert(host.readState('varc', 5) === 9, 'source state write did not reach the host state');
    assert(warnings.length === 0, `source runtime warnings: ${warnings.join('; ')}`);
});

testAsync('the browser WASM adapter loads raw bytecode and synchronously bridges HOST', async () => {
    const fake = fakeWasmModule();
    const requests = [];
    const active = [];
    const host = {
        viewport: { width: 120, height: 100 },
        request(request) {
            requests.push(request);
            return request.kind === 'PUSH_VAR' ? 42 : undefined;
        },
        ref(value) { return value?.ref || value; },
        setActive(value, options = {}) { active.push([value, Boolean(options.dot)]); },
        read() { return 'Choose'; },
    };
    const runtime = await createWasmCS2Runtime({
        host,
        moduleFactory: fake.factory,
        program: {
            available: true, dialect: 'osrs', revision: 'osrs239',
            scripts: [{ id: 70, data: 'AQID' }],
        },
    });
    const component = { key: 'dyn:1', componentId: 700 * 65536 + 2, subId: 7, generation: 1 };
    const result = runtime.invokeIntent({
        component,
        hook: { scriptId: 70, args: [{ type: 'int', value: -2147483645 }, 'hello'] },
        locals: { eventMouseX: 3, eventMouseY: 4, opIndex: 2, keyTyped: 80, keyPressed: 65 },
    });
    assert(fake.loaded.length === 1 && fake.loaded[0].id === 70 &&
           fake.loaded[0].bytes.join(',') === '1,2,3', 'raw .cs2b bytes did not enter C');
    assert(fake.intArgs.join(',') === '-2147483645' && fake.stringArgs.join(',') === 'hello',
           'mixed hook arguments were not staged in their C local banks');
    assert(fake.events.get(0) === 3 && fake.events.get(1) === 4 && fake.events.get(3) === 7,
           `ScriptEvent fields were ${JSON.stringify([...fake.events])}`);
    assert(fake.eventStrings.get(0) === 'Choose', 'event_opbase was not staged');
    assert(active.length === 2 && active[0][1] === false && active[1][1] === true,
           'HostRuntime active and dot refs were not initialized from the hook');
    assert(requests[0].kind === 'PUSH_VAR' && requests[0].id === 5 && requests[0].dot_operand &&
           fake.pushedInts.join(',') === '42', 'HOST getter/result did not cross the WASM seam');
    assert(requests[1].kind === 'POP_VAR' && requests[1].value === 9 &&
           requests[1].transmit === false, 'script POP_VAR incorrectly fanned out transmit hooks');
    assert(result.hostRequests === 2, 'WASM invocation did not report its HOST calls');
    runtime.destroy();
    assert(fake.destroyed, 'WASM session was not destroyed');

    const setOn = {
        kind: 'CC_SETONCLICK', int_args: [12, 0, 34], int_arg_count: 3,
        str_arg_mask: [2, 0], str_args: ['label'], trigger_ids: [5, 6],
    };
    __wasmRuntimeTest.normalizeSetOn(setOn);
    assert(JSON.stringify(setOn.args) === JSON.stringify([12, 'label', 34]) &&
           setOn.triggerIds.join(',') === '5,6', 'reflected set-on arguments lost their stack order');
});

testAsync('browser HOST clock aliases and sound intents let C scripts continue', async () => {
    const { built } = liveHostFixture();
    const fake = fakeWasmModule({
        3300: { name: 'CLIENTCLOCK', fields: [] },
        3200: { name: 'SOUND_SYNTH', fields: [
            ['id', 2277], ['secondary_id', 0], ['loops', 1], ['delay', 4],
            ['fade_out_delay', 0], ['fade_out_speed', 0],
            ['fade_in_delay', 0], ['fade_in_speed', 0],
        ] },
        3201: { name: 'POP_VAR', fields: [['id', 91], ['value', 37]] },
    });
    const host = createHostRuntime(built.ir, {
        viewport: { width: 120, height: 100 }, state: { clock: 2468 },
    });
    const runtime = await createWasmCS2Runtime({
        host,
        moduleFactory: fake.factory,
        program: {
            available: true, dialect: 'osrs', revision: 'osrs239',
            scripts: [{ id: 73, data: 'AQID' }],
        },
    });
    const result = runtime.invokeIntent({
        component: host.ref('root'), hook: { scriptId: 73, args: [] }, locals: {},
    });
    const services = host.snapshot().services;
    assert(fake.pushedInts[0] === 2468 &&
           host.request({ kind: 'CLIENT_CLOCK' }) === 2468,
           'compiled CLIENTCLOCK and the legacy CLIENT_CLOCK alias diverged');
    assert(services.soundSynthCount === 1 && services.lastSoundSynth.id === 2277 &&
           services.lastSoundSynth.loops === 1 && services.lastSoundSynth.delay === 4,
           `sound intent was ${JSON.stringify(services.lastSoundSynth)}`);
    assert(host.readState('varp', 91) === 37,
           'the C script stopped before its request following SOUND_SYNTH');
    assert(result.hostRequests === 3, 'the fake C script did not complete all HOST requests');
    runtime.destroy();
});

test('the browser WASM adapter honors C-owned special HOST result semantics', () => {
    const heap = new Uint8Array(4096);
    let cursor = 256;
    const pushedInts = [];
    const pushedStrings = [];
    const iterators = [];
    const targets = [];
    const read = (pointer) => {
        let end = pointer;
        while( heap[end] ) end++;
        return new TextDecoder().decode(heap.subarray(pointer, end));
    };
    const write = (value) => {
        const bytes = new TextEncoder().encode(value);
        const pointer = cursor;
        heap.set(bytes, pointer);
        heap[pointer + bytes.length] = 0;
        cursor += bytes.length + 1;
        return pointer;
    };
    const api = {
        HEAPU8: heap,
        _malloc(size) { cursor = (cursor + 3) & ~3; const at = cursor; cursor += size; return at; },
        _free() {},
        _cs2w_thread_push_int(thread, value) { pushedInts.push(value); return 1; },
        _cs2w_thread_push_string(thread, pointer) { pushedStrings.push(read(pointer)); return 1; },
        _cs2w_thread_set_target(thread, dot, componentId) {
            targets.push({ dot, componentId }); return 1;
        },
        _cs2w_thread_set_children(thread, parent, pointer, count) {
            const values = count ? [...new Int32Array(heap.buffer, pointer, count)] : [];
            iterators.push({ parent, values }); return 1;
        },
    };
    const parentId = 700 * 65536 + 2;
    const refs = [
        { key: 'dyn:2', componentId: parentId, subId: 2 },
        { key: 'dyn:7', componentId: parentId, subId: 7 },
    ];
    const host = { childIteration: { refs } };

    __wasmRuntimeTest.writeHostResult(api, 33, 211, {
        kind: 'IF_CHILDREN_COLLECT', uid: parentId, dot_operand: true,
    }, refs, host);
    assert(iterators[0].parent === parentId && iterators[0].values.join(',') === '2,7' &&
           targets[0].dot === 1 && targets[0].componentId === parentId && pushedInts.length === 0,
           'IF child collection did not populate C iterator/target without double-pushing count');

    __wasmRuntimeTest.writeHostResult(api, 33, 212, {
        kind: 'CC_CHILDREN_FIND_COUNT', parent_id: parentId, dot_operand: false,
    }, refs.length, host);
    assert(iterators[1].values.join(',') === '2,7' && pushedInts.length === 0,
           'CC child count did not reuse HostRuntime iteration without double-pushing count');

    __wasmRuntimeTest.writeHostResult(api, 33, 3408,
        { kind: 'ENUM', output_type: 's'.charCodeAt(0) }, 'bank', host);
    __wasmRuntimeTest.writeHostResult(api, 33, 3408,
        { kind: 'ENUM', output_type: 'i'.charCodeAt(0) }, 19, host);
    __wasmRuntimeTest.writeHostResult(api, 33, 1703,
        { kind: 'CC_GETCOMPONENTPARAM' }, -1, host);
    __wasmRuntimeTest.writeHostResult(api, 33, 1613,
        { kind: 'CC_GETPARAM' }, 'tag', host);
    __wasmRuntimeTest.writeHostResult(api, 33, 6516,
        { kind: 'STRUCT_PARAM' }, 72, host);
    assert(pushedStrings.join(',') === 'bank,tag' && pushedInts.join(',') === '19,-1,72',
           'polymorphic enum/component-param HOST results used command-table defaults');

    const fieldNames = ['component_id', 'param_id', 'value', 'str_value', 'kind'];
    const namePointers = fieldNames.map(write);
    const requestName = write('CC_SETCOMPONENTPARAM');
    const reflectParam = ({ componentId, paramId, value, stringValue, valueKind }) => {
        const values = [componentId, paramId, value, 0, valueKind];
        const stringPointer = stringValue === null ? 0 : write(stringValue);
        return __wasmRuntimeTest.reflectRequest({
            ...api,
            _cs2w_request_kind_name() { return requestName; },
            _cs2w_request_field_count() { return fieldNames.length; },
            _cs2w_request_field_name(kind, index) { return namePointers[index]; },
            _cs2w_request_field_kind(kind, index) { return index === 3 ? 4 : 1; },
            _cs2w_request_field_length() { return 1; },
            _cs2w_request_field_i32(pointer, index) { return values[index]; },
            _cs2w_request_field_string() { return stringPointer; },
            _cs2w_thread_current_operand() { return 0; },
        }, 44, 1704, 33);
    };
    const { built } = liveHostFixture();
    const live = createHostRuntime(built.ir);
    const button = live.ref('button');
    live.request(reflectParam({ componentId: button.componentId, paramId: 40,
        value: 345, stringValue: null, valueKind: 1 }));
    live.request(reflectParam({ componentId: button.componentId, paramId: 41,
        value: 0, stringValue: 'label', valueKind: 2 }));
    const params = live.resolve(button).runtime.params;
    assert(params[40].value === 345 && params[41].string === 'label',
           `nullable reflected component params were ${JSON.stringify(params)}`);
});

test('state writes dispatch only matching visible transmit hooks', () => {
    const { built, component } = liveHostFixture();
    component('first_key').hooks.onvarptransmit = { script: { id: 30 }, args: [] };
    component('first_key').triggers.varptriggers = [5];
    component('second_key').hooks.on_var_transmit = { script: { id: 31 }, args: [] };
    component('second_key').triggers.varptriggers = [6];
    component('hidden_key').hooks.onvarptransmit = { script: { id: 32 }, args: [] };
    component('hidden_key').triggers.varptriggers = [5];
    const seen = [];
    const host = createHostRuntime(built.ir, { invoke: (intent) => seen.push(intent) });

    const result = host.writeState('varp', 5, 42);
    assert(host.readState('varp', 5) === 42, 'state write was not retained');
    assert(result.intents.length === 1 && result.intents[0].component.name === 'first_key',
           `transmit targets were ${result.intents.map((intent) => intent.component.name)}`);
    assert(result.intents[0].hook.name === 'onvarptransmit' &&
           result.intents[0].hook.canonical === 'on_var_transmit',
           'transmit hook identity was not preserved');
    host.request({ kind: 'POP_VAR', varp_id: 7, value: 81 });
    assert(host.request({ kind: 'PUSH_VAR', varpId: 7 }) === 81,
           'named PUSH/POP variable requests did not use host state');
    host.request({ kind: 'POP_VARC_STRING', varcId: 9, text: 'query' });
    assert(host.request({ kind: 'PUSH_VARC_STRING', varcId: 9 }) === 'query',
           'string state was not preserved as an ordinary JS value');
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

test('sprite metadata restores nominal margins and native tile phase', () => {
    const rgba = Buffer.from([
        255, 0, 0, 255, 0, 255, 0, 255,
        0, 0, 255, 255, 255, 255, 0, 255,
    ]);
    const crop = { width: 2, height: 2, rgba };
    const meta = { canvasWidth: 5, canvasHeight: 4, width: 2, height: 2, x: 1, y: 1 };
    const canvas = spriteCanvas(crop, meta);
    assert(canvas.width === 5 && canvas.height === 4, 'nominal canvas dimensions were lost');
    assert(canvas.rgba.subarray(0, 4).equals(Buffer.alloc(4)),
           'nominal transparent margin was not restored');
    const redAtOffset = (1 * canvas.width + 1) * 4;
    const yellowAtOffset = (2 * canvas.width + 2) * 4;
    assert(canvas.rgba.subarray(redAtOffset, redAtOffset + 4).equals(rgba.subarray(0, 4)),
           'sprite crop was not placed at its pack.meta x/y offset');
    assert(canvas.rgba.subarray(yellowAtOffset, yellowAtOffset + 4).equals(rgba.subarray(12, 16)),
           'sprite crop bottom-right pixel was misplaced');

    const tile = spriteTile(crop, meta);
    assert(tile.width === 2 && tile.height === 2, 'tiled sprite used the nominal canvas as its cell');
    assert(tile.rgba.subarray(0, 4).equals(rgba.subarray(12, 16)),
           'tiled sprite did not phase its first pixel by the crop offset');
});

test('the sprite server applies pack.meta differently for scaled and tiled graphics', () => {
    const content = makeContent('sprite-metadata');
    const spriteDir = join(content, 'sprites', 'offset_icon');
    mkdirSync(spriteDir, { recursive: true });
    writeFileSync(join(spriteDir, 'pack.meta'), 'sprite0=5,3,2,1,2,1\n');

    const header = Buffer.alloc(54);
    header.write('BM', 0, 'ascii');
    header.writeUInt32LE(54 + 8, 2);
    header.writeUInt32LE(54, 10);
    header.writeUInt32LE(40, 14);
    header.writeInt32LE(2, 18);
    header.writeInt32LE(1, 22);
    header.writeUInt16LE(1, 26);
    header.writeUInt16LE(32, 28);
    writeFileSync(join(spriteDir, '0.bmp'), Buffer.concat([
        header,
        Buffer.from([0x10, 0x20, 0x30, 0xff, 0x40, 0x50, 0x60, 0xff]),
    ]));

    const index = new Map([[77, 'offset_icon']]);
    const scaled = spritePng(content, index, 77);
    const tiled = spritePng(content, index, 77, { tiled: true });
    assert(scaled.readUInt32BE(16) === 5 && scaled.readUInt32BE(20) === 3,
           'ordinary sprite response did not restore the nominal canvas');
    assert(tiled.readUInt32BE(16) === 2 && tiled.readUInt32BE(20) === 1,
           'tiled sprite response did not retain the cropped repeat-cell size');
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
    assert(readFileSync(join(cache, 'invocations'), 'utf8').trim().split('\n').length === 2,
           'the interface/source and raw-byte extraction passes were not reused');

    writeFileSync(join(cache, 'main_file_cache.dat2'), 'changed cache bytes');
    const changed = prepareDat2Project(project, { tool, cacheRoot: derived });
    assert(changed.content !== first.content, 'a changed Dat2 cache reused stale output');
    assert(readFileSync(join(cache, 'invocations'), 'utf8').trim().split('\n').length === 4,
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

function fakeWasmModule(customRequests = null) {
    const heap = new Uint8Array(128 * 1024);
    let cursor = 256;
    let options;
    const loaded = [];
    const intArgs = [];
    const stringArgs = [];
    const events = new Map();
    const eventStrings = new Map();
    const pushedInts = [];
    const requests = customRequests || {
        1: { name: 'PUSH_VAR', fields: [['id', 5]] },
        2: { name: 'POP_VAR', fields: [['id', 5], ['value', 9]] },
    };
    const requestKinds = Object.keys(requests).map(Number);
    const persistent = new Map();
    const alloc = (size) => { const at = cursor; cursor += Math.max(1, size); return at; };
    const read = (pointer) => {
        let end = pointer;
        while( heap[end] ) end++;
        return new TextDecoder().decode(heap.subarray(pointer, end));
    };
    const write = (text) => {
        if( persistent.has(text) ) return persistent.get(text);
        const bytes = new TextEncoder().encode(text);
        const at = alloc(bytes.length + 1);
        heap.set(bytes, at); heap[at + bytes.length] = 0;
        persistent.set(text, at);
        return at;
    };
    const api = {
        HEAPU8: heap,
        _malloc: alloc,
        _free() {},
        _cs2w_session_create(dialect, revision) {
            assert(dialect === 0 && revision === 239, 'dialect/revision were not normalized');
            return 11;
        },
        _cs2w_session_destroy() { fixture.destroyed = true; return 1; },
        _cs2w_session_load_script(session, id, pointer, length) {
            loaded.push({ id, bytes: [...heap.slice(pointer, pointer + length)] }); return 1;
        },
        _cs2w_session_seal() { return 1; },
        _cs2w_session_last_error() { return 0; },
        _cs2w_session_last_error_message() { return 0; },
        _cs2w_invocation_create() { return 22; },
        _cs2w_invocation_destroy() { return 1; },
        _cs2w_invocation_add_int_arg(invocation, value) { intArgs.push(value); return 1; },
        _cs2w_invocation_add_string_arg(invocation, pointer) { stringArgs.push(read(pointer)); return 1; },
        _cs2w_invocation_set_event_i32(invocation, field, value) { events.set(field, value); return 1; },
        _cs2w_invocation_set_event_string(invocation, field, pointer) {
            eventStrings.set(field, read(pointer)); return 1;
        },
        _cs2w_invocation_run() {
            for( const kind of requestKinds )
                if( options.cs2HostExec(11, 22, 33, 44, kind) !== 0 ) return 2;
            return 0;
        },
        _cs2w_invocation_error_opcode() { return 0; },
        _cs2w_invocation_error_pc() { return 0; },
        _cs2w_invocation_error_script_id() { return 70; },
        _cs2w_invocation_host_call_count() { return requestKinds.length; },
        _cs2w_request_kind_name(kind) { return write(requests[kind].name); },
        _cs2w_request_field_count(kind) { return requests[kind].fields.length; },
        _cs2w_request_field_name(kind, index) { return write(requests[kind].fields[index][0]); },
        _cs2w_request_field_kind() { return 1; },
        _cs2w_request_field_length() { return 1; },
        _cs2w_request_field_i32(pointer, index) {
            /* The callback's current kind is recoverable from which reflection
             * table last supplied this index in this deterministic fake. */
            const table = fixture.reflectKind;
            return requests[table].fields[index][1];
        },
        _cs2w_request_field_string() { return 0; },
        _cs2w_thread_push_int(thread, value) { pushedInts.push(value); return 1; },
        _cs2w_thread_push_string() { return 1; },
        _cs2w_thread_set_target() { return 1; },
        _cs2w_thread_set_children() { return 1; },
        _cs2w_thread_current_operand() { return 1; },
    };
    /* Reflection does not carry kind into field_i32, so remember it when the
     * field-count walk starts (the real C accessor reads it from the request). */
    const originalFieldCount = api._cs2w_request_field_count;
    api._cs2w_request_field_count = (kind) => {
        fixture.reflectKind = kind;
        return originalFieldCount(kind);
    };
    const fixture = {
        loaded, intArgs, stringArgs, events, eventStrings, pushedInts,
        destroyed: false, reflectKind: 0,
        async factory(value) { options = value; return api; },
    };
    return fixture;
}

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

for( const pending of pendingTests ) {
    try {
        await pending.fn();
        passed++;
    } catch( error ) {
        failures.push({ name: pending.name, error });
    }
}

rmSync(scratch, { recursive: true, force: true });

if( failures.length ) {
    process.stderr.write(`\n${failures.length} failed, ${passed} passed\n\n`);
    for( const failure of failures )
        process.stderr.write(`  ${failure.name}\n    ${failure.error.message.replace(/\n/g, '\n    ')}\n\n`);
    process.exit(1);
}
process.stdout.write(`${passed} passed\n`);
