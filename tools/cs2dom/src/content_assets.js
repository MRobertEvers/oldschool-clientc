/*
 * Reading the content tree's assets into the stores the painter draws from.
 *
 * The decoding itself already exists — `png.js` turns a BMP into pixels — so
 * this file is about the two things the tree's layout requires and a naive
 * reader gets wrong.
 *
 * ------------------------------------------------------------------
 * A BMP is not the whole sprite
 * ------------------------------------------------------------------
 *
 * `sprites/<name>/0.bmp` carries the PIXELS. The `pack.meta` beside it carries
 * what a bitmap cannot hold: the sprite's canvas size and its offset within
 * that canvas. A 35x14 image at offset (2, 13) on a 40x40 canvas is a glyph
 * sitting near the bottom of its cell, and drawing the bitmap at the widget's
 * origin instead puts it 13 pixels too high. The meta is not optional
 * decoration; it is half the sprite.
 *
 * ------------------------------------------------------------------
 * A font is a sprite pack plus a metrics file
 * ------------------------------------------------------------------
 *
 * `fonts/font_495.fm` holds the ascent and one advance per character, and the
 * glyph pictures live in `sprites/p12_full/<code>.bmp`. Neither half is
 * enough: the advances decide where the next character goes and the images
 * decide what it looks like, and measuring by image width — the obvious
 * shortcut — makes every proportional string come out narrow.
 */

import { existsSync, readFileSync, readdirSync } from 'node:fs';
import { join } from 'node:path';

import { decodeBmp } from './png.js';
import { parseFontMetrics, parseSpriteMeta } from './font.js';
import { BitmapFont } from './assets.js';

/** The pack files that map an id to the directory or file holding it. */
const SPRITE_PACK = join('pack', '8_sprites.pack');
const FONT_PACK = join('pack', '13_fonts.pack');

export function createContentAssets(contentDir) {
    return new ContentAssets(contentDir);
}

export class ContentAssets {
    constructor(contentDir) {
        this.root = contentDir;
        this.spriteNames = readPack(join(contentDir, SPRITE_PACK));
        this.fontNames = readPack(join(contentDir, FONT_PACK));
        this.stats = { sprites: 0, fonts: 0, glyphs: 0, missing: 0 };
    }

    /* --------------------------------------------------------------
     * Sprites
     * ----------------------------------------------------------- */

    /**
     * Decode sprite `id`, frame `frame`.
     *
     * A sprite id names a PACK, not an image: `2x_ancient_spells_off_0` holds
     * one frame and a font pack holds ninety-five. The frame is the atlas
     * index the cache field carries alongside the id.
     */
    sprite(id, frame = 0) {
        const name = this.spriteNames.get(id | 0);
        if( !name ) { this.stats.missing++; return null; }
        return this.spriteFromPack(name, frame);
    }

    spriteFromPack(name, frame = 0) {
        const dir = join(this.root, 'sprites', name);
        const bmp = join(dir, `${frame}.bmp`);
        if( !existsSync(bmp) ) { this.stats.missing++; return null; }

        const image = decodeBmp(readFileSync(bmp));
        const meta = this.packMeta(dir).get(frame);
        this.stats.sprites++;

        /*
         * The canvas is what the widget is positioned against; the bitmap sits
         * at (x, y) inside it. Reporting the bitmap's own size as the sprite's
         * would place every offset glyph and every trimmed icon wrong.
         */
        return {
            width: meta ? meta.canvasWidth : image.width,
            height: meta ? meta.canvasHeight : image.height,
            offsetX: meta ? meta.x : 0,
            offsetY: meta ? meta.y : 0,
            bitmap: image,
        };
    }

    /** `pack.meta`, parsed once per directory. */
    packMeta(dir) {
        if( !this._meta ) this._meta = new Map();
        if( this._meta.has(dir) ) return this._meta.get(dir);
        const path = join(dir, 'pack.meta');
        const parsed = existsSync(path)
            ? parseSpriteMeta(readFileSync(path, 'utf8'))
            : new Map();
        this._meta.set(dir, parsed);
        return parsed;
    }

    /* --------------------------------------------------------------
     * Fonts
     * ----------------------------------------------------------- */

