#!/usr/bin/env node
import { createHash } from "crypto";
import { execFileSync, spawnSync } from "child_process";
import fs from "fs";
import path from "path";
import { fileURLToPath } from "url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const REPO_ROOT = path.resolve(__dirname, "../..");
const TS_ROOT = path.resolve(REPO_ROOT, "../xrsps-typescript");
const GOLDEN_DIR = path.join(__dirname, "golden");
const OUT_DIR = path.join(__dirname, "out");
const MANIFEST_PATH = path.join(__dirname, "manifest.json");

function parseArgs(argv) {
    const opts = {
        suite: "all",
        caseId: null,
        cache: null,
        regen: false,
        binary: path.join(__dirname, "cs2_parity"),
    };
    for (let i = 2; i < argv.length; i++) {
        const a = argv[i];
        if (a === "--suite" && argv[i + 1]) opts.suite = argv[++i];
        else if (a === "--case" && argv[i + 1]) opts.caseId = argv[++i];
        else if (a === "--cache" && argv[i + 1]) opts.cache = argv[++i];
        else if (a === "--binary" && argv[i + 1]) opts.binary = argv[++i];
        else if (a === "--regen") opts.regen = true;
    }
    return opts;
}

function loadManifest() {
    return JSON.parse(fs.readFileSync(MANIFEST_PATH, "utf8"));
}

function resolveCacheDir(manifest, override) {
    if (override) return path.resolve(override);
    return path.resolve(TS_ROOT, "caches", manifest.cache.name);
}

function readRgba(filePath) {
    const buf = fs.readFileSync(filePath);
    const width = buf.readUInt32LE(0);
    const height = buf.readUInt32LE(4);
    const rgba = buf.subarray(8);
    return { width, height, rgba, hash: createHash("sha256").update(rgba).digest("hex") };
}

function firstTraceDivergence(golden, actual) {
    const n = Math.min(golden.length, actual.length);
    for (let i = 0; i < n; i++) {
        const g = golden[i];
        const a = actual[i];
        for (const key of ["step", "pc", "opcode", "intSp", "strSp", "topInt"]) {
            if (g[key] !== a[key]) return { index: i, key, golden: g[key], actual: a[key] };
        }
    }
    if (golden.length !== actual.length) {
        return { index: n, key: "length", golden: golden.length, actual: actual.length };
    }
    return null;
}

function compareJsonArtifact(name, goldenPath, actualPath, compareFn) {
    if (!fs.existsSync(goldenPath)) {
        console.error(`MISSING GOLDEN: ${name} (${goldenPath})`);
        return false;
    }
    if (!fs.existsSync(actualPath)) {
        console.error(`MISSING ACTUAL: ${name} (${actualPath})`);
        return false;
    }
    const golden = JSON.parse(fs.readFileSync(goldenPath, "utf8"));
    const actual = JSON.parse(fs.readFileSync(actualPath, "utf8"));
    const diff = compareFn(golden, actual);
    if (diff) {
        console.error(`FAIL ${name}: ${JSON.stringify(diff)}`);
        return false;
    }
    console.log(`PASS ${name}`);
    return true;
}

function compareCs2(golden, actual) {
    if (golden.status !== actual.status) return { field: "status", golden: golden.status, actual: actual.status };
    if (JSON.stringify(golden.intStack) !== JSON.stringify(actual.intStack)) {
        return { field: "intStack", golden: golden.intStack, actual: actual.intStack };
    }
    if (JSON.stringify(golden.stringStack) !== JSON.stringify(actual.stringStack)) {
        return { field: "stringStack", golden: golden.stringStack, actual: actual.stringStack };
    }
  // Trace comparison is most useful for synthetic VM-only cases where both VMs run the same bytecode.
    if (golden.caseId === "math_add" || golden.caseId === "enum_lookup") {
        const traceDiff = firstTraceDivergence(golden.trace ?? [], actual.trace ?? []);
        if (traceDiff) return { field: "trace", ...traceDiff };
    }
    return null;
}

function compareSpriteMeta(golden, actual) {
    for (const key of ["width", "height", "spriteId"]) {
        if (golden[key] !== actual[key]) return { field: key, golden: golden[key], actual: actual[key] };
    }
    const goldenRgba = readRgba(goldenPathForSprite(golden.caseId));
    const actualRgba = readRgba(actualPathForSprite(actual.caseId));
    if (goldenRgba.hash !== actualRgba.hash) {
        return { field: "pixelHash", golden: goldenRgba.hash, actual: actualRgba.hash };
    }
    return null;
}

