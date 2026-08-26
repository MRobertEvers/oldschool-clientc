/*
 * Does the preview come up, and does it take a command?
 *
 * This replaces the emit-parity and pixel-parity harnesses. Both existed to
 * check a renderer written in this repository against the C client's; there is
 * no second renderer now, so there is nothing to compare and nothing to drift.
 * What is left worth asking is much smaller, and this asks it:
 *
 *   1. the dev server serves a page and the client boots inside it
 *   2. the client says it is ready, which is the only signal an embedder has
 *   3. it drew something -- not the right thing, SOMETHING
 *   4. a command posted from the page reaches the client's bus
 *   5. nothing threw on the way
 *
 * A LIVENESS check, deliberately. "Is this the right picture?" is the client's
 * question now, answered by the client's own tests and by TORIRS_EXIT_BMP on a
 * native build. Asking it a second time here, with a second opinion about what
 * the picture should be, is precisely the mistake being undone.
 *
 *   node scripts/smoke_client.mjs [--cache DIR] [--rev NAME] [--content DIR]
 */

import { mkdtempSync, rmSync } from 'node:fs';
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

const cache = resolve(flag('cache', join(REPO, 'cache.osrs239')));
const revision = flag('rev', 'osrs239');
const content = resolve(flag('content', join(REPO, 'OSRS-Content', 'osrs239-content')));
/*
 * Interface 600, the Kudos list: text, sprites, a scrollbar and a close button,
 * which is a lot of the raster surface in one record. A near-empty interface
 * would pass the "something was drawn" checks below on almost any bug.
 */
const wanted = flag('open', 'vm_kudos_info');
const port = Number(flag('port', '8123'));

let failed = 0;
const check = (ok, message) => {
    if( ok ) console.log(`ok   ${message}`);
    else { failed++; console.error(`FAIL ${message}`); }
};

const scratch = mkdtempSync(join(tmpdir(), 'cs2dom-smoke-'));
let server = null;
let chrome = null;

