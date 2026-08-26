/*
 * Pixel parity: the canvas painter against the C client's PIXELS.
 *
 * The emit-parity gate compares draw COMMANDS, and every recent defect lived
 * below that line — in the painter, the font raster, the model path, the
 * devicePixelRatio transform. This harness compares the picture:
 *
 *   1. The C client renders each interface headless and dumps a BMP
 *      (TORIRS_MAX_FRAMES + TORIRS_EXIT_BMP, same invocation as the emit
 *      reference capture).
 *   2. Headless Chrome opens the dev canvas page at ?open=NAME&paused=1 —
 *      paused, so the harness drives session.frame() with its own clock and
 *      ticks exactly as many times as the C capture ran frames. A frozen
 *      clock is what makes a spinning model finishable: the async renderer
 *      can never catch a pose that moves every tick, but it converges on one
 *      that stands still.
 *   3. The page diffs its own canvas against the BMP, both composited over
 *      the client's #202428 background, and reports differing pixels.
 *
 * The capture is at deviceScaleFactor 2 — the last three painter bugs were
 * invisible at DPR 1 — and the 2x canvas is reduced back to 765x503 by
 * nearest sampling, which is lossless for content the painter scaled
 * correctly and glaring for content it did not.
 *
 *   node scripts/verify_pixel_parity.mjs --all
 *   node scripts/verify_pixel_parity.mjs --interfaces 26,600 --keep-matching
 *
 * The report ranks differing interfaces by area. Artifacts (app / reference /
 * diff PNGs) land in --out for every differing interface.
 */

import { execFile } from 'node:child_process';
import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { launchChrome, openPage, navigate, evaluate, delay } from './cdp.mjs';

const HERE = dirname(fileURLToPath(import.meta.url));
const REPO = resolve(join(HERE, '..', '..', '..'));

const args = process.argv.slice(2);
const flag = (name) => {
    const index = args.indexOf(name);
    return index >= 0 && index + 1 < args.length ? args[index + 1] : null;
};

const base = flag('--base') ?? 'http://localhost:8099';
const client = flag('--client') ?? join(REPO, 'src', 'torirs');
const cache = flag('--cache') ?? join(REPO, 'cache.osrs239');
const revision = flag('--rev') ?? 'osrs239';
const outDir = resolve(flag('--out') ?? join(HERE, '..', 'build', 'pixel_parity'));
const frames = Number(flag('--frames') ?? 3);
const ticks = Number(flag('--ticks') ?? frames);
const jobs = Math.max(1, Number(flag('--jobs') ?? 8));
const tolerance = Number(flag('--tolerance') ?? 0);
const recapture = args.includes('--recapture');
const only = flag('--interfaces');

const ROOT = { width: 765, height: 503 };
/* The C client fills its frame with this before the interface draws; the
 * canvas leaves unpainted pixels transparent, so both sides are composited
 * over it before comparing. */
const BACKGROUND = '#202428';

/* ----------------------------------------------------------------------
 * The subjects: the served catalogue, which is the same list the picker has.
 * ---------------------------------------------------------------------- */

async function subjects() {
    let entries;
    try
    {
        entries = await (await fetch(`${base}/api/catalogue`)).json();
    }
    catch
    {
        console.error(`no dev server at ${base}; start it with: npm start`);
        process.exit(2);
    }
    const usable = entries.filter((entry) => entry.interfaceId >= 0);
    if( !only ) return usable;
    const wantedIds = new Set(only.split(',').map((value) => Number(value.trim())));
    return usable.filter((entry) => wantedIds.has(entry.interfaceId));
}

/**
 * The staleness gate: the page the server SERVES must be the page the source
 * tree DEFINES. A server started before an edit keeps the module it imported
 * at boot, and every fix made after that is invisible in a way that looks
 * exactly like the fix not working.
 */
async function assertServedIsCurrent() {
    const { canvasDevClient } = await import('../src/dev_page_canvas.js');
    const served = await (await fetch(`${base}/dev-client.js`)).text();
    if( served !== canvasDevClient() )
    {
        console.error('the dev server is serving a STALE client '
            + '(restart it: kill $(lsof -ti tcp:8099) && npm start)');
        process.exit(2);
    }
}

