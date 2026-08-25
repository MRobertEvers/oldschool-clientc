/* RuneScape bitmap text for the browser-owned interface renderer.
 *
 * Layout and canvas painting for imported interfaces are cooperative: one
 * central, fair queue advances them in <=4ms macrotask slices. The synchronous
 * layout helpers remain available to authored React components and tests.
 */

const FONT_CACHE = new Map();
const GLYPH_CACHE = new Map();
const TINT_CACHE = new Map();
const MAX_LINES = 64;
const MAX_SLICE_MS = 4;
const DEFAULT_SLICE_MS = 3;
const DEFAULT_TINT_ENTRIES = 512;
const DEFAULT_TINT_PIXELS = 2 * 1024 * 1024;
const DEFAULT_METRIC_ENTRIES = 128;
const DEFAULT_PREPARED_ENTRIES = 256;
const DEFAULT_PREPARED_UNITS = 1024 * 1024;
const CANCELLED = Symbol('cancelled font work');
const DEFAULT_TEXT_COLOR = Symbol('default text color');

const METRICS_CACHE = new Map();
let preparedCache = new WeakMap();
const PREPARED_LRU = new Map();
let tintPixels = 0;
let tintEntryLimit = DEFAULT_TINT_ENTRIES;
let tintPixelLimit = DEFAULT_TINT_PIXELS;
let tintCanvasFactory = null;
let preparedUnits = 0;
let preparedEntryLimit = DEFAULT_PREPARED_ENTRIES;
let preparedUnitLimit = DEFAULT_PREPARED_UNITS;

export const FONT_RUNTIME_METRICS = {
    slices: 0, yieldedSlices: 0, steps: 0, completedJobs: 0,
    cancelledJobs: 0, maxSliceMs: 0, overBudgetSlices: 0,
    tintHits: 0, tintMisses: 0, tintEvictions: 0,
    tintEntries: 0, tintPixels: 0, metricHits: 0, metricMisses: 0,
    metricEntries: 0, metricEvictions: 0,
    preparedHits: 0, preparedMisses: 0, preparedBuilds: 0,
    preparedEvictions: 0, preparedSkips: 0,
    preparedEntries: 0, preparedUnits: 0,
};

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

const COL_TAG = /<col=([0-9a-fA-F]{6}|[0-9a-fA-F]{8})>/y;
const UNDERLINE_TAG = /<u(?:=([0-9a-fA-F]{6}|[0-9a-fA-F]{8}))?>/y;
const STRIKE_TAG = /<str(?:=([0-9a-fA-F]{6}|[0-9a-fA-F]{8}))?>/y;
const BREAK_TAG = /<br\s*\/?\s*>/iy;

function now() { return globalThis.performance?.now?.() ?? Date.now(); }
function macrotask(callback) { setTimeout(callback, 0); }

/* Linked jobs avoid Array.shift(), and one iterator step per turn gives newly
 * queued visible text equal access to a slice. */
function createPaintQueue({ budgetMs = DEFAULT_SLICE_MS, clock = now, schedule = macrotask } = {}) {
    const budget = Math.max(0.1, Math.min(MAX_SLICE_MS,
        Number(budgetMs) || DEFAULT_SLICE_MS));
    let head = null, tail = null, scheduled = false, running = false;

    function append(job) {
        job.next = null;
        if( tail ) tail.next = job;
        else head = job;
        tail = job;
    }
    function shift() {
        const job = head;
        if( !job ) return null;
        head = job.next;
        if( !head ) tail = null;
        job.next = null;
        return job;
    }
    function requestPump() {
        if( scheduled ) return;
        scheduled = true;
        schedule(pump);
    }
    function pump() {
        scheduled = false;
        if( running || !head ) return;
        running = true;
        const started = clock();
        let operations = 0;
        try {
            while( head ) {
                if( operations > 0 && clock() - started >= budget ) break;
                const job = shift();
                if( !job.active() ) {
                    try { job.iterator.return?.(); } catch { /* best effort */ }
                    FONT_RUNTIME_METRICS.cancelledJobs++;
                    job.resolve(CANCELLED);
                    operations++;
                    continue;
                }
                try {
                    const result = job.iterator.next();
                    FONT_RUNTIME_METRICS.steps++;
                    operations++;
                    if( result.done ) {
                        FONT_RUNTIME_METRICS.completedJobs++;
                        job.resolve(result.value);
                    } else append(job);
                } catch( error ) {
                    job.reject(error);
                }
            }
        } finally {
            const elapsed = Math.max(0, clock() - started);
            FONT_RUNTIME_METRICS.slices++;
            FONT_RUNTIME_METRICS.maxSliceMs = Math.max(
                FONT_RUNTIME_METRICS.maxSliceMs, elapsed);
            if( elapsed > MAX_SLICE_MS ) FONT_RUNTIME_METRICS.overBudgetSlices++;
            running = false;
            if( head ) {
                FONT_RUNTIME_METRICS.yieldedSlices++;
                requestPump();
            }
        }
    }
    return {
        enqueue(iterator, active = () => true) {
            return new Promise((resolve, reject) => {
                append({ iterator, active, resolve, reject, next: null });
                requestPump();
            });
        },
        isIdle() { return !head && !tail && !scheduled && !running; },
    };
}

