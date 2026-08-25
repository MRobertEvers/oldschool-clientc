import assert from 'node:assert/strict';

import {
    DB_REQUEST_NAMES,
    createDbState,
    decodeDbColumn,
    encodeDbColumn,
    handleDbRequest,
    normalizeDbData,
    parseDbCompack,
    parseDbRowText,
    parseDbTableText,
    parseDbTextData,
    snapshotDbState,
} from '../src/host_db.js';

assert.equal(DB_REQUEST_NAMES.size, 11);
assert(DB_REQUEST_NAMES.has('DB_FIND_FILTER_WITH_COUNT'));
assert.throws(() => DB_REQUEST_NAMES.add('NOPE'), TypeError);

const whole2 = encodeDbColumn(7, 2);
assert.deepEqual(decodeDbColumn(whole2), { tableId: 7, columnId: 2, tupleIndex: -1 });
assert.deepEqual(decodeDbColumn(encodeDbColumn(0xabcde, 0xfe, 3)), {
    tableId: 0xabcde, columnId: 0xfe, tupleIndex: 3,
});

const payload = {
    dbTables: {
        7: {
            name: 'sample',
            columns: {
                0: { types: ['int'] },
                1: { types: ['string'], defaults: [['fallback']] },
                2: { types: ['graphic', 'int', 'string'], defaults: [[99, 5, 'table']] },
                3: { types: ['int', 'int'] },
            },
        },
        8: { name: 'other', columns: { 0: { types: ['int'] } } },
    },
    dbRows: {
        30: {
            tableId: 7,
            columns: {
                0: { types: ['int'], values: [[10], [20]] },
                1: { types: ['string'], values: [['alpha']] },
                2: { types: ['graphic', 'int', 'string'], values: [
                    [100, 6, 'one'], [101, 7, 'two'],
                ] },
            },
        },
        10: {
            tableId: 7,
            columns: {
                0: { types: ['int'], values: [[10]] },
                1: { types: ['string'], values: [['beta']] },
                2: { types: ['graphic', 'int', 'string'], values: [[100, 8, 'three']] },
            },
        },
        20: { tableId: 7, columns: { 0: { types: ['int'], values: [[11]] } } },
        40: { tableId: 8, columns: { 0: { types: ['int'], values: [[10]] } } },
    },
};

const normalized = normalizeDbData(payload);
assert.doesNotThrow(() => JSON.stringify(normalized));
const state = createDbState(normalized);

/* DB_GETROWTABLE */
assert.equal(handleDbRequest(state, 'DB_GETROWTABLE', { rowId: 30 }), 7);
assert.equal(handleDbRequest(state, 'DB_GETROWTABLE', { rowId: 999 }), -1);

/* DB_GETFIELD: whole/single typed tuples, row defaults, table defaults and
 * unknown row/column sentinels. */
assert.deepEqual(handleDbRequest(state, 'DB_GETFIELD', {
    rowId: 30, column: whole2, index: 1,
}), { pattern: 'iis', values: [101, 7, 'two'] });
assert.deepEqual(handleDbRequest(state, 'DB_GETFIELD', {
    rowId: 30, column: encodeDbColumn(7, 2, 2), index: 1,
}), { pattern: 's', values: ['two'] });
assert.deepEqual(handleDbRequest(state, 'DB_GETFIELD', {
    rowId: 30, column: whole2, index: 99,
}), { pattern: 'iis', values: [-1, -1, ''] },
'a row-present column uses type defaults when its index is absent');
assert.deepEqual(handleDbRequest(state, 'DB_GETFIELD', {
    rowId: 20, column: whole2, index: 0,
}), { pattern: 'iis', values: [99, 5, 'table'] },
'a row-omitted column uses its DBTABLE default tuple');
assert.deepEqual(handleDbRequest(state, 'DB_GETFIELD', {
    rowId: 20, column: encodeDbColumn(7, 3), index: 0,
}), { pattern: 'ii', values: [-1, -1] },
'a typed table column without defaults preserves whole-tuple arity');
assert.deepEqual(handleDbRequest(state, 'DB_GETFIELD', {
    rowId: 20, column: encodeDbColumn(7, 3, 1), index: 0,
}), { pattern: 'i', values: [-1] });
assert.deepEqual(handleDbRequest(state, 'DB_GETFIELD', {
    rowId: 999, column: whole2, index: 0,
}), { pattern: 'i', values: [-1] },
'an absent row does not chase its packed table for an arity');
assert.deepEqual(handleDbRequest(state, 'DB_GETFIELD', {
    rowId: 30, column: encodeDbColumn(7, 250), index: 0,
}), { pattern: 'i', values: [-1] });
assert.deepEqual(handleDbRequest(state, 'DB_GETFIELD', {
    rowId: 30, column: encodeDbColumn(7, 2, 10), index: 0,
}), { pattern: 'iis', values: [100, 6, 'one'] },
'an out-of-arity selector follows native whole-tuple behavior');

/* DB_GETFIELDCOUNT */
assert.equal(handleDbRequest(state, 'DB_GETFIELDCOUNT', {
    rowId: 30, column: encodeDbColumn(7, 0),
}), 2);
assert.equal(handleDbRequest(state, 'DB_GETFIELDCOUNT', {
    rowId: 20, column: whole2,
}), 1);
assert.equal(handleDbRequest(state, 'DB_GETFIELDCOUNT', {
    rowId: 999, column: whole2,
}), 0);

/* DB_FINDALL_WITH_COUNT, DB_GETROW, DB_FINDNEXT. Row order is ascending id,
 * matching each cache DB index's master list. */
