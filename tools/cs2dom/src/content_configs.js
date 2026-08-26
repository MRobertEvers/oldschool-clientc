/*
 * Reading the content tree's config tables into the ones a script reads.
 *
 * `configs/all.<type>` plus `all.<type>.compack` is the same data the C client
 * decodes from the cache, one decode earlier. A parity comparison needs it:
 * the reference client answered `inv_size`, `struct_param` and `oc_name` from
 * a real table, and a runtime answering the miss default instead disagrees
 * about layout for a reason that has nothing to do with layout.
 *
 * ------------------------------------------------------------------
 * A table can be present and still be empty
 * ------------------------------------------------------------------
 *
 * The `all.enum` checked into the content tree has a block per enum and not a
 * single value in any of them: it was written before cachepack could emit
 * them, and nothing has re-unpacked it since. That is indistinguishable from
 * "the cache has no enums" unless you look, so `notes` says which tables came
 * back empty and `overrides` lets a caller point one type at a fresher
 * unpack. What is NOT done is inventing a value — a fabricated enum entry
 * resolves to a real sprite or a real string and draws.
 *
 * ------------------------------------------------------------------
 * A param's type decides the SHAPE of its default
 * ------------------------------------------------------------------
 *
 * `type=s` is the only string type; every other letter — `o` obj, `g` graphic,
 * `n` npc, `d`... — is an int with a domain. Reading `defaultstr` for an int
 * param would hand a script the empty string where it expects a number, and
 * `paramValue` would then coerce it to zero: a real obj id.
 */

import { existsSync, readFileSync } from 'node:fs';
import { join } from 'node:path';

import { parseIf, parseCompack } from './if_record.js';
import { HostConfig } from './host_config.js';

/** The only string param type; everything else is an int with a domain. */
const STRING_TYPE = 's';

/**
 * Read the config tables.
 *
 * `overrides` maps a type name to a directory holding a fresher
 * `all.<type>`/`all.<type>.compack` pair — the enum table needs one against
 * the content tree as it stands.
 */