let PAINT_QUEUE = createPaintQueue();

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
    const active = () => Boolean(isCurrent() && element.isConnected);
    if( !font || !active() ) return false;

    const text = String(props.text ?? '');
    const prepared = await preparedText(font, text, box.w, box.h,
        Number(props.lineHeight ?? props.lineheight ?? 0), active);
    if( prepared === CANCELLED || !active() ) return false;

    await Promise.all([...prepared.codes]
        .map((code) => loadGlyph(font, iface.spriteSource, code)));
    if( !active() ) return false;

    const canvas = await PAINT_QUEUE.enqueue(paintCanvasSteps(font, prepared.paintLines, {
        width: Math.max(1, box.w | 0), height: Math.max(1, box.h | 0),
        color: Number(props.color ?? props.colour ?? 0),
        halign: Number(props.halign ?? props.xAlign ?? 0),
        valign: Number(props.valign ?? props.yAlign ?? 0),
        lineHeight: Number(props.lineHeight ?? props.lineheight ?? 0),
        shadow: Boolean(props.shadow ?? props.shadowed),
        source: iface.spriteSource, ariaLabel: prepared.ariaLabel,
    }), active);
    if( canvas === CANCELLED || !active() ) return false;

    element.textContent = '';
    element.classList.add('cache-text');
    element.appendChild(canvas);
    return true;
}

async function preparedText(font, text, width, height, lineHeight, active = () => true) {
    if( !active() ) return CANCELLED;
    const cached = readPrepared(font, text, width, height, lineHeight);
    if( cached ) return active() ? cached : CANCELLED;
    const prepared = await PAINT_QUEUE.enqueue(
        prepareTextSteps(font, text, width, height, lineHeight), active);
    if( prepared === CANCELLED || !active() ) return CANCELLED;
    FONT_RUNTIME_METRICS.preparedBuilds++;
    return storePrepared(font, text, width, height, lineHeight, prepared);
}

function preparedGeometry(width, height, lineHeight) {
    /* The text itself remains a Map key, avoiding an O(text.length) composite
     * key allocation on the browser input thread. */
    return `${String(width)}:${String(height)}:${String(lineHeight)}`;
}

function preparedEntry(font, text, geometry) {
    if( !font || typeof font !== 'object' && typeof font !== 'function' ) return null;
    return preparedCache.get(font)?.get(text)?.get(geometry) || null;
}

function touchPrepared(entry) {
    PREPARED_LRU.delete(entry);
    PREPARED_LRU.set(entry, true);
}

function readPrepared(font, text, width, height, lineHeight) {
    const entry = preparedEntry(font, text,
        preparedGeometry(width, height, lineHeight));
    if( !entry ) {
        FONT_RUNTIME_METRICS.preparedMisses++;
        return null;
    }
    touchPrepared(entry);
    FONT_RUNTIME_METRICS.preparedHits++;
    return entry.value;
}

