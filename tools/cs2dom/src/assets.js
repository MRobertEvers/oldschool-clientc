/*
 * Sprites, bitmap fonts and models, behind the contracts the painter asks for.
 *
 * The painter never decodes anything. It asks a store for an image and draws
 * it, or records the id as wanted and draws nothing. That split is what lets
 * the painter be tested in Node against a recording surface, and it is what
 * lets these stores be swapped for a cache-backed one without the painter
 * knowing.
 *
 * ------------------------------------------------------------------
 * Why the fonts are here and not in the browser
 * ------------------------------------------------------------------
 *
 * A cache font is a set of glyph IMAGES with per-character advances stored as
 * data. The browser's font engine is a different set of shapes at different
 * widths — and a widget sized from a measurement is then sized wrong, which
 * looks like a layout bug rather than a font one. So text is blitted glyph by
 * glyph from an atlas, and the advances come from the cache.
 *
 * ------------------------------------------------------------------
 * Markup is not drawn, and is not measured
 * ------------------------------------------------------------------
 *
 * `<col=...>`, `</col>`, `<lt>`, `<gt>`, `<br>` and `@xxx@` are consumed by
 * the renderer without emitting glyphs. A measurement that counts them
 * measures a string far wider than the one that appears; the C client's own
 * measurement and its renderer once disagreed for exactly that reason, so the
 * scanner below is used by BOTH the drawing and the measuring path here.
 */

/**
 * One tokenised run of text: the characters to draw, with colour changes.
 *
 * Deliberately not a string with escapes stripped — a caller that needs both
 * "what is drawn" and "in what colour" would otherwise scan twice and could
 * disagree with itself.
 */
export function tokenizeMarkup(text, defaultColour = null) {
    const source = String(text ?? '');
    const runs = [];
    let plain = '';
    let colour = defaultColour;
    let current = '';

    const flush = () => {
        if( current === '' ) return;
        runs.push({ text: current, colour });
        current = '';
    };

    for( let i = 0; i < source.length; i++ )
    {
        const ch = source[i];

        if( ch === '<' )
        {
            const close = source.indexOf('>', i);
            if( close < 0 ) { current += ch; plain += ch; continue; }
            const tag = source.slice(i + 1, close);
            i = close;

            /* `<lt>` and `<gt>` are ESCAPES, not tags: they are how `escape`
             * makes a literal angle bracket survive to the screen. */
            if( tag === 'lt' ) { current += '<'; plain += '<'; continue; }
            if( tag === 'gt' ) { current += '>'; plain += '>'; continue; }
            if( tag === 'br' ) { flush(); runs.push({ lineBreak: true }); continue; }

            const coloured = /^col=([0-9a-fA-F]{1,6})$/.exec(tag);
            if( coloured ) { flush(); colour = parseInt(coloured[1], 16); continue; }
            if( tag === '/col' ) { flush(); colour = defaultColour; continue; }
            /* An unknown tag is dropped, not drawn. The font decoder does the
             * same; drawing it would put raw markup on screen. */
            continue;
        }

        /* `@xxx@` is the three-letter colour form. */
        if( ch === '@' && source[i + 4] === '@' )
        {
            flush();
            colour = shortColour(source.slice(i + 1, i + 4), defaultColour);
            i += 4;
            continue;
        }

        current += ch;
        plain += ch;
    }
    flush();
    return { runs, plain };
}

const SHORT_COLOURS = new Map(Object.entries({
    red: 0xff0000, gre: 0x00ff00, blu: 0x0000ff, yel: 0xffff00,
    cya: 0x00ffff, mag: 0xff00ff, whi: 0xffffff, bla: 0x000000,
    lre: 0xff9040, dre: 0x800000, dbl: 0x000080, or1: 0xffb000,
    or2: 0xff7000, or3: 0xff3000, gr1: 0xc0ff00, gr2: 0x80ff00,
    gr3: 0x40ff00,
}));

function shortColour(name, fallback) {
    const found = SHORT_COLOURS.get(name.toLowerCase());
    return found === undefined ? fallback : found;
}

/* -------------------------------------------------------------------------
 * Sprites
 * ---------------------------------------------------------------------- */

/**
 * Decoded sprites by id.
 *
 * `get` answers null for anything not yet decoded, and the painter records
 * that id as wanted. It never blocks and never invents a placeholder: an
 * absent sprite draws nothing, which is visibly absent, whereas a placeholder
 * looks like a design decision and can sit unnoticed for a long time.
 */
export class SpriteStore {
    constructor({ decode = null, limit = 4096 } = {}) {
        this.images = new Map();
        this.decode = decode;
        this.limit = limit;
        this.stats = { hits: 0, misses: 0, decoded: 0, evicted: 0 };
    }