/* ----------------------------------------------------------------------
 * Stage 1: reference BMPs from the C client
 * ---------------------------------------------------------------------- */

function captureReference(id, path) {
    return new Promise((done) => {
        execFile(client, [cache, '--rev', revision, String(id)], {
            maxBuffer: 64 * 1024 * 1024,
            env: {
                ...process.env,
                SDL_VIDEODRIVER: 'dummy',
                TORIRS_MAX_FRAMES: String(frames),
                TORIRS_EXIT_BMP: path,
            },
        }, () => done(existsSync(path)));
    });
}

async function pool(items, limit, worker) {
    const queue = [...items];
    const runners = Array.from({ length: Math.min(limit, queue.length) }, async () => {
        while( queue.length ) await worker(queue.shift());
    });
    await Promise.all(runners);
}

/* ----------------------------------------------------------------------
 * Stage 2: the page-side capture-and-diff, one evaluate per interface
 * ---------------------------------------------------------------------- */

/**
 * Everything after navigation happens in the page: wait for the mount, drive
 * the frozen-clock frames, converge the async asset loads, then diff against
 * the reference BMP. One round trip, one JSON summary back.
 */
function captureExpression(refBase64) {
    return `(async () => {
  const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
  const statusLine = document.getElementById('status');

  /* The mount. */
  const mountDeadline = Date.now() + 45000;
  while( !window.__cs2dev?.runtime )
  {
    if( statusLine.dataset.state === 'bad' )
      return { error: 'mount failed: ' + statusLine.textContent,
               log: document.getElementById('log').textContent };
    if( Date.now() > mountDeadline )
      return { error: 'mount timeout: ' + statusLine.textContent,
               log: document.getElementById('log').textContent };
    await sleep(100);
  }
  const runtime = window.__cs2dev.runtime;
  const session = runtime.session;

  /*
   * The frames, on the harness's clock.
   *
   * The pointer sits at the origin because the headless SDL client's does,
   * and that is observable — cr_ui's top-left resize handle fades in under
   * it. A move is posted before every frame so onMouseRepeat fires each
   * tick, which is the reference's cadence.
   */
  const T0 = 1000;
  /*
   * A frame resumes at most ONE parked load, by design — the live loop calls
   * frame() a hundred times a second and an onload with fifteen if_setmodels
   * settles across fifteen of them. The frozen clock changes nothing there:
   * frame(t) with the same t ticks no timers, so it is pumped until the
   * driver reports settled.
   */
  const settleAt = async (t) => {
    const settleDeadline = Date.now() + 20000;
    while( Date.now() < settleDeadline )
    {
      await session.frame(t);
      if( session.driver.settled ) return true;
      await sleep(15);
    }
    return false;
  };
  session.post({ type: 'move', x: 0, y: 0 });
  if( !await settleAt(T0) )
    return { error: 'settlement timeout after mount',
             log: document.getElementById('log').textContent.slice(0, 2000) };
  for( let i = 1; i <= ${ticks}; i++ )
  {
    session.post({ type: 'move', x: 0, y: 0 });
    if( !await settleAt(T0 + i * 20) )
      return { error: 'settlement timeout at tick ' + i,
               log: document.getElementById('log').textContent.slice(0, 2000) };
  }

  /*
   * Convergence: repaint at the FROZEN clock until a forced paint reports no
   * new missing assets and nothing is still in flight. An asset the content
   * tree genuinely lacks would miss on every repaint forever, so "the same
   * number missing three rounds running" also counts as converged — reported,
   * not hidden.
   */
  const t = T0 + ${ticks} * 20;
  const deadline = Date.now() + 30000;
  let missingPerPaint = -1;
  let steady = 0;
  while( Date.now() < deadline && steady < 3 )
  {
    const before = session.painter.stats.missingAssets;
    session.repaintWanted = true;
    await session.frame(t);
    const delta = session.painter.stats.missingAssets - before;
    const pending = session.painter.wanted.sprites.size
      + session.painter.wanted.fonts.size
      + session.painter.wanted.modelPoses.size
      + (runtime.modelSource?.inFlight?.size ?? 0);
    if( pending === 0 && delta === missingPerPaint ) steady++;
    else steady = 0;
    missingPerPaint = pending === 0 ? delta : -1;
    await sleep(150);
  }

  /* Both pictures at 765x503 over the client's background. */
  const W = ${ROOT.width}, H = ${ROOT.height};
  const flatten = (source, width, height) => {
    const flat = document.createElement('canvas');
    flat.width = W; flat.height = H;
    const context = flat.getContext('2d', { willReadFrequently: true });
    context.imageSmoothingEnabled = false;
    context.fillStyle = '${BACKGROUND}';
    context.fillRect(0, 0, W, H);
    context.drawImage(source, 0, 0, W, H);
    return context;
  };
  const app = flatten(runtime.canvas);

  const reference = new Image();
  reference.src = 'data:image/bmp;base64,${refBase64}';
  try { await reference.decode(); }
  catch { return { error: 'reference BMP did not decode' }; }
  const ref = flatten(reference);

  const appData = app.getImageData(0, 0, W, H).data;
  const refData = ref.getImageData(0, 0, W, H).data;
  let differing = 0, maxDelta = 0;
  let minX = W, minY = H, maxX = -1, maxY = -1;
  const diff = document.createElement('canvas');
  diff.width = W; diff.height = H;
  const diffContext = diff.getContext('2d');
  const diffImage = diffContext.createImageData(W, H);
  for( let y = 0; y < H; y++ )
    for( let x = 0; x < W; x++ )
    {
      const i = (y * W + x) * 4;
      const delta = Math.max(
        Math.abs(appData[i] - refData[i]),
        Math.abs(appData[i + 1] - refData[i + 1]),
        Math.abs(appData[i + 2] - refData[i + 2]));
      const grey = (refData[i] + refData[i + 1] + refData[i + 2]) / 6;
      diffImage.data[i] = grey; diffImage.data[i + 1] = grey;
      diffImage.data[i + 2] = grey; diffImage.data[i + 3] = 255;
      if( delta > ${tolerance} )
      {
        differing++;
        if( delta > maxDelta ) maxDelta = delta;
        if( x < minX ) minX = x;
        if( x > maxX ) maxX = x;
        if( y < minY ) minY = y;
        if( y > maxY ) maxY = y;
        diffImage.data[i] = 255; diffImage.data[i + 1] = 0; diffImage.data[i + 2] = 0;
      }
    }
  diffContext.putImageData(diffImage, 0, 0);

  const result = {
    differing, maxDelta, missingPerPaint,
    ticks: session.stats.ticks, painted: session.stats.painted,
    log: document.getElementById('log').textContent.slice(0, 2000),
  };
  if( differing > 0 )
  {
    result.bbox = { x: minX, y: minY, width: maxX - minX + 1, height: maxY - minY + 1 };
    result.appPng = runtime.canvas.toDataURL('image/png');
    result.refPng = ref.canvas.toDataURL('image/png');
    result.diffPng = diff.toDataURL('image/png');
  }
  return result;
})()`;
}

