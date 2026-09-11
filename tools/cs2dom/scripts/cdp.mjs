/*
 * A Chrome DevTools Protocol client small enough to read.
 *
 * The pixel-parity harness needs exactly four verbs — launch, open a page,
 * evaluate, close — and pulling in a driver framework for that would add a
 * dependency tree to a repo that deliberately has almost none. Node 24 ships
 * a WebSocket, which is the whole transport.
 *
 * Headless Chrome is launched with `--remote-debugging-port=0`; the port it
 * actually chose lands in `DevToolsActivePort` inside the user-data dir, and
 * polling that file is the documented handshake, not a workaround.
 */

import { spawn } from 'node:child_process';
import { existsSync, mkdirSync, readFileSync, rmSync } from 'node:fs';
import { join } from 'node:path';

const CHROME = process.env.CHROME_BIN
    ?? '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';

export async function launchChrome({ profileDir }) {
    if( !existsSync(CHROME) )
        throw new Error(`no Chrome at ${CHROME}; set CHROME_BIN`);
    mkdirSync(profileDir, { recursive: true });
    const child = spawn(CHROME, [
        '--headless=new',
        '--remote-debugging-port=0',
        `--user-data-dir=${profileDir}`,
        '--no-first-run', '--no-default-browser-check',
        '--disable-background-timer-throttling',
        '--disable-renderer-backgrounding',
        /*
         * Software canvas, not SwiftShader-GPU. Under the GPU path a page
         * that repaints a large canvas in a tight driven loop LOSES the
         * backing store mid-run — isContextLost() still answers false while
         * fills stop landing and every readback returns transparent black.
         * The third forced repaint of the quest list came back empty that
         * way, deterministically, with the painter blameless.
         */
        '--disable-gpu',
        '--hide-scrollbars',
        'about:blank',
    ], { stdio: ['ignore', 'ignore', 'pipe'] });
    let stderr = '';
    child.stderr.on('data', (chunk) => { stderr += chunk; });

    const portFile = join(profileDir, 'DevToolsActivePort');
    const port = await waitFor(async () => {
        if( child.exitCode !== null )
            throw new Error(`Chrome exited: ${stderr.slice(-500)}`);
        if( !existsSync(portFile) ) return null;
        const chosen = Number(readFileSync(portFile, 'utf8').split('\n')[0]);
        return Number.isInteger(chosen) && chosen > 0 ? chosen : null;
    }, 15000, 'DevToolsActivePort');

    const version = await (await fetch(`http://127.0.0.1:${port}/json/version`)).json();
    const browser = await connect(version.webSocketDebuggerUrl);
    return {
        port,
        browser,
        close: async () => {
            try { await browser.send('Browser.close'); } catch { /* already gone */ }
            browser.socket.close();
            await new Promise((resolve) => {
                child.once('exit', resolve);
                setTimeout(() => { child.kill('SIGKILL'); resolve(); }, 3000);
            });
            rmSync(profileDir, { recursive: true, force: true });
        },
    };
}

/** One page, as its own attached CDP session. */
export async function openPage(chrome, { width = 1700, height = 1100, scale = 2 } = {}) {
    const { browser } = chrome;
    const { targetId } = await browser.send('Target.createTarget', { url: 'about:blank' });
    const { sessionId } = await browser.send('Target.attachToTarget', { targetId, flatten: true });
    const page = {
        send: (method, params = {}) => browser.send(method, params, sessionId),
        close: () => browser.send('Target.closeTarget', { targetId }),
        on: (method, handler) => browser.listen(sessionId, method, handler),
    };
    await page.send('Page.enable');
    await page.send('Runtime.enable');
    /* A created target is "backgrounded" and its requestAnimationFrame never
     * fires — the runtime mounts, paints nothing, and the canvas stays blank
     * while every probe reads healthy. Foreground it. */
    await page.send('Page.bringToFront');
    /* deviceScaleFactor 2 is not decoration: the last three painter bugs were
     * invisible at DPR 1, because the scale transform is where they lived. */
    await page.send('Emulation.setDeviceMetricsOverride', {
        width, height, deviceScaleFactor: scale, mobile: false,
    });
    return page;
}

export async function navigate(page, url) {
    const loaded = new Promise((resolve) => {
        const stop = page.on('Page.loadEventFired', () => { stop(); resolve(); });
    });
    await page.send('Page.navigate', { url });
    await Promise.race([loaded, delay(20000)]);
}

/**
 * Evaluate in the page and return the JSON value.
 *
 * `returnByValue` carries the result across the wire; a thrown page-side
 * error becomes a thrown Node-side error with the page's message, because a
 * harness that turns exceptions into `undefined` reports every bug as "the
 * canvas was blank".
 */
export async function evaluate(page, expression, { timeoutMs = 120000 } = {}) {
    /*
     * Bounded, because a hung renderer hangs the whole run. The page-side
     * scripts carry their own deadlines, but a tab that has stopped
     * scheduling JavaScript at all never reaches them — a corpus run sat 39
     * minutes on one interface that way. The caller gets an error naming the
     * timeout and decides whether a fresh tab is worth a retry.
     */
    const reply = await Promise.race([
        page.send('Runtime.evaluate', {
            expression, returnByValue: true, awaitPromise: true,
        }),
        delay(timeoutMs).then(() => { throw new Error(`evaluate timeout after ${timeoutMs}ms`); }),
    ]);
    if( reply.exceptionDetails )
    {
        const detail = reply.exceptionDetails;
        throw new Error(`page: ${detail.exception?.description ?? detail.text}`);
    }
    return reply.result?.value;
}

/* ---------------------------------------------------------------------- */

function connect(url) {
    return new Promise((resolve, reject) => {
        const socket = new WebSocket(url);
        let nextId = 1;
        const pending = new Map();
        const listeners = new Set();
        const client = {
            socket,
            send(method, params = {}, sessionId = undefined) {
                const id = nextId++;
                socket.send(JSON.stringify({ id, method, params, sessionId }));
                return new Promise((ok, bad) => pending.set(id, { ok, bad, method }));
            },
            /** Subscribe to one event on one session; returns unsubscribe. */
            listen(sessionId, method, handler) {
                const entry = { sessionId, method, handler };
                listeners.add(entry);
                return () => listeners.delete(entry);
            },
        };
        socket.addEventListener('open', () => resolve(client));
        socket.addEventListener('error', () => reject(new Error(`could not connect to ${url}`)));
        socket.addEventListener('message', (event) => {
            const message = JSON.parse(event.data);
            if( message.id === undefined )
            {
                for( const entry of listeners )
                    if( entry.method === message.method
                        && entry.sessionId === message.sessionId )
                        entry.handler(message.params ?? {});
                return;
            }
            const waiter = pending.get(message.id);
            if( !waiter ) return;
            pending.delete(message.id);
            if( message.error )
                waiter.bad(new Error(`${waiter.method}: ${message.error.message}`));
            else waiter.ok(message.result ?? {});
        });
    });
}

export function delay(ms) {
    return new Promise((resolve) => setTimeout(resolve, ms));
}

async function waitFor(probe, timeoutMs, what) {
    const start = Date.now();
    for( ;; )
    {
        const value = await probe();
        if( value ) return value;
        if( Date.now() - start > timeoutMs ) throw new Error(`timed out waiting for ${what}`);
        await delay(100);
    }
}
