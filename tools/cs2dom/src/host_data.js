/* Cache-backed data needed by synchronous browser HOST requests.
 *
 * CS2 execution cannot await a fetch halfway through an opcode. The dev server
 * therefore makes the lookup tables that affect UI construction available to
 * the browser HOST: enums, inventory capacities, parameter/struct/object/
 * npc/loc/map-element records, and font advances.
 * Entity models/sprites remain regular lazy browser resources because rendering
 * them is asynchronous-safe.
 */

import { existsSync, readFileSync, readdirSync } from 'node:fs';
import { join } from 'node:path';

import { parseFontMetrics } from './font.js';
import { parseDbTextData } from './host_db.js';
import { parseWorldMapFiles } from './host_worldmap.js';

export const HOST_DATA_SCHEMA = 'cs2dom-host-data/1';

export function contentHostData(contentDir) {
    const configs = join(contentDir, 'configs');
    const objIds = parseSymbolIds(readOptional(join(configs, 'all.obj.compack')));
    const npcIds = parseSymbolIds(readOptional(join(configs, 'all.npc.compack')));
    const locIds = parseSymbolIds(readOptional(join(configs, 'all.loc.compack')));
    const inventoryIds = parseSymbolIds(readOptional(join(configs, 'all.inv.compack')));
    const varbitIds = parseSymbolIds(readOptional(join(configs, 'all.varbit.compack')));
    const varpIds = parseSymbolIds(readOptional(join(configs, 'all.varp.compack')));
    const mapElementIds = parseSymbolIds(
        readOptional(join(configs, 'all.mapelement.compack')));
    const paramIds = parseSymbolIds(readOptional(join(configs, 'all.param.compack')));
    const structIds = parseSymbolIds(readOptional(join(configs, 'all.struct.compack')));
    const db = parseDbTextData({
        tableText: readOptional(join(configs, 'all.dbtable')),
        rowText: readOptional(join(configs, 'all.dbrow')),
        tableCompackText: readOptional(join(configs, 'all.dbtable.compack')),
        rowCompackText: readOptional(join(configs, 'all.dbrow.compack')),
    });
    const mapElements = parseMapElements(
        readOptional(join(configs, 'all.mapelement')), mapElementIds);
    const worldMapDir = join(contentDir, 'worldmap', 'areas');
    const worldMap = parseWorldMapFiles(
        readOptional(join(worldMapDir, 'details.wma')),
        readOptional(join(worldMapDir, 'compositemap.wmc')),
        {
            detailsCompack: readOptional(join(worldMapDir, 'details.compack')),
            compositeCompack: readOptional(join(worldMapDir, 'compositemap.compack')),
            mapElements,
        });
    return {
        schema: HOST_DATA_SCHEMA,
        enums: parseEnums(
            readOptional(join(configs, 'all.enum')),
            readOptional(join(configs, 'all.enum.compack'))),
        fonts: readFonts(join(contentDir, 'fonts')),
        objects: parseObjects(readOptional(join(configs, 'all.obj')), objIds, paramIds),
        npcs: parseNpcs(readOptional(join(configs, 'all.npc')), npcIds, paramIds),
        locs: parseLocs(readOptional(join(configs, 'all.loc')), locIds, paramIds),
        inventoryTypes: parseInventoryTypes(
            readOptional(join(configs, 'all.inv')), inventoryIds),
        varbitVarp: parseVarbitVarps(
            readOptional(join(configs, 'all.varbit')), varbitIds, varpIds),
        mapElements,
        params: parseParams(readOptional(join(configs, 'all.param')), paramIds),
        structs: parseStructs(readOptional(join(configs, 'all.struct')), structIds, paramIds),
        dbTables: db.dbTables,
        dbRows: db.dbRows,
        /* Map-element configs already live above. Keep immutable map geometry
         * here and let HostRuntime own only the small changing cursor/zoom state. */
        worldMap: { areas: worldMap.areas },
    };
}

/** Immutable InvType capacities consumed by INV_SIZE/INVS_GET_SIZE. */
export function parseInventoryTypes(text, symbols = new Map()) {
    const ids = symbols instanceof Map ? symbols : parseSymbolIds(symbols);
    const result = {};
    let current = null;
    for( const line of configLines(text) ) {
        const opened = /^\[([^\]]+)\]$/.exec(line);
        if( opened ) {
            const id = configId(opened[1], ids, 'inv');
            current = id < 0 ? null : { size: 0 };
            if( current ) result[id] = current;
            continue;
        }
        if( !current ) continue;
        const match = /^size=(-?\d+)$/.exec(line);
        if( match ) current.size = Math.max(0, Number(match[1]));
    }
    return result;
}

