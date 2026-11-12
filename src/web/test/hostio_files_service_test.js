/*
 * ToriRS_CreateHostIO().filesService -- what a browser answers for the
 * files service (plugin/task_plugin_io.h CreateTask_PluginFiles), the
 * screenshot gallery's listing and its downloads -- against a fake IndexedDB
 * and a fake document that records what the browser was handed.
 *
 * Node only, no browser or build step. The shipped file is evaluated as-is.
 *
 * What it pins: a listing is the PNG keys under the prefix and nothing else
 * (no other plugin's, no other file type, and the keys only -- never the
 * pictures); Download all is ONE .zip whose entries keep the player/album
 * folders, whose CRCs match the bytes, and which a desktop unpacker reads; a
 * single picture downloads as itself; and the two desktop-only verbs say
 * `unsupported:` rather than nothing.
 */
const assert = require('assert');
const fs = require('fs');
const path = require('path');

const SRC = fs.readFileSync(path.join(__dirname, '..', 'torirs_hostio.js'), 'utf8');

const ROOT = 'plugin_assets/screenshot';
const bytes = (value) => new Uint8Array(Buffer.from(value, 'utf8'));
const text = (value) => Buffer.from(value).toString('utf8');

const store = new Map([
    [`${ROOT}/Zezima/Levels/Attack-40_2026-11-01_12-00-00.png`, bytes('PNG one')],
    [`${ROOT}/Zezima/Boss-Kills/Vorkath-1_2026-11-02_12-00-00.png`, bytes('PNG two, longer')],
    [`${ROOT}/Zezima/notes.txt`, bytes('not a picture')],
    ['plugin_assets/other-plugin/x.png', bytes('someone else\'s')],
]);
const gets = [];
const downloads = [];

global.window = {
    ToriRS_IDB: {
        async fileKeysUnder(prefix) {
            return [...store.keys()].filter((key) => key.startsWith(prefix)).sort();
        },
        async fileGet(key) { gets.push(key); return store.get(key) || null; },
        async fileKeysPresent() { return new Set(store.keys()); },
    },
};
let blob = null;
global.Blob = class { constructor(parts, options) { this.parts = parts; this.type = options.type; } };
global.URL = { createObjectURL(made) { blob = made; return 'blob:test'; }, revokeObjectURL() {} };
global.document = {
    createElement() {
        return { click() { downloads.push({ name: this.download, blob }); }, remove() {} };
    },
    body: { appendChild() {} },
};
global.setTimeout = () => 0;

eval(SRC);
const host = window.ToriRS_CreateHostIO('cache', '', null, '');

/* The archive, read back the way an unpacker reads it: the end record, then
 * each central entry, then its local header and its bytes. */
function readZip(zip) {
    const view = new DataView(zip.buffer, zip.byteOffset, zip.byteLength);
    const end = zip.byteLength - 22;
    assert.strictEqual(view.getUint32(end, true), 0x06054b50, 'the end record is last');
    const count = view.getUint16(end + 10, true);
    let at = view.getUint32(end + 16, true);
    const entries = [];
    for (let i = 0; i < count; i++) {
        assert.strictEqual(view.getUint32(at, true), 0x02014b50, 'a central entry');
        const crc = view.getUint32(at + 16, true);
        const size = view.getUint32(at + 24, true);
        const nameLength = view.getUint16(at + 28, true);
        const local = view.getUint32(at + 42, true);
        const name = text(zip.subarray(at + 46, at + 46 + nameLength));
        assert.strictEqual(view.getUint32(local, true), 0x04034b50, `${name}: its local header`);
        assert.strictEqual(view.getUint16(local + 8, true), 0, `${name}: stored, not deflated`);
        const data = zip.subarray(local + 30 + nameLength, local + 30 + nameLength + size);
        entries.push({ name, crc, data });
        at += 46 + nameLength;
    }
    return entries;
}

function crc32(data) {
    let crc = 0xffffffff;
    for (const byte of data) {
        crc ^= byte;
        for (let k = 0; k < 8; k++) { crc = crc & 1 ? 0xedb88320 ^ (crc >>> 1) : crc >>> 1; }
    }
    return (crc ^ 0xffffffff) >>> 0;
}

(async () => {
    const ask = async (verb, body) => text(await host.filesService(verb, bytes(body)));

    const list = await ask('list', `dir=${ROOT}\n`);
    assert.strictEqual(list,
        'ok\nZezima/Boss-Kills/Vorkath-1_2026-11-02_12-00-00.png\t0\t0\n'
        + 'Zezima/Levels/Attack-40_2026-11-01_12-00-00.png\t0\t0\n',
        'the PNGs under the prefix, as paths below it, and nothing else');
    assert.deepStrictEqual(gets, [], 'a listing reads no picture');
    assert.strictEqual(await ask('list', 'dir=plugin_assets/nobody\n'), 'ok\n',
        'an empty folder is an empty list, not an error');

    for (const verb of ['reveal', 'pick_folder']) {
        assert.match(await ask(verb, `dir=${ROOT}\n`), /^unsupported: /,
            `${verb} says a browser cannot, rather than nothing`);
    }

    const one = `${ROOT}/Zezima/Levels/Attack-40_2026-11-01_12-00-00.png`;
    assert.strictEqual(await ask('download', `dir=${ROOT}\npath=${one}\n`), 'ok\n1\n');
    assert.strictEqual(downloads[0].name, 'Attack-40_2026-11-01_12-00-00.png',
        'one picture downloads under its own name');
    assert.strictEqual(downloads[0].blob.type, 'image/png');

    assert.strictEqual(await ask('download', `dir=${ROOT}\n`), 'ok\n2\n');
    const all = downloads[1];
    assert.match(all.name, /^screenshots_\d{4}-\d\d-\d\d-\d\d-\d\d-\d\d\.zip$/);
    assert.strictEqual(all.blob.type, 'application/zip');
    const entries = readZip(all.blob.parts[0]);
    assert.deepStrictEqual(entries.map((entry) => entry.name), [
        'Zezima/Boss-Kills/Vorkath-1_2026-11-02_12-00-00.png',
        'Zezima/Levels/Attack-40_2026-11-01_12-00-00.png',
    ], 'the archive keeps the player and album folders');
    for (const entry of entries) {
        const key = `${ROOT}/${entry.name}`;
        assert.deepStrictEqual([...entry.data], [...store.get(key)], `${entry.name}: its bytes`);
        assert.strictEqual(entry.crc, crc32(entry.data), `${entry.name}: its CRC`);
    }

    assert.match(await ask('download', 'dir=plugin_assets/nobody\n'), /^error: /,
        'nothing to download is said, not an empty archive');

    console.log('hostio files service: list, downloads and archive verified');
})().catch((err) => { console.error(err); process.exit(1); });
