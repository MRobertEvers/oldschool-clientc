/*
 * Pure JavaScript data/runtime model for the C client's DB_* HOST requests.
 *
 * The C CS2VM remains responsible for opcode decoding and popping the dynamic
 * find value. This module consumes those explicit reflected fields and mirrors
 * exec_db's row/table fallback, typed results and stateful find iterator.
 */

import { packName } from './pack.js';

const DB_NAMES = Object.freeze([
    'DB_FIND_WITH_COUNT',
    'DB_FINDNEXT',
    'DB_GETFIELD',
    'DB_GETFIELDCOUNT',
    'DB_FINDALL_WITH_COUNT',
    'DB_GETROWTABLE',
    'DB_GETROW',
    'DB_FIND_FILTER_WITH_COUNT',
    'DB_FIND',
    'DB_FINDALL',
    'DB_FIND_FILTER',
]);

function immutableSet(values) {
    const target = new Set(values);
    const immutable = () => {
        throw new TypeError('request-name sets are immutable');
    };
    return Object.freeze(new Proxy(target, {
        get(set, property) {
            if( property === 'add' || property === 'delete' || property === 'clear' )
                return immutable;
            const value = Reflect.get(set, property, set);
            return typeof value === 'function' ? value.bind(set) : value;
        },
    }));
}

/** Exact DB request names emitted by the generated C/WASM bridge. */
export const DB_REQUEST_NAMES = immutableSet(DB_NAMES);

const DB_DATA = Symbol('cs2dom.db.data');
const DB_STATE = Symbol('cs2dom.db.state');

function brand(value, symbol) {
    Object.defineProperty(value, symbol, { value: true });
    return value;
}

function int32(value, fallback = 0) {
    const number = Number(value);
    return Number.isFinite(number) ? Math.trunc(number) | 0 : fallback | 0;
}

function id(value) {
    const number = Number(value);
    return Number.isSafeInteger(number) && number >= 0 ? number : null;
}

function requestName(value) {
    return String(value ?? '').trim().toUpperCase();
}

function isStringType(type) {
    const normalized = String(type ?? '').toLowerCase();
    /* 36 is ScriptVarType's legacy DB base code for string. Current
     * all.dbtable files spell it `string`, but cachepack still accepts 36. */
    return normalized === 'string' || normalized === '36';
}

function typePattern(types) {
    return types.map((type) => isStringType(type) ? 's' : 'i').join('');
}

function typedDefault(type) {
    return isStringType(type) ? '' : -1;
}

function entriesOf(value) {
    if( value instanceof Map ) return Array.from(value.entries());
    if( Array.isArray(value) ) return value.map((entry, index) => [index, entry]);
    if( value && typeof value === 'object' ) return Object.entries(value);
    return [];
}

function orderedEntries(value) {
    return entriesOf(value).sort((left, right) => {
        const leftId = id(left[1]?.id ?? left[0]);
        const rightId = id(right[1]?.id ?? right[0]);
        return (leftId ?? Number.MAX_SAFE_INTEGER) - (rightId ?? Number.MAX_SAFE_INTEGER);
    });
}

function scalarValue(type, value) {
    if( isStringType(type) ) {
        if( value && typeof value === 'object' )
            value = value.string ?? value.string_value ?? value.value ?? '';
        return String(value ?? '');
    }
    if( value && typeof value === 'object' )
        value = value.int ?? value.int_value ?? value.value ?? -1;
    return int32(value, -1);
}

function normalizeTuples(value, types) {
    let tuples;
    if( Array.isArray(value) ) {
        if( value.length === 0 ) tuples = [];
        else if( Array.isArray(value[0]) ) tuples = value;
        else if( types.length <= 1 ) tuples = value.map((field) => [field]);
        else {
            tuples = [];
            for( let at = 0; at < value.length; at += types.length )
                tuples.push(value.slice(at, at + types.length));
        }
    } else {
        tuples = orderedEntries(value).map((entry) => entry[1]);
    }
    return tuples.map((tuple) => {
        const fields = Array.isArray(tuple) ? tuple : [tuple];
        return types.map((type, index) =>
            index < fields.length ? scalarValue(type, fields[index]) : typedDefault(type));
    });
}

