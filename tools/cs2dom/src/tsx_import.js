/*
 * An imported interface as editable TSX, and the edits back again.
 *
 * The read-only view this replaces was honest but useless: you could look at a
 * cache interface as JSX and change nothing. Making it editable needs one
 * property that a pretty-printer does not have — **anything the vocabulary
 * does not model must survive the trip** — because an IF3 component has more
 * fields than any table will keep up with, and a prop-typed view silently
 * drops the rest.
 *
 * So a component's props come in two piles:
 *
 *   MODELLED   `x`, `colour`, `font` … named props with types, which is what
 *              makes the file worth editing at all;
 *   RAW        everything else, carried verbatim in a `raw` object and written
 *              back exactly as it came.
 *
 * A hook is neither. `onload=i:703,i:-2147483645,s:Kudos List,i:0` is a
 * BINDING — a script id and its arguments, including sentinel values like
 * `-2147483645` that the client substitutes at dispatch — so it becomes a
 * record, not a JavaScript closure. Turning it into a function would lose the
 * sentinels and invent a call that never happens.
 *
 * ------------------------------------------------------------------
 * The gate
 * ------------------------------------------------------------------
 *
 * Import then export with no edits must leave the record untouched — zero
 * changed blocks, not "an equivalent file". That is what makes an edit's diff
 * one line long, and it is why the exporter works by APPLYING PROPS to the
 * original record rather than by rebuilding a file from the tree.
 */

import { ELEMENTS, EVENTS, IF_TYPE } from './components.js';
import { parseIf } from './if_record.js';

/** Cache type number -> the element that presents it. */
const ELEMENT_BY_TYPE = new Map([
    [IF_TYPE.layer, 'Layer'],
    [IF_TYPE.rectangle, 'Rect'],
    [IF_TYPE.text, 'Text'],
    [IF_TYPE.graphic, 'Graphic'],
    [IF_TYPE.model, 'Model'],
    [IF_TYPE.tooltip, 'Text'],
    [IF_TYPE.line, 'Line'],
    [IF_TYPE.inv, 'Layer'],
]);

/** Field name -> the prop that owns it, per element. Built once. */
const PROP_BY_FIELD = buildFieldIndex();

function buildFieldIndex() {
    const index = new Map();
    for( const [element, schema] of Object.entries(ELEMENTS) )
    {
        const byField = new Map();
        for( const [prop, definition] of Object.entries(schema.props) )
            byField.set(definition.field, { prop, ...definition });
        index.set(element, byField);
    }
    return index;
}

/** Fields that are structure, not presentation: they never become props. */
const STRUCTURAL_FIELDS = new Set(['if3', 'type', 'layer']);

/** A hook field is `on<something>`; its value is a binding, not a value. */
const HOOK_FIELD = /^on[a-z]+$/;

export class TsxImportError extends Error {
    constructor(message) {
        super(message);
        this.name = 'TsxImportError';
    }
}

/* -------------------------------------------------------------------------
 * Import
 * ---------------------------------------------------------------------- */

/**
 * Read an `.if` into an editable component model.
 *
 * The model keeps a reference to the RECORD it came from, so an export can
 * write back into the same text rather than regenerating it.
 */
export function importInterface({ ifText, compack = null, name = 'interface' }) {
    const record = parseIf(ifText);
    const components = record.blocks.map((block) => readBlock(record, block, compack));
    return { name, record, components };
}

function readBlock(record, block, compack) {
    const type = Number(record.get(block.name, 'type') ?? 0);
    const element = ELEMENT_BY_TYPE.get(type) ?? 'Layer';
    const byField = PROP_BY_FIELD.get(element) ?? new Map();

    const props = {};
    const raw = {};
    const hooks = {};

    for( const [field, entries] of block.fields )
    {
        if( STRUCTURAL_FIELDS.has(field) ) continue;

        if( HOOK_FIELD.test(field) )
        {
            hooks[field] = entries.map((entry) => parseBinding(entry.value));
            continue;
        }

        const definition = byField.get(field);
        if( definition && entries.length === 1 )
        {
            props[definition.prop] = decodeValue(entries[0].value, definition);
            continue;
        }
        /*
         * Unmodelled, or repeated. Both go raw and both come back verbatim —
         * a repeated key is an op list the encoder reads all of, and picking
         * one would change the interface.
         */
        raw[field] = entries.length === 1 ? entries[0].value : entries.map((e) => e.value);
    }

    return {
        block: block.name,
        element,
        type,
        fileId: compack?.byName.get(block.name) ?? -1,
        /* `layer` is the parent link the cache stores as a component uid; it
         * is structure, so it stays out of props and is reported separately. */
        layer: numberOrNull(record.get(block.name, 'layer')),
        props, raw, hooks,
    };
}

/**
 * One hook value: `i:703,i:-2147483645,s:Kudos List,i:0`.
 *
 * The first argument is the script id; the rest are its arguments, tagged `i`
 * or `s`. The magic negatives are EVENT SENTINELS the client substitutes at
 * dispatch — `-2147483645` is `event_com` — so they are kept as the numbers
 * they are rather than resolved into anything.
 */
