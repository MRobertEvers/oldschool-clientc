/*
 * Sprites, from the content tree to the browser.
 *
 * cachepack unpacks a sprite pack as 32-bit BGRA bitmaps beside a `pack.meta`
 * (tools/cachepack/cp_decode.c, `sprite_write`), which is the form the preview
 * wants — the real pixels of the real sprite, not a stand-in. A browser will not
 * take a BMP with alpha, so this reads one and writes a PNG.
 *
 * Both halves are small enough to do by hand and doing them by hand keeps the tool
 * to one dependency. PNG here is deliberately the simplest file that is still a
 * valid one: 8-bit RGBA, no interlacing, one IDAT.
 */

import { deflateSync } from 'node:zlib';

/** Decode the BMP shapes cachepack writes: 32-bit BGRA, and 24-bit as a fallback. */
export function decodeBmp(buffer) {
    if( buffer.length < 54 || buffer[0] !== 0x42 || buffer[1] !== 0x4d )
        throw new Error('not a BMP');

    const pixelOffset = buffer.readUInt32LE(10);
    const headerSize = buffer.readUInt32LE(14);
    const width = buffer.readInt32LE(18);
    const rawHeight = buffer.readInt32LE(22);
    const bpp = buffer.readUInt16LE(28);

    if( headerSize < 40 ) throw new Error(`unsupported BMP header (${headerSize} bytes)`);
    if( bpp !== 32 && bpp !== 24 ) throw new Error(`unsupported BMP depth (${bpp} bits)`);

    /* A negative height means the rows are already top-down. */
    const height = Math.abs(rawHeight);
    const topDown = rawHeight < 0;
    const bytesPerPixel = bpp / 8;
    const stride = Math.ceil((width * bytesPerPixel) / 4) * 4;

    const rgba = Buffer.alloc(width * height * 4);
    for( let y = 0; y < height; y++ ) {
        const sourceRow = topDown ? y : height - 1 - y;
        let from = pixelOffset + sourceRow * stride;
        let to = y * width * 4;
        for( let x = 0; x < width; x++ ) {
            const b = buffer[from];
            const g = buffer[from + 1];
            const r = buffer[from + 2];
            const a = bpp === 32 ? buffer[from + 3] : 255;
            rgba[to] = r; rgba[to + 1] = g; rgba[to + 2] = b; rgba[to + 3] = a;
            from += bytesPerPixel;
            to += 4;
        }
    }
    return { width, height, rgba };
}

export function encodePng({ width, height, rgba }) {
    /* Each scanline is prefixed with its filter type; 0 is "none". */
    const raw = Buffer.alloc((width * 4 + 1) * height);
    for( let y = 0; y < height; y++ ) {
        raw[y * (width * 4 + 1)] = 0;
        rgba.copy(raw, y * (width * 4 + 1) + 1, y * width * 4, (y + 1) * width * 4);
    }

    const ihdr = Buffer.alloc(13);
    ihdr.writeUInt32BE(width, 0);
    ihdr.writeUInt32BE(height, 4);
    ihdr[8] = 8;    /* bit depth */
    ihdr[9] = 6;    /* colour type: RGBA */
    ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;

    return Buffer.concat([
        Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
        chunk('IHDR', ihdr),
        chunk('IDAT', deflateSync(raw)),
        chunk('IEND', Buffer.alloc(0)),
    ]);
}

/**
 * Restore the nominal sprite canvas that cachepack records in pack.meta.
 *
 * Sprite BMPs contain only the non-transparent crop. The native IF3 renderer
 * first places that crop at (x, y) in the nominal canvas and then scales the
 * whole canvas to the component rectangle. Returning the cropped BMP directly
 * therefore enlarges and misaligns icons whose transparent margins were
 * stripped by the cache.
 */
export function spriteCanvas(sprite, meta = null) {
    if( !meta ) return sprite;
    const width = positiveInteger(meta.canvasWidth, sprite.width);
    const height = positiveInteger(meta.canvasHeight, sprite.height);
    const x = integer(meta.x, 0);
    const y = integer(meta.y, 0);
    if( width === sprite.width && height === sprite.height && x === 0 && y === 0 )
        return sprite;

    const rgba = Buffer.alloc(width * height * 4);
    const sourceLeft = Math.max(0, -x);
    const sourceTop = Math.max(0, -y);
    const sourceRight = Math.min(sprite.width, width - x);
    const sourceBottom = Math.min(sprite.height, height - y);
    if( sourceRight <= sourceLeft || sourceBottom <= sourceTop ) return { width, height, rgba };

    const copyWidth = (sourceRight - sourceLeft) * 4;
    for( let sourceY = sourceTop; sourceY < sourceBottom; sourceY++ ) {
        const from = (sourceY * sprite.width + sourceLeft) * 4;
        const to = ((sourceY + y) * width + sourceLeft + x) * 4;
        sprite.rgba.copy(rgba, to, from, from + copyWidth);
    }
    return { width, height, rgba };
}

/**
 * Produce the repeat cell used by a native tiled sprite.
 *
 * Native tiling repeats the cropped pixels, not the nominal transparent
 * canvas. Its first cell is phased by the crop offset. Rotating the repeat
 * cell by that phase lets the browser repeat it beginning at (0, 0) without a
 * second metadata request.
 */
export function spriteTile(sprite, meta = null) {
    if( !meta || sprite.width <= 0 || sprite.height <= 0 ) return sprite;
    const phaseX = modulo(-integer(meta.x, 0), sprite.width);
    const phaseY = modulo(-integer(meta.y, 0), sprite.height);
    if( phaseX === 0 && phaseY === 0 ) return sprite;

    const rgba = Buffer.alloc(sprite.rgba.length);
    for( let y = 0; y < sprite.height; y++ ) {
        const sourceY = (y + phaseY) % sprite.height;
        for( let x = 0; x < sprite.width; x++ ) {
            const sourceX = (x + phaseX) % sprite.width;
            const from = (sourceY * sprite.width + sourceX) * 4;
            const to = (y * sprite.width + x) * 4;
            sprite.rgba.copy(rgba, to, from, from + 4);
        }
    }
    return { width: sprite.width, height: sprite.height, rgba };
}

function positiveInteger(value, fallback) {
    return Number.isInteger(value) && value > 0 ? value : fallback;
}

function integer(value, fallback) {
    return Number.isInteger(value) ? value : fallback;
}

function modulo(value, divisor) {
    return ((value % divisor) + divisor) % divisor;
}

function chunk(type, data) {
    const out = Buffer.alloc(data.length + 12);
    out.writeUInt32BE(data.length, 0);
    out.write(type, 4, 'ascii');
    data.copy(out, 8);
    out.writeUInt32BE(crc32(out.subarray(4, 8 + data.length)), 8 + data.length);
    return out;
}

const CRC_TABLE = (() => {
    const table = new Int32Array(256);
    for( let n = 0; n < 256; n++ ) {
        let c = n;
        for( let k = 0; k < 8; k++ )
            c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
        table[n] = c;
    }
    return table;
})();

function crc32(buffer) {
    let c = 0xffffffff;
    for( let i = 0; i < buffer.length; i++ )
        c = CRC_TABLE[(c ^ buffer[i]) & 0xff] ^ (c >>> 8);
    return (c ^ 0xffffffff) >>> 0;
}

export function bmpToPng(buffer) {
    return encodePng(decodeBmp(buffer));
}