function normalizeColumns(value, table) {
    const columns = {};
    for( const [key, candidate] of orderedEntries(value) ) {
        if( !candidate || typeof candidate !== 'object' || candidate.present === false ) continue;
        const columnId = id(candidate.id ?? candidate.columnId ?? candidate.column_id ?? key);
        if( columnId === null || columnId > 0xff ) continue;
        const types = Array.isArray(candidate.types)
            ? candidate.types.map((type) => String(type)) : [];
        const source = table
            ? candidate.defaults ?? candidate.values
            : candidate.values;
        const values = normalizeTuples(source, types);
        const explicitCount = id(candidate.tupleCount ?? candidate.tuple_count);
        columns[columnId] = {
            id: columnId,
            name: String(candidate.name ?? ''),
            types,
            values,
            tupleCount: explicitCount ?? values.length,
        };
    }
    return columns;
}

/**
 * Normalize host_data's `{dbTables, dbRows}` payload. Columns use tuple arrays:
 *
 *   dbTables[id].columns[col] = { types, values/defaults }
 *   dbRows[id] = { tableId, columns: { [col]: { types, values } } }
 */
export function normalizeDbData(payload = {}) {
    if( payload?.[DB_DATA] === true ) return payload;
    if( !payload || typeof payload !== 'object' ) payload = {};
    const dbTables = {};
    const dbRows = {};

    for( const [key, candidate] of orderedEntries(payload.dbTables ?? payload.tables) ) {
        if( !candidate || typeof candidate !== 'object' ) continue;
        const tableId = id(candidate.id ?? candidate.tableId ?? candidate.table_id ?? key);
        if( tableId === null ) continue;
        dbTables[tableId] = {
            id: tableId,
            name: String(candidate.name ?? ''),
            columnCount: id(candidate.columnCount ?? candidate.column_count) ?? 0,
            columns: normalizeColumns(candidate.columns, true),
        };
    }
    for( const [key, candidate] of orderedEntries(payload.dbRows ?? payload.rows) ) {
        if( !candidate || typeof candidate !== 'object' ) continue;
        const rowId = id(candidate.id ?? candidate.rowId ?? candidate.row_id ?? key);
        if( rowId === null ) continue;
        dbRows[rowId] = {
            id: rowId,
            name: String(candidate.name ?? ''),
            tableId: int32(candidate.tableId ?? candidate.table_id, -1),
            columnCount: id(candidate.columnCount ?? candidate.column_count) ?? 0,
            columns: normalizeColumns(candidate.columns, false),
        };
    }
    return brand({ dbTables, dbRows }, DB_DATA);
}

/** Decode `(table << 12) | (column << 4) | selector` exactly as exec_db. */
export function decodeDbColumn(packed) {
    const value = int32(packed);
    return {
        tableId: (value >>> 12) & 0xfffff,
        columnId: (value >>> 4) & 0xff,
        /* Low nibble 0 means the whole tuple; N selects field N - 1. */
        tupleIndex: (value & 0xf) - 1,
    };
}

/** Convenience inverse used by parsers/tests and authored React hosts. */
export function encodeDbColumn(tableId, columnId, tupleIndex = -1) {
    const selector = tupleIndex < 0 ? 0 : (int32(tupleIndex) + 1) & 0xf;
    return ((int32(tableId) & 0xfffff) << 12) |
        ((int32(columnId) & 0xff) << 4) | selector;
}