    get(id) {
        const key = id | 0;
        const found = this.images.get(key);
        if( found ) { this.stats.hits++; return found; }
        this.stats.misses++;
        return null;
    }

    has(id) { return this.images.has(id | 0); }

    /** Install a decoded image. `{ width, height, bitmap }` is the contract. */
    put(id, image) {
        const key = id | 0;
        if( !image || !(image.width >= 0) || !(image.height >= 0) )
            throw new TypeError(`sprite ${key} needs width and height`);
        /* Insertion-ordered eviction: a Map iterates in insertion order, so
         * the oldest key is the first one. Nothing here is hot enough to want
         * true LRU bookkeeping on every read. */
        if( this.images.size >= this.limit && !this.images.has(key) )
        {
            const oldest = this.images.keys().next().value;
            this.images.delete(oldest);
            this.stats.evicted++;
        }
        this.images.set(key, { id: key, ...image });
        this.stats.decoded++;
        return this.images.get(key);
    }

    /** Decode and install, for a loader servicing a park. */
    async load(id) {
        if( this.has(id) ) return this.get(id);
        if( !this.decode ) return null;
        const image = await this.decode(id | 0);
        return image ? this.put(id, image) : null;
    }
}

/* -------------------------------------------------------------------------
 * Bitmap fonts
 * ---------------------------------------------------------------------- */

/**
 * One cache font: an atlas of glyph images plus the advances that place them.
 *
 * The advances are DATA, not measured from the images — a glyph's drawn width
 * and the distance to the next character are different numbers, and using the
 * image width for both makes every proportional string come out narrow.
 */
export class BitmapFont {
    constructor({ id, ascent = 0, advances = [], glyphs = new Map(), lineHeight = 0 } = {}) {
        this.id = id;
        this.ascent = ascent;
        this.advances = advances;
        this.glyphs = glyphs;
        this.lineHeight = lineHeight || ascent + 2;
    }

    advanceOf(code) {
        return this.advances[code] ?? 0;
    }

    glyphOf(code) {
        return this.glyphs.get(code) ?? null;
    }

    /** Width of one run of already-tokenised text, in pixels. */
    runWidth(text) {
        let width = 0;
        for( let i = 0; i < text.length; i++ ) width += this.advanceOf(text.charCodeAt(i));
        return width;
    }

    /**
     * Break a string into lines at `maxWidth`.
     *
     * Between WORDS, never inside one, and honouring the explicit breaks the
     * markup carries. `maxWidth <= 0` means "do not wrap", which is what a
     * component with no width constraint asks for.
     */
    wrap(text, maxWidth) {
        const { runs } = tokenizeMarkup(text);
        const lines = [];
        let line = '';
        let lineWidth = 0;

        /*
         * The whitespace a line broke AT is dropped.
         *
         * It exists only as the break point and renders as nothing, but it is
         * still counted by `runWidth` — so keeping it shifts a centred line
         * left by its advance, which is a real and puzzling artefact. The
         * text that survives is what appears.
         */
        const push = () => { lines.push(line.replace(/\s+$/, '')); line = ''; lineWidth = 0; };

        for( const run of runs )
        {
            if( run.lineBreak ) { push(); continue; }
            for( const segment of run.text.split(/(\s+)/) )
            {
                if( segment === '' ) continue;
                if( segment === '\n' ) { push(); continue; }
                const width = this.runWidth(segment);
                if( maxWidth > 0 && lineWidth + width > maxWidth && line !== '' )
                    push();
                line += segment;
                lineWidth += width;
            }
        }
        push();
        return lines;
    }

    /** The widest line, which is what `parawidth` answers. */
    measureWidth(text, maxWidth) {
        let widest = 0;
        for( const line of this.wrap(text, maxWidth) )
            widest = Math.max(widest, this.runWidth(line));
        return widest;
    }

    /** Total height, which is what `paraheight` answers. */
    measureHeight(text, maxWidth) {
        return this.wrap(text, maxWidth).length * this.lineHeight;
    }