function storePrepared(font, text, width, height, lineHeight, value) {
    if( !font || typeof font !== 'object' && typeof font !== 'function' ) return value;
    const geometry = preparedGeometry(width, height, lineHeight);
    const existing = preparedEntry(font, text, geometry);
    if( existing ) {
        touchPrepared(existing);
        return existing.value;
    }
    const units = Math.max(1, Number(value.cacheUnits) || text.length || 1);
    if( preparedEntryLimit <= 0 || preparedUnitLimit <= 0 ||
        units > preparedUnitLimit ) {
        FONT_RUNTIME_METRICS.preparedSkips++;
        return value;
    }
    let byText = preparedCache.get(font);
    if( !byText ) {
        byText = new Map();
        preparedCache.set(font, byText);
    }
    let byGeometry = byText.get(text);
    if( !byGeometry ) {
        byGeometry = new Map();
        byText.set(text, byGeometry);
    }
    const entry = { font, text, geometry, byText, byGeometry, value, units };
    byGeometry.set(geometry, entry);
    PREPARED_LRU.set(entry, true);
    preparedUnits += units;
    trimPreparedCache();
    return value;
}

function removePrepared(entry, evicted = true) {
    if( !PREPARED_LRU.delete(entry) ) return;
    entry.byGeometry.delete(entry.geometry);
    if( entry.byGeometry.size === 0 ) entry.byText.delete(entry.text);
    if( entry.byText.size === 0 ) preparedCache.delete(entry.font);
    preparedUnits -= entry.units;
    if( evicted ) FONT_RUNTIME_METRICS.preparedEvictions++;
}

function trimPreparedCache() {
    while( PREPARED_LRU.size > preparedEntryLimit || preparedUnits > preparedUnitLimit ) {
        const oldest = PREPARED_LRU.keys().next().value;
        if( !oldest ) break;
        removePrepared(oldest);
    }
    preparedUnits = Math.max(0, preparedUnits);
    FONT_RUNTIME_METRICS.preparedEntries = PREPARED_LRU.size;
    FONT_RUNTIME_METRICS.preparedUnits = preparedUnits;
}

/** Pure synchronous layout half retained for authored components/tests. */
export function layoutBitmapText(font, rawText, width, height, requestedLineHeight = 0) {
    const text = String(rawText ?? '');
    if( !text ) return [];
    const lineHeight = requestedLineHeight > 0
        ? requestedLineHeight : Math.max(1, font.lineHeight | 0);
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
    return visibleGlyphs(String(text ?? ''))
        .map((glyph) => String.fromCodePoint(glyph.unicode)).join('');
}

/* Incremental equivalent of layoutBitmapText plus its aria-label pass. */
function* prepareTextSteps(font, rawText, width, height, requestedLineHeight = 0) {
    const text = String(rawText ?? '');
    const codes = new Set();
    const aria = [];
    const delimiters = [];
    let delimiterEnd = 0;
    let index = 0;
    const ariaStyle = freshStyle(0);
    while( index < text.length ) {
        /* Break tags deliberately remain visible to stripMarkup/ARIA, matching
         * the original client helper. Merely remember their ranges while this
         * mandatory token pass is already visiting the source. */
        if( index >= delimiterEnd && delimiters.length < MAX_LINES ) {
            const delimiter = explicitDelimiterLength(text, index);
            if( delimiter ) {
                delimiters.push({ index, length: delimiter });
                delimiterEnd = index + delimiter;
            }
        }
        const parsed = nextToken(text, index, ariaStyle, 0);
        index = parsed.index;
        if( parsed.token ) aria.push(String.fromCodePoint(parsed.token.unicode));
        yield;
    }
    const ariaLabel = aria.join('');
    if( !text ) return {
        lines: [], paintLines: [], codes, ariaLabel, cacheUnits: 1,
    };

    const lineHeight = requestedLineHeight > 0
        ? requestedLineHeight : Math.max(1, font.lineHeight | 0);
    const metrics = verticalMetrics(font);
    const autoWrap = width > 0 && height > 0 &&
        !(height < lineHeight + metrics.ascent + metrics.descent && height < lineHeight * 2);
    const lines = [];
    let segmentStart = 0;
    for( const delimiter of delimiters ) {
        if( lines.length >= MAX_LINES ) break;
        yield* appendSegmentSteps(font, text.slice(segmentStart, delimiter.index), width,
            autoWrap, lines, codes);
        segmentStart = delimiter.index + delimiter.length;
    }
    if( lines.length < MAX_LINES )
        yield* appendSegmentSteps(font, text.slice(segmentStart), width,
            autoWrap, lines, codes);
    const paintLines = [];
    let cacheUnits = text.length + ariaLabel.length;
    for( const line of lines ) {
        const tokens = yield* compileLineSteps(line.text, codes);
        paintLines.push({ width: line.width, tokens });
        /* A coarse retained-memory weight accounts for source/result UTF-16,
         * token objects and the glyph-code set without an unbounded sizing
         * walk when the completed job returns to the promise continuation. */
        cacheUnits += line.text.length + tokens.length * 6;
    }
    cacheUnits += codes.size * 2;
    return { lines, paintLines, codes, ariaLabel, cacheUnits };
}