/** Create a stateful DB find iterator over normalized tables and rows. */
export function createDbState(seed = {}) {
    const source = seed?.data && typeof seed.data === 'object' ? seed.data : seed;
    const data = normalizeDbData(source);
    const tableRows = {};
    for( const row of Object.values(data.dbRows) ) {
        if( row.tableId < 0 ) continue;
        (tableRows[row.tableId] ??= []).push(row.id);
    }
    for( const rows of Object.values(tableRows) ) rows.sort((left, right) => left - right);

    let iteratorRows = [];
    const seededIterator = seed?.iterator && typeof seed.iterator === 'object'
        ? seed.iterator : {};
    if( Array.isArray(seededIterator.rows) )
        iteratorRows = seededIterator.rows.map((row) => id(row)).filter((row) => row !== null);
    const cursor = Math.min(iteratorRows.length,
        Math.max(0, int32(seededIterator.cursor)));
    return brand({ data, tableRows, iterator: { rows: iteratorRows, cursor } }, DB_STATE);
}

/** Detach data and iterator state for persistence or a React state update. */
export function snapshotDbState(state) {
    assertDbState(state);
    return {
        data: JSON.parse(JSON.stringify(state.data)),
        iterator: { rows: [...state.iterator.rows], cursor: state.iterator.cursor },
    };
}

function assertDbState(state) {
    if( !state || state[DB_STATE] !== true ) throw new TypeError('DB state is invalid');
}

function setIterator(state, rows) {
    state.iterator.rows = [...rows];
    state.iterator.cursor = 0;
}

function columnRecord(state, row, tableId, columnId) {
    const rowColumn = row?.columns?.[columnId];
    if( rowColumn ) return rowColumn;
    return state.data.dbTables[tableId]?.columns?.[columnId] ?? null;
}

function selectedTypes(column, tupleIndex) {
    if( !column || column.types.length === 0 ) return ['int'];
    if( tupleIndex >= 0 && tupleIndex < column.types.length ) return [column.types[tupleIndex]];
    return column.types;
}

function missingField(column, tupleIndex) {
    if( !column ) return { pattern: 'i', values: [-1] };
    const types = selectedTypes(column, tupleIndex);
    return { pattern: typePattern(types), values: types.map(typedDefault) };
}

function getField(state, request) {
    const rowId = int32(request.rowId ?? request.row_id, -1);
    const row = rowId >= 0 ? state.data.dbRows[rowId] ?? null : null;
    const decoded = decodeDbColumn(request.column);

    /* Native exec_db does not chase a table default when the row itself is
     * absent: doing so would make its two async loads ping-pong. */
    if( !row ) return { pattern: 'i', values: [-1] };
    const column = columnRecord(state, row, decoded.tableId, decoded.columnId);
    if( !column ) return { pattern: 'i', values: [-1] };

    const index = int32(request.index, -1);
    if( column.types.length <= 0 || index < 0 || index >= column.tupleCount ||
        index >= column.values.length ) return missingField(column, decoded.tupleIndex);
    const tuple = column.values[index];
    if( decoded.tupleIndex >= 0 && decoded.tupleIndex < column.types.length ) {
        const type = column.types[decoded.tupleIndex];
        return {
            pattern: isStringType(type) ? 's' : 'i',
            values: [tuple[decoded.tupleIndex] ?? typedDefault(type)],
        };
    }
    return {
        pattern: typePattern(column.types),
        values: column.types.map((type, field) => tuple[field] ?? typedDefault(type)),
    };
}

