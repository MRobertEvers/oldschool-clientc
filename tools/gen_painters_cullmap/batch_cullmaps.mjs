#!/usr/bin/env node
/**
 * Batch-generate painters cullmap .bin files. Requires `make` to build ./gen_painters_cullmap first.
 *
 * Single source of truth for viewport/radius grids: keep in sync with PCULL_BAKE_* in
 * src/osrs/painters_cullmap_baked_path.c. Basename pattern matches painters_cullmap_baked_format_* in
 * src/osrs/painters_cullmap_baked_path.c.
 */

import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const execFileAsync = promisify(execFile);

const __dirname = path.dirname(fileURLToPath(import.meta.url));

/** Default near clip Z in generated filenames (must match runtime when loading). */
const DEFAULT_NEAR = 50;

const BAKE_SCREEN = { min: 100, max: 2000, step: 100 };
const BAKE_RADIUS = { min: 5, max: 50, step: 5 };

function defaultJobCount() {
  const n = typeof os.availableParallelism === 'function' ? os.availableParallelism() : os.cpus().length;
  return Math.max(1, n || 4);
}

function usage() {
  console.error(`usage: node batch_cullmaps.mjs [options]
  --near <int>     near clip z (default ${DEFAULT_NEAR})
  --out-dir <path> output directory (default: repo src/osrs/revconfig/configs/cullmaps)
  -j [N], --jobs [N]  run up to N generators in parallel (default: CPU count; 1 = serial)
  --dry-run        print commands only
  -h, --help       this message`);
}

function parseArgs(argv) {
  let near = DEFAULT_NEAR;
  let outDir = path.join(__dirname, '../../src/osrs/revconfig/configs/cullmaps');
  let dryRun = false;
  let jobs = defaultJobCount();
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a === '-h' || a === '--help') {
      usage();
      process.exit(0);
    }
    if (a === '--near' && argv[i + 1]) {
      near = parseInt(argv[++i], 10);
      if (!Number.isFinite(near)) {
        console.error('batch_cullmaps: bad --near');
        process.exit(1);
      }
      continue;
    }
    if (a === '--out-dir' && argv[i + 1]) {
      outDir = path.resolve(argv[++i]);
      continue;
    }
    if (a === '--dry-run') {
      dryRun = true;
      continue;
    }
    if (a === '-j' || a === '--jobs') {
      const next = argv[i + 1];
      if (next !== undefined && /^\d+$/.test(next)) {
        jobs = parseInt(argv[++i], 10);
      } else {
        jobs = defaultJobCount();
      }
      if (jobs < 1) {
        console.error('batch_cullmaps: jobs must be >= 1');
        process.exit(1);
      }
      continue;
    }
    if (/^-j\d+$/.test(a)) {
      jobs = parseInt(a.slice(2), 10);
      if (jobs < 1) {
        console.error('batch_cullmaps: jobs must be >= 1');
        process.exit(1);
      }
      continue;
    }
    if (a.startsWith('--jobs=')) {
      const v = a.slice('--jobs='.length);
      jobs = parseInt(v, 10);
      if (!Number.isFinite(jobs) || jobs < 1) {
        console.error('batch_cullmaps: bad --jobs=');
        process.exit(1);
      }
      continue;
    }
    console.error(`batch_cullmaps: unknown arg: ${a}`);
    usage();
    process.exit(1);
  }
  return { near, outDir, dryRun, jobs };
}

const genExe = path.join(__dirname, 'gen_painters_cullmap');

function fPart(nz) {
  return nz >= 0 ? `f${nz}` : `fm${-nz}`;
}

function collectTasks(near, outDir) {
  const tasks = [];
  for (let w = BAKE_SCREEN.min; w <= BAKE_SCREEN.max; w += BAKE_SCREEN.step) {
    for (let h = BAKE_SCREEN.min; h <= BAKE_SCREEN.max; h += BAKE_SCREEN.step) {
      for (let r = BAKE_RADIUS.min; r <= BAKE_RADIUS.max; r += BAKE_RADIUS.step) {
        const base = `painters_cullmap_baked_r${r}_${fPart(near)}_w${w}_h${h}.bin`;
        const outPath = path.join(outDir, base);
        const args = [
          `--radius`,
          String(r),
          `--near`,
          String(near),
          `--width`,
          String(w),
          `--height`,
          String(h),
          `--blob`,
          `-o`,
          outPath,
        ];
        tasks.push({ base, args });
      }
    }
  }
  return tasks;
}

async function runPool(genExePath, tasks, concurrency) {
  let next = 0;
  let ok = 0;
  let fail = 0;

  async function worker() {
    for (;;) {
      const i = next++;
      if (i >= tasks.length) break;
      const t = tasks[i];
      try {
        /* Do not use stdio: 'ignore' here: parallel runs would hide gen_painters_cullmap stderr
         * (fopen errors, painters_cullmap_build failed, etc.). Default pipes are tiny per task. */
        await execFileAsync(genExePath, t.args, {
          maxBuffer: 64 * 1024 * 1024,
          encoding: 'utf8',
        });
        ok++;
      } catch (err) {
        fail++;
        console.error(`batch_cullmaps: failed: ${t.base}`);
        const es = err && typeof err.stderr === 'string' && err.stderr.trim();
        if (es) console.error(es);
        if (err && err.message) console.error(`batch_cullmaps: ${err.message}`);
      }
    }
  }

  const nWorkers = Math.min(concurrency, tasks.length);
  await Promise.all(Array.from({ length: nWorkers }, () => worker()));
  return { ok, fail };
}

async function main() {
  const { near, outDir, dryRun, jobs } = parseArgs(process.argv.slice(2));

  if (!fs.existsSync(genExe)) {
    console.error(`batch_cullmaps: missing ${genExe} — run make in tools/gen_painters_cullmap`);
    process.exit(1);
  }

  if (!dryRun) {
    fs.mkdirSync(outDir, { recursive: true });
  }

  const tasks = collectTasks(near, outDir);

  if (dryRun) {
    for (const t of tasks) {
      console.log(`${genExe} ${t.args.join(' ')}`);
    }
    console.error(`batch_cullmaps: dry-run ${tasks.length} commands`);
    return;
  }

  const { ok, fail } = await runPool(genExe, tasks, jobs);
  console.error(
    `batch_cullmaps: done ok=${ok} fail=${fail} jobs=${jobs}`,
  );
  if (fail > 0) process.exit(1);
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
