/*
 * Integration test for the production C-client preview bridge.
 *
 * This intentionally uses the checked-out rev239 cache and native binary. It
 * is a separate npm target because ordinary compiler tests must also run in a
 * source-only checkout where neither artifact exists.
 */

import { createHash } from 'node:crypto';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import {
    nativePreviewFingerprint, nativePreviewStatus, renderNativeInterface,
} from '../src/native_preview.js';

const HERE = dirname(fileURLToPath(import.meta.url));
const REPO = resolve(HERE, '..', '..', '..');
const project = {
    cache: join(REPO, 'cache.osrs239'),
    nativeClient: join(REPO, 'src', process.platform === 'win32' ? 'torirs.exe' : 'torirs'),
    revision: 'osrs239',
};
const expectedBankBaseline = '78cb454a6e43bbfaa1c809099ac056f436def3e4deea85249773c058786c9bcd';
const expectedPirateCombilockBaseline =
    '9de8b099997095a2247db6e5930bbae2ee35cc05f7ce68a5f872c2bffe8f5623';

const status = nativePreviewStatus(project);
if( !status.available ) {
    process.stdout.write(`native_preview_test: skipped (${status.reason})\n`);
    process.exit(0);
}

const hash = (frame) => createHash('sha256').update(frame.rgba).digest('hex');
const check = (condition, message) => {
    if( !condition ) throw new Error(message);
};

/* Interface 12's bankmain_init registers its note button on varp 115. Varbit
 * 3958 is bit zero of that varp, so these two state declarations must produce
 * the same live-client presentation. */
const baseline = await renderNativeInterface(project, 12);
const notedByBit = await renderNativeInterface(project, 12, { state: { 'varbit:3958': 1 } });
const notedByVarp = await renderNativeInterface(project, 12, { state: { 'varp:115': 1 } });
check(hash(baseline) === expectedBankBaseline,
      `empty bankmain baseline changed: ${hash(baseline)}`);
check(hash(notedByBit) !== hash(baseline), 'bankmain state did not change a pixel');
check(hash(notedByBit) === hash(notedByVarp), 'varbit and backing varp presentations disagree');
check(baseline.tree.viewport.width === 512 && baseline.tree.viewport.height === 334,
      'native snapshot viewport is not the requested framebuffer');
check(baseline.tree.interfaceId === 12 && baseline.tree.nodes.length > 100,
      'bankmain runtime tree sidecar is missing');
check(baseline.tree.tree.length === baseline.tree.nodes.length &&
      baseline.tree.nodes.every((node) => node.box.resolved),
      'bankmain snapshot did not preserve the settled runtime topology/layout');
check(baseline.tree.nodes.some((node) => node.dynamic) &&
      baseline.tree.nodes.some((node) => node.visibility.effectiveHidden) &&
      baseline.tree.nodes.some((node) => node.hooks.length > 0),
      'bankmain snapshot omitted dynamic children, visibility, or runtime hooks');
check(nativePreviewFingerprint(project, 12) !==
      nativePreviewFingerprint(project, 12, 512, 334, { 'varbit:3958': 1 }),
      'state is absent from the render fingerprint');

/* Lock the model-heavy combilock reference which exposed several camera/model
 * parameter regressions in the browser renderer. The native sidecar records
 * fifteen model widgets and the framebuffer hash covers their settled script
 * state, overlap, clipping, and border presentation together. */
const pirateCombilock = await renderNativeInterface(project, 26);
check(hash(pirateCombilock) === expectedPirateCombilockBaseline,
      `pirate combilock baseline changed: ${hash(pirateCombilock)}`);
check(pirateCombilock.tree.nodes.filter((node) => node.widgetType === 6).length === 15,
      'pirate combilock native snapshot lost its model widgets');
check(pirateCombilock.tree.nodes.filter((node) => node.widgetType === 6)
    .every((node) => node.box.resolved && node.draw?.kind === 6),
      'pirate combilock model geometry/draw records are incomplete');

/* Stats are seeded before onLoad and then dispatched once, after hooks exist,
 * against only the supplied skill ids. The stats panel provides a compact
 * pixel-level assertion for that path. */
const statsBaseline = await renderNativeInterface(project, 320, { width: 190, height: 261 });
const boostedAttack = await renderNativeInterface(project, 320, {
    width: 190,
    height: 261,
    state: { 'stat:0': 77 },
});
check(hash(statsBaseline) !== hash(boostedAttack), 'stat state did not change the stats panel');

process.stdout.write(
    `native_preview_test: ok (bank ${hash(baseline)}, state ${hash(notedByBit)}, ` +
    `pirate ${hash(pirateCombilock)})\n`);