function findRows(state, packedColumn, typeTag, requestedValue) {
    const decoded = decodeDbColumn(packedColumn);
    const table = state.data.dbTables[decoded.tableId] ?? null;
    const tableColumn = table?.columns?.[decoded.columnId] ?? null;
    const rows = state.tableRows[decoded.tableId] ?? [];
    let tupleSize = tableColumn?.types?.length ?? 0;
    if( tupleSize <= 0 ) {
        for( const rowId of rows ) {
            const length = state.data.dbRows[rowId]?.columns?.[decoded.columnId]?.types?.length ?? 0;
            if( length > tupleSize ) tupleSize = length;
        }
    }
    if( tupleSize <= 0 ) return [];

    const first = decoded.tupleIndex >= 0 && decoded.tupleIndex < tupleSize
        ? decoded.tupleIndex : 0;
    const last = decoded.tupleIndex >= 0 && decoded.tupleIndex < tupleSize
        ? decoded.tupleIndex : tupleSize - 1;
    const wantsString = int32(typeTag) === 2;
    const wanted = wantsString ? String(requestedValue ?? '') : int32(requestedValue);

    /* The native inverted index returns the first tuple-position entry that
     * matches a whole-column search; it does not union later positions. */
    for( let position = first; position <= last; position++ ) {
        const found = [];
        for( const rowId of rows ) {
            /*
             * A row that STATES nothing for the column is still a row with
             * that column's DEFAULT value, and the cache's inverted index
             * indexes it under that value.
             *
             * The music list is the case: `db_find_with_count(music:hidden, 0)`
             * asks for every unhidden track, `hidden` is a boolean defaulting
             * to 0, and no track states it — so scanning the rows alone found
             * nothing and the panel built 0 of its 857 rows. Nine other list
             * panels (quests, hiscores, minigames, the recipe books) are the
             * same shape.
             */
            const column = state.data.dbRows[rowId]?.columns?.[decoded.columnId]
                ?? tableColumn;
            if( !column || position >= column.types.length ) continue;
            const fieldIsString = isStringType(column.types[position]);
            if( fieldIsString !== wantsString ) continue;
            let matched = false;
            for( const tuple of column.values ) {
                if( tuple[position] === wanted ) {
                    matched = true;
                    break;
                }
            }
            if( matched ) found.push(rowId);
        }
        if( found.length > 0 ) return found;
    }
    return [];
}

/**
 * Execute one DB request. The WASM adapter supplies already-popped explicit
 * fields: `rowId`, `column`, `index`, `tableId`, `typeTag`, and `value`.
 * DB_GETFIELD returns `{pattern, values}`; all other requests return a scalar
 * or `null` for the native void forms.
 */
export function handleDbRequest(state, kind, request = {}) {
    assertDbState(state);
    const name = requestName(kind);
    if( !DB_REQUEST_NAMES.has(name) )
        throw new RangeError(`unknown DB HOST request ${name || '(empty)'}`);

    switch( name ) {
    case 'DB_FINDNEXT':
        return state.iterator.cursor < state.iterator.rows.length
            ? state.iterator.rows[state.iterator.cursor++] : -1;
    case 'DB_GETROW': {
        const index = int32(request.index, -1);
        return index >= 0 && index < state.iterator.rows.length ? state.iterator.rows[index] : -1;
    }
    case 'DB_GETROWTABLE': {
        const rowId = int32(request.rowId ?? request.row_id, -1);
        return rowId >= 0 ? state.data.dbRows[rowId]?.tableId ?? -1 : -1;
    }
    case 'DB_GETFIELDCOUNT': {
        const rowId = int32(request.rowId ?? request.row_id, -1);
        const row = rowId >= 0 ? state.data.dbRows[rowId] ?? null : null;
        if( !row ) return 0;
        const decoded = decodeDbColumn(request.column);
        return columnRecord(state, row, decoded.tableId, decoded.columnId)?.tupleCount ?? 0;
    }
    case 'DB_GETFIELD':
        return getField(state, request);
    case 'DB_FINDALL':
    case 'DB_FINDALL_WITH_COUNT': {
        const tableId = int32(request.tableId ?? request.table_id, -1);
        setIterator(state, tableId >= 0 ? state.tableRows[tableId] ?? [] : []);
        return name === 'DB_FINDALL_WITH_COUNT' ? state.iterator.rows.length : null;
    }
    case 'DB_FIND':
    case 'DB_FIND_WITH_COUNT':
    case 'DB_FIND_FILTER':
    case 'DB_FIND_FILTER_WITH_COUNT': {
        const filter = name === 'DB_FIND_FILTER' || name === 'DB_FIND_FILTER_WITH_COUNT';
        const prior = filter ? [...state.iterator.rows] : null;
        const matches = findRows(state, request.column, request.typeTag ?? request.type_tag,
            request.value);
        if( filter ) {
            const matching = new Set(matches);
            setIterator(state, prior.filter((rowId) => matching.has(rowId)));
        } else {
            setIterator(state, matches);
        }
        return name === 'DB_FIND_WITH_COUNT' || name === 'DB_FIND_FILTER_WITH_COUNT'
            ? state.iterator.rows.length : null;
    }
    default:
        throw new RangeError(`unhandled DB HOST request ${name}`);
    }
}

