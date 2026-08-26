/*
 * Pixels. One canvas, painted from the emit list.
 *
 * There is no DOM node per widget and there is no React component per widget.
 * A `ca_tasks` rebuild creates ~3,230 components in one tick; as DOM that is
 * 3,230 elements built, styled, laid out by the browser and thrown away on the
 * next rebuild, which is what the old runtime spent its frame budget on and
 * why it grew a second, cooperatively-sliced renderer to hide the cost. A
 * canvas draws 3,230 rectangles without allocating anything.
 *
 * ------------------------------------------------------------------
 * The target is an interface, not a canvas
 * ------------------------------------------------------------------
 *
 * Everything below issues calls against a small drawing surface contract —
 * `fillRect`, `drawImage`, `clip`, `save`/`restore`. A browser 2D context
 * satisfies it;
 * so does a recorder that just remembers the calls, which is how this is
 * tested without a browser and how a future run can be diffed against the C
 * client's own emit list.
 *
 * ------------------------------------------------------------------
 * What the painter decides, and what it does not
 * ------------------------------------------------------------------
 *
 * The emit walk decided WHERE and IN WHAT ORDER. The painter decides WHAT a
 * command means — which sprite frame a hovered graphic shows, how a colour
 * becomes an rgb, where a glyph goes. Keeping the split there is what lets the
 * emit list be compared against the C client's without a renderer in the way.
 */

import { EMIT_KIND, effectiveText, variantOf } from './emit.js';

/** Transparency is 0 (opaque) to 255 (invisible), as the cache means it. */
function alphaOf(trans) {
    const t = Math.max(0, Math.min(255, trans | 0));
    return (255 - t) / 255;
}

/** A 24-bit cache colour as CSS. -1 means "no colour", which draws nothing. */
export function colourToCss(colour) {
    const value = colour | 0;
    if( value < 0 ) return null;
    return `#${(value & 0xffffff).toString(16).padStart(6, '0')}`;
}

export function createPainter(options = {}) {
    return new Painter(options);
}

export class Painter {
    constructor({ surface, sprites = null, fonts = null, models = null } = {}) {
        if( !surface ) throw new TypeError('a painter needs a drawing surface');
        this.surface = surface;
        this.sprites = sprites;
        this.fonts = fonts;
        this.models = models;
        this.stats = { frames: 0, commands: 0, skipped: 0, missingAssets: 0 };
        /** Assets a frame wanted and did not have, for the caller to load. */
        this.wanted = { sprites: new Set(), fonts: new Set(), models: new Set() };
    }

    /**
     * Paint one emit list.
     *
     * Whole-surface repaint, which is the client's own model: at 765x503 the
     * clear costs less than tracking damage would, and per-clip partial
     * repaint is a later optimisation that has to be justified by a profile
     * rather than assumed.
     */
    paint(commands, { width, height, background = null } = {}) {
        this.stats.frames++;
        const surface = this.surface;
        surface.begin?.(width, height);

        /*
         * The world viewport is NOT cleared to black.
         *
         * The C client leaves it alone deliberately — clearing it is how the
         * 3D scene gets a one-frame flash of background between the clear and
         * the world draw. A caller that wants a cleared surface says so.
         */
        if( background !== null ) surface.fillRect(0, 0, width, height, background, 1);

        for( const command of commands )
        {
            this.stats.commands++;
            if( command.clip.width <= 0 || command.clip.height <= 0 )
            {
                this.stats.skipped++;
                continue;
            }
            surface.save();
            surface.clip(command.clip.x, command.clip.y, command.clip.width, command.clip.height);
            this._paintOne(command);
            surface.restore();
        }

        surface.end?.();
    }

    _paintOne(command) {
        switch( command.kind )
        {
        case EMIT_KIND.RECT: return this._rect(command);
        case EMIT_KIND.TEXT: return this._text(command);
        case EMIT_KIND.SPRITE: return this._sprite(command);
        case EMIT_KIND.LINE: return this._line(command);
        case EMIT_KIND.MODEL: return this._model(command);
        default: return undefined;
        }
    }

