/*
 * Reading the content tree's real assets.
 *
 * The one thing worth testing hard here is that a sprite is NOT its bitmap.
 * `sprites/<name>/0.bmp` carries the pixels and `pack.meta` beside it carries
 * the canvas and the offset within it; the tree's own sprite 0 is a 26x18
 * bitmap on a 40x40 canvas at (7, 11), so a reader that returns the bitmap's
 * dimensions places it eleven pixels too high and reports it as less than half
 * the size the widget was laid out for.
 *
 * Skips cleanly when the content tree is not present — this is a real-data
 * test, and pretending otherwise with a fixture would test the fixture.
 */

import assert from 'node:assert/strict';
import { existsSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

import { createContentAssets, readPack } from '../src/content_assets.js';
import { spritePayload, fontPayload } from '../src/dev_canvas.js';

const HERE = dirname(fileURLToPath(import.meta.url));
const CONTENT = process.env.CS2DOM_CONTENT
    ?? join(HERE, '..', '..', '..', 'OSRS-Content', 'osrs239-content');

const tests = [];
function test(name, fn) { tests.push([name, fn]); }

const available = existsSync(join(CONTENT, 'pack', '8_sprites.pack'));
const assets = available ? createContentAssets(CONTENT) : null;

/* -------------------------------------------------------------------------
 * Pack files
 * ---------------------------------------------------------------------- */

test('a pack line keeps only the name, not the hash beside it', () => {
    /*
     * `0=name hashname("name,0")` — taking the rest of the line as the name
     * builds a directory path that does not exist, and every sprite silently
     * fails to load.
     */
    const packed = readPack('/nonexistent');
    assert.equal(packed.size, 0, 'a missing pack is empty, not an error');

    if( !available ) return;
    const name = assets.spriteNames.get(0);
    assert.ok(name && !name.includes(' '), `sprite 0 resolved to '${name}'`);
});

/* -------------------------------------------------------------------------
 * Sprites
 * ---------------------------------------------------------------------- */

test('a sprite reports its CANVAS, and where the bitmap sits in it', () => {
    if( !available ) { console.log('     (no content tree; skipped)'); return; }
    const sprite = assets.sprite(0);
    assert.ok(sprite, 'sprite 0 decoded');
    assert.ok(sprite.width >= sprite.bitmap.width,
        'the canvas is at least as wide as the bitmap on it');
    assert.ok(sprite.height >= sprite.bitmap.height);
    assert.ok(Number.isInteger(sprite.offsetX) && Number.isInteger(sprite.offsetY));
    /* If they were equal for every sprite the meta would be doing nothing;
     * the tree's own sprite 0 is offset, which is the case that matters. */
    assert.ok(sprite.offsetX > 0 || sprite.offsetY > 0,
        'sprite 0 is offset within its canvas — that is why pack.meta exists');
});

test('a sprite id nobody has is null, not an exception', () => {
    if( !available ) return;
    assert.equal(assets.sprite(999999), null);
});

/* -------------------------------------------------------------------------
 * Fonts
 * ---------------------------------------------------------------------- */

test('a font carries both halves: glyph images and data advances', () => {
    if( !available ) { console.log('     (no content tree; skipped)'); return; }
    const font = assets.font(495);
    assert.ok(font, 'font 495 decoded');
    assert.ok(font.glyphs.size > 90, `only ${font.glyphs.size} glyphs`);
    assert.ok(font.ascent > 0);

    /* Proportional: a capital A and a lowercase i must not advance the same,
     * or the font is being measured by something other than its advances. */
    const wideAdvance = font.advanceOf(65);
    const narrowAdvance = font.advanceOf(105);
    assert.ok(wideAdvance > narrowAdvance,
        `A advances ${wideAdvance}, i advances ${narrowAdvance}`);

    /* And the advance is not the image width — the thing a shortcut would
     * use, and the reason every proportional string would come out narrow. */
    const capitalA = font.glyphOf(65);
    assert.ok(capitalA, 'the A glyph is present');
    assert.notEqual(wideAdvance, capitalA.width,
        'the advance and the drawn width are different numbers');
});

test('measuring uses the advances, so a real string has a real width', () => {
    if( !available ) return;
    const font = assets.font(495);
    const width = font.measureWidth('Total', 0);
    assert.ok(width > 0 && width < 200, `"Total" measured ${width}px`);
    assert.equal(width, [...'Total'].reduce((sum, ch) => sum + font.advanceOf(ch.charCodeAt(0)), 0));
});

test('a font whose metrics are missing is no font', () => {
    /* Half a font lays every character out at zero advance — one glyph on top
     * of another — which is worse than nothing at all. */
    if( !available ) return;
    assert.equal(assets.font(999999), null);
});

/* -------------------------------------------------------------------------
 * The wire payloads
 * ---------------------------------------------------------------------- */

test('a sprite payload keeps the canvas and offset alongside the image', () => {
    if( !available ) { console.log('     (no content tree; skipped)'); return; }
    const payload = spritePayload(assets.sprite(0));
    assert.match(payload.png, /^data:image\/png;base64,/);
    assert.ok(payload.width > 0 && payload.height > 0);
    assert.ok('offsetX' in payload && 'offsetY' in payload,
        'a bare image would place every trimmed icon wrong');
});

test('a font payload sends advances sparsely and glyphs whole', () => {
    if( !available ) return;
    const font = assets.font(495);
    const payload = fontPayload(font);
    assert.equal(Object.keys(payload.glyphs).length, font.glyphs.size);
    /* 256 advances of which most are zero would triple the payload. */
    assert.ok(Object.keys(payload.advances).length <= 256);
    assert.equal(payload.advances[65], font.advanceOf(65));
    assert.ok(!('0' in payload.advances) || payload.advances['0'] !== 0,
        'a zero advance is omitted rather than sent');
});

test('the decode hooks match the stores\' contract', () => {
    if( !available ) return;
    const hooks = assets.hooks();
    for( const name of ['sprite', 'font', 'model'] )
        assert.equal(typeof hooks[name], 'function', `${name} hook`);
});

let failed = 0;
for( const [name, fn] of tests )
{
    try { fn(); console.log(`ok   ${name}`); }
    catch( error ) { failed++; console.error(`FAIL ${name}\n     ${error.message}`); }
}
console.log(`\n${tests.length - failed}/${tests.length} passed`
    + (available ? '' : ' (content tree absent; asset cases skipped)'));
process.exit(failed ? 1 : 0);