/** Varbit id -> backing varp id, used by native var-transmit trigger filters. */
export function parseVarbitVarps(text, varbitSymbols = new Map(), varpSymbols = new Map()) {
    const varbits = varbitSymbols instanceof Map ? varbitSymbols : parseSymbolIds(varbitSymbols);
    const varps = varpSymbols instanceof Map ? varpSymbols : parseSymbolIds(varpSymbols);
    const result = {};
    let id = -1;
    for( const line of configLines(text) ) {
        const opened = /^\[([^\]]+)\]$/.exec(line);
        if( opened ) {
            id = configId(opened[1], varbits, 'varbit');
            continue;
        }
        if( id < 0 ) continue;
        const match = /^basevar=(.*)$/.exec(line);
        if( !match ) continue;
        const baseName = match[1].trim();
        const numeric = /^varp_(\d+)$/.exec(baseName);
        const varp = numeric ? Number(numeric[1]) : referenceId(baseName, varps);
        if( varp >= 0 ) result[id] = varp;
    }
    return result;
}

export function parseEnums(text, compack = '') {
    const result = {};
    const ids = compack instanceof Map ? compack : parseSymbolIds(compack);
    let current = null;
    for( const line of configLines(text) ) {
        if( !line ) continue;
        const opened = /^\[([^\]]+)\]$/.exec(line);
        if( opened ) {
            const id = configId(opened[1], ids, 'enum');
            current = id < 0 ? null
                : { string: false, defaultInt: 0, defaultString: 'null', values: {} };
            if( current ) result[id] = current;
            continue;
        }
        if( !current ) continue;
        if( line === 'outputstring=yes' ) { current.string = true; continue; }
        let match = /^default=(-?\d+)$/.exec(line);
        if( match ) { current.defaultInt = Number(match[1]); continue; }
        match = /^defaultstr=(.*)$/.exec(line);
        if( match ) { current.defaultString = match[1]; continue; }
        match = /^val=(-?\d+),(-?\d+)$/.exec(line);
        if( match ) { current.values[match[1]] = Number(match[2]); continue; }
        match = /^valstr=(-?\d+),(.*)$/.exec(line);
        if( match ) { current.string = true; current.values[match[1]] = match[2]; }
    }
    return result;
}

export function parseParams(text, symbols = new Map()) {
    const ids = symbols instanceof Map ? symbols : parseSymbolIds(symbols);
    const result = {};
    let current = null;
    for( const line of configLines(text) ) {
        const opened = /^\[([^\]]+)\]$/.exec(line);
        if( opened ) {
            const id = configId(opened[1], ids, 'param');
            current = id < 0 ? null
                : { string: false, defaultInt: 0, defaultString: '' };
            if( current ) result[id] = current;
            continue;
        }
        if( !current ) continue;
        let match = /^type=(.*)$/.exec(line);
        if( match ) { current.string = match[1].trim() === 's'; continue; }
        match = /^default=(-?\d+)$/.exec(line);
        if( match ) { current.defaultInt = Number(match[1]); continue; }
        match = /^defaultstr=(.*)$/.exec(line);
        if( match ) { current.string = true; current.defaultString = match[1]; }
    }
    return result;
}

export function parseStructs(text, symbols = new Map(), paramSymbols = new Map()) {
    const ids = symbols instanceof Map ? symbols : parseSymbolIds(symbols);
    const params = paramSymbols instanceof Map ? paramSymbols : parseSymbolIds(paramSymbols);
    const result = {};
    let current = null;
    for( const line of configLines(text) ) {
        const opened = /^\[([^\]]+)\]$/.exec(line);
        if( opened ) {
            const id = configId(opened[1], ids, 'struct');
            current = id < 0 ? null : { params: {} };
            if( current ) result[id] = current;
            continue;
        }
        if( !current || !line.startsWith('param=') ) continue;
        const entry = parseParamValue(line.slice(6), params);
        if( entry ) current.params[entry.id] = entry.value;
    }
    return result;
}