assert.equal(handleDbRequest(state, 'DB_FINDALL_WITH_COUNT', { tableId: 7 }), 3);
assert.equal(handleDbRequest(state, 'DB_GETROW', { index: 0 }), 10);
assert.equal(handleDbRequest(state, 'DB_GETROW', { index: 2 }), 30);
assert.equal(handleDbRequest(state, 'DB_GETROW', { index: 3 }), -1);
assert.equal(handleDbRequest(state, 'DB_FINDNEXT'), 10);
assert.equal(handleDbRequest(state, 'DB_FINDNEXT'), 20);
assert.equal(handleDbRequest(state, 'DB_FINDNEXT'), 30);
assert.equal(handleDbRequest(state, 'DB_FINDNEXT'), -1);

/* DB_FIND_WITH_COUNT and DB_FIND. Type tag 2 is string; every other value is
 * on the int stack. */
assert.equal(handleDbRequest(state, 'DB_FIND_WITH_COUNT', {
    column: encodeDbColumn(7, 0), typeTag: 0, value: 10,
}), 2);
assert.equal(handleDbRequest(state, 'DB_FINDNEXT'), 10);
assert.equal(handleDbRequest(state, 'DB_FINDNEXT'), 30);
assert.equal(handleDbRequest(state, 'DB_FIND', {
    column: encodeDbColumn(7, 1), typeTag: 2, value: 'beta',
}), null);
assert.equal(handleDbRequest(state, 'DB_FINDNEXT'), 10);
assert.equal(handleDbRequest(state, 'DB_FIND_WITH_COUNT', {
    column: encodeDbColumn(7, 1), typeTag: 0, value: 0,
}), 0, 'an int query cannot match a string index tuple');

/* Whole-tuple FIND checks positions in order and returns the first matching
 * position's row list rather than unioning all positions. */
assert.equal(handleDbRequest(state, 'DB_FIND_WITH_COUNT', {
    column: whole2, typeTag: 0, value: 7,
}), 1);
assert.equal(handleDbRequest(state, 'DB_FINDNEXT'), 30);

/* DB_FIND_FILTER_WITH_COUNT and DB_FIND_FILTER preserve the prior order. */
assert.equal(handleDbRequest(state, 'DB_FINDALL', { tableId: 7 }), null);
assert.equal(handleDbRequest(state, 'DB_FIND_FILTER_WITH_COUNT', {
    column: encodeDbColumn(7, 0), typeTag: 0, value: 10,
}), 2);
assert.equal(handleDbRequest(state, 'DB_FIND_FILTER', {
    column: encodeDbColumn(7, 1), typeTag: 2, value: 'alpha',
}), null);
assert.equal(handleDbRequest(state, 'DB_GETROW', { index: 0 }), 30);
assert.equal(handleDbRequest(state, 'DB_GETROW', { index: 1 }), -1);

handleDbRequest(state, 'DB_FIND', {
    column: encodeDbColumn(7, 0), typeTag: 0, value: 999,
});
assert.equal(handleDbRequest(state, 'DB_FIND_FILTER_WITH_COUNT', {
    column: encodeDbColumn(7, 0), typeTag: 0, value: 10,
}), 0, 'FILTER cannot create rows when the prior iterator is empty');

/* cachepack parser: sparse compack IDs, tuple types/defaults and two-layer
 * comma/backslash escaping. */
const tableCompackText = `
// sparse on purpose
7=sample
8=other
`;
const rowCompackText = `
10=first
30=last
`;
const tableText = `
[sample]
columns=3
columndef=0:id,int
columndef=1:title,string
defaults=1:0:Fallback
columndef=2:mixed,int,string
defaults=2:0:-1,Default\\\\, value

[other]
columns=0
`;
const rowText = `
[last]
columns=3
table=sample
columndef=0:id,int
values=0:0:30
columndef=1:title,string
values=1:0:Hello\\\\, world${' '}
columndef=2:mixed,int,string
values=2:0:5,Path\\\\\\\\name

[first]
columns=3
table=sample
columndef=0:id,int
values=0:0:10
`;

const tableIds = parseDbCompack(tableCompackText);
const rowIds = parseDbCompack(rowCompackText);
assert.equal(tableIds.byName.sample, 7);
assert.equal(rowIds.byId[30], 'last');
const parsedTables = parseDbTableText(tableText, { ids: tableIds });
const parsedRows = parseDbRowText(rowText, { ids: rowIds, tableIds });
assert.deepEqual(parsedTables[7].columns[2].values, [[-1, 'Default, value']]);
assert.equal(parsedRows[30].tableId, 7);
assert.deepEqual(parsedRows[30].columns[1].values, [['Hello, world ']],
    'string values retain significant trailing whitespace');
assert.deepEqual(parsedRows[30].columns[2].values, [[5, 'Path\\name']]);

const parsed = parseDbTextData({ tableText, rowText, tableCompackText, rowCompackText });
const parsedState = createDbState(parsed);
assert.equal(handleDbRequest(parsedState, 'DB_FINDALL_WITH_COUNT', { tableId: 7 }), 2);
assert.deepEqual(handleDbRequest(parsedState, 'DB_GETFIELD', {
    rowId: 10, column: encodeDbColumn(7, 1), index: 0,
}), { pattern: 's', values: ['Fallback'] });

const snapshot = snapshotDbState(state);
const restored = createDbState(JSON.parse(JSON.stringify(snapshot)));
assert.deepEqual(snapshotDbState(restored), snapshot);
snapshot.iterator.rows.push(999);
assert(!state.iterator.rows.includes(999), 'snapshot iterator does not alias live state');

assert.throws(() => handleDbRequest(state, 'DB_NOT_REAL'), RangeError);
process.stdout.write('host_db_test: ok\n');
