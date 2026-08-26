/*
 * The asset stores, and the two places text goes wrong.
 *
 * Markup is the first: it is consumed by the renderer without drawing, so
 * anything that counts it measures a string far wider than the one that
 * appears — and the widget sized from that measurement is then wrong in a way
 * that reads as a layout bug. The C client's own measurement and its renderer
 * disagreed for exactly this reason.
 *
 * Advances are the second: a glyph's drawn width and the distance to the next
 * character are different numbers, and using the image width for both makes
 * every proportional string come out narrow.
 */

import assert from 'node:assert/strict';

import {
    BitmapFont, FontStore, ModelStore, SpriteStore, poseKey, tokenizeMarkup,
} from '../src/assets.js';

const tests = [];
function test(name, fn) { tests.push([name, fn]); }

/** A font where every glyph is 6 wide but advances 4 — the trap, made explicit. */
function testFont(id = 495) {
    const advances = new Array(256).fill(4);
    const glyphs = new Map();
    for( let code = 32; code < 127; code++ )
        glyphs.set(code, { id: `g${code}`, width: 6, height: 9, offsetX: 0, offsetY: 0 });
    return new BitmapFont({ id, ascent: 9, advances, glyphs, lineHeight: 12 });
}

function recorder() {
    const calls = [];
    return {
        calls,
        drawImage(image, x, y, alpha, options) {
            calls.push({ id: image.id, x, y, alpha, tint: options?.tint ?? null });
        },
    };
}

/* -------------------------------------------------------------------------
 * Markup
 * ---------------------------------------------------------------------- */

test('a colour tag changes colour and draws nothing', () => {
    const { runs, plain } = tokenizeMarkup('a<col=ff0000>b</col>c', 0x111111);
    assert.equal(plain, 'abc', 'the tag contributes no characters');
    assert.deepEqual(runs.map((r) => [r.text, r.colour]),
        [['a', 0x111111], ['b', 0xff0000], ['c', 0x111111]]);
});

test('<lt> and <gt> are escapes, not tags', () => {
    /*
     * `escape` emits these so a literal angle bracket survives to the screen.
     * Treating them as unknown tags would drop the character the script went
     * out of its way to preserve.
     */
    const { plain } = tokenizeMarkup('<lt>col=ff0000<gt>hi');
    assert.equal(plain, '<col=ff0000>hi');
});

test('an unknown tag is dropped, not drawn', () => {
    const { plain } = tokenizeMarkup('a<shadow=1>b');
    assert.equal(plain, 'ab');
});

test('the @xxx@ colour form is recognised', () => {
    const { runs, plain } = tokenizeMarkup('@red@danger', 0x000000);
    assert.equal(plain, 'danger');
    assert.equal(runs[0].colour, 0xff0000);
});

/* -------------------------------------------------------------------------
 * Measurement
 * ---------------------------------------------------------------------- */

test('markup is not measured', () => {
    const font = testFont();
    const bare = font.measureWidth('abc', 0);
    const tagged = font.measureWidth('<col=ff0000>abc</col>', 0);
    assert.equal(tagged, bare, 'the tag must add no width');
    assert.equal(bare, 12, 'three characters at advance 4');
});

test('the advance decides the width, not the glyph image', () => {
    /* The glyphs here are 6 wide and advance 4. Measuring by image width
     * would answer 18 for three characters instead of 12. */
    const font = testFont();
    assert.equal(font.measureWidth('abc', 0), 12);
});

test('wrapping breaks between words, never inside one', () => {
    const font = testFont();
    /* Advance 4: "hello" is 20 wide, so 24 fits one word per line. The space
     * the line broke at renders as nothing and is dropped — keeping it would
     * shift a centred line left by its advance. */
    assert.deepEqual(font.wrap('hello world', 24), ['hello', 'world']);
    assert.equal(font.measureWidth('hello world', 24), 20,
        'and the trailing space is not measured either');
});

test('a zero width does not wrap', () => {
    const font = testFont();
    assert.deepEqual(font.wrap('hello world', 0), ['hello world']);
});

test('<br> forces a break wherever it appears', () => {
    const font = testFont();
    assert.deepEqual(font.wrap('a<br>b', 0), ['a', 'b']);
    /* A LINE COUNT, not pixels: `paraheight` is `wrap_line_count` in the C
     * host, and the scripts multiply it by their own per-line step. */
    assert.equal(font.measureHeight('a<br>b', 0), 2, 'two lines');
    assert.equal(font.measureBlockHeight('a<br>b', 0), 24,
        'and the pixel figure is the painter\'s, at lineHeight 12');
});

/* -------------------------------------------------------------------------
 * Drawing
 * ---------------------------------------------------------------------- */

test('glyphs advance by the advance, not the glyph width', () => {
    const font = testFont();
    const surface = recorder();
    font.draw(surface, 'abc', { x: 10, y: 20, width: 100, height: 12 });
    assert.deepEqual(surface.calls.map((c) => c.x), [10, 14, 18]);
});

