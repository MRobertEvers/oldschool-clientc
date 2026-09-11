/*
 * Host commands, as bytes.
 *
 * The client takes every input as a frame on one ring — `[u32 type][u16 length]
 * [payload]`, little-endian, several concatenated into a batch (src/cmd/
 * cmdring.h). That layout is also the record-file format and also what
 * src/web/torirs_channel.js posts between tabs, which is why a batch built here
 * can be handed straight to `_torirs_cmdbus_push_bytes` in the wasm client.
 *
 * This module is the other half of `CmdBus_PushUi*` in src/cmd/cmdbus.c: the
 * same five commands, the same struct layouts, written from JavaScript. Two
 * implementations of one wire is a thing that drifts, so `test/cmd_frames_test.js`
 * pins the exact bytes and `scripts/verify_cmd_wire.mjs` feeds them to the real
 * C client and checks it agrees.
 *
 * Nothing here talks to a client. Encoding is separable from delivery, and
 * keeping it so is what lets the wire be tested in Node with no browser.
 */

/** cmdring.h: [u32 type][u16 length]. */
export const HEADER_BYTES = 6;

/** cmdring.h's TORIRS_CMD_MAX_PAYLOAD. A frame over this is a caller bug. */
export const MAX_PAYLOAD = 8192;

/** cmdbus.h's enum ToriRS_CmdType, the host-command half. */
export const CMD = {
    UI_OPEN_ROOT: 64,
    UI_SET_VARP: 65,
    UI_SET_VARBIT: 66,
    UI_RUNSCRIPT: 67,
    EXEC_TEXT: 68,
};

/** cmdbus.h's TORIRS_CMD_UI_RUNSCRIPT_MAX_ARGS. */
export const RUNSCRIPT_MAX_ARGS = 4;

/**
 * One frame: header then payload.
 *
 * Byte-identical to `ToriRSChannel.writeFrame`, deliberately — the two ends of
 * this wire are a browser page and a C ring, and the page may have either
 * implementation loaded.
 */
export function writeFrame(type, payload) {
    const body = payload ?? new Uint8Array(0);
    if( body.length > MAX_PAYLOAD )
        throw new RangeError(`cmd frame payload ${body.length} exceeds ${MAX_PAYLOAD}`);
    const frame = new Uint8Array(HEADER_BYTES + body.length);
    const view = new DataView(frame.buffer);
    view.setUint32(0, type, true);
    view.setUint16(4, body.length, true);
    frame.set(body, HEADER_BYTES);
    return frame;
}

/** Several frames as one batch, which is what one postMessage carries. */
export function concatFrames(frames) {
    let total = 0;
    for( const frame of frames ) total += frame.length;
    const batch = new Uint8Array(total);
    let at = 0;
    for( const frame of frames ) { batch.set(frame, at); at += frame.length; }
    return batch;
}

function ints(values) {
    const payload = new Uint8Array(values.length * 4);
    const view = new DataView(payload.buffer);
    values.forEach((value, index) => view.setInt32(index * 4, value | 0, true));
    return payload;
}

/** struct ToriRS_CmdUiOpenRoot { int32 interface_id }. */
export function openRoot(interfaceId) {
    return writeFrame(CMD.UI_OPEN_ROOT, ints([interfaceId]));
}

/** struct ToriRS_CmdUiSetVar { int32 id; int32 value }. */
export function setVarp(id, value) {
    return writeFrame(CMD.UI_SET_VARP, ints([id, value]));
}

export function setVarbit(id, value) {
    return writeFrame(CMD.UI_SET_VARBIT, ints([id, value]));
}

/**
 * struct ToriRS_CmdUiRunScript { int32 script_id; int32 argc; int32 args[argc] }.
 *
 * Sized to the arguments it carries rather than to the maximum, which is why
 * argc is on the wire at all: the drain bounds its read by it.
 */
export function runScript(scriptId, args = []) {
    if( args.length > RUNSCRIPT_MAX_ARGS )
        throw new RangeError(
            `run script takes at most ${RUNSCRIPT_MAX_ARGS} arguments, got ${args.length}`);
    return writeFrame(CMD.UI_RUNSCRIPT, ints([scriptId, args.length, ...args]));
}

/**
 * A debugproc command, WITHOUT the leading "::" — App_SendCommand's own form.
 *
 * Not NUL-terminated: the header's length is the string's, as NET_CONNECT has
 * it. The drain bounds its copy by that length, so a terminator would be a
 * second, disagreeing statement of the same fact.
 */
export function execText(text) {
    return writeFrame(CMD.EXEC_TEXT, new TextEncoder().encode(text));
}
