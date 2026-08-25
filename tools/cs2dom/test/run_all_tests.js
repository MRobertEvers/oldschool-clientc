import { spawnSync } from 'node:child_process';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

/* Keep the focused HOST parity suites isolated from the large compiler/runtime
 * suite. A failed suite is reported without preventing the remaining suites
 * from running, and this entrypoint never shells back through npm or make. */
const SUITES = Object.freeze([
    'host_activity_test.js',
    'host_chat_social_test.js',
    'host_chat_transmit_test.js',
    'host_db_test.js',
    'host_loot_test.js',
    'host_overlay_test.js',
    'host_subject_test.js',
    'host_state_transmit_test.js',
    'host_worldmap_test.js',
    'font_runtime_test.js',
    'dev_page_render_test.js',
    'model_render_worker_test.js',
    'runtime_worker_test.js',
    'preview_target_box_test.js',
    'fast_host_edge_test.js',
    'bankmain_wasm_stress_test.js',
    'run_tests.js',
]);

const here = dirname(fileURLToPath(import.meta.url));
const failed = [];

for( const suite of SUITES ) {
    process.stdout.write(`[cs2dom test] ${suite}\n`);
    const run = spawnSync(process.execPath, [join(here, suite)], { stdio: 'inherit' });
    if( run.status !== 0 ) failed.push(suite);
}

if( failed.length > 0 ) {
    process.stderr.write(`cs2dom test suites failed: ${failed.join(', ')}\n`);
    process.exitCode = 1;
}
