/* Cache-backed data needed by synchronous browser HOST requests.
 *
 * CS2 execution cannot await a fetch halfway through an opcode. The dev server
 * therefore makes the lookup tables that affect UI construction available to
 * the browser HOST: enums, parameter/struct/object records, and font advances.
 * Entity models/sprites remain regular lazy browser resources because rendering
 * them is asynchronous-safe.
 */

import { existsSync, readFileSync, readdirSync } from 'node:fs';
import { join } from 'node:path';

import { parseFontMetrics } from './font.js';

export const HOST_DATA_SCHEMA = 'cs2dom-host-data/1';

export function contentHostData(contentDir) {
    const configs = join(contentDir, 'configs');
    const objIds = parseSymbolIds(readOptional(join(configs, 'all.obj.compack')));
    const paramIds = parseSymbolIds(readOptional(join(configs, 'all.param.compack')));
    const structIds = parseSymbolIds(readOptional(join(configs, 'all.struct.compack')));
    return {
        schema: HOST_DATA_SCHEMA,
        enums: parseEnums(
            readOptional(join(configs, 'all.enum')),
            readOptional(join(configs, 'all.enum.compack'))),
        fonts: readFonts(join(contentDir, 'fonts')),
        objects: parseObjects(readOptional(join(configs, 'all.obj')), objIds, paramIds),
        params: parseParams(readOptional(join(configs, 'all.param')), paramIds),
        structs: parseStructs(readOptional(join(configs, 'all.struct')), structIds, paramIds),
    };
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
