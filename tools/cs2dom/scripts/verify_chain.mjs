/*
 * The whole chain, on a real interface and a real cache.
 *
 *   if_binary -> if_text -> [one field edited] -> if_text -> if_binary -> read back
 *
 * The unit suites prove each link; this proves they JOIN, and it proves the
 * property that makes the round trip usable rather than merely correct: the
 * edit produces exactly one changed line, and that change survives all the way
 * into the packed bytes the client reads.
 *
 * Slow — it copies the content tree and a cache — so it is its own target
 * rather than part of `make test`.
 *
 *   node scripts/verify_chain.mjs <scratch-dir>
 */
import { execFileSync } from 'node:child_process';
import { cpSync, readFileSync, mkdirSync, rmSync, existsSync } from 'node:fs';
import { join } from 'node:path';

import { fileURLToPath } from 'node:url';
import { dirname } from 'node:path';

const HERE = dirname(fileURLToPath(import.meta.url));
const REPO = join(HERE, '..', '..', '..');
const TOOL = join(HERE, '..');
const { saveInterface } = await import(join(TOOL, 'src/export.js'));
const { parseIf } = await import(join(TOOL, 'src/if_record.js'));

const scratch = process.argv[2];
const tree = join(scratch, 'content');
const cachepack = join(REPO, '3rd/rscache/tools/cachepack/cachepack');

/* A private copy of the content tree, so nothing edits the checkout. */
cpSync(join(REPO, 'OSRS-Content/osrs239-content'), tree, { recursive: true });

const NAME = 'vm_kudos_info';
const ifPath = join(tree, 'interfaces', `${NAME}.if`);
const before = readFileSync(ifPath, 'utf8');
const originalWidth = parseIf(before).get('universe', 'width');

/* --- the edit --- */
const NEW_WIDTH = String(Number(originalWidth) + 7);
const saved = saveInterface({
    contentDir: tree, name: NAME, edits: { universe: { width: NEW_WIDTH } },
});

const after = readFileSync(ifPath, 'utf8');
const changedLines = before.split('\n').filter((line, i) => line !== after.split('\n')[i]);

/* --- bake --- */
const out = join(scratch, 'cache');
mkdirSync(out, { recursive: true });
let packStatus = 0, packOut = '';
try {
    /* Interfaces are an ASSET, not a config type: `--asset-only` with
     * `--assets=interfaces` is the form that writes table 3. */
    cpSync(join(REPO, 'cache.osrs239'), out, { recursive: true });
    packOut = execFileSync(cachepack, [
        'pack', '--src', tree, '--out', out, '--rev', 'osrs239',
        '--asset-only', '--assets=interfaces',
    ], { encoding: 'utf8', stdio: 'pipe' });
} catch (error) {
    packStatus = error.status ?? 1;
    packOut = `${error.stdout ?? ''}${error.stderr ?? ''}`;
}

/* --- read it back out of the BINARY --- */
const back = join(scratch, 'back');
mkdirSync(back, { recursive: true });
let unpackErr = '';
try {
    execFileSync(cachepack, [
        'unpack', '--cache', out, '--rev', 'osrs239', '--src', back,
        '--assets=interfaces',
    ], { encoding: 'utf8', stdio: 'pipe' });
} catch (error) { unpackErr = `${error.stdout ?? ''}${error.stderr ?? ''}`.slice(-400); }

const backPath = join(back, 'interfaces', `${NAME}.if`);
const roundTripped = existsSync(backPath)
    ? parseIf(readFileSync(backPath, 'utf8')).get('universe', 'width')
    : null;

const report = {
    originalWidth,
    editedTo: NEW_WIDTH,
    filesWritten: saved.written.length,
    linesChangedInIfText: changedLines,
    packExit: packStatus,
    packTail: packOut.trim().split('\n').slice(-2),
    widthReadBackFromBinary: roundTripped,
    chainHolds: roundTripped === NEW_WIDTH,
    unpackErr: unpackErr || undefined,
};
console.log(JSON.stringify(report, null, 2));

/* The gate: one line changed, and that line survived to the binary. */
if( !report.chainHolds || report.linesChangedInIfText.length !== 1 )
{
    console.error('the chain does not hold');
    process.exit(1);
}
rmSync(scratch, { recursive: true, force: true });