export function parseObjects(text, symbols = new Map(), paramSymbols = new Map()) {
    const ids = symbols instanceof Map ? symbols : parseSymbolIds(symbols);
    const params = paramSymbols instanceof Map ? paramSymbols : parseSymbolIds(paramSymbols);
    const result = {};
    let current = null;
    for( const line of configLines(text) ) {
        const opened = /^\[([^\]]+)\]$/.exec(line);
        if( opened ) {
            const id = configId(opened[1], ids, 'obj');
            current = id < 0 ? null : {
                name: 'null', cost: 0, stackable: 0, members: false,
                model: -1, zoom2d: 2000, xan2d: 0, yan2d: 0, zan2d: 0,
                offsetX2d: 0, offsetY2d: 0, countVariants: [],
            };
            if( current ) result[id] = current;
            continue;
        }
        if( !current ) continue;
        let match = /^name=(.*)$/.exec(line);
        if( match ) { current.name = match[1]; continue; }
        match = /^desc=(.*)$/.exec(line);
        if( match ) { current.examine = match[1]; continue; }
        match = /^cost=(-?\d+)$/.exec(line);
        if( match ) { current.cost = Number(match[1]); continue; }
        match = /^stackable=(.*)$/.exec(line);
        if( match ) { current.stackable = yes(match[1]) ? 1 : 0; continue; }
        match = /^members=(.*)$/.exec(line);
        if( match ) { current.members = yes(match[1]); continue; }
        match = /^(model|2dzoom|2dxan|2dyan|2dzan|2dxof|2dyof)=(-?\d+)$/.exec(line);
        if( match ) {
            const key = ({
                model: 'model', '2dzoom': 'zoom2d', '2dxan': 'xan2d',
                '2dyan': 'yan2d', '2dzan': 'zan2d', '2dxof': 'offsetX2d',
                '2dyof': 'offsetY2d',
            })[match[1]];
            current[key] = Number(match[2]);
            continue;
        }
        match = /^countobj(\d+)=(.+),(-?\d+)$/.exec(line);
        if( match ) {
            const slot = Number(match[1]);
            if( slot >= 1 && slot <= 10 ) current.countVariants[slot - 1] = {
                id: referenceId(match[2].trim(), ids), count: Number(match[3]),
            };
            continue;
        }
        match = /^(placeholderlink|placeholdertemplate|certlink|certtemplate)=(.*)$/.exec(line);
        if( match ) {
            const key = ({
                placeholderlink: 'placeholderLink', placeholdertemplate: 'placeholderTemplate',
                certlink: 'certLink', certtemplate: 'certTemplate',
            })[match[1]];
            current[key] = referenceId(match[2], ids);
            continue;
        }
        match = /^(wearpos|wearpos2|wearpos3)=(-?\d+)$/.exec(line);
        if( match ) { current[match[1]] = Number(match[2]); continue; }
        match = /^shiftclickdrop=(-?\d+)$/.exec(line);
        if( match ) { current.shiftClickDropIndex = Number(match[1]); continue; }
        match = /^(ifop|op)([1-5])=(.*)$/.exec(line);
        if( match ) {
            const key = match[1] === 'ifop' ? 'inventoryOps' : 'groundOps';
            current[key] ||= Array(5).fill(null);
            current[key][Number(match[2]) - 1] = match[3];
            continue;
        }
        if( line.startsWith('param=') ) {
            const entry = parseParamValue(line.slice(6), params);
            if( entry ) {
                current.params ||= {};
                current.params[entry.id] = entry.value;
            }
        }
    }
    return result;
}

/**
 * Keep only the part of an NPC config that synchronous CS2 reads: NC_NAME and
 * NC_PARAM. Empty records are omitted because all supported reads observe the
 * same values as an absent record ("null" and the ParamType default). This
 * matters for a full cache: most of the 16k records otherwise contribute an
 * object containing nothing useful to the browser payload.
 */
export function parseNpcs(text, symbols = new Map(), paramSymbols = new Map()) {
    return parseParamEntities(text, symbols, paramSymbols, 'npc', true);
}

/** LC_PARAM is the only loc-config read currently crossing this HOST. */
export function parseLocs(text, symbols = new Map(), paramSymbols = new Map()) {
    return parseParamEntities(text, symbols, paramSymbols, 'loc', false);
}