function* compileLineSteps(text, codes) {
    const tokens = [];
    const style = freshStyle(DEFAULT_TEXT_COLOR);
    let index = 0;
    while( index < text.length ) {
        const parsed = nextToken(text, index, style, DEFAULT_TEXT_COLOR);
        index = parsed.index;
        if( parsed.token ) {
            tokens.push(parsed.token);
            codes.add(parsed.token.code);
        }
        yield;
    }
    return tokens;
}

function* appendSegmentSteps(font, segment, maxWidth, autoWrap, lines, codes) {
    if( !autoWrap ) {
        const measured = yield* measureSteps(font, segment, codes);
        lines.push({ text: segment, width: measured.width });
        return;
    }
    if( !segment ) {
        lines.push({ text: '', width: 0 });
        return;
    }

    const words = [];
    let anyVisible = false;
    let index = 0;
    while( index < segment.length ) {
        while( index < segment.length && (segment[index] === ' ' || segment[index] === '|') ) {
            index++;
            yield;
        }
        if( index >= segment.length ) break;
        const start = index;
        while( index < segment.length && segment[index] !== ' ' && segment[index] !== '|' ) {
            index++;
            yield;
        }
        const text = segment.slice(start, index);
        const measured = yield* measureSteps(font, text, codes);
        anyVisible ||= measured.visible > 0;
        words.push({ text, width: measured.width });
    }
    if( !anyVisible ) {
        lines.push({ text: '', width: 0 });
        return;
    }

    let current = [];
    let currentWidth = 0;
    for( const word of words ) {
        const nextWidth = current.length
            ? currentWidth + spaceAdvance(font) + word.width : word.width;
        if( current.length && nextWidth > maxWidth ) {
            lines.push({ text: current.join(' '), width: currentWidth });
            if( lines.length >= MAX_LINES ) return;
            current = [word.text];
            currentWidth = word.width;
        } else {
            current.push(word.text);
            currentWidth = nextWidth;
        }
        yield;
    }
    if( current.length && lines.length < MAX_LINES )
        lines.push({ text: current.join(' '), width: currentWidth });
}

function* measureSteps(font, text, codes) {
    let width = 0, visible = 0, index = 0;
    const style = freshStyle(0);
    while( index < text.length ) {
        const parsed = nextToken(text, index, style, 0);
        index = parsed.index;
        if( parsed.token ) {
            const code = parsed.token.code;
            width += code === 32 || code === 124
                ? spaceAdvance(font) : glyphAdvance(font, code);
            codes.add(code);
            visible++;
        }
        yield;
    }
    return { width, visible };
}

function* paintCanvasSteps(font, lines, options) {
    const canvas = document.createElement('canvas');
    canvas.width = options.width;
    canvas.height = options.height;
    canvas.setAttribute('aria-label', options.ariaLabel);
    const context = canvas.getContext('2d');
    if( context ) {
        context.imageSmoothingEnabled = false;
        yield;
        yield* drawTextSteps(context, font, lines, options);
    }
    return canvas;
}

