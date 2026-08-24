/* RuneScape bitmap text for the browser-owned interface renderer.
 *
 * The server exposes the original cache font metrics and each glyph's alpha
 * bitmap. This module performs the same pen movement, wrapping, alignment and
 * markup walk as ToriDraw2D_DrawStringBox, but paints into a component-local
 * canvas. Keeping it as a regular ESM component means authored React widgets
 * and imported cache widgets share the exact same text primitive.
 */

const FONT_CACHE = new Map();
const GLYPH_CACHE = new Map();
const MAX_LINES = 64;

const AT_COLORS = Object.freeze({
    red: 0xff0000, gre: 0x00ff00, blu: 0x0000ff, yel: 0xffff00,
    cya: 0x00ffff, mag: 0xff00ff, whi: 0xffffff, bla: 0x000000,
    lre: 0xff9040, dre: 0x800000, dbl: 0x000080, or1: 0xffb000,
    or2: 0xff7000, or3: 0xff3000, gr1: 0xc0ff00, gr2: 0x80ff00,
    gr3: 0x40ff00,
});

const CP1252 = new Map([
    [0x20ac, 0x80], [0x201a, 0x82], [0x0192, 0x83], [0x201e, 0x84],
    [0x2026, 0x85], [0x2020, 0x86], [0x2021, 0x87], [0x02c6, 0x88],
    [0x2030, 0x89], [0x0160, 0x8a], [0x2039, 0x8b], [0x0152, 0x8c],
    [0x017d, 0x8e], [0x2018, 0x91], [0x2019, 0x92], [0x201c, 0x93],
    [0x201d, 0x94], [0x2022, 0x95], [0x2013, 0x96], [0x2014, 0x97],
    [0x02dc, 0x98], [0x2122, 0x99], [0x0161, 0x9a], [0x203a, 0x9b],
    [0x0153, 0x9c], [0x017e, 0x9e], [0x0178, 0x9f],
]);

export async function loadCacheFont(source, rawId) {
    const id = Number(rawId);
    if( !Number.isInteger(id) || id < 0 ) return null;
    const key = `${source}:${id}`;
    if( !FONT_CACHE.has(key) ) {
        FONT_CACHE.set(key, fetch(`/font/${encodeURIComponent(source)}/${id}.json`)
            .then(async (response) => response.ok ? response.json() : null)
            .catch(() => null));
    }
    return FONT_CACHE.get(key);
}

/** Replace a text component's CSS fallback with an exact cache-font canvas. */
export async function paintCacheText(element, box, iface, isCurrent = () => true) {
    const props = box.props || {};
    const fontId = Number(props.font ?? props.fontId ?? -1);
    const font = await loadCacheFont(iface.spriteSource, fontId);
    if( !font || !isCurrent() || !element.isConnected ) return false;

    const text = String(props.text ?? '');
    const lines = layoutBitmapText(font, text, box.w, box.h,
        Number(props.lineHeight ?? props.lineheight ?? 0));
    const codes = new Set(lines.flatMap((line) => visibleGlyphs(line.text).map((glyph) => glyph.code)));
    await Promise.all([...codes].map((code) => loadGlyph(font, iface.spriteSource, code)));
    if( !isCurrent() || !element.isConnected ) return false;

    const width = Math.max(1, box.w | 0);
    const height = Math.max(1, box.h | 0);
    const canvas = document.createElement('canvas');
    canvas.width = width;
    canvas.height = height;
    canvas.setAttribute('aria-label', stripMarkup(text));
    const context = canvas.getContext('2d');
    context.imageSmoothingEnabled = false;
    drawBitmapText(context, font, lines, {
        width, height,
        color: Number(props.color ?? props.colour ?? 0),
        halign: Number(props.halign ?? props.xAlign ?? 0),
        valign: Number(props.valign ?? props.yAlign ?? 0),
        lineHeight: Number(props.lineHeight ?? props.lineheight ?? 0),
        shadow: Boolean(props.shadow ?? props.shadowed),
        source: iface.spriteSource,
    });
    element.textContent = '';
    element.classList.add('cache-text');
    element.appendChild(canvas);
    return true;
}

/** Pure layout half, exported so it can be regression-tested without a DOM. */
export function layoutBitmapText(font, rawText, width, height, requestedLineHeight = 0) {
    const text = String(rawText ?? '');
    if( !text ) return [];
    const lineHeight = requestedLineHeight > 0 ? requestedLineHeight : Math.max(1, font.lineHeight | 0);
    const metrics = verticalMetrics(font);
    const autoWrap = width > 0 && height > 0 &&
        !(height < lineHeight + metrics.ascent + metrics.descent && height < lineHeight * 2);
    const lines = [];
    for( const segment of explicitLines(text) ) {
        if( lines.length >= MAX_LINES ) break;
        const wrapped = autoWrap ? wrapSegment(font, segment, width) : [segment];
        for( const value of wrapped ) {
            lines.push({ text: value, width: measureBitmapText(font, value) });
            if( lines.length >= MAX_LINES ) break;
        }
    }
    return lines;
}

