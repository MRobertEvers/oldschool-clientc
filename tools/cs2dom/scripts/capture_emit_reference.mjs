/*
 * Phase 0's reference capture.
 *
 * Runs the headless C client over a set of interfaces and stores each one's
 * draw list. That stored list is what `emit_parity.js` compares against, and
 * storing it — rather than re-running the client on every comparison — is what
 * makes the comparison usable: the client takes seconds per interface and a
 * gate nobody waits for is a gate nobody runs.
 *
 * The invocation is not obvious and is worth writing down:
 *
 *   SDL_VIDEODRIVER=dummy   the dummy driver runs the full tick loop
 *   --rev <name>            the client refuses to boot without a cache identity
 *   TORIRS_MAX_FRAMES=N     exit after N iterations instead of on a window close
 *   TORIRS_EXIT_BMP=<path>  **gates the dump block** — the emit dump lives
 *                           inside `frame_loop_teardown`'s BMP branch, so
 *                           without it TORIRS_DUMP_EMIT_EXIT prints nothing
 *                           and the run looks like an interface with no
 *                           content rather than a missing flag
 *   TORIRS_DUMP_EMIT_EXIT=all   every command, not just the viewport-covering
 *                           filter the flag was originally written for
 *
 *   node scripts/capture_emit_reference.mjs --cache DIR --rev NAME [--out DIR]
 *        [--interfaces 600,12,...] [--frames N]
 */

