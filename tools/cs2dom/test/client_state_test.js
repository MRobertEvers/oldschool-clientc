import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import {
    OSRS239_LOGIN_STATE, clientStateForRevision, osrs239ClientState,
} from '../src/client_state.js';

const HERE = dirname(fileURLToPath(import.meta.url));
const REPO = resolve(HERE, '../../..');

/* This is deliberately tied to the C/server reference rather than inferred
 * from the failing DIV. ToriRSServer_Login sends script 605 exactly these four
 * camera bounds, and the literal wire test independently gates their order. */
assert.deepEqual(OSRS239_LOGIN_STATE, {
    'varc:1338': 128,
    'varc:1339': 896,
    'varc:1340': 128,
    'varc:1341': 896,
});
const loginSource = readFileSync(
    resolve(REPO, 'src/torirsserver/torirs_server_world.c'), 'utf8');
assert.match(loginSource,
    /static const int zoom_limits\[4\]\s*=\s*\{\s*128,\s*896,\s*128,\s*896\s*\}/,
    'JavaScript login state drifted from the C client/server bootstrap');
const wireTest = readFileSync(
    resolve(REPO, 'src/torirsserver/test/mock239_runclientscript_test.c'), 'utf8');
assert.match(wireTest, /script 605 carries 128,896,128,896/,
    'the C literal-wire provenance for the zoom bootstrap is missing');

assert.deepEqual(osrs239ClientState({ 'varc:1339': 777, 'varp:5': 9 }), {
    'varc:1338': 128,
    'varc:1339': 777,
    'varc:1340': 128,
    'varc:1341': 896,
    'varp:5': 9,
}, 'explicit host/interface state must override login defaults');

assert.deepEqual(clientStateForRevision('osrs239', { 'varc:1338': 321 }), {
    'varc:1338': 321,
    'varc:1339': 896,
    'varc:1340': 128,
    'varc:1341': 896,
}, 'the live rev-239 preview did not receive its client-global bootstrap');
assert.deepEqual(clientStateForRevision('future', { 'varc:1338': 321 }), {
    'varc:1338': 321,
}, 'an unknown cache revision inherited rev-239 client globals');

console.log('client state fixtures passed');