/* ---------------------------------------------------------------------- */

const entries = await subjects();
if( entries.length === 0 ) { console.error('nothing to compare'); process.exit(2); }
await assertServedIsCurrent();
if( !existsSync(client) )
{
    console.error(`no client at ${client}; build it with: make -C src torirs`);
    process.exit(2);
}

const bmpDir = join(outDir, 'bmp');
const diffDir = join(outDir, 'diff');
mkdirSync(bmpDir, { recursive: true });
mkdirSync(diffDir, { recursive: true });

/* Stage 1: the reference frames. Cached across runs — the C client does not
 * change under this harness — and re-taken with --recapture when it has. */
const captureFailures = [];
let captured = 0;
await pool(entries, jobs, async (entry) => {
    const path = join(bmpDir, `${entry.interfaceId}.bmp`);
    if( !recapture && existsSync(path) ) { captured++; return; }
    const produced = await captureReference(entry.interfaceId, path);
    if( !produced ) captureFailures.push(entry);
    captured++;
    if( entries.length > 20 && captured % 50 === 0 )
        process.stderr.write(`reference ${captured}/${entries.length}\n`);
});

/* Stage 2: the canvas, one interface at a time through one Chrome. */
const chrome = await launchChrome({
    profileDir: join(process.env.TMPDIR ?? '/tmp', `cs2dom-pixel-${process.pid}`),
});
const results = [];
try
{
    const page = await openPage(chrome);
    let done = 0;
    for( const entry of entries )
    {
        const bmpPath = join(bmpDir, `${entry.interfaceId}.bmp`);
        if( !existsSync(bmpPath) )
        {
            results.push({ ...entry, error: 'no reference BMP (client produced none)' });
            continue;
        }
        const refBase64 = readFileSync(bmpPath).toString('base64');
        try
        {
            await navigate(page,
                `${base}/?open=${encodeURIComponent(entry.name)}&paused=1`);
            const outcome = await evaluate(page, captureExpression(refBase64));
            if( outcome.differing > 0 )
            {
                for( const [kind, dataUrl] of [
                    ['app', outcome.appPng], ['ref', outcome.refPng], ['diff', outcome.diffPng]] )
                    writeFileSync(join(diffDir, `${entry.interfaceId}_${entry.name}_${kind}.png`),
                        Buffer.from(dataUrl.split(',')[1], 'base64'));
                delete outcome.appPng;
                delete outcome.refPng;
                delete outcome.diffPng;
            }
            results.push({ ...entry, ...outcome });
        }
        catch( error )
        {
            results.push({ ...entry, error: error.message });
        }
        done++;
        if( entries.length > 20 && done % 25 === 0 )
            process.stderr.write(`canvas ${done}/${entries.length}\n`);
    }
}
finally
{
    await chrome.close();
}