/** The four fields consumed by MEC_TEXT/TEXTSIZE/CATEGORY/SPRITE. */
export function parseMapElements(text, symbols = new Map()) {
    const ids = symbols instanceof Map ? symbols : parseSymbolIds(symbols);
    const result = {};
    let current = null;
    for( const line of configLines(text) ) {
        const opened = /^\[([^\]]+)\]$/.exec(line);
        if( opened ) {
            const id = configId(opened[1], ids, 'mapelement');
            current = id < 0 ? null : {
                name: '', textSize: 0, category: -1, sprite: -1,
            };
            if( current ) result[id] = current;
            continue;
        }
        if( !current ) continue;
        let match = /^name=(.*)$/.exec(line);
        if( match ) { current.name = match[1]; continue; }
        match = /^(textsize|category|sprite)=(-?\d+)$/.exec(line);
        if( !match ) continue;
        const key = match[1] === 'textsize' ? 'textSize' : match[1];
        current[key] = Number(match[2]);
    }
    return result;
}

export function parseSymbolIds(text) {
    const result = new Map();
    for( const line of configLines(text) ) {
        const match = /^(\d+)=([^=]+)$/.exec(line);
        if( match ) result.set(match[2].trim(), Number(match[1]));
    }
    return result;
}

function parseParamValue(value, ids) {
    const first = value.indexOf(',');
    const second = first < 0 ? -1 : value.indexOf(',', first + 1);
    if( first < 1 || second < 0 ) return null;
    const id = configId(value.slice(0, first).trim(), ids, 'param');
    if( id < 0 ) return null;
    const type = value.slice(first + 1, second).trim();
    const raw = value.slice(second + 1);
    return {
        id,
        value: type === 'str' ? { string: raw } : Number.parseInt(raw, 10) || 0,
    };
}

function parseParamEntities(text, symbols, paramSymbols, prefix, names) {
    const ids = symbols instanceof Map ? symbols : parseSymbolIds(symbols);
    const params = paramSymbols instanceof Map ? paramSymbols : parseSymbolIds(paramSymbols);
    const result = {};
    let current = null;
    for( const line of configLines(text) ) {
        const opened = /^\[([^\]]+)\]$/.exec(line);
        if( opened ) {
            const id = configId(opened[1], ids, prefix);
            current = id < 0 ? null : { id, record: names ? { name: 'null' } : {} };
            continue;
        }
        if( !current ) continue;
        const name = names ? /^name=(.*)$/.exec(line) : null;
        if( name ) {
            /* The C host turns an empty cached NPC name into its "null"
             * sentinel, exactly like a missing or negative NPC id. */
            current.record.name = name[1] || 'null';
            if( current.record.name !== 'null' ) result[current.id] = current.record;
            continue;
        }
        if( !line.startsWith('param=') ) continue;
        const entry = parseParamValue(line.slice(6), params);
        if( !entry ) continue;
        current.record.params ||= {};
        current.record.params[entry.id] = entry.value;
        result[current.id] = current.record;
    }
    return result;
}

function configId(name, symbols, prefix) {
    const numeric = new RegExp(`^${prefix}_(\\d+)$`).exec(name);
    if( numeric ) return Number(numeric[1]);
    return symbols.get(name) ?? -1;
}

function referenceId(value, symbols) {
    return /^-?\d+$/.test(value) ? Number(value) : symbols.get(value) ?? -1;
}

function yes(value) {
    return value === 'yes' || value === 'true' || Number(value) !== 0;
}

function configLines(text) {
    return String(text || '').split(/\r?\n/).map((raw) => raw.trim())
        /* `//` is data inside enum/object strings (notably https:// URLs). */
        .filter((line) => line && !line.startsWith('//'));
}

function readFonts(directory) {
    const result = {};
    if( !existsSync(directory) ) return result;
    for( const file of readdirSync(directory) ) {
        const match = /^font_(\d+)\.fm$/.exec(file);
        if( !match ) continue;
        const metrics = parseFontMetrics(readFileSync(join(directory, file), 'utf8'));
        result[match[1]] = { lineHeight: metrics.ascent, advances: metrics.advances };
    }
    return result;
}

function readOptional(path) {
    return existsSync(path) ? readFileSync(path, 'utf8') : '';
}