export function measureBitmapText(font, text) {
    let width = 0;
    for( const glyph of visibleGlyphs(String(text ?? '')) )
        width += glyph.code === 32 || glyph.code === 124
            ? spaceAdvance(font) : glyphAdvance(font, glyph.code);
    return width;
}

export function stripMarkup(text) {
    return visibleGlyphs(String(text ?? '')).map((glyph) => String.fromCodePoint(glyph.unicode)).join('');
}

function drawBitmapText(context, font, lines, options) {
    if( lines.length === 0 ) return;
    const lineHeight = options.lineHeight > 0 ? options.lineHeight : Math.max(1, font.lineHeight | 0);
    const fontAscent = Math.max(1, font.lineHeight | 0);
    const metrics = verticalMetrics(font);
    const blockHeight = lineHeight * (lines.length - 1) + metrics.ascent + metrics.descent;
    const logicalHeight = options.height > 0 ? options.height : blockHeight;
    let baseY = metrics.ascent;
    if( options.valign === 1 ) {
        const space = logicalHeight - metrics.ascent - metrics.descent - lineHeight * (lines.length - 1);
        baseY = metrics.ascent + Math.trunc(space / 2);
    } else if( options.valign === 2 ) {
        baseY = logicalHeight - metrics.descent - lineHeight * (lines.length - 1);
    }

    for( let index = 0; index < lines.length; index++ ) {
        const line = lines[index];
        let x = options.halign === 1 ? Math.trunc((options.width - line.width) / 2)
            : options.halign === 2 ? options.width - line.width : 0;
        const y = baseY + index * lineHeight - fontAscent;
        if( options.shadow ) drawGlyphLine(context, font, line.text, x + 1, y + 1,
            0x000000, true, options.source);
        drawGlyphLine(context, font, line.text, x, y, options.color, false, options.source);
    }
}

function drawGlyphLine(context, font, text, startX, y, defaultColor, shadowOnly, source) {
    let x = startX;
    const style = { color: defaultColor & 0xffffff, underline: -1, strike: -1 };
    for( const token of tokenize(text, style, defaultColor) ) {
        if( token.kind !== 'glyph' ) continue;
        const advance = token.code === 32 || token.code === 124
            ? spaceAdvance(font) : glyphAdvance(font, token.code);
        const glyph = font.glyphs?.[token.code];
        const image = glyph && GLYPH_CACHE.get(`${source}:${font.id}:${token.code}`)?.value;
        if( image && token.code !== 32 && token.code !== 124 ) {
            const color = shadowOnly ? 0 : token.color;
            drawTinted(context, image, x + glyph.x, y + glyph.y, color);
        }
        if( !shadowOnly ) {
            if( token.strike >= 0 ) {
                context.fillStyle = cssColor(token.strike);
                context.fillRect(x, y + Math.trunc(font.lineHeight * 0.7), advance, 1);
            }
            if( token.underline >= 0 ) {
                context.fillStyle = cssColor(token.underline);
                context.fillRect(x, y + font.lineHeight + 1, advance, 1);
            }
        }
        x += advance;
    }
}

function drawTinted(context, image, x, y, color) {
    const width = image.width || 0;
    const height = image.height || 0;
    if( width <= 0 || height <= 0 ) return;
    const scratch = typeof OffscreenCanvas === 'function'
        ? new OffscreenCanvas(width, height) : document.createElement('canvas');
    scratch.width = width; scratch.height = height;
    const paint = scratch.getContext('2d');
    paint.imageSmoothingEnabled = false;
    paint.drawImage(image, 0, 0);
    paint.globalCompositeOperation = 'source-in';
    paint.fillStyle = cssColor(color);
    paint.fillRect(0, 0, width, height);
    context.drawImage(scratch, x, y);
}

async function loadGlyph(font, source, code) {
    const glyph = font.glyphs?.[code];
    if( !glyph || glyph.width <= 0 || glyph.height <= 0 ) return null;
    const key = `${source}:${font.id}:${code}`;
    if( !GLYPH_CACHE.has(key) ) {
        const entry = { value: null, promise: null };
        entry.promise = new Promise((resolve) => {
            const image = new Image();
            image.onload = () => { entry.value = image; resolve(image); };
            image.onerror = () => resolve(null);
            image.src = glyph.url;
        });
        GLYPH_CACHE.set(key, entry);
    }
    return GLYPH_CACHE.get(key).promise;
}