function* drawTextSteps(context, font, lines, options) {
    if( !lines.length ) return;
    const lineHeight = options.lineHeight > 0
        ? options.lineHeight : Math.max(1, font.lineHeight | 0);
    const fontAscent = Math.max(1, font.lineHeight | 0);
    const metrics = verticalMetrics(font);
    const blockHeight = lineHeight * (lines.length - 1) + metrics.ascent + metrics.descent;
    const logicalHeight = options.height > 0 ? options.height : blockHeight;
    let baseY = metrics.ascent;
    if( options.valign === 1 ) {
        const space = logicalHeight - metrics.ascent - metrics.descent
            - lineHeight * (lines.length - 1);
        baseY = metrics.ascent + Math.trunc(space / 2);
    } else if( options.valign === 2 ) {
        baseY = logicalHeight - metrics.descent - lineHeight * (lines.length - 1);
    }

    for( let index = 0; index < lines.length; index++ ) {
        const line = lines[index];
        const x = options.halign === 1 ? Math.trunc((options.width - line.width) / 2)
            : options.halign === 2 ? options.width - line.width : 0;
        const y = baseY + index * lineHeight - fontAscent;
        if( options.shadow ) yield* drawTokenSteps(context, font, line.tokens,
            x + 1, y + 1, 0, true, options.source);
        yield* drawTokenSteps(context, font, line.tokens,
            x, y, options.color, false, options.source);
    }
}

