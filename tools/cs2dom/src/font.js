/* Cache bitmap fonts for the React-side renderer.
 *
 * cachepack exports the two halves without losing information: `fonts/font_N.fm`
 * contains the baseline and advances, while the same-numbered sprite archive
 * contains one cropped BMP per CP-1252 character plus its original offsets in
 * `pack.meta`.  Keeping this adapter in Node leaves the browser renderer small
 * and makes OSRS-Content and a derived Dat2 tree use the identical manifest.
 */

import { existsSync, readFileSync } from 'node:fs';
import { join } from 'node:path';

import { bmpToPng } from './png.js';
import { packName } from './pack.js';

const MAX_FONT_ID = 1_000_000;
const MAX_GLYPHS = 256;

export function fontManifest(contentDir, rawId, options = {}) {
    const id = fontId(rawId);
    const metricsPath = join(contentDir, 'fonts', `font_${id}.fm`);
    if( !existsSync(metricsPath) ) return null;

    const names = options.spriteNames instanceof Map
        ? options.spriteNames : readPack(join(contentDir, 'pack', '8_sprites.pack'));
    const spriteName = names.get(id);
    if( !spriteName ) return null;
    const spriteDir = join(contentDir, 'sprites', spriteName);
    const metaPath = join(spriteDir, 'pack.meta');
    if( !existsSync(metaPath) ) return null;

    const metrics = parseFontMetrics(readFileSync(metricsPath, 'utf8'));
    const sprites = parseSpriteMeta(readFileSync(metaPath, 'utf8'));
    const source = String(options.source || 'content');
    const glyphs = {};
    for( let code = 0; code < MAX_GLYPHS; code++ ) {
        const sprite = sprites.get(code);
        const bitmap = join(spriteDir, `${code}.bmp`);
        if( !sprite || !existsSync(bitmap) ) continue;
        glyphs[code] = {
            advance: metrics.advances[code] || 0,
            ...sprite,
            url: `/font/${encodeURIComponent(source)}/${id}/${code}.png`,
        };
    }
    return {
        schema: 'cs2dom-font/1',
        id,
        name: spriteName,
        ascent: metrics.ascent,
        /* The cache metric is the renderer's baseline/line step. The sprite
         * canvas can be taller because descenders extend below that baseline. */
        lineHeight: metrics.ascent,
        canvasWidth: sprites.get(0)?.canvasWidth || 0,
        canvasHeight: sprites.get(0)?.canvasHeight || metrics.ascent,
        advances: metrics.advances,
        glyphs,
    };
}

export function fontGlyphPng(contentDir, rawId, rawCode, options = {}) {
    const id = fontId(rawId);
    const code = boundedInteger('glyph code', rawCode, 0, MAX_GLYPHS - 1);
    const names = options.spriteNames instanceof Map
        ? options.spriteNames : readPack(join(contentDir, 'pack', '8_sprites.pack'));
    const spriteName = names.get(id);
    if( !spriteName ) return null;
    const path = join(contentDir, 'sprites', spriteName, `${code}.bmp`);
    if( !existsSync(path) ) return null;
    try { return bmpToPng(readFileSync(path)); }
    catch { return null; }
}

export function parseFontMetrics(text) {
    const advances = new Array(MAX_GLYPHS).fill(0);
    let ascent = 0;
    for( const raw of String(text || '').split(/\r?\n/) ) {
        const line = raw.replace(/\/\/.*$/, '').trim();
        let match = /^ascent\s*=\s*(\d+)$/.exec(line);
        if( match ) {
            ascent = boundedInteger('font ascent', Number(match[1]), 0, 255);
            continue;
        }
        match = /^advance\s*=\s*(\d+)\s*:\s*(\d+)$/.exec(line);
        if( !match ) continue;
        const code = boundedInteger('advance code', Number(match[1]), 0, MAX_GLYPHS - 1);
        advances[code] = boundedInteger('advance width', Number(match[2]), 0, 255);
    }
    return { ascent, advances };
}

export function parseSpriteMeta(text) {
    const sprites = new Map();
    for( const raw of String(text || '').split(/\r?\n/) ) {
        const line = raw.replace(/\/\/.*$/, '').trim();
        const match = /^sprite(\d+)\s*=\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)$/.exec(line);
        if( !match ) continue;
        const code = Number(match[1]);
        if( !Number.isInteger(code) || code < 0 || code >= MAX_GLYPHS ) continue;
        const values = match.slice(2).map(Number);
        if( values.some((value) => !Number.isInteger(value)) ) continue;
        sprites.set(code, {
            canvasWidth: values[0], canvasHeight: values[1],
            width: values[2], height: values[3], x: values[4], y: values[5],
        });
    }
    return sprites;
}

function readPack(path) {
    const result = new Map();
    if( !existsSync(path) ) return result;
    for( const raw of readFileSync(path, 'utf8').split(/\r?\n/) ) {
        const line = raw.replace(/\/\/.*$/, '').trim();
        const split = line.indexOf('=');
        if( split < 1 ) continue;
        const id = Number(line.slice(0, split));
        const name = packName(line.slice(split + 1));
        if( Number.isInteger(id) && name ) result.set(id, name);
    }
    return result;
}

function fontId(value) {
    return boundedInteger('font id', value, 0, MAX_FONT_ID);
}

function boundedInteger(name, value, low, high) {
    const number = Number(value);
    if( !Number.isInteger(number) || number < low || number > high )
        throw new Error(`${name} must be an integer in ${low}..${high}`);
    return number;
}
