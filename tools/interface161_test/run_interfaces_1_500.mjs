#!/usr/bin/env node
/**
 * Run interface161_test for CACHE_INTERFACES archive ids 1..500.
 *
 * Usage:
 *   node run_interfaces_1_500.mjs <cache_directory> [--sprites] [--out-dir DIR] [--binary PATH]
 *
 * BMPs are written as <out-dir>/interface_<id>.bmp (zero-padded to 3 digits).
 */

import { execFileSync } from 'node:child_process';
import { mkdirSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));

const IFACE_START = 1;
const IFACE_END = 500;

function parseArgs(argv) {
    const cacheDir = argv[0];
    let wantSprites = false;
    let outDir = path.join(process.cwd(), 'interface_exports');
    let binary =
        process.platform === 'win32'
            ? path.join(__dirname, '..', '..', '..', 'build', 'interface161_test.exe')
            : path.join(__dirname, '..', '..', '..', 'build', 'interface161_test');

    const rest = argv.slice(1);
    for (let i = 0; i < rest.length; i++) {
        if (rest[i] === '--sprites') {
            wantSprites = true;
        } else if (rest[i] === '--out-dir' && rest[i + 1]) {
            outDir = path.resolve(rest[++i]);
        } else if (rest[i] === '--binary' && rest[i + 1]) {
            binary = path.resolve(rest[++i]);
        } else {
            console.error(`Unknown argument: ${rest[i]}`);
            process.exit(1);
        }
    }

    return { cacheDir, wantSprites, outDir, binary };
}

function main() {
    const argv = process.argv.slice(2);
    if (!argv.length || argv[0] === '-h' || argv[0] === '--help') {
        console.error(
            'usage: node run_interfaces_1_500.mjs <cache_directory> [--sprites] [--out-dir DIR] [--binary PATH]'
        );
        process.exit(argv.length ? 0 : 1);
    }

    const { cacheDir, wantSprites, outDir, binary } = parseArgs(argv);
    if (!cacheDir) {
        console.error('error: cache_directory required');
        process.exit(1);
    }

    mkdirSync(outDir, { recursive: true });

    const results = { ok: 0, fail: 0 };

    for (let id = IFACE_START; id <= IFACE_END; id++) {
        const name = `interface_${String(id).padStart(3, '0')}.bmp`;
        const outPath = path.join(outDir, name);
        const args = [cacheDir, '--iface', String(id)];
        if (wantSprites) {
            args.push('--sprites');
        }
        args.push(outPath);

        try {
            execFileSync(binary, args, {
                stdio: ['ignore', 'pipe', 'pipe'],
                encoding: 'utf8',
            });
            results.ok++;
            process.stdout.write(`${id}\t${outPath}\n`);
        } catch (e) {
            results.fail++;
            const err = e.stderr || e.message || String(e);
            process.stderr.write(`${id}\tfail\t${err.trim()}\n`);
        }
    }

    process.stderr.write(
        `\ndone: ${results.ok} ok, ${results.fail} failed (range ${IFACE_START}-${IFACE_END})\n`
    );
    process.exit(results.fail > 0 ? 1 : 0);
}

main();
