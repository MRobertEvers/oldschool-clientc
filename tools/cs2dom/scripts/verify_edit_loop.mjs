/*
 * Does an edit reach the screen?
 *
 * The whole point of the tool is the loop: change a record, and see the client
 * draw the change. Every part of that has its own check already -- the bake
 * runs cachepack, the client boots, the page reboots it -- and the loop can
 * still be broken with all of them green, because "the client read the cache we
 * packed into" is not something any of them assert.
 *
 * So this one edits a real interface, waits for the reboot, and compares the
 * pixels. If the picture does not change, the client is reading a cache nobody
 * is writing to, and every other check would have said everything was fine.
 *
 * THE EDIT IS MADE IN PLACE AND PUT BACK. The content tree is 2GB, so copying
 * it for a test is not sensible; instead one file is backed up, edited, and
 * restored in a finally, and the restore is verified by hash rather than
 * assumed. A test that leaves the tree modified is worse than no test.
 *
 *     node scripts/verify_edit_loop.mjs [--content DIR] [--cache DIR] [--rev NAME]
 */

import { createHash } from 'node:crypto';
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import { delay, evaluate, launchChrome, navigate, openPage } from './cdp.mjs';
import { serveClient } from '../src/dev_client.js';

const HERE = dirname(fileURLToPath(import.meta.url));
const ROOT = resolve(HERE, '..');
const REPO = resolve(ROOT, '..', '..');

function flag(name, fallback) {
    const at = process.argv.indexOf(`--${name}`);
    return at > 0 && process.argv[at + 1] ? process.argv[at + 1] : fallback;
}

const content = resolve(flag('content', join(REPO, 'OSRS-Content', 'osrs239-content')));
const cache = resolve(flag('cache', join(REPO, 'cache.osrs239')));
const revision = flag('rev', 'osrs239');
const port = Number(flag('port', '8244'));

/*
 * vm_kudos_info's title is a literal in an onload argument -- `s:Kudos List`,
 * handed to the script that draws the window frame. Editing a STRING is the
 * clearest possible signal: if the new one is on screen, the bytes the client
 * read came from this edit and from nowhere else.
 */
const NAME = 'vm_kudos_info';
const TARGET = join(content, 'interfaces', `${NAME}.if`);
const FROM = 's:Kudos List';
const TO = 's:Zzzz Edited';

const digest = (path) => createHash('sha256').update(readFileSync(path)).digest('hex');

let failed = 0;
const check = (ok, message) => {
    if( ok ) console.log(`ok   ${message}`);
    else { failed++; console.error(`FAIL ${message}`); }
};

const original = readFileSync(TARGET, 'utf8');
const originalHash = digest(TARGET);
const scratch = mkdtempSync(join(tmpdir(), 'cs2dom-edit-'));
let server = null;
let chrome = null;
let edited = false;

try
{
    if( !original.includes(FROM) )
        throw new Error(`${TARGET} no longer contains "${FROM}"; pick another edit`);

    server = serveClient({
        root: ROOT, contentDir: content, cache, revision,
        port, ioPort: port + 1, log: () => {},
    });
    await new Promise((ready) => server.once('listening', ready));

    chrome = await launchChrome({ profileDir: join(scratch, 'chrome') });
    const page = await openPage(chrome, { width: 1400, height: 900, scale: 2 });
    await navigate(page, `http://localhost:${port}/?open=${NAME}`);

    const before = await settledCanvas(page);
    check(before.lit > 1000, `the interface drew before the edit (${before.lit} pixels)`);

    /* The edit. */
    writeFileSync(TARGET, original.replace(FROM, TO));
    edited = true;

    /*
     * Wait for the picture to change rather than for a duration: the bake is
     * cachepack over a 218MB cache and the reboot is a whole client start, and
     * a fixed sleep long enough for a slow machine is wasted on every fast one.
     */
    const changed = await waitForChange(page, before.hash, 180_000);
    check(changed !== null, 'the picture changed after the edit');
    if( changed )
        check(changed.lit > 1000, `and it is still an interface (${changed.lit} pixels)`);
}
catch( error )
{
    failed++;
    console.error(`FAIL ${error.stack ?? error.message}`);
}
finally
{
    /*
     * Put it back, and PROVE it went back. A restore that silently failed would
     * leave a working tree with an edit in it that nobody made on purpose.
     */
    if( edited )
    {
        writeFileSync(TARGET, original);
        const restored = digest(TARGET) === originalHash;
        if( restored ) console.log('ok   the content tree was restored');
        else { failed++; console.error(`FAIL ${TARGET} was NOT restored -- check it by hand`); }
    }
    try { await chrome?.close(); } catch { /* already gone */ }
    server?.close();
    rmSync(scratch, { recursive: true, force: true });
}

console.log(failed ? `\n${failed} check(s) failed` : '\nan edit reaches the screen');
process.exit(failed ? 1 : 0);

/** The client's canvas once it has stopped changing, as a hash and a pixel count. */
async function settledCanvas(page, budgetMs = 90_000) {
    const deadline = Date.now() + budgetMs;
    let last = null;
    let stable = 0;
    while( Date.now() < deadline )
    {
        const shot = await readCanvas(page);
        if( shot && last && shot.hash === last.hash ) { if( ++stable >= 2 ) return shot; }
        else stable = 0;
        last = shot ?? last;
        await delay(1000);
    }
    return last ?? { hash: null, lit: 0 };
}

async function waitForChange(page, hash, budgetMs) {
    const deadline = Date.now() + budgetMs;
    while( Date.now() < deadline )
    {
        const shot = await readCanvas(page);
        if( shot && shot.hash && shot.hash !== hash ) return await settledCanvas(page, 60_000);
        await delay(1000);
    }
    return null;
}

/**
 * Read the client's canvas.
 *
 * Hashed over a downsample rather than every pixel: the client animates (a
 * cursor, a scrollbar highlight), and a hash of the full buffer would report a
 * change every frame and pass this test without an edit.
 */
function readCanvas(page) {
    return evaluate(page, `
        (() => {
            const doc = document.getElementById('client')?.contentDocument;
            const canvas = doc && doc.getElementById('canvas');
            if( !canvas || !canvas.width ) return null;
            const probe = document.createElement('canvas');
            probe.width = canvas.width; probe.height = canvas.height;
            probe.getContext('2d').drawImage(canvas, 0, 0);
            const { data } = probe.getContext('2d').getImageData(0, 0, probe.width, probe.height);
            let lit = 0;
            let sum = 0;
            for( let i = 0; i < data.length; i += 4 )
            {
                if( data[i] || data[i + 1] || data[i + 2] ) lit++;
                /* Every 37th pixel: dense enough to catch a changed word, and
                 * a prime stride so it cannot land on one column forever. */
                if( (i >> 2) % 37 === 0 )
                    sum = (sum * 31 + data[i] + data[i + 1] * 3 + data[i + 2] * 7) >>> 0;
            }
            return { hash: String(sum), lit };
        })()`);
}