export function parseBinding(value) {
    const text = String(value ?? '');
    const parts = splitArguments(text);
    if( parts.length === 0 ) return null;
    const first = /^i:(-?\d+)$/.exec(parts[0]);
    /* Not a binding this parser understands. It still has to survive, so it
     * carries its own text and is written back unchanged. */
    if( !first ) return { raw: text, source: text };

    const binding = {
        scriptId: Number(first[1]),
        args: parts.slice(1).map((part) => {
            const match = /^([is]):([\s\S]*)$/.exec(part);
            if( !match ) return unescapeArgument(part);
            return match[1] === 'i' ? Number(match[2]) : unescapeArgument(match[2]);
        }),
        source: text,
    };
    /* A snapshot of what was read, so `formatBinding` can tell an EDITED
     * binding from an untouched one. See the note there. */
    binding.original = JSON.stringify([binding.scriptId, binding.args]);
    return binding;
}

/**
 * The inverse — but an UNTOUCHED binding is written back as its own text.
 *
 * Re-serialising every binding would be simpler and would rewrite two of the
 * tree's interfaces, because their hook strings carry an escape sequence this
 * parser reads differently than the encoder wrote it: `makeover_mage` holds
 * `...colour\\, body type...`, which decodes as an escaped BACKSLASH followed
 * by a real separator rather than as an escaped comma. Whether that is what
 * its author meant is not this tool's question; preserving it is.
 *
 * So the rule is the record's own, one level down: verbatim unless edited.
 */
export function formatBinding(binding) {
    if( !binding ) return '';
    if( binding.raw !== undefined ) return binding.raw;
    if( binding.source !== undefined
        && binding.original === JSON.stringify([binding.scriptId, binding.args]) )
        return binding.source;

    const parts = [`i:${binding.scriptId}`];
    for( const arg of binding.args ?? [] )
        parts.push(typeof arg === 'number' ? `i:${arg}` : `s:${escapeArgument(arg)}`);
    return parts.join(',');
}

/**
 * Split on commas the encoder did not escape.
 *
 * `cp_decode.c`'s `append_escaped_arg` puts a backslash before `,` and before
 * `\`, so a backslash consumes the character after it whatever that is —
 * which is why an escaped backslash leaves the following comma as a real
 * separator.
 */
function splitArguments(text) {
    const parts = [];
    let current = '';
    for( let i = 0; i < text.length; i++ )
    {
        const ch = text[i];
        if( ch === '\\' && i + 1 < text.length ) { current += ch + text[i + 1]; i++; continue; }
        if( ch === ',' ) { parts.push(current); current = ''; continue; }
        current += ch;
    }
    parts.push(current);
    return parts.map((part) => part.trim()).filter((part, index) => part !== '' || index === 0);
}

function unescapeArgument(text) {
    return text.replace(/\\(.)/g, '$1');
}

function escapeArgument(text) {
    return String(text).replace(/([\\,])/g, '\\$1');
}

function decodeValue(text, definition) {
    if( definition.type?.name === 'string' || definition.type === 'string' ) return text;
    if( definition.type?.name === 'bool' || definition.type === 'bool' )
        return text === 'yes' || text === 'true' || text === '1';
    const number = Number(text);
    return Number.isFinite(number) ? number : text;
}

function encodeValue(value, definition) {
    if( typeof value === 'boolean' )
    {
        /* `yes`/`no` is what cachepack's reader accepts for a flag; `true`
         * would be read as a number and land as zero. */
        return value ? 'yes' : 'no';
    }
    return String(value);
}

function numberOrNull(text) {
    if( text === null || text === undefined ) return null;
    const number = Number(text);
    return Number.isFinite(number) ? number : null;
}

/* -------------------------------------------------------------------------
 * TSX
 * ---------------------------------------------------------------------- */

/**
 * Write the model as TSX.
 *
 * Nesting follows the cache's `layer` links, which are component uids rather
 * than block names — so the tree is rebuilt through the compack. A component
 * whose parent is outside this interface is a root here, which is what it is.
 */
export function toTsx(model, { componentName = null } = {}) {
    const byUid = new Map();
    for( const component of model.components )
        if( component.fileId >= 0 ) byUid.set(component.fileId, component);

    const children = new Map();
    const roots = [];
    for( const component of model.components )
    {
        const parentFileId = component.layer === null ? -1 : component.layer & 0xffff;
        const parentGroup = component.layer === null ? -1 : (component.layer >>> 16) & 0xffff;
        const parent = parentGroup >= 0 ? byUid.get(parentFileId) : null;
        /* A parent in a different interface is a MOUNT, not a nesting: this
         * file describes one interface, so such a component is a root of it. */
        if( parent && parent !== component ) addChild(children, parent.block, component);
        else roots.push(component);
    }

    const name = componentName ?? toIdentifier(model.name);
    const lines = [
        `/* ${model.name} — imported from the cache.`,
        ' *',
        ' * Props the vocabulary models are named; everything else rides `raw` and',
        ' * is written back exactly as it came. Hooks are BINDINGS — a script id and',
        ' * its arguments, sentinels included — not callbacks.',
        ' */',
        "import { Layer, Rect, Text, Graphic, Model, Line } from 'cs2dom';",
        '',
        `export default function ${name}() {`,
        '    return (',
    ];
    for( const root of roots ) lines.push(...renderComponent(root, children, 2));
    lines.push('    );', '}', '');
    return lines.join('\n');
}

