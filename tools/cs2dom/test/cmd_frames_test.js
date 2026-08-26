/*
 * The host-command wire, byte for byte.
 *
 * These frames are built here and read by C — `CmdBus_Push*` writes the same
 * five structs from the other side, and `torirs_cmdbus_push_bytes` parses the
 * batch. Two implementations of one layout drift the moment nothing is watching
 * the bytes, and the failure is silent in the worst way: a frame that parses as
 * a different command, or an interface id read out of a length field.
 *
 * So these tests assert the actual octets rather than round-tripping through
 * the encoder's own decoder, which would agree with itself no matter what it
 * did. `scripts/verify_cmd_wire.mjs` closes the other half by handing these
 * exact bytes to the real client.
 */

import assert from 'node:assert/strict';

import {
    CMD, HEADER_BYTES, MAX_PAYLOAD, RUNSCRIPT_MAX_ARGS,
    concatFrames, execText, openRoot, runScript, setVarbit, setVarp, writeFrame,
} from '../src/cmd_frames.js';

const tests = [];
function test(name, fn) { tests.push([name, fn]); }

const hex = (bytes) => Array.from(bytes, (b) => b.toString(16).padStart(2, '0')).join(' ');

test('the header is type then length, little-endian', () => {
    const frame = writeFrame(CMD.UI_OPEN_ROOT, new Uint8Array([0xaa]));
    assert.equal(frame.length, HEADER_BYTES + 1);
    /* 64 = 0x40 as u32, then 1 as u16. A big-endian writer would put the 0x40
     * in byte 3 and the interface id would arrive as a type nobody handles. */
    assert.equal(hex(frame), '40 00 00 00 01 00 aa');
});

test('open root carries the interface id as one int32', () => {
    assert.equal(hex(openRoot(600)), '40 00 00 00 04 00 58 02 00 00');
});

test('a varp frame is id then value, and a varbit differs only in type', () => {
    assert.equal(hex(setVarp(300, 100)), '41 00 00 00 08 00 2c 01 00 00 64 00 00 00');
    assert.equal(hex(setVarbit(300, 100)), '42 00 00 00 08 00 2c 01 00 00 64 00 00 00');
});

test('a negative value is two-complement, not a sign byte', () => {
    /* -1 has to arrive as -1 and not as 4294967295 read into an int: the two
     * are the same bits, and this is the assertion that they are WRITTEN as
     * the same bits. */
    const frame = setVarp(1, -1);
    assert.equal(hex(frame), '41 00 00 00 08 00 01 00 00 00 ff ff ff ff');
});

test('run script is sized to the arguments it carries', () => {
    /* script 3967 with three args: id, argc, then exactly three ints. Not the
     * four the struct reserves — the drain reads argc of them, so sending the
     * padding would only cost bytes. */
    const three = runScript(3967, [11, -22, 33]);
    assert.equal(three.length, HEADER_BYTES + 4 * (2 + 3));
    assert.equal(
        hex(three),
        '43 00 00 00 14 00 7f 0f 00 00 03 00 00 00 0b 00 00 00 ea ff ff ff 21 00 00 00');

    const none = runScript(915);
    assert.equal(none.length, HEADER_BYTES + 8, 'no arguments costs no argument bytes');
    assert.equal(hex(none), '43 00 00 00 08 00 93 03 00 00 00 00 00 00');
});

test('more arguments than the struct holds is refused by name', () => {
    assert.throws(
        () => runScript(1, [1, 2, 3, 4, 5]),
        /at most 4 arguments/,
        'a fifth argument would be written past the C struct');
    assert.equal(RUNSCRIPT_MAX_ARGS, 4);
});

test('exec text is the string with no terminator', () => {
    const frame = execText('gc');
    /* Length 2, not 3. The drain bounds its copy by the header, so a NUL would
     * be a second and disagreeing statement of where the string ends. */
    assert.equal(hex(frame), '44 00 00 00 02 00 67 63');
});

test('a batch is frames concatenated, nothing between them', () => {
    const batch = concatFrames([openRoot(600), setVarp(300, 100)]);
    assert.equal(batch.length, openRoot(600).length + setVarp(300, 100).length);
    assert.equal(hex(batch.subarray(0, 10)), hex(openRoot(600)));
    assert.equal(hex(batch.subarray(10)), hex(setVarp(300, 100)));
});

test('a payload over the cap throws rather than truncating', () => {
    /* Truncation here would produce a frame whose header disagrees with its
     * body, and the C side would read the next frame from the wrong offset —
     * every command after it in the batch becomes garbage. */
    assert.throws(() => writeFrame(1, new Uint8Array(MAX_PAYLOAD + 1)), /exceeds/);
});

let failed = 0;
for( const [name, fn] of tests )
{
    try { fn(); console.log(`ok   ${name}`); }
    catch( error ) { failed++; console.error(`FAIL ${name}\n     ${error.message}`); }
}
console.log(`\n${tests.length - failed}/${tests.length} passed`);
process.exit(failed ? 1 : 0);
