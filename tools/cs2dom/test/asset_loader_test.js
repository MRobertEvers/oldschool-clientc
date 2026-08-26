/*
 * Servicing parks, end to end through a session.
 *
 * The two properties that matter are not about loading at all:
 *
 *   a park that CAN be answered synchronously must not suspend, because a
 *   3,000-row rebuild would otherwise become 3,000 microtask hops spread over
 *   many frames;
 *
 *   a park that can NEVER be answered must still complete, because an id this
 *   cache does not have will never arrive and a script waiting forever is a
 *   frozen interface rather than a visible gap.
 */

import assert from 'node:assert/strict';

import { AssetLoader, READY_LOADER, createLoader } from '../src/asset_loader.js';
import { SpriteStore, FontStore, BitmapFont } from '../src/assets.js';
import { createSession } from '../src/session.js';
import { createRecordingSurface } from '../src/painter.js';
import { HOST_PARK } from '../src/host_kernel.js';
import { WIDGET_TYPE } from '../src/uitree.js';

const tests = [];
function test(name, fn) { tests.push([name, fn]); }

function fontWith(id) {
    return new BitmapFont({
        id, ascent: 9, advances: new Array(256).fill(4),
        glyphs: new Map([[65, { id: 'A', width: 4, height: 9 }]]), lineHeight: 12,
    });
}

/* -------------------------------------------------------------------------
 * Routing
 * ---------------------------------------------------------------------- */

test('an already-decoded asset answers synchronously', () => {
    const sprites = new SpriteStore();
    sprites.put(7, { width: 4, height: 4 });
    const loader = createLoader({ sprites });

    assert.equal(loader.loadSync('sprite', 7), true);
    assert.equal(loader.stats.sync, 1);
    assert.equal(loader.loadSync('sprite', 8), false, 'and an absent one does not');
});

test('an unknown class is refused, not silently answered', () => {
    const warnings = [];
    const loader = createLoader({ onWarning: (message) => warnings.push(message) });
    assert.equal(loader.loadSync('whatever', 1), false);
    return loader.load('whatever', 1).then((ok) => {
        assert.equal(ok, false);
        assert.match(warnings[0], /no loader for 'whatever'/);
    });
});

test('a decode that throws settles rather than rejecting', async () => {
    /*
     * A rejection here abandons a parked script with no way to finish it. The
     * loader reports and resolves false; the host then completes with its miss
     * answer, which is a visible gap rather than a hang.
     */
    const warnings = [];
    const loader = createLoader({
        sprites: { has: () => false, load: () => { throw new Error('bad archive'); } },
        onWarning: (message) => warnings.push(message),
    });
    assert.equal(await loader.load('sprite', 3), false);
    assert.match(warnings[0], /bad archive/);
    assert.deepEqual(loader.report().missing, ['sprite:3']);
});

/* -------------------------------------------------------------------------
 * Through a running session
 * ---------------------------------------------------------------------- */

/*
 * `sprites`/`fonts` are parameters because the PAINTER reads them.
 *
 * A caller that builds a store, hands it to the loader and lets the harness
 * make a second empty one for the session has a painter that can draw
 * nothing — it then asks the loader for all of it, which is the opposite of
 * what a test about "no awaited loads" is measuring.
 */
function harness({ loader, sprites = new SpriteStore(), fonts = new FontStore() }) {
    const surface = createRecordingSurface();
    const session = createSession({
        surface, loader, sprites, fonts, onWarning: () => {},
        root: { x: 0, y: 0, width: 400, height: 300 },
    });
    return { session, surface, sprites, fonts };
}

test('a sprite park resolves and the next frame draws it', async () => {
    const sprites = new SpriteStore({
        decode: async (id) => ({ width: 8, height: 8, bitmap: `s${id}` }),
    });
    const loader = createLoader({ sprites });
    const surface = createRecordingSurface();
    const session = createSession({
        surface, loader, sprites, onWarning: () => {},
        root: { x: 0, y: 0, width: 400, height: 300 },
    });

    const node = session.tree.push({
        componentId: 0x10001, type: WIDGET_TYPE.GRAPHIC,
        props: { x: 0, y: 0, width: 8, height: 8 },
    });
    session.scripts.add(1, function* (H) {
        while( H.if_setgraphic(42, 0x10001) === HOST_PARK ) yield;
    });
    session.driver.dispatch(1);

    /* Frame one reaches the park and paints nothing. */
    assert.equal(await session.frame(0), false);
    assert.equal(session.tree.at(node).props.sprite, undefined);

    /* The load lands; frame two completes the script and draws. */
    await session.driver.parkedOn;
    assert.equal(await session.frame(16), true);
    assert.equal(session.tree.at(node).props.sprite, 42);
    assert.equal(surface.calls.filter((c) => c[0] === 'drawImage').length, 1);
});