try
{
    server = serveClient({
        root: ROOT, contentDir: content, cache, revision,
        port, ioPort: port + 1, log: () => {},
    });
    await new Promise((ready) => server.once('listening', ready));

    chrome = await launchChrome({ profileDir: join(scratch, 'chrome') });
    const page = await openPage(chrome, { width: 1500, height: 900, scale: 2 });

    /*
     * UNCAUGHT EXCEPTIONS, not console.error.
     *
     * The client is a C program: everything it writes to stderr -- the cache
     * line, the plugin roster, "config group absent" -- arrives here as
     * console.error, because that is what emscripten maps stderr to. Failing on
     * those means failing on a healthy boot. What actually indicates a broken
     * page is JavaScript that threw, which is its own event.
     *
     * Collected from before the first navigation, because the ones that matter
     * happen during boot.
     */
    const thrown = [];
    page.on('Runtime.exceptionThrown', (event) => {
        const detail = event.exceptionDetails;
        thrown.push(detail?.exception?.description ?? detail?.text ?? 'unknown exception');
    });
    const clientLog = [];
    page.on('Runtime.consoleAPICalled', (event) => {
        if( event.type !== 'error' ) return;
        clientLog.push((event.args ?? []).map((a) => a.value ?? a.description).join(' '));
    });

    await navigate(page, `http://localhost:${port}/?open=${encodeURIComponent(wanted)}`);

    const picked = await evaluate(page, `
        (async () => {
            const list = await (await fetch('/api/catalogue')).json();
            const hit = list.find((e) => e.name === ${JSON.stringify(wanted)});
            return { count: list.length, found: !!hit, id: hit ? hit.interfaceId : -1 };
        })()`);
    check(picked.count > 0, `the catalogue has ${picked.count} interfaces`);
    /*
     * Named separately, because the page's answer to an interface it cannot
     * find is to say so and boot nothing -- which reads downstream as "the
     * client failed to start" and sends you looking at the wasm.
     */
    check(picked.found, `the catalogue has ${wanted} (id ${picked.id})`);

    /*
     * Wait for the client's own ready announcement rather than for a duration.
     * A boot behind a cold cache is slower than a boot behind a warm one, and
     * every harness that guessed at that difference guessed wrong somewhere.
     */
    const ready = await waitFor(page, `
        !!(document.getElementById('client')?.contentWindow?.Module?._torirs_cmdbus_push_bytes)`,
    60_000);
    check(ready, 'the client booted and its command entry point is callable');

    if( ready )
    {
        /* Give it frames to draw with. The ready signal is the loop STARTING;
         * the first painted frame is one iteration later. */
        await delay(3000);

        const drawn = await evaluate(page, `
            (() => {
                const doc = document.getElementById('client').contentDocument;
                const canvas = doc.getElementById('canvas');
                if( !canvas ) return { error: 'no canvas in the client page' };
                const probe = document.createElement('canvas');
                probe.width = canvas.width; probe.height = canvas.height;
                const ctx = probe.getContext('2d');
                ctx.drawImage(canvas, 0, 0);
                const { data } = ctx.getImageData(0, 0, probe.width, probe.height);
                let lit = 0;
                const seen = new Set();
                for( let i = 0; i < data.length; i += 4 )
                {
                    if( data[i] || data[i + 1] || data[i + 2] ) lit++;
                    if( seen.size < 64 )
                        seen.add((data[i] << 16) | (data[i + 1] << 8) | data[i + 2]);
                }
                return { w: canvas.width, h: canvas.height, lit, colours: seen.size };
            })()`);

        check(!drawn.error, drawn.error ?? 'the client has a canvas');
        check(drawn.w > 0 && drawn.h > 0, `the canvas is sized (${drawn.w}x${drawn.h})`);
        /*
         * Non-black pixels, and more than one colour among them. Either alone
         * is a weak claim: a canvas cleared to a background colour is "lit"
         * everywhere and still shows nothing, and a canvas with two colours
         * could be one stripe. Together they say a picture was rasterised.
         */
        check(drawn.lit > 1000, `${drawn.lit} pixels are not black`);
        check(drawn.colours > 4, `${drawn.colours}+ distinct colours were drawn`);

        /*
         * And the wire, live: post a frame the way the state pane does, and
         * see the client accept it. A negative return is the client refusing
         * the batch; a throw is the export being absent or mis-declared.
         */
        const accepted = await evaluate(page, `
            (async () => {
                const { openRoot } = await import('/src/cmd_frames.js');
                const frame = openRoot(${JSON.stringify(0)} + 600);
                const win = document.getElementById('client').contentWindow;
                const bytes = frame;
                const at = win.Module._malloc(bytes.length);
                win.Module.HEAPU8.set(bytes, at);
                const n = win.Module._torirs_cmdbus_push_bytes(at, bytes.length);
                win.Module._free(at);
                return n;
            })()`);
        check(accepted === 1, `the client accepted a command frame (returned ${accepted})`);
    }

    check(
        thrown.length === 0,
        thrown.length
            ? `JavaScript threw:\n     ${thrown.join('\n     ')}`
            : 'nothing threw');

    /*
     * The client's own stderr is not a failure, but a boot that opened the
     * wrong cache says so here and nowhere else -- and reads downstream as a
     * blank canvas with no explanation. So it is checked for the specific
     * sentence that means it, rather than for the presence of output.
     */
    const wrongCache = clientLog.find((line) => /dat2 cache=\d+\b/.test(line));
    check(!wrongCache, wrongCache
        ? `the client opened a cache named after an argument: "${wrongCache.trim()}"`
        : 'the client opened a cache directory, not a stray argument');

    if( failed ) console.error(`\n--- client log ---\n${clientLog.join('\n')}`);

    await page.close();
}
catch( error )
{
    failed++;
    console.error(`FAIL ${error.stack ?? error.message}`);
}
finally
{
    try { await chrome?.close(); } catch { /* it may already be gone */ }
    server?.close();
    rmSync(scratch, { recursive: true, force: true });
}

console.log(failed ? `\n${failed} check(s) failed` : '\nthe preview is alive');
process.exit(failed ? 1 : 0);

/** Poll a page-side predicate until it holds or the budget runs out. */
async function waitFor(page, expression, budgetMs) {
    const deadline = Date.now() + budgetMs;
    while( Date.now() < deadline )
    {
        if( await evaluate(page, expression) ) return true;
        await delay(250);
    }
    return false;
}
