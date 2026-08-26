/*
 * One interface in headless Chrome, reported.
 *
 * node scripts/probe_canvas.mjs <name-or-id> [--png out.png] [--base URL]
 *
 * Opens the dev canvas page with ?open=, waits for the session to settle
 * (paints stop, nothing still wanted), then prints the page's own account of
 * itself — status line, warnings log, paint stats — and optionally saves the
 * canvas as a PNG. This is the manual half of the pixel-parity harness.
 */

import { writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { launchChrome, openPage, navigate, evaluate, delay } from './cdp.mjs';

const args = process.argv.slice(2);
const subject = args.find((value) => !value.startsWith('--'));
const png = flag('--png');
const base = flag('--base') ?? 'http://localhost:8099';
if( !subject ) { console.error('usage: probe_canvas.mjs <name-or-id> [--png out.png]'); process.exit(2); }

function flag(name) {
    const index = args.indexOf(name);
    return index >= 0 && index + 1 < args.length ? args[index + 1] : null;
}

const PROBE = `(() => {
  const status = document.getElementById('status');
  const dev = window.__cs2dev;
  if( !dev?.runtime )
    return { phase: 'mounting', status: status?.textContent ?? '', state: status?.dataset.state ?? '' };
  const session = dev.runtime.session;
  const wanted = session.painter?.wanted;
  return {
    phase: 'running',
    status: status.textContent, state: status.dataset.state,
    painted: session.stats.painted,
    missing: session.painter?.stats.missingAssets ?? -1,
    commands: session.painter?.stats.commands ?? -1,
    wanted: wanted ? wanted.sprites.size + wanted.fonts.size + wanted.modelPoses.size : -1,
    ratio: dev.runtime.ratio,
    canvas: { width: dev.runtime.canvas.width, height: dev.runtime.canvas.height },
    log: document.getElementById('log').textContent,
  };
})()`;

/**
 * Settled means: mounted, nothing wanted, and two probes 400 ms apart agree
 * on the paint counter. "state ok" alone is not enough — assets land after
 * the status line is written and each arrival repaints.
 */
export async function waitForSettle(page, timeoutMs = 30000) {
    const start = Date.now();
    let previous = null;
    for( ;; )
    {
        const now = await evaluate(page, PROBE);
        if( now.phase === 'running' && now.state !== 'busy'
            && now.wanted === 0 && now.painted > 0
            && previous && previous.painted === now.painted )
            return now;
        if( now.state === 'bad' ) return now;
        if( Date.now() - start > timeoutMs ) return { ...now, timedOut: true };
        previous = now;
        await delay(400);
    }
}

const scratch = process.env.TMPDIR ?? '/tmp';
const chrome = await launchChrome({ profileDir: join(scratch, `cs2dom-probe-${process.pid}`) });
try
{
    const page = await openPage(chrome);
    await navigate(page, `${base}/?open=${encodeURIComponent(subject)}`);
    const result = await waitForSettle(page);
    if( png )
    {
        const dataUrl = await evaluate(page,
            `document.getElementById('surface').toDataURL('image/png')`);
        writeFileSync(png, Buffer.from(dataUrl.split(',')[1], 'base64'));
        result.png = png;
    }
    console.log(JSON.stringify(result, null, 2));
}
finally
{
    await chrome.close();
}