    /* --------------------------------------------------------------
     * Shapes
     * ----------------------------------------------------------- */

    _rect(command) {
        const colour = colourToCss(this._variant(command, 'colour'));
        if( colour === null ) return;
        const alpha = alphaOf(command.trans);
        const { x, y, width, height } = command;
        /* `filled` decides fill versus a one-pixel outline, and the outline is
         * four fills rather than a stroke: a stroke straddles the path and
         * lands on half-pixels, which is a different image. */
        if( command.props.filled )
        {
            this.surface.fillRect(x, y, width, height, colour, alpha);
            return;
        }
        this.surface.fillRect(x, y, width, 1, colour, alpha);
        this.surface.fillRect(x, y + height - 1, width, 1, colour, alpha);
        this.surface.fillRect(x, y, 1, height, colour, alpha);
        this.surface.fillRect(x + width - 1, y, 1, height, colour, alpha);
    }

    _line(command) {
        const colour = colourToCss(this._variant(command, 'colour'));
        if( colour === null ) return;
        const alpha = alphaOf(command.trans);
        const thickness = Math.max(1, command.props.lineWidth | 0);
        /* The cache's LINE is axis-aligned: `lineDirection` picks which. */
        if( command.props.lineDirection )
            this.surface.fillRect(command.x, command.y, thickness, command.height, colour, alpha);
        else
            this.surface.fillRect(command.x, command.y, command.width, thickness, colour, alpha);
    }

    /* --------------------------------------------------------------
     * Sprites
     * ----------------------------------------------------------- */

    _sprite(command) {
        const id = this._variant(command, 'sprite', 'spriteOver');
        if( (id | 0) < 0 ) return;
        const image = this.sprites?.get(id);
        if( !image )
        {
            /* Record it and draw nothing. A placeholder would be a lie that
             * looks like a design decision; an empty box is visibly absent. */
            this.wanted.sprites.add(id | 0);
            this.stats.missingAssets++;
            return;
        }
        const alpha = alphaOf(command.trans);
        if( command.props.tiled )
        {
            this._tile(image, command, alpha);
            return;
        }
        const options = {
            flipH: !!command.props.flipH,
            flipV: !!command.props.flipV,
            /* IF3 sprite angle is 65536 to a full turn — NOT the 2048-per-turn
             * scale the compass and minimap use. Mixing them is a silent 32x. */
            angle: command.props.spriteAngle | 0,
        };
        /*
         * AN IF3 GRAPHIC IS STRETCHED TO ITS BOX. A cache one is not.
         *
         * `soft3d_draw_sprite` branches on the component's `if3` flag: an if3
         * sprite is composed onto its nominal canvas and that canvas is
         * scaled to the laid-out width and height, while a non-if3 sprite is
         * blitted at the sprite's own size and only OFFSET by the crop.
         *
         * Drawing everything at native size is not a subtle difference. The
         * bank's scrollbar track is a 16x347 box over a short sprite, and it
         * painted as a stub under the up arrow with 340 empty pixels beneath
         * — which reads as a broken scrollbar, not as a missing scale.
         */
        if( command.props.if3 )
        {
            const width = command.width > 0 ? command.width : image.width;
            const height = command.height > 0 ? command.height : image.height;
            /* The offset scales WITH the box: it is a position inside the
             * nominal canvas, not a margin in destination pixels. */
            const scaleX = image.width > 0 ? width / image.width : 1;
            const scaleY = image.height > 0 ? height / image.height : 1;
            const bitmap = image.bitmap ?? image;
            this.surface.drawImage(image,
                command.x + (image.offsetX | 0) * scaleX,
                command.y + (image.offsetY | 0) * scaleY, alpha, {
                ...options,
                width: (bitmap.width ?? image.width) * scaleX,
                height: (bitmap.height ?? image.height) * scaleY,
            });
            return;
        }
        /*
         * The bitmap sits at (offsetX, offsetY) INSIDE the sprite's canvas,
         * and the widget is positioned against the canvas. Drawing the bitmap
         * at the widget's origin instead puts every trimmed icon and every
         * offset glyph wrong — the content tree's own sprite 0 is a 26x18
         * image at (7, 11) on a 40x40 canvas.
         */
        this.surface.drawImage(image,
            command.x + (image.offsetX | 0), command.y + (image.offsetY | 0), alpha, options);
    }