function* drawTokenSteps(context, font, tokens, startX, y, defaultColor, shadow, source) {
    let x = startX;
    for( const token of tokens ) {
        const advance = token.code === 32 || token.code === 124
            ? spaceAdvance(font) : glyphAdvance(font, token.code);
        const glyph = font.glyphs?.[token.code];
        const image = glyph && GLYPH_CACHE.get(`${source}:${font.id}:${token.code}`)?.value;
        const color = token.color === DEFAULT_TEXT_COLOR ? defaultColor : token.color;
        if( image && token.code !== 32 && token.code !== 124 ) {
            const tinted = tintedGlyph(source, font, token.code, image,
                shadow ? 0 : color);
            if( tinted ) context.drawImage(tinted, x + glyph.x, y + glyph.y);
        }
        if( !shadow ) {
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
        yield;
    }
}

function tintedGlyph(source, font, code, image, color) {
    const width = image.width || 0, height = image.height || 0;
    if( width <= 0 || height <= 0 ) return null;
    const normalized = Number(color) & 0xffffff;
    const key = `${source}:${font.id}:${code}:${normalized}`;
    const hit = TINT_CACHE.get(key);
    if( hit ) {
        TINT_CACHE.delete(key);
        TINT_CACHE.set(key, hit);
        FONT_RUNTIME_METRICS.tintHits++;
        return hit.canvas;
    }

    FONT_RUNTIME_METRICS.tintMisses++;
    const canvas = tintCanvasFactory ? tintCanvasFactory(width, height)
        : typeof OffscreenCanvas === 'function'
            ? new OffscreenCanvas(width, height) : document.createElement('canvas');
    canvas.width = width;
    canvas.height = height;
    const paint = canvas.getContext('2d');
    if( !paint ) return null;
    paint.imageSmoothingEnabled = false;
    paint.drawImage(image, 0, 0);
    paint.globalCompositeOperation = 'source-in';
    paint.fillStyle = cssColor(normalized);
    paint.fillRect(0, 0, width, height);

    const pixels = width * height;
    if( tintEntryLimit > 0 && pixels <= tintPixelLimit ) {
        TINT_CACHE.set(key, { canvas, pixels });
        tintPixels += pixels;
        trimTintCache();
    }
    syncTintMetrics();
    return canvas;
}

function trimTintCache() {
    while( TINT_CACHE.size > tintEntryLimit || tintPixels > tintPixelLimit ) {
        const oldest = TINT_CACHE.entries().next().value;
        if( !oldest ) break;
        TINT_CACHE.delete(oldest[0]);
        tintPixels -= oldest[1].pixels;
        FONT_RUNTIME_METRICS.tintEvictions++;
    }
    syncTintMetrics();
}

function syncTintMetrics() {
    FONT_RUNTIME_METRICS.tintEntries = TINT_CACHE.size;
    FONT_RUNTIME_METRICS.tintPixels = tintPixels;
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
    const active = style || freshStyle(defaultColor);
    for( let index = 0; index < text.length; ) {
        const parsed = nextToken(text, index, active, defaultColor);
        index = parsed.index;
        if( parsed.token ) yield parsed.token;
    }
}

/* Parse one control sequence/code point without slice(index); the old version
 * became quadratic on long strings. */
function nextToken(text, index, style, defaultColor) {
    if( text[index] === '@' && index + 4 < text.length && text[index + 4] === '@' ) {
        const name = text.slice(index + 1, index + 4);
        if( /^[A-Za-z0-9]{3}$/.test(name) ) {
            const tagged = AT_COLORS[name.toLowerCase()];
            if( tagged !== undefined ) style.color = tagged;
            return { index: index + 5, token: null };
        }
    }
    if( text[index] === '<' ) {
        const four = text.slice(index, index + 4).toLowerCase();
        if( four === '<gt>' || four === '<lt>' )
            return { index: index + 4,
                token: glyphToken(four === '<gt>' ? 62 : 60, style) };
        if( text.startsWith('</col>', index) ) {
            style.color = normalizedTextColor(defaultColor);
            return { index: index + 6, token: null };
        }
        if( text.startsWith('</u>', index) ) {
            style.underline = -1;
            return { index: index + 4, token: null };
        }
        if( text.startsWith('</str>', index) ) {
            style.strike = -1;
            return { index: index + 6, token: null };
        }
        let match = matchAt(COL_TAG, text, index);
        if( match ) {
            style.color = parseInt(match[1].slice(-6), 16);
            return { index: index + match[0].length, token: null };
        }
        match = matchAt(UNDERLINE_TAG, text, index);
        if( match ) {
            style.underline = match[1] ? parseInt(match[1].slice(-6), 16) : 0;
            return { index: index + match[0].length, token: null };
        }
        match = matchAt(STRIKE_TAG, text, index);
        if( match ) {
            style.strike = match[1] ? parseInt(match[1].slice(-6), 16) : 0x800000;
            return { index: index + match[0].length, token: null };
        }
    }
    const point = text.codePointAt(index);
    return { index: index + (point > 0xffff ? 2 : 1), token: glyphToken(point, style) };
}

function matchAt(expression, text, index) {
    expression.lastIndex = index;
    return expression.exec(text);
}

function freshStyle(defaultColor) {
    return { color: normalizedTextColor(defaultColor), underline: -1, strike: -1 };
}

function normalizedTextColor(value) {
    return value === DEFAULT_TEXT_COLOR ? value : Number(value) & 0xffffff;
}

function glyphToken(unicode, style) {
    return { kind: 'glyph', unicode, code: runeScapeByte(unicode),
        color: style.color, underline: style.underline, strike: style.strike };
}

function runeScapeByte(point) {
    if( point >= 0 && point <= 0x7f || point >= 0xa0 && point <= 0xff ) return point;
    return CP1252.get(point) ?? 63;
}

function explicitLines(text) {
    return text.split(/\\n|\r\n|[\r\n]|<br\s*\/?\s*>/i);
}

function explicitDelimiterLength(text, index) {
    if( text[index] === '\\' && text[index + 1] === 'n' ) return 2;
    if( text[index] === '\r' ) return text[index + 1] === '\n' ? 2 : 1;
    if( text[index] === '\n' ) return 1;
    if( text[index] !== '<' ) return 0;
    const match = matchAt(BREAK_TAG, text, index);
    return match ? match[0].length : 0;
}

function wrapSegment(font, text, maxWidth) {
    if( !text ) return [''];
    const words = text.split(/[ |]+/);
    if( words.every((word) => stripMarkup(word) === '') ) return [''];
    const lines = [];
    let current = '', width = 0;
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
    if( font && typeof font === 'object' ) {
        const cached = METRICS_CACHE.get(font);
        if( cached ) {
            METRICS_CACHE.delete(font);
            METRICS_CACHE.set(font, cached);
            FONT_RUNTIME_METRICS.metricHits++;
            return cached;
        }
    }
    FONT_RUNTIME_METRICS.metricMisses++;
    const lineHeight = Math.max(1, font.lineHeight | 0);
    let minY = 0, maxBottom = 0, any = false;
    for( const glyph of Object.values(font.glyphs || {}) ) {
        if( glyph.width <= 0 || glyph.height <= 0 ) continue;
        minY = any ? Math.min(minY, glyph.y) : glyph.y;
        maxBottom = any ? Math.max(maxBottom, glyph.y + glyph.height) : glyph.y + glyph.height;
        any = true;
    }
    const result = any ? { ascent: Math.max(1, lineHeight - minY),
        descent: Math.max(0, maxBottom - lineHeight) }
        : { ascent: lineHeight, descent: 0 };
    if( font && typeof font === 'object' ) {
        METRICS_CACHE.set(font, result);
        while( METRICS_CACHE.size > DEFAULT_METRIC_ENTRIES ) {
            const oldest = METRICS_CACHE.keys().next().value;
            METRICS_CACHE.delete(oldest);
            FONT_RUNTIME_METRICS.metricEvictions++;
        }
        FONT_RUNTIME_METRICS.metricEntries = METRICS_CACHE.size;
    }
    return result;
}

function glyphAdvance(font, code) {
    return Number(font.advances?.[code] || font.glyphs?.[code]?.advance || 4);
}
function spaceAdvance(font) { return Number(font.advances?.[32] || 4); }
function cssColor(value) {
    return `#${(Number(value) & 0xffffff).toString(16).padStart(6, '0')}`;
}

function resetMetrics() {
    for( const key of Object.keys(FONT_RUNTIME_METRICS) ) FONT_RUNTIME_METRICS[key] = 0;
}

/* Deterministic hooks for the focused queue/cache tests. */
export const __fontRuntimeTest = Object.freeze({
    configure({ budgetMs, clock, schedule, tintEntries, tintPixels: pixels,
        canvasFactory, preparedEntries,
        preparedUnits: preparedUnitBudget } = {}) {
        if( !PAINT_QUEUE.isIdle() )
            throw new Error('cannot configure the font queue while work is pending');
        PAINT_QUEUE = createPaintQueue({ budgetMs, clock, schedule });
        if( tintEntries !== undefined ) tintEntryLimit = Math.max(0, tintEntries | 0);
        if( pixels !== undefined ) tintPixelLimit = Math.max(0, pixels | 0);
        if( canvasFactory !== undefined ) tintCanvasFactory = canvasFactory;
        if( preparedEntries !== undefined ) preparedEntryLimit = Math.max(0,
            Math.min(4096, preparedEntries | 0));
        if( preparedUnitBudget !== undefined ) preparedUnitLimit = Math.max(0,
            Math.min(64 * 1024 * 1024, Number(preparedUnitBudget) || 0));
        trimTintCache();
        trimPreparedCache();
    },
    async prepare(font, text, width, height, lineHeight = 0, active = () => true) {
        const result = await preparedText(font, String(text ?? ''),
            width, height, lineHeight, active);
        return result === CANCELLED ? null : result;
    },
    reset() {
        if( !PAINT_QUEUE.isIdle() )
            throw new Error('cannot reset the font queue while work is pending');
        FONT_CACHE.clear();
        GLYPH_CACHE.clear();
        TINT_CACHE.clear();
        METRICS_CACHE.clear();
        PREPARED_LRU.clear();
        preparedCache = new WeakMap();
        tintPixels = 0;
        tintEntryLimit = DEFAULT_TINT_ENTRIES;
        tintPixelLimit = DEFAULT_TINT_PIXELS;
        tintCanvasFactory = null;
        preparedUnits = 0;
        preparedEntryLimit = DEFAULT_PREPARED_ENTRIES;
        preparedUnitLimit = DEFAULT_PREPARED_UNITS;
        resetMetrics();
        syncTintMetrics();
        PAINT_QUEUE = createPaintQueue();
    },
    cacheKeys() { return [...TINT_CACHE.keys()]; },
    isIdle() { return PAINT_QUEUE.isIdle(); },
});

if( typeof globalThis === 'object' )
    globalThis.__cs2domFontRuntimeMetrics = FONT_RUNTIME_METRICS;
