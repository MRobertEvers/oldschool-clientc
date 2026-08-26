/*
 * The painter, through a recording surface.
 *
 * Recording rather than rasterising, because the thing worth pinning is the
 * DECISION — which sprite a hovered graphic shows, that an unfilled rectangle
 * is four fills and not a stroke, that a missing asset draws nothing rather
 * than a placeholder. Pixels can be compared against the C client later; these
 * are the choices that would make that comparison fail for reasons no image
 * diff could explain.
 */

import assert from 'node:assert/strict';

import { createUITree, WIDGET_TYPE } from '../src/uitree.js';
import { createLayout } from '../src/layout.js';
import { createEmitter, EMIT_KIND } from '../src/emit.js';
import { colourToCss, createPainter, createRecordingSurface } from '../src/painter.js';
import { BitmapFont } from '../src/assets.js';

const tests = [];
function test(name, fn) { tests.push([name, fn]); }

function paint(commands, options = {}) {
    const surface = createRecordingSurface();
    const painter = createPainter({ surface, ...options });
    painter.paint(commands, { width: 800, height: 600, ...options.frame });
    return { surface, painter, calls: surface.calls };
}

function command(kind, props = {}, extra = {}) {
    return {
        kind, node: 0, componentId: -1,
        x: 10, y: 20, width: 100, height: 50,
        clip: { x: 0, y: 0, width: 800, height: 600 },
        props, hovered: false, trans: 0, ...extra,
    };
}

const drawn = (calls, name) => calls.filter((call) => call[0] === name);

/** A font that blits one glyph per character, so draw order is observable. */
function testFont() {
    const glyphs = new Map();
    for( let code = 32; code < 127; code++ )
        glyphs.set(code, { id: `g${code}`, width: 6, height: 9 });
    return new BitmapFont({
        id: 495, ascent: 9, advances: new Array(256).fill(4), glyphs, lineHeight: 12,
    });
}
/* `begin`/`end` bracket every frame; a test about draw ORDER wants neither. */
const drawOrder = (calls, skip = ['begin', 'end']) =>
    calls.map((call) => call[0]).filter((name) => !skip.includes(name));

/* -------------------------------------------------------------------------
 * Colour and transparency
 * ---------------------------------------------------------------------- */

test('a cache colour becomes six hex digits, and -1 draws nothing', () => {
    assert.equal(colourToCss(0xff981f), '#ff981f');
    assert.equal(colourToCss(0x000000), '#000000');
    assert.equal(colourToCss(-1), null);
});

test('transparency runs 0 opaque to 255 invisible', () => {
    const { calls } = paint([command(EMIT_KIND.RECT, { colour: 0xffffff, filled: 1 },
        { trans: 128 })]);
    const [fill] = drawn(calls, 'fillRect');
    assert.ok(Math.abs(fill[6] - (127 / 255)) < 1e-9,
        'trans 128 is roughly half visible, not half transparent the other way');

    const opaque = paint([command(EMIT_KIND.RECT, { colour: 0xffffff, filled: 1 })]);
    assert.equal(drawn(opaque.calls, 'fillRect')[0][6], 1);
});

/* -------------------------------------------------------------------------
 * Shapes
 * ---------------------------------------------------------------------- */

test('an unfilled rectangle is four fills, not a stroke', () => {
    /*
     * A stroke straddles the path and lands on half-pixels, which is a
     * different image from the client's — its outline is four one-pixel spans.
     */
    const { calls } = paint([command(EMIT_KIND.RECT, { colour: 0x00ff00, filled: 0 })]);
    const fills = drawn(calls, 'fillRect');
    assert.equal(fills.length, 4);
    assert.deepEqual(fills.map((f) => [f[3], f[4]]),
        [[100, 1], [100, 1], [1, 50], [1, 50]], 'top, bottom, left, right');
});

test('a line is axis-aligned, and its direction picks the axis', () => {
    const horizontal = paint([command(EMIT_KIND.LINE, { colour: 1, lineWidth: 3 })]);
    assert.deepEqual(drawn(horizontal.calls, 'fillRect')[0].slice(1, 5), [10, 20, 100, 3]);

    const vertical = paint([command(EMIT_KIND.LINE,
        { colour: 1, lineWidth: 3, lineDirection: 1 })]);
    assert.deepEqual(drawn(vertical.calls, 'fillRect')[0].slice(1, 5), [10, 20, 3, 50]);
});