    /**
     * Repeat a sprite to fill its box.
     *
     * The step is the CANVAS size, not the bitmap's: a tile trimmed of its
     * transparent margin would otherwise repeat every few pixels and the
     * pattern would close up.
     */
    _tile(image, command, alpha) {
        const stepX = image.width;
        const stepY = image.height;
        if( stepX <= 0 || stepY <= 0 ) return;
        for( let y = command.y; y < command.y + command.height; y += stepY )
            for( let x = command.x; x < command.x + command.width; x += stepX )
                this.surface.drawImage(image,
                    x + (image.offsetX | 0), y + (image.offsetY | 0), alpha, {});
    }

    /* --------------------------------------------------------------
     * Text
     * ----------------------------------------------------------- */

    _text(command) {
        const text = effectiveText(command.props, command.hovered);
        if( text === undefined || text === null || text === '' ) return;
        const fontId = command.props.font | 0;
        const font = this.fonts?.get(fontId);
        if( !font )
        {
            this.wanted.fonts.add(fontId);
            this.stats.missingAssets++;
            return;
        }
        /*
         * The FONT does the layout, onto the same surface everything else
         * draws to. Delegating to a `surface.text` hook instead would give the
         * canvas surface a private path that no recording surface — and so no
         * comparison against the C client's own draw list — could see.
         */
        font.draw(this.surface, String(text), {
            x: command.x, y: command.y, width: command.width, height: command.height,
            colour: colourToCss(this._variant(command, 'colour')),
            alpha: alphaOf(command.trans),
            halign: command.props.halign | 0,
            valign: command.props.valign | 0,
            lineHeight: command.props.lineHeight | 0,
            shadowed: !!command.props.shadowed,
        });
    }

    /* --------------------------------------------------------------
     * Models
     * ----------------------------------------------------------- */

    /**
     * A model widget is a reserved box, composited from elsewhere.
     *
     * The 3D rasteriser is toridraw, in a worker, and it is the one WASM
     * dependency this design keeps — it is a raster kernel, not a VM, and
     * reimplementing it in JavaScript would buy nothing. The painter's job is
     * to say where its output goes.
     */
    _model(command) {
        const id = command.props.model | 0;
        if( id < 0 ) return;
        const surfaceForModel = this.models?.get(id, {
            width: command.width, height: command.height,
            zoom: command.props.modelZoom | 0,
            angleX: command.props.modelAngleX | 0,
            angleY: command.props.modelAngleY | 0,
            angleZ: command.props.modelAngleZ | 0,
            offsetX: command.props.modelOffsetX | 0,
            offsetY: command.props.modelOffsetY | 0,
            anim: command.props.modelAnim ?? -1,
        });
        if( !surfaceForModel )
        {
            this.wanted.models.add(id);
            this.stats.missingAssets++;
            return;
        }
        this.surface.drawImage(surfaceForModel, command.x, command.y,
            alphaOf(command.trans), {});
    }

    /* --------------------------------------------------------------
     * Hover variants
     * ----------------------------------------------------------- */

    /**
     * The hovered component swaps to its `over` colour, text or sprite.
     *
     * A widget declares both and the client picks at draw time — which is why
     * a hover change rebuilds the emit list even though nothing in the tree is
     * dirty. When no `over` variant is declared the base value is used, so a
     * component without one simply does not react.
     */
    _variant(command, base, over = `${base}Over`) {
        return variantOf(command.props, command.hovered, base, over);
    }
}

/**
 * A drawing surface that remembers instead of drawing.
 *
 * The painter's tests use this, and so can a differential run against the C
 * client: the call list is the thing to compare, and it is comparable without
 * a browser, a canvas or a pixel buffer.
 */