function addChild(children, parentBlock, component) {
    if( !children.has(parentBlock) ) children.set(parentBlock, []);
    children.get(parentBlock).push(component);
}

function renderComponent(component, children, depth) {
    const pad = '    '.repeat(depth);
    const kids = children.get(component.block) ?? [];
    const attributes = renderAttributes(component, depth + 1);

    if( kids.length === 0 )
        return attributes.length
            ? [`${pad}<${component.element}`, ...attributes, `${pad}/>`]
            : [`${pad}<${component.element} />`];

    const out = attributes.length
        ? [`${pad}<${component.element}`, ...attributes, `${pad}>`]
        : [`${pad}<${component.element}>`];
    for( const child of kids ) out.push(...renderComponent(child, children, depth + 1));
    out.push(`${pad}</${component.element}>`);
    return out;
}

function renderAttributes(component, depth) {
    const pad = '    '.repeat(depth);
    const out = [`${pad}id=${JSON.stringify(component.block)}`];

    for( const [prop, value] of Object.entries(component.props) )
        out.push(`${pad}${prop}={${JSON.stringify(value)}}`);

    for( const [field, bindings] of Object.entries(component.hooks) )
    {
        const event = EVENTS[field]?.prop ?? field;
        const rendered = bindings.length === 1
            ? JSON.stringify(bindings[0])
            : JSON.stringify(bindings);
        out.push(`${pad}${event}={${rendered}}`);
    }

    if( Object.keys(component.raw).length )
        out.push(`${pad}raw={${JSON.stringify(component.raw)}}`);
    return out;
}

function toIdentifier(name) {
    const cleaned = String(name).replace(/[^A-Za-z0-9]+(.)/g, (_, ch) => ch.toUpperCase());
    const capitalised = cleaned.charAt(0).toUpperCase() + cleaned.slice(1);
    return /^[A-Za-z]/.test(capitalised) ? capitalised : `Interface${capitalised}`;
}

/* -------------------------------------------------------------------------
 * Export
 * ---------------------------------------------------------------------- */

/**
 * Apply an edited model back onto its record.
 *
 * Writes through the ORIGINAL record, so an untouched field keeps its exact
 * line and an untouched block is re-emitted verbatim. Rebuilding the file from
 * the model instead would be simpler and would reformat all 968 records in the
 * tree, burying every real edit.
 *
 * Returns the blocks whose text actually changed.
 */
export function applyModel(model, edited = null) {
    const source = edited ?? model;
    const record = model.record;

    for( const component of source.components )
    {
        const block = record.block(component.block);
        if( !block ) throw new TsxImportError(`no block [${component.block}] to write back`);
        const byField = PROP_BY_FIELD.get(component.element) ?? new Map();

        for( const [prop, value] of Object.entries(component.props) )
        {
            const definition = findDefinition(byField, prop);
            if( !definition )
                throw new TsxImportError(
                    `[${component.block}] has no field for prop '${prop}'`);
            record.set(component.block, definition.field, encodeValue(value, definition));
        }

        for( const [field, value] of Object.entries(component.raw) )
        {
            /* A repeated key came in as an array and goes back as one; the
             * record's `set` handles a single value, so a list is only
             * rewritten when it changed. */
            if( Array.isArray(value) )
            {
                const current = record.getAll(component.block, field);
                if( current.length === value.length
                    && current.every((entry, index) => entry === value[index]) ) continue;
                throw new TsxImportError(
                    `[${component.block}] ${field} is a repeated key; editing one is not ` +
                    'supported — edit the .if directly');
            }
            record.set(component.block, field, value);
        }

        for( const [field, bindings] of Object.entries(component.hooks) )
        {
            const list = Array.isArray(bindings) ? bindings : [bindings];
            if( list.length === 1 )
            {
                record.set(component.block, field, formatBinding(list[0]));
                continue;
            }
            const current = record.getAll(component.block, field);
            const wanted = list.map(formatBinding);
            if( current.length !== wanted.length
                || current.some((entry, index) => entry !== wanted[index]) )
                throw new TsxImportError(
                    `[${component.block}] ${field} is repeated; editing one is not supported`);
        }
    }

    return { text: record.toText(), changed: record.changed() };
}

function findDefinition(byField, prop) {
    for( const definition of byField.values() ) if( definition.prop === prop ) return definition;
    return null;
}

export { PROP_BY_FIELD, ELEMENT_BY_TYPE };