export function createContentConfigs(contentDir, { overrides = {} } = {}) {
    const dir = join(contentDir, 'configs');
    const where = (type) => overrides[type] ?? dir;
    const params = readParams(dir);

    const config = new HostConfig({
        params,
        structs: readTable(where('struct'), 'struct', (block, record) => ({
            params: readParamBlock(block, params, record.paramNames),
        }), { params: true }),
        /*
         * An ABSENT field is the decoder's default, not zero.
         *
         * The unpacker writes a field only when it differs from the default,
         * so `[coins]` carries no `cost=` at all — and reading that as 0 makes
         * every unpriced object free. The defaults here are
         * `dat2_config_obj.c`'s own: cost 1, name "null", members false.
         */
        objects: readTable(where('obj'), 'obj', (block, record, get) => ({
            name: get('name') ?? 'null',
            cost: get('cost') === undefined ? 1 : int(get('cost')),
            /* Any non-zero is stackable; `app.c` answers `stackable ? 1 : 0`
             * and the text keeps the decoder's 1-or-2. */
            stackable: get('stackable') !== undefined && int(get('stackable')) !== 0 ? 1 : 0,
            members: get('members') === 'yes' ? 1 : 0,
            /*
             * The bank placeholder pair, by NAME.
             *
             * `oc_unplaceholder($obj) ! $obj` is the client's "is this slot a
             * placeholder" test (script 278, `bankmain_drawitem`), and it
             * reads both fields: an ITEM states a link and no template, a
             * PLACEHOLDER states both. Storing the link without the template
             * makes every item look like a placeholder of itself.
             */
            placeholderLink: record.names.get(get('placeholderlink')) ?? 0,
            placeholderTemplate: record.names.get(get('placeholdertemplate')) ?? -1,
            /*
             * The NOTE pair, read exactly like the placeholder pair above: an
             * item states a link and no template, a note states both and its
             * link is the item. `oc_cert($obj) ! $obj` is how the client asks
             * "can this be noted", so both halves are needed — a link without
             * a template makes every item look like a note of itself.
             */
            certLink: record.names.get(get('certlink')) ?? 0,
            certTemplate: record.names.get(get('certtemplate')) ?? -1,
            examine: get('desc') ?? '',
            weight: int(get('weight')),
            /*
             * The 2D presentation, which a type-6 widget draws the obj WITH.
             *
             * `cc_setobject` on a MODEL node does not set an icon: the
             * reference builds the objtype's inventory model and then stamps
             * `xan2d`/`yan2d`/`zoom2d`/`yof2d` over whatever the CC_CREATE
             * defaults were, because an item drawn at its model's own
             * orientation and at zoom 100 is not the shape a player
             * recognises. The defaults are `dat2_config_obj.c`'s.
             */
            model: get('model') === undefined
                ? -1 : (record.names.get(get('model')) ?? int(get('model'))),
            zoom2d: get('2dzoom') === undefined ? 2000 : int(get('2dzoom')),
            xan2d: int(get('2dxan')),
            yan2d: int(get('2dyan')),
            zan2d: int(get('2dzan')),
            yof2d: int(get('2dyof')),
            /* Three equip slots; -1 is "not equippable", and 0 is a real
             * slot (the head), so an absent field cannot read as zero. */
            wearpos: get('wearpos') === undefined ? -1 : int(get('wearpos')),
            wearpos2: get('wearpos2') === undefined ? -1 : int(get('wearpos2')),
            wearpos3: get('wearpos3') === undefined ? -1 : int(get('wearpos3')),
            /*
             * -2, not -1, when the field is absent. The three states are
             * distinct: >= 0 names an op, -1 is "opted out", and -2 is
             * UNSTATED, which falls back to "Drop is the fifth inventory op".
             * Almost no obj states the field, so -2 is what actually drives
             * shift-click drop; defaulting to -1 turns it off game-wide.
             */
            shiftClickDrop: get('shiftclickdrop') === undefined
                ? -2 : int(get('shiftclickdrop')),
            ops: readOpList(block, 'op'),
            invOps: readOpList(block, 'ifop'),
            params: readParamBlock(block, params, record.paramNames),
        }), { params: true }),
        npcs: readTable(where('npc'), 'npc', (block, record, get) => ({
            name: get('name') ?? 'null',
            params: readParamBlock(block, params, record.paramNames),
        }), { params: true }),
        locs: readTable(where('loc'), 'loc', (block, record, get) => ({
            name: get('name') ?? 'null',
            params: readParamBlock(block, params, record.paramNames),
        }), { params: true }),
        /* `inv_size` reads a number or a `{ size }`; the table is the type's
         * capacity, never the live container's used length. */
        inventories: readTable(where('inv'), 'inv', (block, record, get) => ({ size: int(get('size')) })),
        enums: readEnums(where('enum')),
    });

    config.counts = Object.fromEntries(
        ['params', 'structs', 'objects', 'npcs', 'locs', 'inventories', 'enums']
            .map((kind) => [kind, Object.keys(config[kind]).length]));
    /*
     * A table that parsed but holds nothing usable is the failure worth
     * naming. `all.enum` is the live case: 3,988 blocks, no values in any of
     * them, and every `enum()` in every script quietly answering its default.
     */
    config.notes = {
        source: dir,
        overrides: { ...overrides },
        empty: Object.entries(config.counts)
            .filter(([, count]) => count === 0).map(([kind]) => kind),
        enumsWithoutValues: Object.values(config.enums)
            .filter((record) => Object.keys(record.values).length === 0).length,
    };
    return config;
}

/* -------------------------------------------------------------------------
 * One table
 * ---------------------------------------------------------------------- */

function readTable(dir, type, build, { params: wantParams = false } = {}) {
    const path = join(dir, `all.${type}`);
    const compackPath = `${path}.compack`;
    const table = {};
    if( !existsSync(path) || !existsSync(compackPath) ) return table;

    const record = parseIf(readFileSync(path, 'utf8'));
    const compack = parseCompack(readFileSync(compackPath, 'utf8'));
    const paramNames = wantParams ? readParamNames(dir) : null;

    for( const block of record.blocks )
    {
        const id = compack.byName.get(block.name);
        if( id === undefined ) continue;
        const get = (field) => block.fields.get(field)?.[0]?.value;
        table[String(id)] = build(block, { paramNames, names: compack.byName }, get);
    }
    return table;
}

/* -------------------------------------------------------------------------
 * Params
 * ---------------------------------------------------------------------- */

function readParams(dir) {
    const path = join(dir, 'all.param');
    const compackPath = `${path}.compack`;
    const table = {};
    if( !existsSync(path) || !existsSync(compackPath) ) return table;

    const record = parseIf(readFileSync(path, 'utf8'));
    const compack = parseCompack(readFileSync(compackPath, 'utf8'));

    for( const block of record.blocks )
    {
        const id = compack.byName.get(block.name);
        if( id === undefined ) continue;
        const get = (field) => block.fields.get(field)?.[0]?.value;
        const string = get('type') === STRING_TYPE;
        table[String(id)] = {
            string,
            defaultInt: string ? 0 : int(get('default')),
            defaultString: string ? (get('defaultstr') ?? '') : '',
        };
    }
    return table;
}

