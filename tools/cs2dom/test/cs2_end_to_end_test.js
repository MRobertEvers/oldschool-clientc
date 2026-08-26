/*
 * The whole Phase-1 chain, on real cache scripts.
 *
 *   cache bytecode -> `cs2 --emit ast-json` -> generated JavaScript
 *                  -> HostKernel -> UITreeJS -> a settled tree
 *
 * Every other test in this directory checks one link. This one checks that the
 * links join: a script the cache actually ships, lowered by the real emitter,
 * executed by the real driver against the real tree, producing components with
 * the fields the script set.
 *
 * The trees come from a fixture directory so the suite does not need a cache
 * checkout. Regenerate with:
 *
 *   ./3rd/rscache/tools/cs2/cs2 decompile --cache cache.osrs239 --rev osrs239 \
 *       --names ../cs2/src/main/resources/org/runestar/cs2 --emit ast-json \
 *       --out tools/cs2dom/test/fixtures/ast <id>...
 */

import assert from 'node:assert/strict';
import { readFileSync, existsSync, readdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

import { emitScript } from '../src/cs2_js_emit.js';
import { createUITree, WIDGET_TYPE, DYNAMIC_SUB_ID_BASE } from '../src/uitree.js';
import {
    createHostKernel, HostState, ReadyAssetSource, NullAssetSource, hostCoverage,
    UnimplementedHostOp, HOST_PARK,
} from '../src/host_kernel.js';
import { createDriver, ScriptRegistry } from '../src/cs2_driver.js';
import * as K from '../src/cs2_intrinsics.js';

const HERE = dirname(fileURLToPath(import.meta.url));
const FIXTURES = join(HERE, 'fixtures', 'ast');

const tests = [];
function test(name, fn) { tests.push([name, fn]); }

/* -------------------------------------------------------------------------
 * Loading generated code
 * ---------------------------------------------------------------------- */

/**
 * Compile emitted source into a callable generator.
 *
 * `new Function` rather than a data: URL import, so the test stays synchronous
 * and the closure over K/PARK is explicit — the generated module references
 * both by bare name, exactly as the runtime bundle will supply them.
 */
function compile(sources) {
    const body = sources
        .map((source) => source.replace(/^export function\*/m, 'function*'))
        .join('\n');
    const names = sources.map((source) => /function\* (\w+)/.exec(source)[1]);
    // eslint-disable-next-line no-new-func
    const factory = new Function('K', 'PARK', `${body}\nreturn { ${names.join(', ')} };`);
    return factory(K, HOST_PARK);
}

function fixture(name) {
    const path = join(FIXTURES, `${name}.json`);
    if( !existsSync(path) ) return null;
    return JSON.parse(readFileSync(path, 'utf8'));
}

function fixtureNames() {
    if( !existsSync(FIXTURES) ) return [];
    return readdirSync(FIXTURES).filter((f) => f.endsWith('.json')).map((f) => f.slice(0, -5));
}

/** A kernel with a tree, a driver, and everything already loaded. */
function harness({ assets = new ReadyAssetSource(), state = new HostState() } = {}) {
    const tree = createUITree();
    const host = createHostKernel({ tree, state, assets });
    const scripts = new ScriptRegistry();
    const driver = createDriver({ host, scripts, onWarning: () => {} });
    return { tree, host, scripts, driver };
}

/** Bake a minimal interface group so component-parking calls can resolve. */
function bakeGroup(tree, groupId, subCount = 4) {
    const root = tree.push({ componentId: (groupId << 16) | 0, type: WIDGET_TYPE.LAYER });
    for( let i = 1; i <= subCount; i++ )
        tree.push({
            parentIndex: root, componentId: (groupId << 16) | i,
            subId: i, type: WIDGET_TYPE.LAYER,
        });
    return root;
}

/* -------------------------------------------------------------------------
 * The chain, on synthesised scripts
 * ---------------------------------------------------------------------- */

test('a generated script mutates the tree through the kernel', async () => {
    const { tree, host, scripts, driver } = harness();
    const group = bakeGroup(tree, 0x0100);

    const module = compile([emitScript({
        schema: 'rscache-cs2-ast/1', id: 1, name: '[clientscript,paint]',
        arguments: [], returns: [],
        frame: { localInts: 0, localStrings: 0, intArguments: 0, stringArguments: 0 },
        body: {
            kind: 'seq', next: null,
            instructions: [
                { kind: 'assignment', definitions: [], expression: {
                    kind: 'operation', opcode: 1101, name: 'if_setcolour', dot: false,
                    calcInfix: null, branchInfix: null, stackTypes: [],
                    arguments: [
                        { kind: 'constant', stackType: 'int', value: 0xff0000,
                          type: 'int', identifier: 'colour', literal: '^red' },
                        { kind: 'constant', stackType: 'int', value: (0x0100 << 16) | 1,
                          type: 'component', identifier: 'component', literal: null },
                    ],
                } },
            ],
        },
    }).code]);

    scripts.add(1, module.cs2_1);
    driver.dispatch(1);
    assert.equal(await driver.settle(), true);

    const painted = tree.findByComponentId((0x0100 << 16) | 1);
    assert.equal(painted.props.colour, 0xff0000);
    assert.equal(driver.stats.invocations, 1);
});

test('a parked asset suspends the script and the retry completes it', async () => {
    /*
     * The whole park contract in one case: the sprite is absent, so the host
     * answers PARK without touching the node; the generator suspends; the
     * loader supplies it; the retry loop re-runs the same call and it lands.
     */
    const decoded = new Set();
    const assets = { has: (kind, id) => decoded.has(`${kind}:${id}`) };
    const { tree, host, scripts, driver } = harness({ assets });
    bakeGroup(tree, 0x0200);
    driver.loader = {
        loadSync: () => false,
        load: async (kind, id) => { decoded.add(`${kind}:${id}`); },
    };

    const module = compile([emitScript({
        schema: 'rscache-cs2-ast/1', id: 2, name: '[clientscript,sprite]',
        arguments: [], returns: [],
        frame: { localInts: 0, localStrings: 0, intArguments: 0, stringArguments: 0 },
        body: { kind: 'seq', next: null, instructions: [
            { kind: 'assignment', definitions: [], expression: {
                kind: 'operation', opcode: 2105, name: 'if_setgraphic', dot: false,
                calcInfix: null, branchInfix: null, stackTypes: [],
                arguments: [
                    { kind: 'constant', stackType: 'int', value: 77,
                      type: 'graphic', identifier: 'graphic', literal: null },
                    { kind: 'constant', stackType: 'int', value: (0x0200 << 16) | 2,
                      type: 'component', identifier: 'component', literal: null },
                ],
            } },
        ] },
    }).code]);

    scripts.add(2, module.cs2_2);
    driver.dispatch(2);

    /* The first settle stops on the load and must NOT report settled. */
    assert.equal(await driver.settle(), false);
    assert.equal(driver.settled, false);
    assert.equal(tree.findByComponentId((0x0200 << 16) | 2).props.sprite, undefined,
        'a parked call must not have applied anything');

    assert.equal(await driver.settle(), true);
    assert.equal(tree.findByComponentId((0x0200 << 16) | 2).props.sprite, 77);
    assert.equal(driver.stats.parks, 1);
});

test('a proc parking suspends its caller too', async () => {
    const decoded = new Set();
    const { tree, host, scripts, driver } = harness({
        assets: { has: (kind, id) => decoded.has(`${kind}:${id}`) },
    });
    bakeGroup(tree, 0x0300);
    driver.loader = {
        loadSync: () => false,
        load: async (kind, id) => { decoded.add(`${kind}:${id}`); },
    };

    /* The callee parks; `yield*` must carry that all the way out to the
     * driver, or the caller resumes with a PARK sentinel as a value. */
    const callee = emitScript({
        schema: 'rscache-cs2-ast/1', id: 20, name: '[proc,inner]',
        arguments: [], returns: [],
        frame: { localInts: 0, localStrings: 0, intArguments: 0, stringArguments: 0 },
        body: { kind: 'seq', next: null, instructions: [
            { kind: 'assignment', definitions: [], expression: {
                kind: 'operation', opcode: 2105, name: 'if_setgraphic', dot: false,
                calcInfix: null, branchInfix: null, stackTypes: [],
                arguments: [
                    { kind: 'constant', stackType: 'int', value: 5, type: 'graphic',
                      identifier: 'graphic', literal: null },
                    { kind: 'constant', stackType: 'int', value: (0x0300 << 16) | 1,
                      type: 'component', identifier: 'component', literal: null },
                ],
            } },
        ] },
    }).code;

    const caller = emitScript({
        schema: 'rscache-cs2-ast/1', id: 21, name: '[clientscript,outer]',
        arguments: [], returns: [],
        frame: { localInts: 0, localStrings: 0, intArguments: 0, stringArguments: 0 },
        body: { kind: 'seq', next: null, instructions: [
            { kind: 'assignment', definitions: [], expression: {
                kind: 'proc', scriptId: 20, name: 'inner', arguments: [], stackTypes: [],
            } },
        ] },
    }).code;

    const module = compile([callee, caller]);
    scripts.add(20, module.cs2_20).add(21, module.cs2_21);
    driver.dispatch(21);

    assert.equal(await driver.settle(), false);
    assert.equal(await driver.settle(), true);
    assert.equal(tree.findByComponentId((0x0300 << 16) | 1).props.sprite, 5);
});

test('a script writing a var does not re-trigger the transmit pump', () => {
    /*
     * The reference applies a script's own var write optimistically and never
     * puts the id in the changed set — otherwise a hook that writes a var
     * re-runs itself forever.
     */
    const state = new HostState();
    const { host } = harness({ state });
    host.setVarp(300, 5);
    assert.equal(state.changedAt(300), 0, 'a script write must not arm the pump');
    state.setVarp(300, 6);
    assert.notEqual(state.changedAt(300), 0, 'a server write must');
});

test('a stale cursor writes nowhere', async () => {
    /*
     * The failure this prevents: `cc_find` sets the cursor, the container is
     * rebuilt, the slot is recycled, and a later `cc_settext` writes into an
     * unrelated component that now occupies it.
     */
    const { tree, host } = harness();
    const parent = tree.push({ componentId: 0x00400000 });
    const row = tree.push({ parentIndex: parent.index ?? parent, subId: 1, dynamic: true });

    host.setActive(row);
    tree.remove(row);
    const replacement = tree.push({ parentIndex: parent, subId: 2, dynamic: true });
    assert.equal(replacement, row, 'the slot should have been recycled');

    host.cc_settext('should go nowhere');
    assert.equal(tree.at(replacement).props.text, undefined);
});

test('an unimplemented operation throws by name rather than returning zero', () => {
    const { host } = harness();
    assert.throws(() => host.stockmarket_value(1),
        (error) => error instanceof UnimplementedHostOp && /stockmarket_value/.test(error.message));
});

test('settlement refuses to spin forever', async () => {
    const { host, scripts, driver } = harness();
    scripts.add(1, function* () { /* nothing */ });
    let passes = 0;
    driver.collectFollowUps = () => { passes++; driver.dispatch(1); return 1; };
    await assert.rejects(() => driver.settle(), /did not converge/);
    assert.ok(passes > 1);
});

/* -------------------------------------------------------------------------
 * Real cache scripts
 * ---------------------------------------------------------------------- */

test('every fixture tree lowers and compiles', () => {
    const names = fixtureNames();
    if( names.length === 0 )
    {
        console.log('     (no fixtures; see the header for how to regenerate)');
        return;
    }
    for( const name of names )
    {
        const result = emitScript(fixture(name));
        const module = compile([result.code]);
        assert.equal(typeof module[result.functionName], 'function', name);
    }
});

test('a real cache script runs against the real tree', async () => {
    const ast = fixture('[proc,gnome_cuisine_title]');
    if( !ast )
    {
        console.log('     (fixture missing; skipped)');
        return;
    }
    const state = new HostState();
    state.setVarbit(698, 2);
    const { scripts, driver } = harness({ state });

    const result = emitScript(ast);
    const module = compile([result.code]);
    scripts.add(ast.id, module[result.functionName]);

    /* A proc returns its value; the driver runs it as a task, so read it by
     * driving the generator directly — this is the shape a `yield*` caller
     * sees. */
    const generator = module[result.functionName](driver.host);
    const step = generator.next();
    assert.equal(step.done, true);
    assert.equal(step.value, 'Gnome Crunchies',
        'varbit 698 = 2 selects the third title');
});

/* -------------------------------------------------------------------------
 * Coverage
 * ---------------------------------------------------------------------- */

test('host coverage is reported honestly', () => {
    const { implemented, total, missing } = hostCoverage();
    assert.ok(implemented > 0);
    assert.equal(implemented + missing.length, total);
    console.log(`     ${implemented}/${total} host methods implemented`);
});

/* -------------------------------------------------------------------------
 * Runner
 * ---------------------------------------------------------------------- */

let failed = 0;
for( const [name, fn] of tests )
{
    try { await fn(); console.log(`ok   ${name}`); }
    catch( error ) { failed++; console.error(`FAIL ${name}\n     ${error.stack}`); }
}
console.log(`\n${tests.length - failed}/${tests.length} passed`);
process.exit(failed ? 1 : 0);
