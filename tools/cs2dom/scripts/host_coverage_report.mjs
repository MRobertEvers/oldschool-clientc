/*
 * What "host coverage" actually measures.
 *
 * "501 of 1,141 methods" is a true number and a misleading one. The surface
 * lists every command the DECOMPILER can name, and the reference client does
 * not implement most of them either: the clan channel, the trading post, the
 * stock market, the hiscores and the world-map editor all reach
 * `CS2VM2_Op_StackMetaStub`, which balances the stack, pushes zeros and
 * announces the opcode once. This runtime reproduces that exactly.
 *
 * So the number worth tracking is not "how many of 1,141" but "how many of
 * the ones the reference ANSWERS". A method the C client stubs is not a gap —
 * implementing it would mean inventing behaviour the reference does not have,
 * and a fabricated clan roster is worse than an empty one.
 *
 * The C's own manifest of what it answers is
 * `src/cs2vm2/cs2vm2_host_request_kinds.def`: one line per request the host
 * can be asked to execute, keyed by opcode.
 *
 *   node scripts/host_coverage_report.mjs [--missing]
 */

import { readFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import { hostCoverage, HostKernel } from '../src/host_kernel.js';
import { HOST_SURFACE } from '../src/generated/cs2_host_surface.js';

const HERE = dirname(fileURLToPath(import.meta.url));
const REPO = resolve(join(HERE, '..', '..', '..'));
const MANIFEST = join(REPO, 'src', 'cs2vm2', 'cs2vm2_host_request_kinds.def');

/** Opcodes the C host has a request kind for — the ones it really answers. */
function referenceOpcodes() {
    const opcodes = new Set();
    for( const line of readFileSync(MANIFEST, 'utf8').split('\n') )
    {
        const match = /^CS2VM_HOST_REQUEST_KIND\(\s*([A-Z0-9_]+)\s*,\s*(\d+)/.exec(line.trim());
        if( match ) opcodes.add(Number(match[2]));
    }
    return opcodes;
}

const answered = referenceOpcodes();
const { implemented, total, missing } = hostCoverage();
const missingSet = new Set(missing);

let referenceTotal = 0;
let referenceDone = 0;
const referenceMissing = [];
let stubbedTotal = 0;
let stubbedDone = 0;

for( const [name, row] of HOST_SURFACE )
{
    const isAnswered = answered.has(row.opcode);
    for( const method of row.dotCapable ? [name, `dot_${name}`] : [name] )
    {
        if( !(method in HostKernel.prototype) ) continue;
        const done = !missingSet.has(method);
        if( isAnswered )
        {
            referenceTotal++;
            if( done ) referenceDone++;
            else referenceMissing.push(`${method} (${row.opcode})`);
        }
        else
        {
            stubbedTotal++;
            if( done ) stubbedDone++;
        }
    }
}

const percent = (a, b) => (b === 0 ? '100.0' : ((a / b) * 100).toFixed(1));

console.log(JSON.stringify({
    surface: { implemented, total, percent: percent(implemented, total) },
    /* The number that means something. */
    referenceAnswers: {
        implemented: referenceDone, total: referenceTotal,
        percent: percent(referenceDone, referenceTotal),
    },
    /* Methods the reference stubs too. Implementing one is inventing. */
    referenceStubs: {
        implemented: stubbedDone, total: stubbedTotal,
        note: 'the C client balances the stack and pushes zeros for these; '
            + 'this runtime does the same and records each one it faked',
    },
    missingCount: referenceMissing.length,
}, null, 2));

if( process.argv.includes('--missing') )
    for( const entry of referenceMissing.sort() ) console.log(entry);