/* -------------------------------------------------------------------------
 * Enums
 * ---------------------------------------------------------------------- */

/**
 * One enum record: `{ values, string, defaultInt, defaultString }`.
 *
 * Three value spellings, one per wire opcode — `valstr` (5), `val` (6),
 * `vallong` (7) — and the spelling is what says whether "1" is the number or
 * the text. A reader that took the first comma-separated pair and guessed
 * would turn every string enum whose values look numeric into an int one.
 *
 * `outputstring` is not redundant with `outputtype`: a record with no opcode 2
 * has no output type at all and can still be a string enum, because the value
 * arrays decide the wire opcode.
 */
function readEnums(dir) {
    const path = join(dir, 'all.enum');
    const compackPath = `${path}.compack`;
    const table = {};
    if( !existsSync(path) || !existsSync(compackPath) ) return table;

    const record = parseIf(readFileSync(path, 'utf8'));
    const compack = parseCompack(readFileSync(compackPath, 'utf8'));

    for( const block of record.blocks )
    {
        const id = compack.byName.get(block.name);
        if( id === undefined ) continue;
        const get = (field) => block.fields.get(field)?.[0]?.value;
        const strings = block.fields.get('valstr') ?? [];
        const string = strings.length > 0 || get('outputstring') === 'yes'
            || get('outputtype') === 'string';

        const values = {};
        for( const entry of strings )
        {
            const split = entry.value.indexOf(',');
            if( split < 0 ) continue;
            /* Only the FIRST comma separates: a string value may hold commas
             * of its own, and splitting on all of them truncates the text. */
            values[entry.value.slice(0, split).trim()] = entry.value.slice(split + 1);
        }
        for( const field of ['val', 'vallong'] )
            for( const entry of block.fields.get(field) ?? [] )
            {
                const [key, value] = entry.value.split(',');
                if( value === undefined ) continue;
                values[String(int(key))] = int(value);
            }

        table[String(id)] = {
            values, string,
            defaultInt: int(get('default')),
            defaultString: get('defaultstr') ?? 'null',
        };
    }
    return table;
}

let paramNameCache = null;

/** `param_506` -> 506, for the `param=` lines that reference params by name. */
function readParamNames(dir) {
    if( paramNameCache && paramNameCache.dir === dir ) return paramNameCache.byName;
    const path = join(dir, 'all.param.compack');
    const byName = existsSync(path)
        ? parseCompack(readFileSync(path, 'utf8')).byName : new Map();
    paramNameCache = { dir, byName };
    return byName;
}

/**
 * The `param=<name>,<type>,<value>` lines of one record.
 *
 * Split on the FIRST TWO commas only: a string param's value may contain
 * commas of its own, and splitting on all of them truncates it at the first —
 * which shows up as a label that lost half its text rather than as an error.
 */
function readParamBlock(block, params, paramNames) {
    const entries = block.fields.get('param');
    if( !entries || !paramNames ) return undefined;

    const out = {};
    for( const entry of entries )
    {
        const first = entry.value.indexOf(',');
        if( first < 0 ) continue;
        const second = entry.value.indexOf(',', first + 1);
        if( second < 0 ) continue;
        const name = entry.value.slice(0, first).trim();
        const kind = entry.value.slice(first + 1, second).trim();
        const value = entry.value.slice(second + 1);
        const id = paramNames.get(name);
        if( id === undefined ) continue;
        out[String(id)] = kind === 'str' ? value : int(value);
    }
    return out;
}

/**
 * `op1=`..`op5=` / `ifop1=`..`ifop5=` as a dense 5-slot array.
 *
 * Indexed from ZERO here; the opcodes that read them are one-based and
 * convert at their own edge. Absent slots stay '' rather than being skipped —
 * `oc_op(obj, 3)` must answer for slot 3 whether or not slots 1 and 2 exist.
 */
function readOpList(block, prefix) {
    const ops = ['', '', '', '', ''];
    for( let slot = 1; slot <= 5; slot++ )
    {
        const entry = block.fields.get(`${prefix}${slot}`)?.[0];
        if( entry ) ops[slot - 1] = entry.value;
    }
    return ops;
}

function int(value) {
    const n = Number(value);
    return Number.isFinite(n) ? n | 0 : 0;
}