/* -------------------------------------------------------------------------
 * Clipping
 * ---------------------------------------------------------------------- */

test('every command is drawn inside its own clip', () => {
    const { calls } = paint([
        command(EMIT_KIND.RECT, { colour: 1, filled: 1 },
            { clip: { x: 5, y: 5, width: 50, height: 50 } }),
    ]);
    assert.deepEqual(drawOrder(calls), ['clip', 'fillRect']);
    assert.deepEqual(drawn(calls, 'clip')[0].slice(1), [5, 5, 50, 50]);
});

test('a collapsed clip skips the command entirely', () => {
    const { painter, calls } = paint([
        command(EMIT_KIND.RECT, { colour: 1, filled: 1 },
            { clip: { x: 0, y: 0, width: 0, height: 40 } }),
    ]);
    assert.equal(drawn(calls, 'fillRect').length, 0);
    assert.equal(painter.stats.skipped, 1);
});

test('the surface is not cleared unless the caller asks', () => {
    /*
     * The C client leaves the world viewport alone deliberately: clearing it
     * gives the 3D scene a one-frame flash of background between the clear and
     * the draw.
     */
    const bare = paint([]);
    assert.equal(drawn(bare.calls, 'fillRect').length, 0);

    const cleared = paint([], { frame: { background: '#000000' } });
    assert.equal(drawn(cleared.calls, 'fillRect').length, 1);
});

/* -------------------------------------------------------------------------
 * Sprites
 * ---------------------------------------------------------------------- */

test('a missing sprite draws nothing and is recorded as wanted', () => {
    /*
     * Not a placeholder. A placeholder looks like a design decision and is
     * the reason a missing asset can sit unnoticed; an empty box is visibly
     * absent and the id is in `wanted` for the caller to load.
     */
    const { painter, calls } = paint([command(EMIT_KIND.SPRITE, { sprite: 42 })],
        { sprites: { get: () => null } });
    assert.equal(drawn(calls, 'drawImage').length, 0);
    assert.deepEqual([...painter.wanted.sprites], [42]);
    assert.equal(painter.stats.missingAssets, 1);
});

test('a sprite draws at its offset within its canvas', () => {
    /*
     * The content tree's own sprite 0 is a 26x18 bitmap at (7, 11) on a 40x40
     * canvas. Drawing at the widget's origin instead puts it eleven pixels
     * too high — and every trimmed icon with it.
     */
    const image = { id: 3, width: 40, height: 40, offsetX: 7, offsetY: 11 };
    const { calls } = paint([command(EMIT_KIND.SPRITE, { sprite: 3 })],
        { sprites: { get: () => image } });
    const [draw] = drawn(calls, 'drawImage');
    assert.deepEqual([draw[2], draw[3]], [17, 31], 'the widget is at (10, 20)');
});

test('a tile steps by the canvas, not by the trimmed bitmap', () => {
    /* Stepping by the bitmap closes the pattern up wherever a tile has a
     * transparent margin. */
    const image = { id: 4, width: 50, height: 25, offsetX: 0, offsetY: 0 };
    const { calls } = paint([command(EMIT_KIND.SPRITE, { sprite: 4, tiled: 1 })],
        { sprites: { get: () => image } });
    assert.equal(drawn(calls, 'drawImage').length, 4, '100x50 filled by 50x25');
});

test('the sprite angle is 65536 to a turn, not 2048', () => {
    /*
     * IF3 `spriteAngle` and the compass/minimap camera yaw use DIFFERENT
     * scales, and mixing them is a silent 32x — a widget that should tilt a
     * few degrees spins instead.
     */
    const image = { id: 1, width: 10, height: 10 };
    const { calls } = paint([command(EMIT_KIND.SPRITE, { sprite: 1, spriteAngle: 16384 })],
        { sprites: { get: () => image } });
    assert.equal(drawn(calls, 'drawImage')[0][5].angle, 16384,
        'the raw value reaches the surface, which knows the scale');
});

/* -------------------------------------------------------------------------
 * Hover variants
 * ---------------------------------------------------------------------- */

test('a hovered component swaps to its over variant', () => {
    const base = paint([command(EMIT_KIND.RECT,
        { colour: 0x111111, colourOver: 0x222222, filled: 1 })]);
    assert.equal(drawn(base.calls, 'fillRect')[0][5], '#111111');

    const hovered = paint([command(EMIT_KIND.RECT,
        { colour: 0x111111, colourOver: 0x222222, filled: 1 }, { hovered: true })]);
    assert.equal(drawn(hovered.calls, 'fillRect')[0][5], '#222222');
});