test('an id the cache does not have completes with the miss answer', async () => {
    /* The whole point: a load that resolves to nothing is a correct outcome.
     * The host recorded that it already waited, so the retry completes. */
    const sprites = new SpriteStore({ decode: async () => null });
    const loader = createLoader({ sprites });
    const surface = createRecordingSurface();
    const session = createSession({
        surface, loader, sprites, onWarning: () => {},
        root: { x: 0, y: 0, width: 400, height: 300 },
    });
    session.tree.push({
        componentId: 0x20001, type: WIDGET_TYPE.GRAPHIC,
        props: { x: 0, y: 0, width: 8, height: 8 },
    });
    session.scripts.add(1, function* (H) {
        while( H.if_setgraphic(999, 0x20001) === HOST_PARK ) yield;
    });
    session.driver.dispatch(1);

    assert.equal(await session.frame(0), false);
    await session.driver.parkedOn;
    await session.frame(16);
    assert.equal(session.driver.settled, true, 'the script finished rather than hanging');
    assert.deepEqual(loader.report().missing, ['sprite:999']);
});

test('a mass rebuild whose assets are present never suspends', async () => {
    /*
     * The property that keeps a rebuild inside one frame. Every one of these
     * calls is park-capable; every one is answerable synchronously; so the
     * whole rebuild must settle in a single frame with zero awaited loads.
     */
    const sprites = new SpriteStore();
    for( let id = 0; id < 64; id++ ) sprites.put(id, { width: 4, height: 4 });
    const loader = createLoader({ sprites });
    const { session } = harness({ loader, sprites });

    const container = session.tree.push({
        componentId: 0x30001, type: WIDGET_TYPE.LAYER,
        props: { x: 0, y: 0, width: 400, height: 300 },
    });
    session.scripts.add(1, function* (H) {
        for( let i = 0; i < 500; i++ )
        {
            while( H.cc_create(0x30001, WIDGET_TYPE.GRAPHIC, 0x8000 + i, 0) === HOST_PARK ) yield;
            while( H.cc_setgraphic(i % 64) === HOST_PARK ) yield;
        }
    });
    session.driver.dispatch(1);

    assert.equal(await session.frame(0), true, 'settled and painted in one frame');
    assert.equal(loader.stats.async, 0, 'nothing was awaited');
    /*
     * And every one of the 500 graphics DREW. A store the painter cannot see
     * makes the frame settle just as fast while painting nothing, so the
     * timing assertion above passes for a session that shows an empty panel;
     * this is the half that says the rebuild produced pixels.
     */
    assert.equal(session.painter.stats.missingAssets, 0, 'every graphic had its sprite');
    assert.equal(session.tree.children(container).length, 500);
});

test('the ready loader answers everything', async () => {
    assert.equal(READY_LOADER.loadSync('anything', 1), true);
    assert.equal(await READY_LOADER.load('anything', 1), true);
});

/* -------------------------------------------------------------------------
 * Text through a real font
 * ---------------------------------------------------------------------- */

test('a font park resolves and text then draws its glyphs', async () => {
    const fonts = new FontStore({ decode: async (id) => fontWith(id) });
    const loader = createLoader({ fonts });
    const surface = createRecordingSurface();
    const session = createSession({
        surface, loader, fonts, onWarning: () => {},
        root: { x: 0, y: 0, width: 400, height: 300 },
    });
    session.tree.push({
        componentId: 0x40001, type: WIDGET_TYPE.TEXT,
        props: { x: 0, y: 0, width: 100, height: 12, text: 'AAA', colour: 0xffffff },
    });
    session.scripts.add(1, function* (H) {
        while( H.if_settextfont(495, 0x40001) === HOST_PARK ) yield;
    });
    session.driver.dispatch(1);

    assert.equal(await session.frame(0), false);
    await session.driver.parkedOn;
    assert.equal(await session.frame(16), true);

    const glyphs = surface.calls.filter((call) => call[0] === 'drawImage');
    assert.equal(glyphs.length, 3, 'three glyphs, blitted from the atlas');
    assert.deepEqual(glyphs.map((g) => g[2]), [0, 4, 8], 'advanced by 4, not by the glyph width');
});

let failed = 0;
for( const [name, fn] of tests )
{
    try { await fn(); console.log(`ok   ${name}`); }
    catch( error ) { failed++; console.error(`FAIL ${name}\n     ${error.message}`); }
}
console.log(`\n${tests.length - failed}/${tests.length} passed`);
process.exit(failed ? 1 : 0);
