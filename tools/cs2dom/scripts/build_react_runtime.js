import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

import { build } from 'esbuild';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');

await build({
    entryPoints: [join(root, 'src', 'react_browser_runtime.js')],
    outfile: join(root, 'web', 'react_browser_runtime.js'),
    bundle: true,
    format: 'esm',
    platform: 'browser',
    target: ['es2022'],
    minify: true,
    legalComments: 'none',
    sourcemap: false,
    logLevel: 'warning',
});

/* The TypeScript VM is emitted as reviewable TS from shared semantics, then
 * compiled to browser ESM. It stays a whole-script opt-in backend; unsupported
 * closures continue to use the production C/WASM VM. */
await build({
    entryPoints: [join(root, 'src', 'cs2_vm_core.ts')],
    outfile: join(root, 'web', 'cs2_vm_core.js'),
    bundle: true,
    format: 'esm',
    platform: 'browser',
    target: ['es2022'],
    minify: true,
    legalComments: 'none',
    sourcemap: false,
    logLevel: 'warning',
});

/* Exact Dat2 clientscript records are decoded independently of backend
 * execution. This bundle is the future whole-closure routing seam; the live
 * preview deliberately remains on C/WASM until a complete closure passes the
 * generated TS semantics gates. */
await build({
    entryPoints: [join(root, 'src', 'cs2_bytecode_decoder.ts')],
    outfile: join(root, 'web', 'cs2_bytecode_decoder.js'),
    bundle: true,
    format: 'esm',
    platform: 'browser',
    target: ['es2022'],
    minify: true,
    legalComments: 'none',
    sourcemap: false,
    logLevel: 'warning',
});

/* Whole-closure backend selection and the core-only TypeScript session ship as
 * one worker-loadable module. The production worker imports it lazily only for
 * explicit `typescript`/`auto` sessions; the default C/WASM path is unchanged. */
await build({
    entryPoints: [join(root, 'src', 'cs2_engine_router.ts')],
    outfile: join(root, 'web', 'cs2_engine_router.js'),
    bundle: true,
    format: 'esm',
    platform: 'browser',
    target: ['es2022'],
    minify: true,
    legalComments: 'none',
    sourcemap: false,
    logLevel: 'warning',
});

/* Node/CI coverage tooling consumes the exact decoder and the same fail-closed
 * router used by the browser. A bundle keeps the audit runnable without a
 * TypeScript loader. */
await build({
    entryPoints: [join(root, 'src', 'cs2_backend_coverage.ts')],
    outfile: join(root, 'web', 'cs2_backend_coverage.js'),
    bundle: true,
    format: 'esm',
    platform: 'browser',
    target: ['es2022'],
    minify: true,
    legalComments: 'none',
    sourcemap: false,
    logLevel: 'warning',
});