function goldenPathForSprite(caseId) {
    return path.join(GOLDEN_DIR, `sprite_${caseId}.rgba`);
}

function actualPathForSprite(caseId) {
    return path.join(OUT_DIR, `sprite_${caseId}.rgba`);
}

function compareTree(golden, actual) {
    const goldenByPath = new Map();
    for (const node of golden.nodes) {
        if (!goldenByPath.has(node.path)) goldenByPath.set(node.path, node);
    }
    const actualByPath = new Map();
    for (const node of actual.nodes) {
        if (!actualByPath.has(node.path)) actualByPath.set(node.path, node);
    }

    for (const [path, g] of goldenByPath) {
        const a = actualByPath.get(path);
        if (!a) {
            return { field: "missingPath", path, golden: g, actual: null };
        }
        for (const key of ["type", "x", "y", "w", "h", "spriteId", "color", "obj", "dynamic"]) {
            if (g[key] !== a[key]) {
                return { field: `nodes.${path}.${key}`, golden: g[key], actual: a[key] };
            }
        }
        if ((g.text ?? null) !== (a.text ?? null)) {
            return { field: `nodes.${path}.text`, golden: g.text, actual: a.text };
        }
    }

    for (const path of actualByPath.keys()) {
        if (!goldenByPath.has(path)) {
            return { field: "extraPath", path, golden: null, actual: actualByPath.get(path) };
        }
    }

    return null;
}

function runBinary(binary, cacheDir, args) {
    execFileSync(binary, [cacheDir, ...args], { stdio: "inherit", cwd: __dirname });
}

function regenGoldens(cacheDir) {
    const extra = cacheDir ? [`--cache=${cacheDir}`] : [];
    const r = spawnSync("tsx", [path.join(TS_ROOT, "scripts/parity/gen-all.ts"), ...extra], {
        cwd: TS_ROOT,
        stdio: "inherit",
    });
    if (r.status !== 0) process.exit(r.status ?? 1);
}

function main() {
    const opts = parseArgs(process.argv);
    const manifest = loadManifest();
    const cacheDir = resolveCacheDir(manifest, opts.cache);

    if (!fs.existsSync(cacheDir)) {
        console.error(`Cache not found: ${cacheDir}`);
        process.exit(1);
    }

    if (opts.regen) {
        console.log("Regenerating goldens from TypeScript reference...");
        regenGoldens(cacheDir);
    }

    fs.mkdirSync(OUT_DIR, { recursive: true });

    if (!fs.existsSync(opts.binary)) {
        console.log("Building cs2_parity...");
        execFileSync("make", [], { cwd: __dirname, stdio: "inherit" });
    }

    let ok = true;

    if (opts.suite === "all" || opts.suite === "cs2") {
        for (const csCase of manifest.cs2Cases) {
            if (opts.caseId && csCase.id !== opts.caseId) continue;
            const outPath = path.join(OUT_DIR, `cs2_${csCase.id}.json`);
            runBinary(opts.binary, cacheDir, ["exec", csCase.id, outPath]);
            ok &= compareJsonArtifact(
                `cs2/${csCase.id}`,
                path.join(GOLDEN_DIR, `cs2_${csCase.id}.json`),
                outPath,
                compareCs2,
            );
        }
    }

    if (opts.suite === "all" || opts.suite === "sprite") {
        for (const sp of manifest.spriteCases) {
            if (opts.caseId && sp.id !== opts.caseId) continue;
            runBinary(opts.binary, cacheDir, ["sprite", sp.id, OUT_DIR]);
            ok &= compareJsonArtifact(
                `sprite/${sp.id}`,
                path.join(GOLDEN_DIR, `sprite_${sp.id}.json`),
                path.join(OUT_DIR, `sprite_${sp.id}.json`),
                (golden, actual) => compareSpriteMeta(golden, actual),
            );
        }
    }

    if (opts.suite === "all" || opts.suite === "tree") {
        for (const treeCase of manifest.treeCases) {
            if (opts.caseId && treeCase.id !== opts.caseId) continue;
            const outPath = path.join(OUT_DIR, `tree_${treeCase.id}.json`);
            runBinary(opts.binary, cacheDir, ["tree", treeCase.id, outPath]);
            ok &= compareJsonArtifact(
                `tree/${treeCase.id}`,
                path.join(GOLDEN_DIR, `tree_${treeCase.id}.json`),
                outPath,
                compareTree,
            );
        }
    }

    process.exit(ok ? 0 : 1);
}

main();