export function createRecordingSurface() {
    const calls = [];
    const clips = [];
    return {
        calls,
        begin(width, height) { calls.push(['begin', width, height]); },
        end() { calls.push(['end']); },
        save() { clips.push(clips[clips.length - 1] ?? null); },
        restore() { clips.pop(); },
        clip(x, y, width, height) {
            clips[clips.length - 1] = { x, y, width, height };
            calls.push(['clip', x, y, width, height]);
        },
        fillRect(x, y, width, height, colour, alpha) {
            calls.push(['fillRect', x, y, width, height, colour, alpha]);
        },
        drawImage(image, x, y, alpha, options) {
            calls.push(['drawImage', image.id ?? null, x, y, alpha, options]);
        },
    };
}

/**
 * A browser 2D context, behind the same contract.
 *
 * Kept here rather than in the painter so the painter never touches a browser
 * API and stays runnable in Node — which is what makes every test above
 * possible without a headless browser.
 */
export function createCanvasSurface(context) {
    return {
        /*
         * Clear in the CURRENT transform, do not reset it.
         *
         * `resize` sets a devicePixelRatio scale so the interface is laid out
         * in CSS pixels and drawn into a backing store that many times
         * larger. Resetting the transform here threw that away on the first
         * frame: every subsequent draw went in at 1:1, so on a 2x display the
         * whole interface painted into the top-left QUARTER of the canvas and
         * the browser then scaled the oversized store down to the element's
         * box -- a half-size picture in a large black field, with every
         * pointer coordinate landing at twice the distance it should.
         *
         * The width and height are the ROOT's, which under the scale covers
         * the store exactly.
         */
        begin(width, height) {
            context.clearRect(0, 0, width, height);
        },
        end() {},
        save() { context.save(); },
        restore() { context.restore(); },
        clip(x, y, width, height) {
            context.beginPath();
            context.rect(x, y, width, height);
            context.clip();
        },
        fillRect(x, y, width, height, colour, alpha) {
            context.globalAlpha = alpha;
            context.fillStyle = colour;
            context.fillRect(x, y, width, height);
        },
        drawImage(image, x, y, alpha, {
            flipH = false, flipV = false, angle = 0, tint = null,
            width = 0, height = 0,
        } = {}) {
            context.globalAlpha = alpha;
            const bitmap = image.bitmap ?? image;
            const drawWidth = width > 0 ? width : (bitmap.width ?? image.width);
            const drawHeight = height > 0 ? height : (bitmap.height ?? image.height);
            /*
             * A tinted glyph is drawn through a scratch canvas rather than
             * with a composite mode on the main one: `source-atop` would tint
             * everything already painted inside the current clip, not just
             * this glyph. A production path should pre-tint the atlas per
             * colour instead — the glyph set is small and the colours few.
             */
            if( tint ) { drawTinted(context, image, x, y, tint); return; }
            if( !flipH && !flipV && !angle )
            {
                context.drawImage(bitmap, x, y, drawWidth, drawHeight);
                return;
            }
            context.save();
            context.translate(x + drawWidth / 2, y + drawHeight / 2);
            /* 65536 to a full turn, the IF3 scale — see the note in _sprite. */
            if( angle ) context.rotate((angle / 65536) * Math.PI * 2);
            context.scale(flipH ? -1 : 1, flipV ? -1 : 1);
            context.drawImage(bitmap, -drawWidth / 2, -drawHeight / 2, drawWidth, drawHeight);
            context.restore();
        },
    };
}

/** One tinted blit, through a scratch canvas so the tint cannot leak. */
function drawTinted(context, image, x, y, tint) {
    const scratch = context.canvas.ownerDocument.createElement('canvas');
    scratch.width = image.width;
    scratch.height = image.height;
    const scratchContext = scratch.getContext('2d');
    scratchContext.drawImage(image.bitmap ?? image, 0, 0);
    scratchContext.globalCompositeOperation = 'source-in';
    scratchContext.fillStyle = tint;
    scratchContext.fillRect(0, 0, image.width, image.height);
    context.drawImage(scratch, x, y);
}