test('alignment uses the measured width', () => {
    const font = testFont();
    const centred = recorder();
    font.draw(centred, 'abc',
        { x: 0, y: 0, width: 100, height: 12, halign: 1 });
    assert.equal(centred.calls[0].x, 44, '(100 - 12) / 2');

    const right = recorder();
    font.draw(right, 'abc', { x: 0, y: 0, width: 100, height: 12, halign: 2 });
    assert.equal(right.calls[0].x, 88);
});

test('vertical centring uses the block height', () => {
    const font = testFont();
    const surface = recorder();
    font.draw(surface, 'a<br>b', { x: 0, y: 0, width: 100, height: 100, valign: 1 });
    assert.equal(surface.calls[0].y, 38, '(100 - 24) / 2');
});

test('the shadow is drawn first, offset by one', () => {
    /*
     * Drawing it after would put the shadow over the text it is meant to sit
     * behind — which is legible as "the text looks bold and wrong".
     */
    const font = testFont();
    const surface = recorder();
    font.draw(surface, 'a', { x: 5, y: 5, width: 50, height: 12, shadowed: true,
        colour: '#ffffff' });
    assert.equal(surface.calls.length, 2);
    assert.deepEqual([surface.calls[0].x, surface.calls[0].y], [6, 6]);
    assert.equal(surface.calls[0].tint, '#000000');
    assert.deepEqual([surface.calls[1].x, surface.calls[1].y], [5, 5]);
});

test('a colour tag resolves to a run colour the caller can apply', () => {
    /* The blit paints one colour per call; per-run colour is the tokeniser's
     * answer, which is where a caller that wants coloured spans reads it. */
    const { runs } = tokenizeMarkup('a<col=ff0000>b', 0x111111);
    assert.deepEqual(runs.map((r) => r.colour), [0x111111, 0xff0000]);
});

/* -------------------------------------------------------------------------
 * Stores
 * ---------------------------------------------------------------------- */

test('a sprite store never blocks and never invents', () => {
    const store = new SpriteStore();
    assert.equal(store.get(7), null);
    assert.equal(store.stats.misses, 1);

    store.put(7, { width: 10, height: 10, bitmap: 'x' });
    assert.equal(store.get(7).width, 10);
    assert.equal(store.stats.hits, 1);
});

test('a sprite store evicts oldest-first at its limit', () => {
    const store = new SpriteStore({ limit: 2 });
    store.put(1, { width: 1, height: 1 });
    store.put(2, { width: 1, height: 1 });
    store.put(3, { width: 1, height: 1 });
    assert.equal(store.get(1), null, 'the oldest went');
    assert.ok(store.get(3));
    assert.equal(store.stats.evicted, 1);
});

test('a sprite without dimensions is refused', () => {
    const store = new SpriteStore();
    assert.throws(() => store.put(1, { bitmap: 'x' }), /needs width and height/);
});

test('a font store measures through the font it holds', () => {
    const store = new FontStore();
    store.put(495, testFont());
    assert.equal(store.measureWidth(495, 'abcd', 0), 16);
    assert.equal(store.measureWidth(999, 'abcd', 0), 0, 'an absent font measures nothing');
});

/* -------------------------------------------------------------------------
 * Models
 * ---------------------------------------------------------------------- */

test('a model pose key includes everything that changes the pixels', () => {
    const base = { width: 100, height: 50, zoom: 500, angleX: 150, angleY: 0, angleZ: 0,
                   offsetX: 0, offsetY: 0, anim: -1 };
    assert.equal(poseKey(7, base), poseKey(7, { ...base }));
    assert.notEqual(poseKey(7, base), poseKey(7, { ...base, angleY: 1 }));
    /* The box especially: the same model at the same angles in a different
     * box is a different image, and keying without it hands a 32-pixel icon
     * the 200-pixel render. */
    assert.notEqual(poseKey(7, base), poseKey(7, { ...base, width: 32 }));
});

test('an unrendered model asks rather than blocking', () => {
    const store = new ModelStore();
    const pose = { width: 10, height: 10, zoom: 1 };
    assert.equal(store.get(3, pose), null);
    assert.equal(store.pending.id, 3);

    store.put(3, pose, { width: 10, height: 10, bitmap: 'm' });
    assert.ok(store.get(3, pose));
    assert.equal(store.stats.hits, 1);
});

test('an unchanged pose rasterises once', () => {
    const store = new ModelStore();
    const pose = { width: 10, height: 10, zoom: 1, angleY: 40 };
    store.put(3, pose, { width: 10, height: 10 });
    for( let frame = 0; frame < 100; frame++ ) store.get(3, pose);
    assert.equal(store.stats.rendered, 1, 'a still model must not re-render per frame');
    assert.equal(store.stats.hits, 100);
});

let failed = 0;
for( const [name, fn] of tests )
{
    try { fn(); console.log(`ok   ${name}`); }
    catch( error ) { failed++; console.error(`FAIL ${name}\n     ${error.message}`); }
}
console.log(`\n${tests.length - failed}/${tests.length} passed`);
process.exit(failed ? 1 : 0);
