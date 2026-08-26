/*
 * The JavaScript-native canvas dev server, as its own entry.
 *
 * `cs2dom dev-canvas` now serves the REAL client (torirs.wasm in an iframe,
 * driven over the cmdbus) — that is the tool's future. This entry keeps the
 * canvas engine reachable for what still needs it: the pixel-parity harness
 * compares the canvas PAINTER against the C client, and a harness whose
 * subject can no longer be started is a harness in name only.
 *
 *   node scripts/serve_canvas.mjs [--project example] [--port 8099]
 */

import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import { serveCanvas } from '../src/dev_canvas.js';
import { loadProject } from '../src/build.js';

const HERE = dirname(fileURLToPath(import.meta.url));
const args = process.argv.slice(2);
const flag = (name) => {
    const index = args.indexOf(name);
    return index >= 0 && index + 1 < args.length ? args[index + 1] : null;
};

const project = loadProject(flag('--project') ?? 'example');
serveCanvas({
    root: resolve(join(HERE, '..')),
    contentDir: project.content ?? null,
    cache: project.cache ?? null,
    revision: project.revision ?? null,
    names: project.cs2Names ?? null,
    port: Number(flag('--port') ?? 8099),
    onListen: (address) => process.stdout.write(`cs2dom canvas: ${address}\n`),
});