    /**
     * Decode font `id` into glyphs plus advances.
     *
     * The two halves come from different places and both are required. A font
     * whose metrics are missing would lay every character out at zero advance
     * — one glyph on top of another — so an incomplete font is no font.
     */
    font(id) {
        /*
         * The two halves are named by two DIFFERENT packs.
         *
         * Metrics live in table 13 and glyphs in table 8, at the same id — and
         * `13_fonts.pack` has a real name for only three of the cache's
         * twenty-one fonts. The rest are `font_496`, `font_1442`, placeholders
         * the unpacker generated because nothing named them; the glyph pack
         * for 496 is `b12_full` and for 1442 `verdana_11pt_regular`, and both
         * of those names are in `8_sprites.pack`.
         *
         * Looking the directory up by the FONT name therefore found 3 of 21,
         * and the other 18 answered "no font" — which `parawidth` and
         * `paraheight` report as 0, so every widget sized from a measurement
         * in one of them laid out at its padding. Interface 600's list rows
         * came out 10 pixels tall against the reference's 73.
         */
        const spriteName = this.spriteNames.get(id | 0);
        const fontName = this.fontNames.get(id | 0);
        if( !spriteName && !fontName ) { this.stats.missing++; return null; }

        const metricsPath = join(this.root, 'fonts', `font_${id | 0}.fm`);
        if( !existsSync(metricsPath) ) { this.stats.missing++; return null; }
        const { ascent, advances } = parseFontMetrics(readFileSync(metricsPath, 'utf8'));

        const name = [spriteName, fontName].find((candidate) =>
            candidate && existsSync(join(this.root, 'sprites', candidate)));
        if( !name ) { this.stats.missing++; return null; }
        const dir = join(this.root, 'sprites', name);
        const meta = this.packMeta(dir);

        const glyphs = new Map();
        for( const file of readdirSync(dir) )
        {
            if( !file.endsWith('.bmp') ) continue;
            const code = Number(file.slice(0, -4));
            if( !Number.isInteger(code) ) continue;
            const image = decodeBmp(readFileSync(join(dir, file)));
            const box = meta.get(code);
            glyphs.set(code, {
                id: `${name}:${code}`,
                width: image.width,
                height: image.height,
                /* A glyph's offset within its cell is what puts a descender
                 * below the baseline and a comma below that; ignoring it
                 * aligns every character to the same top edge. */
                offsetX: box ? box.x : 0,
                offsetY: box ? box.y : 0,
                bitmap: image,
            });
            this.stats.glyphs++;
        }
        if( glyphs.size === 0 ) { this.stats.missing++; return null; }

        this.stats.fonts++;
        return new BitmapFont({
            id: id | 0, ascent, advances, glyphs,
            /* The line step the cache implies: the ascent plus the descender
             * room the tallest glyph needs. Where a component states its own
             * `lineHeight`, that wins — this is only the default. */
            lineHeight: ascent + 2,
        });
    }

    /* --------------------------------------------------------------
     * Wiring
     * ----------------------------------------------------------- */

    /**
     * The `decode` hooks the stores take.
     *
     * Synchronous under the hood — these are files on disk — but declared
     * async because the store's contract is, and a browser-side source reading
     * over the network will be.
     */
    hooks() {
        return {
            sprite: async (id) => this.sprite(id),
            font: async (id) => this.font(id),
            /* Models are toridraw's; there is no content-tree decoder for
             * them and pretending otherwise would draw an empty box that
             * looked deliberate. */
            model: async () => null,
        };
    }
}

/**
 * A pack file: `id=name` per line, with an optional trailing `hashname(...)`.
 *
 * The hash is the cache's own name lookup and is not part of the id-to-name
 * mapping; taking the whole rest of the line as the name would build a
 * directory path that does not exist.
 */
export function readPack(path) {
    const out = new Map();
    if( !existsSync(path) ) return out;
    for( const raw of readFileSync(path, 'utf8').split('\n') )
    {
        const line = raw.replace(/\/\/.*$/, '').trim();
        const split = line.indexOf('=');
        if( split < 1 ) continue;
        const id = Number(line.slice(0, split));
        if( !Number.isInteger(id) ) continue;
        const name = line.slice(split + 1).trim().split(/\s+/)[0];
        if( name ) out.set(id, name);
    }
    return out;
}