/* ---------------------------------------------------------------------- */

const matching = results.filter((row) => !row.error && row.differing === 0);
/*
 * A maxDelta of 1-2 is the BLEND-ROUNDING FLOOR, not a defect to chase: the
 * C client blends translucent fills with an integer round-to-nearest div255
 * while canvas compositing works in float, and the two round the same pixel
 * apart. It is reported separately so the actionable list is the actionable
 * list — but it is still reported, because a change that turns Δ2 into Δ20
 * must not hide in a bucket nobody reads.
 */
const rounding = results.filter((row) => !row.error && row.differing > 0 && row.maxDelta <= 2)
    .sort((a, b) => b.differing - a.differing);
const differing = results.filter((row) => !row.error && row.differing > 0 && row.maxDelta > 2)
    .sort((a, b) => b.differing - a.differing);
const failed = results.filter((row) => row.error);

writeFileSync(join(outDir, 'report.json'), `${JSON.stringify({
    schema: 'cs2dom-pixel-parity/1',
    base, client, revision, frames, ticks, tolerance,
    total: results.length,
    matching: matching.length,
    differing: differing.map(({ appPng, refPng, diffPng, ...row }) => row),
    rounding: rounding.map(({ appPng, refPng, diffPng, ...row }) => row),
    failed,
    captureFailures: captureFailures.map((entry) => entry.interfaceId),
}, null, 1)}\n`);

console.log(`pixel parity: ${matching.length} matching, `
    + `${differing.length} differing, ${rounding.length} blend-rounding only (Δ≤2), `
    + `${failed.length} failed of ${results.length} `
    + `(frames=${frames} ticks=${ticks} tolerance=${tolerance})`);
for( const row of differing )
    console.log(`  DIFF ${String(row.interfaceId).padStart(4)} ${row.name}: `
        + `${row.differing}px (max Δ${row.maxDelta}`
        + `${row.missingPerPaint ? `, ${row.missingPerPaint} assets missing` : ''}) `
        + `bbox ${row.bbox.width}x${row.bbox.height}@${row.bbox.x},${row.bbox.y}`);
for( const row of rounding )
    console.log(`  ROUND ${String(row.interfaceId).padStart(4)} ${row.name}: `
        + `${row.differing}px (max Δ${row.maxDelta})`);
for( const row of failed )
    console.log(`  FAIL ${String(row.interfaceId).padStart(4)} ${row.name}: ${row.error}`);
process.exit(differing.length + failed.length > 0 ? 1 : 0);