import { execFile, execFileSync } from 'node:child_process';
import { existsSync, mkdirSync, readFileSync, writeFileSync, rmSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { tmpdir } from 'node:os';

import { emitFingerprint, parseCEmitDump, UNHOSTED_KINDS } from '../src/emit_parity.js';

const HERE = dirname(fileURLToPath(import.meta.url));
const REPO = resolve(join(HERE, '..', '..', '..'));

const args = process.argv.slice(2);
const cache = flag('--cache') ?? join(REPO, 'cache.osrs239');
const revision = flag('--rev') ?? 'osrs239';
const outDir = resolve(flag('--out') ?? join(HERE, '..', 'test', 'fixtures', 'emit'));
const frames = Number(flag('--frames') ?? 60);
const client = flag('--client') ?? join(REPO, 'src', 'torirs');

/*
 * The default set: one interface per shape the walk has to get right.
 *
 * 600 is a plain panel of text and sprites; 12 is the bank, which is the mass
 * container rebuild; 162 is the chatbox, whose 500 rows are the scroll and
 * clip case; 218 is the spellbook, a grid of hover-variant icons.
 */
const DEFAULT_INTERFACES = [600, 12, 162, 218];

/*
 * `--all` captures every interface the content tree's pack names.
 *
 * Most of them do produce a draw list; the ones that do not are components
 * meant to be mounted INSIDE another interface, and booting one on its own is
 * a tree with nothing in it. Those are reported as failures rather than
 * skipped silently — "no draw list" is a fact about the interface, and a
 * corpus run that quietly dropped a third of its subjects would report a
 * pass rate against a denominator nobody chose.
 */
const interfaces = args.includes('--all') ? allInterfaceIds()
    : ((flag('--interfaces') ?? '').trim()
        ? flag('--interfaces').split(',').map((value) => Number(value.trim()))
        : DEFAULT_INTERFACES);

/* Captures are independent processes; run them across the machine. */
const jobs = Math.max(1, Number(flag('--jobs') ?? 8));

function allInterfaceIds() {
    const path = join(flag('--content') ?? join(REPO, 'OSRS-Content', 'osrs239-content'),
        'pack', '3_interfaces.pack');
    const ids = [];
    for( const raw of readFileSync(path, 'utf8').split('\n') )
    {
        const line = raw.replace(/\/\/.*$/, '').trim();
        const split = line.indexOf('=');
        if( split < 1 ) continue;
        const id = Number(line.slice(0, split));
        if( Number.isInteger(id) ) ids.push(id);
    }
    return ids.sort((a, b) => a - b);
}

function flag(name) {
    const index = args.indexOf(name);
    return index >= 0 && index + 1 < args.length ? args[index + 1] : null;
}

if( !existsSync(client) )
{
    console.error(`no client at ${client}; build it with: make -C src torirs`);
    process.exit(2);
}
mkdirSync(outDir, { recursive: true });

const scratch = join(tmpdir(), `cs2dom-emit-${process.pid}`);
mkdirSync(scratch, { recursive: true });

const captured = [];
const failures = [];

/** One capture, as a promise; the client is a process per interface. */
function runClient(id) {
    return new Promise((done) => {
        execFile(client, [cache, '--rev', revision, String(id)], {
            encoding: 'utf8',
            maxBuffer: 256 * 1024 * 1024,
            env: {
                ...process.env,
                SDL_VIDEODRIVER: 'dummy',
                TORIRS_MAX_FRAMES: String(frames),
                /* Gates the dump block; see the header. */
                TORIRS_EXIT_BMP: join(scratch, `${id}.bmp`),
                TORIRS_DUMP_EMIT_EXIT: 'all',
            },
        }, (error, stdout, stderr) => {
            /* The client may exit non-zero for reasons unrelated to the dump —
             * an audio device it could not open, a plugin it declined to load.
             * What matters is whether a draw list came out. */
            done(`${stderr ?? ''}${error && !stderr ? `${error.stderr ?? ''}` : ''}`);
        });
    });
}

/** Run `worker` over `items`, `limit` at a time. */
async function pool(items, limit, worker) {
    const queue = [...items];
    const runners = Array.from({ length: Math.min(limit, queue.length) }, async () => {
        while( queue.length ) await worker(queue.shift());
    });
    await Promise.all(runners);
}

let done = 0;
await pool(interfaces, jobs, async (id) => {
    const stderr = await runClient(id);
    done++;
    if( interfaces.length > 8 && done % 50 === 0 )
        process.stderr.write(`captured ${done}/${interfaces.length}\n`);

    const commands = parseCEmitDump(stderr);
    if( commands.length === 0 ) { failures.push({ id, reason: 'no draw list' }); return; }

    const hosted = commands.filter((command) =>
        command.kind !== null && !UNHOSTED_KINDS.has(command.cKind));

    const path = join(outDir, `interface_${id}.json`);
    writeFileSync(path, `${JSON.stringify({
        schema: 'cs2dom-emit-reference/1',
        interface: id, revision, frames,
        /* The fingerprint is stored WITH the list so a stale capture is
         * obvious: a reference whose own commands do not hash to its recorded
         * value was edited by hand. */
        fingerprint: emitFingerprint(hosted),
        total: commands.length,
        hosted: hosted.length,
        commands: hosted,
    }, null, 1)}\n`);

    captured.push({ id, total: commands.length, hosted: hosted.length, path });
});

rmSync(scratch, { recursive: true, force: true });

/**
 * Re-run capturing stderr when the first attempt exited cleanly.
 *
 * `execFileSync` gives stderr on the error path but discards it on success,
 * and the dump is on stderr — so a run that WORKED is the one whose output
 * would otherwise be lost.
 */
function readCapturedStderr(clientPath, cachePath, rev, id, frameCount, scratchDir) {
    const log = join(scratchDir, `${id}.stderr`);
    try
    {
        execFileSync('/bin/sh', ['-c',
            `"${clientPath}" "${cachePath}" --rev "${rev}" ${id} 2>"${log}" >/dev/null`], {
            env: {
                ...process.env,
                SDL_VIDEODRIVER: 'dummy',
                TORIRS_MAX_FRAMES: String(frameCount),
                TORIRS_EXIT_BMP: join(scratchDir, `${id}.bmp`),
                TORIRS_DUMP_EMIT_EXIT: 'all',
            },
        });
    }
    catch { /* the log is what matters, not the status */ }
    return existsSync(log) ? readFileSync(log, 'utf8') : '';
}

captured.sort((a, b) => a.id - b.id);
failures.sort((a, b) => a.id - b.id);

console.log(JSON.stringify({
    client, cache, revision, frames, jobs,
    capturedCount: captured.length,
    failureCount: failures.length,
    /* Listed in full only for a small run; a corpus capture would bury the
     * numbers under a thousand rows. */
    ...(interfaces.length > 8 ? {} : { captured, failures }),
    outDir,
}, null, 2));

/* A corpus capture EXPECTS failures — an interface meant to be mounted inside
 * another has nothing to draw on its own — so only a targeted run treats one
 * as an error. */
process.exit(interfaces.length > 8 ? 0 : (failures.length ? 1 : 0));