test('a component with no over variant does not react to hover', () => {
    const { calls } = paint([command(EMIT_KIND.RECT, { colour: 0x111111, filled: 1 },
        { hovered: true })]);
    assert.equal(drawn(calls, 'fillRect')[0][5], '#111111');
});

/* -------------------------------------------------------------------------
 * Text and models
 * ---------------------------------------------------------------------- */

test('text needs a real font; without one nothing is drawn', () => {
    /* The cache's fonts are bitmap images with data advances. Falling back to
     * the browser's font engine would be text of a different shape at a
     * different width, which then sizes the widget around it. */
    const { painter, calls } = paint([command(EMIT_KIND.TEXT, { text: 'hi', font: 495 })],
        { fonts: { get: () => null } });
    assert.equal(drawn(calls, 'drawImage').length, 0);
    assert.deepEqual([...painter.wanted.fonts], [495]);
});

test('text is laid out by the font, onto the same surface', () => {
    /* The font blits glyphs; there is no private `surface.text` path, so a
     * recording surface sees exactly what a canvas would. */
    const seen = [];
    const font = {
        id: 495,
        draw(surface, value, options) { seen.push([value, options]); },
    };
    paint([command(EMIT_KIND.TEXT, {
        text: 'Total', font: 495, colour: 0xff981f, halign: 1, valign: 2,
        lineHeight: 14, shadowed: 1,
    })], { fonts: { get: () => font } });

    const [[value, options]] = seen;
    assert.equal(value, 'Total');
    assert.equal(options.halign, 1);
    assert.equal(options.valign, 2);
    assert.equal(options.lineHeight, 14);
    assert.equal(options.shadowed, true);
    assert.equal(options.colour, '#ff981f');
});

test('a model widget composites from elsewhere and asks for what it lacks', () => {
    const { painter, calls } = paint([command(EMIT_KIND.MODEL, {
        model: 88, modelZoom: 500, modelAngleX: 150,
    })], { models: { get: () => null } });
    assert.equal(drawn(calls, 'drawImage').length, 0);
    assert.deepEqual([...painter.wanted.models], [88]);

    const rendered = paint([command(EMIT_KIND.MODEL, { model: 88 })],
        { models: { get: () => ({ id: 'model88', width: 100, height: 50 }) } });
    assert.equal(drawn(rendered.calls, 'drawImage').length, 1);
});

/* -------------------------------------------------------------------------
 * End to end, from a tree
 * ---------------------------------------------------------------------- */

test('a tree paints in emit order, which is TREE order', () => {
    const tree = createUITree();
    const layout = createLayout({ tree, root: { x: 0, y: 0, width: 400, height: 300 } });
    const emitter = createEmitter({ tree, layout });

    const panel = tree.push({
        type: WIDGET_TYPE.LAYER,
        props: { x: 0, y: 0, width: 200, height: 100 },
    });
    /*
     * The background is the EARLIER sibling, which is how a real interface
     * authors it. There is no text pass to lift the label out of tree order —
     * that was the C client's own bug, and its fix is why a widget group can
     * cover an earlier one at all.
     */
    tree.push({
        parentIndex: panel, type: WIDGET_TYPE.RECTANGLE,
        props: { x: 0, y: 0, width: 200, height: 100, colour: 0x201d18, filled: 1 },
    });
    tree.push({
        parentIndex: panel, type: WIDGET_TYPE.TEXT,
        props: { x: 4, y: 4, width: 190, height: 16, text: 'Bank', font: 495, colour: 0xff981f },
    });

    layout.resolve();
    emitter.walk({ force: true });

    const surface = createRecordingSurface();
    createPainter({ surface, fonts: { get: () => testFont() } })
        .paint(emitter.commands, { width: 400, height: 300 });

    const kinds = drawOrder(surface.calls, ['begin', 'end', 'clip']);
    assert.deepEqual(kinds, ['fillRect', 'drawImage', 'drawImage', 'drawImage', 'drawImage'],
        'the background rectangle first, then the label\'s four glyphs over it');
});

let failed = 0;
for( const [name, fn] of tests )
{
    try { fn(); console.log(`ok   ${name}`); }
    catch( error ) { failed++; console.error(`FAIL ${name}\n     ${error.message}`); }
}
console.log(`\n${tests.length - failed}/${tests.length} passed`);
process.exit(failed ? 1 : 0);