    /**
     * Blit one string onto a surface.
     *
     * Alignment is resolved here rather than in the painter because it needs
     * the measured width, and measuring is this object's job. `halign` is
     * 0 left / 1 centre / 2 right and `valign` 0 top / 1 centre / 2 bottom,
     * as the cache numbers them.
     */
    draw(surface, text, {
        x, y, width, height, colour = null, alpha = 1,
        halign = 0, valign = 0, lineHeight = 0, shadowed = false,
    }) {
        const step = lineHeight > 0 ? lineHeight : this.lineHeight;
        const lines = this.wrap(text, width);
        const blockHeight = lines.length * step;

        let cursorY = y;
        if( valign === 1 ) cursorY += ((height - blockHeight) / 2) | 0;
        else if( valign === 2 ) cursorY += height - blockHeight;

        for( const line of lines )
        {
            const lineWidth = this.runWidth(line);
            let cursorX = x;
            if( halign === 1 ) cursorX += ((width - lineWidth) / 2) | 0;
            else if( halign === 2 ) cursorX += width - lineWidth;

            /* The shadow is the same glyphs one pixel down and right, in
             * black, drawn FIRST — drawing it after would put it over the
             * text it is meant to sit behind. */
            if( shadowed )
                this._blitLine(surface, line, cursorX + 1, cursorY + 1, '#000000', alpha);
            this._blitLine(surface, line, cursorX, cursorY, colour, alpha);
            cursorY += step;
        }
    }

    _blitLine(surface, line, x, y, colour, alpha) {
        let cursorX = x;
        for( let i = 0; i < line.length; i++ )
        {
            const code = line.charCodeAt(i);
            const glyph = this.glyphOf(code);
            if( glyph )
                surface.drawImage(glyph, cursorX + (glyph.offsetX | 0),
                    y + (glyph.offsetY | 0), alpha, { tint: colour });
            cursorX += this.advanceOf(code);
        }
    }
}

/** Decoded fonts by id, with the same never-block contract as sprites. */
export class FontStore {
    constructor({ decode = null } = {}) {
        this.fonts = new Map();
        this.decode = decode;
    }

    get(id) { return this.fonts.get(id | 0) ?? null; }
    has(id) { return this.fonts.has(id | 0); }

    put(id, font) {
        const value = font instanceof BitmapFont ? font : new BitmapFont({ id: id | 0, ...font });
        this.fonts.set(id | 0, value);
        return value;
    }

    async load(id) {
        if( this.has(id) ) return this.get(id);
        if( !this.decode ) return null;
        const font = await this.decode(id | 0);
        return font ? this.put(id, font) : null;
    }

    /** What `parawidth` asks: the widest line, markup not counted. */
    measureWidth(id, text, maxWidth) {
        const font = this.get(id);
        return font ? font.measureWidth(text, maxWidth) : 0;
    }

    measureHeight(id, text, maxWidth) {
        const font = this.get(id);
        return font ? font.measureHeight(text, maxWidth) : 0;
    }
}

/* -------------------------------------------------------------------------
 * Models
 * ---------------------------------------------------------------------- */

/**
 * The seam to toridraw.
 *
 * The 3D rasteriser stays where it is — in WASM, in a worker. It is a raster
 * kernel and not a VM, and reimplementing it in JavaScript would buy nothing
 * this design cares about. What crosses the boundary is a REQUEST (which
 * model, at what angles and zoom, in what box) and an image back.
 *
 * The request is keyed so an unchanged widget does not re-render: a model
 * spinning at a fixed angle for a hundred frames should rasterise once.
 */
export class ModelStore {
    constructor({ render = null, limit = 256 } = {}) {
        this.images = new Map();
        this.render = render;
        this.limit = limit;
        this.stats = { hits: 0, misses: 0, rendered: 0 };
    }

    /**
     * The rendered image for this model in this pose, or null.
     *
     * Null means "not ready" and the painter records it as wanted; the caller
     * services it and the next frame draws it. A model is never waited for
     * inside a frame.
     */
    get(id, pose) {
        const key = poseKey(id, pose);
        const found = this.images.get(key);
        if( found ) { this.stats.hits++; return found; }
        this.stats.misses++;
        this.pending = { id: id | 0, pose, key };
        return null;
    }

    put(id, pose, image) {
        const key = poseKey(id, pose);
        if( this.images.size >= this.limit && !this.images.has(key) )
            this.images.delete(this.images.keys().next().value);
        this.images.set(key, { id: key, ...image });
        this.stats.rendered++;
        return this.images.get(key);
    }

    async load(id, pose) {
        if( !this.render ) return null;
        const image = await this.render(id | 0, pose);
        return image ? this.put(id, pose, image) : null;
    }
}

/**
 * A pose key.
 *
 * Every field that changes the pixels, and nothing that does not. Including
 * the box is not optional: the same model at the same angles in a different
 * box is a different image, and keying without it hands a 32-pixel icon the
 * 200-pixel render.
 */
function poseKey(id, pose = {}) {
    return [
        id | 0, pose.width | 0, pose.height | 0, pose.zoom | 0,
        pose.angleX | 0, pose.angleY | 0, pose.angleZ | 0,
        pose.offsetX | 0, pose.offsetY | 0, pose.anim ?? -1,
    ].join(':');
}

export { poseKey };
