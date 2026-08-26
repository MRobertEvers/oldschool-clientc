/*
 * The other half of the host-command wire: does C agree?
 *
 * `test/cmd_frames_test.js` pins the bytes this tool's encoder produces, but a
 * test can only check the encoder against itself and against octets someone
 * typed out. The question that matters is whether the REAL client reads them as
 * the commands they were meant to be — and the client is right here.
 *
 * So: build the frames with src/cmd_frames.js, wrap them in the record-file
 * container (which is the same ring layout with a magic header, src/cmd/
 * cmdbus.c), and hand the file to the native client as a replay. If the wire
 * agrees, the client opens the interface the frames asked for and says so.
 * If a field is at the wrong offset or the wrong width, it does not.
 *
 * This is a MEASUREMENT with a client and a cache behind it, not part of
 * `make test`. Run it when either side of the wire moves:
 *
 *     node scripts/verify_cmd_wire.mjs [--client PATH] [--cache DIR] [--rev NAME]
 */

import { spawnSync } from 'node:child_process';
import { mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import { concatFrames, openRoot, setVarp } from '../src/cmd_frames.js';

const HERE = dirname(fileURLToPath(import.meta.url));
const REPO = resolve(HERE, '..', '..', '..');

function flag(name, fallback) {
    const at = process.argv.indexOf(`--${name}`);
    return at > 0 && process.argv[at + 1] ? process.argv[at + 1] : fallback;
}

const client = resolve(flag('client', join(REPO, 'src', 'torirs')));
const cache = resolve(flag('cache', join(REPO, 'cache.osrs239')));
const revision = flag('rev', 'osrs239');
const interfaceId = Number(flag('interface', '600'));

/**
 * The record container: "TRSCMD1\0", version 1, reserved 0, then frames.
 *
 * A replay needs FRAME delimiters (type 1, u64 milliseconds) because the pump
 * reads one iteration's worth at a time and the delimiter carries the clock
 * that iteration runs at. The commands ride between them exactly as they would
 * have on a live bus.
 */
function trscmd(commandsByFrame, frames = 240) {
    const parts = [];
    const header = new Uint8Array(16);
    header.set(new TextEncoder().encode('TRSCMD1'), 0);
    new DataView(header.buffer).setUint32(8, 1, true);
    parts.push(header);

    for( let frame = 0; frame < frames; frame++ )
    {
        const delimiter = new Uint8Array(6 + 8);
        const view = new DataView(delimiter.buffer);
        view.setUint32(0, 1, true);            /* TORIRS_CMD_FRAME */
        view.setUint16(4, 8, true);
        view.setBigUint64(6, BigInt(frame) * 20n, true);
        parts.push(delimiter);
        if( commandsByFrame.has(frame) ) parts.push(...commandsByFrame.get(frame));
    }
    return concatFrames(parts);
}

const scratch = mkdtempSync(join(tmpdir(), 'cs2dom-wire-'));
let failed = 0;

function check(condition, message) {
    if( condition ) console.log(`ok   ${message}`);
    else { failed++; console.error(`FAIL ${message}`); }
}

try
{
    const path = join(scratch, 'wire.trscmd');
    writeFileSync(path, trscmd(new Map([
        [8, [openRoot(interfaceId)]],
        [120, [setVarp(300, 100)]],
    ])));

    const run = spawnSync(client, [cache, '--rev', revision], {
        env: {
            ...process.env,
            SDL_VIDEODRIVER: 'dummy',
            TORIRS_CMD_REPLAY: path,
            TORIRS_MAX_FRAMES: '250',
        },
        encoding: 'utf8',
        timeout: 180_000,
    });

    if( run.error ) throw run.error;
    const log = `${run.stdout ?? ''}${run.stderr ?? ''}`;

    /*
     * The client announces each interface it builds. Frames written here made
     * it say `iface=<id>` with a tree under it, which is only reachable through
     * App_OpenRootInterface — i.e. the drain read our bytes as UI_OPEN_ROOT and
     * found the id where the struct says it is.
     */
    const built = new RegExp(`RevConfigBuild done: iface=${interfaceId} .*tree_components=(\\d+)`)
        .exec(log);
    check(built !== null, `the client built interface ${interfaceId} from our frames`);
    check(
        built !== null && Number(built[1]) > 1,
        'and built a tree under it, not an empty root');
    check(
        !/cmdbus: unhandled command type/.test(log),
        'no frame was read as a command the client does not know');

    if( failed )
    {
        console.error('\n--- client output ---');
        console.error(log.split('\n').slice(-25).join('\n'));
    }
}
finally
{
    rmSync(scratch, { recursive: true, force: true });
}

console.log(failed ? `\n${failed} check(s) failed` : '\nthe C client reads this wire');
process.exit(failed ? 1 : 0);