/* ----------------------------------------------------------------------
 * cachepack all.dbtable / all.dbrow parser
 * ------------------------------------------------------------------- */

/** Parse an `id=name` .compack into JSON-safe lookup objects. */
export function parseDbCompack(text) {
    const byId = {};
    const byName = {};
    for( const rawLine of String(text ?? '').split(/\r?\n/) ) {
        const line = rawLine.trim();
        if( !line || line.startsWith('//') ) continue;
        const equals = line.indexOf('=');
        if( equals <= 0 ) continue;
        const recordId = id(line.slice(0, equals).trim());
        const name = packName(line.slice(equals + 1));
        if( recordId === null || !name ) continue;
        byId[recordId] = name;
        byName[name] = recordId;
    }
    return { byId, byName };
}

function parseBlocks(text) {
    const blocks = [];
    let block = null;
    for( const rawLine of String(text ?? '').split(/\r?\n/) ) {
        const trimmed = rawLine.trim();
        if( !trimmed || trimmed.startsWith('//') ) continue;
        const header = /^\[([^\]]+)\]$/.exec(trimmed);
        if( header ) {
            block = { name: header[1], lines: [] };
            blocks.push(block);
            continue;
        }
        if( !block ) continue;
        const equals = rawLine.indexOf('=');
        if( equals < 0 ) continue;
        block.lines.push({
            key: rawLine.slice(0, equals).trim(),
            /* String fields may intentionally end in a space. cachepack keeps
             * everything after '=', so do not trim the value half. */
            value: rawLine.slice(equals + 1),
        });
    }
    return blocks;
}

/* Undo cachepack's outer config escaping. The inner DB tuple escaping is left
 * intact for splitEscaped(), exactly like cp_unescape + split_escaped in C. */
function unescapeOuter(value) {
    let result = '';
    for( let index = 0; index < value.length; index++ ) {
        if( value[index] !== '\\' || index + 1 >= value.length ) {
            result += value[index];
            continue;
        }
        const escaped = value[++index];
        if( escaped === 'n' ) result += '\n';
        else if( escaped === 'r' ) result += '\r';
        else result += escaped;
    }
    return result;
}

function splitEscaped(value) {
    const fields = [];
    let field = '';
    for( let index = 0; index < value.length; index++ ) {
        if( value[index] === '\\' && index + 1 < value.length ) {
            field += value[++index];
        } else if( value[index] === ',' ) {
            fields.push(field);
            field = '';
        } else {
            field += value[index];
        }
    }
    fields.push(field);
    return fields;
}

function lookupId(name, lookup, fallback) {
    const numeric = id(name);
    if( numeric !== null ) return numeric;
    const source = lookup?.byName ?? lookup;
    if( source instanceof Map && source.has(name) ) return id(source.get(name));
    if( source && Object.hasOwn(source, name) ) return id(source[name]);
    return fallback;
}

function columnDefinition(raw, named) {
    const value = unescapeOuter(raw);
    const colon = value.indexOf(':');
    if( colon < 0 ) return null;
    const columnId = id(value.slice(0, colon));
    if( columnId === null || columnId > 0xff ) return null;
    const fields = splitEscaped(value.slice(colon + 1));
    return {
        id: columnId,
        name: named ? fields.shift() ?? '' : '',
        types: fields,
        values: [],
    };
}