function visibleGlyphs(text) {
    return [...tokenize(text, null, 0)].filter((token) => token.kind === 'glyph');
}

function* tokenize(text, style, defaultColor) {
    const active = style || { color: defaultColor & 0xffffff, underline: -1, strike: -1 };
    for( let index = 0; index < text.length; ) {
        const rest = text.slice(index);
        const at = /^@([A-Za-z0-9]{3})@/.exec(rest);
        if( at ) {
            const tagged = AT_COLORS[at[1].toLowerCase()];
            if( tagged !== undefined ) active.color = tagged;
            index += 5; continue;
        }
        const lower = rest.toLowerCase();
        if( lower.startsWith('<gt>') || lower.startsWith('<lt>') ) {
            const unicode = lower.startsWith('<gt>') ? 62 : 60;
            yield glyphToken(unicode, active); index += 4; continue;
        }
        if( rest.startsWith('</col>') ) { active.color = defaultColor & 0xffffff; index += 6; continue; }
        if( rest.startsWith('</u>') ) { active.underline = -1; index += 4; continue; }
        if( rest.startsWith('</str>') ) { active.strike = -1; index += 6; continue; }
        let match = /^<col=([0-9a-fA-F]{6}|[0-9a-fA-F]{8})>/.exec(rest);
        if( match ) { active.color = parseInt(match[1].slice(-6), 16); index += match[0].length; continue; }
        match = /^<u(?:=([0-9a-fA-F]{6}|[0-9a-fA-F]{8}))?>/.exec(rest);
        if( match ) { active.underline = match[1] ? parseInt(match[1].slice(-6), 16) : 0; index += match[0].length; continue; }
        match = /^<str(?:=([0-9a-fA-F]{6}|[0-9a-fA-F]{8}))?>/.exec(rest);
        if( match ) { active.strike = match[1] ? parseInt(match[1].slice(-6), 16) : 0x800000; index += match[0].length; continue; }
        const point = text.codePointAt(index);
        yield glyphToken(point, active);
        index += point > 0xffff ? 2 : 1;
    }
}

function glyphToken(unicode, style) {
    return {
        kind: 'glyph', unicode, code: runeScapeByte(unicode),
        color: style.color, underline: style.underline, strike: style.strike,
    };
}

function runeScapeByte(point) {
    if( point >= 0 && point <= 0x7f || point >= 0xa0 && point <= 0xff ) return point;
    return CP1252.get(point) ?? 63;
}

function explicitLines(text) {
    return text.split(/\\n|\r\n|[\r\n]|<br\s*\/?\s*>/i);
}

function wrapSegment(font, text, maxWidth) {
    if( !text ) return [''];
    const words = text.split(/[ |]+/);
    if( words.every((word) => stripMarkup(word) === '') ) return [''];
    const lines = [];
    let current = '';
    let width = 0;
    for( const word of words ) {
        if( !word ) continue;
        const wordWidth = measureBitmapText(font, word);
        const nextWidth = current ? width + spaceAdvance(font) + wordWidth : wordWidth;
        if( current && nextWidth > maxWidth ) {
            lines.push(current); current = word; width = wordWidth;
        } else {
            current = current ? `${current} ${word}` : word;
            width = nextWidth;
        }
    }
    if( current ) lines.push(current);
    return lines.length ? lines : [''];
}

function verticalMetrics(font) {
    const lineHeight = Math.max(1, font.lineHeight | 0);
    let minY = 0, maxBottom = 0, any = false;
    for( const glyph of Object.values(font.glyphs || {}) ) {
        if( glyph.width <= 0 || glyph.height <= 0 ) continue;
        minY = any ? Math.min(minY, glyph.y) : glyph.y;
        maxBottom = any ? Math.max(maxBottom, glyph.y + glyph.height) : glyph.y + glyph.height;
        any = true;
    }
    return any ? {
        ascent: Math.max(1, lineHeight - minY),
        descent: Math.max(0, maxBottom - lineHeight),
    } : { ascent: lineHeight, descent: 0 };
}

function glyphAdvance(font, code) {
    return Number(font.advances?.[code] || font.glyphs?.[code]?.advance || 4);
}

function spaceAdvance(font) {
    return Number(font.advances?.[32] || 4);
}

function cssColor(value) {
    return `#${(Number(value) & 0xffffff).toString(16).padStart(6, '0')}`;
}