function tupleDefinition(raw) {
    const value = unescapeOuter(raw);
    const first = value.indexOf(':');
    const second = first < 0 ? -1 : value.indexOf(':', first + 1);
    if( first < 0 || second < 0 ) return null;
    const columnId = id(value.slice(0, first));
    const tupleIndex = id(value.slice(first + 1, second));
    if( columnId === null || tupleIndex === null ) return null;
    return { columnId, tupleIndex, fields: splitEscaped(value.slice(second + 1)) };
}

function parseValue(type, value, resolver) {
    if( isStringType(type) ) return value;
    const number = Number(value);
    if( Number.isFinite(number) ) return Math.trunc(number) | 0;
    if( typeof resolver === 'function' ) return int32(resolver(type, value), -1);
    return -1;
}

function parseColumns(block, table, resolver) {
    const columns = {};
    let columnCount = 0;
    for( const line of block.lines ) {
        if( line.key === 'columns' ) {
            columnCount = id(line.value) ?? 0;
            continue;
        }
        const named = line.key === 'columndef';
        const legacy = table ? line.key === 'defaulttypes' : line.key === 'types';
        if( !named && !legacy ) continue;
        const definition = columnDefinition(line.value, named);
        if( definition ) columns[definition.id] = definition;
    }
    const valuesKey = table ? 'defaults' : 'values';
    for( const line of block.lines ) {
        if( line.key !== valuesKey ) continue;
        const tuple = tupleDefinition(line.value);
        const column = tuple ? columns[tuple.columnId] : null;
        if( !tuple || !column ) continue;
        const fields = column.types.map((type, index) =>
            parseValue(type, tuple.fields[index] ?? '', resolver));
        /* cachepack reads the stated tuple id for validation/documentation but
         * appends records in source order; the exported files are dense. */
        column.values.push(fields);
    }
    for( const column of Object.values(columns) ) {
        column.values = column.values.filter((tuple) => Array.isArray(tuple));
        column.tupleCount = column.values.length;
    }
    return { columnCount, columns };
}

/** Parse cachepack's configs/all.dbtable. Pass its .compack for sparse IDs. */
export function parseDbTableText(text, { ids = null, resolveValue = null } = {}) {
    const dbTables = {};
    const blocks = parseBlocks(text);
    for( let index = 0; index < blocks.length; index++ ) {
        const block = blocks[index];
        const tableId = lookupId(block.name, ids, index);
        if( tableId === null ) continue;
        const parsed = parseColumns(block, true, resolveValue);
        dbTables[tableId] = { id: tableId, name: block.name, ...parsed };
    }
    return dbTables;
}

/** Parse cachepack's configs/all.dbrow. Pass both row and table compacks. */
export function parseDbRowText(text, {
    ids = null,
    tableIds = null,
    resolveValue = null,
} = {}) {
    const dbRows = {};
    const blocks = parseBlocks(text);
    for( let index = 0; index < blocks.length; index++ ) {
        const block = blocks[index];
        const rowId = lookupId(block.name, ids, index);
        if( rowId === null ) continue;
        const tableLine = block.lines.find((line) => line.key === 'table');
        const tableId = tableLine ? lookupId(tableLine.value, tableIds, -1) : -1;
        const parsed = parseColumns(block, false, resolveValue);
        dbRows[rowId] = { id: rowId, name: block.name, tableId: tableId ?? -1, ...parsed };
    }
    return dbRows;
}

/** Parse both all.* files directly into normalizeDbData's payload shape. */
export function parseDbTextData({
    tableText = '',
    rowText = '',
    tableCompackText = '',
    rowCompackText = '',
    resolveValue = null,
} = {}) {
    const tableIds = parseDbCompack(tableCompackText);
    const rowIds = parseDbCompack(rowCompackText);
    return normalizeDbData({
        dbTables: parseDbTableText(tableText, { ids: tableIds, resolveValue }),
        dbRows: parseDbRowText(rowText, {
            ids: rowIds,
            tableIds,
            resolveValue,
        }),
    });
}
