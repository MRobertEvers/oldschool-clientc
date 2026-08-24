/*
 * Exact Dat2 interface preview.
 *
 * The browser preview is useful while authoring, but it must not grow a second
 * implementation of the client. This module invokes the production native
 * App/UITree/Soft3D path in src/torirs and converts its deterministic BMP into
 * the RGBA/PNG forms Node and a browser want.
 */

import { spawn } from 'node:child_process';
import { createHash } from 'node:crypto';
import {
    existsSync, mkdtempSync, readFileSync, readdirSync, rmSync, statSync, writeFileSync,
} from 'node:fs';
import { tmpdir } from 'node:os';
import { basename, dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import { decodeBmp, encodePng } from './png.js';
import { parseNativeTree } from './native_tree.js';

const HERE = dirname(fileURLToPath(import.meta.url));
const REPO = resolve(HERE, '..', '..', '..');
const DEFAULT_BINARY = join(REPO, 'src', process.platform === 'win32' ? 'torirs.exe' : 'torirs');
const MAX_LOG_BYTES = 1024 * 1024;
const MAX_STATE_BYTES = 1024 * 1024;
const MAX_STATE_RECORDS = 4096;
const MAX_STATE_ID = 65535;
const MAX_STATE_STRING = 4096;
const STATE_KIND = { varp: 1, varbit: 2, varc: 3, varcstr: 4, stat: 5 };

/** Describe whether the native reference renderer can serve this project. */
export function nativePreviewStatus(project = {}) {
    const binary = resolve(project.nativeClient || DEFAULT_BINARY);
    const cache = project.cache ? resolve(project.cache) : null;
    if( !existsSync(binary) )
        return { available: false, binary, cache,
            reason: `native client is not built (run make -C ${join(REPO, 'src')} torirs)` };
    if( !cache || !existsSync(join(cache, 'main_file_cache.dat2')) )
        return { available: false, binary, cache,
            reason: 'a Dat2 cache directory is required for the native preview' };
    return { available: true, binary, cache, reason: null };
}

/**
 * Render one cache interface through the real C client.
 *
 * The returned `rgba` is top-down RGBA. `bmp` and `png` contain the same frame,
 * making the function suitable for a file-writing CLI or an HTTP response.
 */
export async function renderNativeInterface(project, interfaceId, {
    width = 512,
    height = 334,
    timeoutMs = 30_000,
    state = {},
} = {}) {
    const status = nativePreviewStatus(project);
    if( !status.available ) throw new Error(status.reason);
    if( !Number.isInteger(interfaceId) || interfaceId <= 0 )
        throw new Error(`invalid interface id '${interfaceId}'`);
    if( !Number.isInteger(width) || !Number.isInteger(height) ||
        width <= 0 || height <= 0 || width > 4096 || height > 4096 )
        throw new Error(`invalid native preview size ${width}x${height}`);
    if( !Number.isInteger(timeoutMs) || timeoutMs < 1 || timeoutMs > 120_000 )
        throw new Error(`invalid native preview timeout '${timeoutMs}'`);

    const revision = project.revision || inferRevision(status.cache) || 'osrs239';
    const manifest = project.manifest
        ? resolve(project.manifest)
        : discoverManifest(revision);
    const scratch = mkdtempSync(join(tmpdir(), 'cs2dom-native-preview-'));
    const bitmap = join(scratch, `interface-${interfaceId}.bmp`);
    const treeFile = join(scratch, `interface-${interfaceId}.tree.json`);
    const statePacket = encodeNativeState(state);
    const stateFile = join(scratch, `interface-${interfaceId}.state`);

    try {
        if( statePacket.readUInt32LE(8) > 0 ) writeFileSync(stateFile, statePacket);
        const args = [status.cache, String(interfaceId)];
        if( manifest ) args.push('--manifest', manifest);
        else args.push('--rev', revision);
        args.push('--offline', '--no-js5', '--soft3d');

        const previewEnv = {
            ...process.env,
            SDL_VIDEODRIVER: 'dummy',
            SDL_AUDIODRIVER: 'dummy',
            TORIRS_PLUGINS: '0',
            TORIRS_PREFS: '',
            TORIRS_PLUGIN_PREFS: '',
            TORIRS_ROOT_SIZE: `${width}x${height}`,
            TORIRS_PREVIEW_BMP: bitmap,
            TORIRS_PREVIEW_TREE: treeFile,
        };
        /* Do not inherit a caller's state packet accidentally. */
        delete previewEnv.TORIRS_PREVIEW_STATE;
        if( statePacket.readUInt32LE(8) > 0 ) previewEnv.TORIRS_PREVIEW_STATE = stateFile;

        const result = await runBounded(status.binary, args, {
            cwd: REPO,
            timeoutMs,
            env: previewEnv,
        });
        if( result.code !== 0 || result.timedOut || result.overflow )
            throw new Error(nativeFailure(result, status.binary, args));
        if( !existsSync(bitmap) )
            throw new Error(`native client exited without writing ${bitmap}${logSuffix(result)}`);
        if( !existsSync(treeFile) )
            throw new Error(`native client exited without writing ${treeFile}${logSuffix(result)}`);

        const bmp = readFileSync(bitmap);
        const tree = parseNativeTree(readFileSync(treeFile));
        const decoded = decodeBmp(bmp);
        if( decoded.width !== width || decoded.height !== height )
            throw new Error(
                `native preview size mismatch: requested ${width}x${height}, ` +
                `received ${decoded.width}x${decoded.height}`);
        return {
            width: decoded.width,
            height: decoded.height,
            rgba: decoded.rgba,
            bmp,
            png: encodePng(decoded),
            tree,
            stdout: result.stdout,
            stderr: result.stderr,
            revision,
        };
    } finally {
        rmSync(scratch, { recursive: true, force: true });
    }
}

/** A stable fingerprint suitable for an HTTP memory cache. */
export function nativePreviewFingerprint(
    project,
    interfaceId,
    width = 512,
    height = 334,
    state = {}) {
    const status = nativePreviewStatus(project);
    if( !status.available ) return null;
    const binary = statSync(status.binary);
    const cacheFiles = readdirSync(status.cache)
        .filter((name) => name === 'main_file_cache.dat2' || /^main_file_cache\.idx\d+$/.test(name))
        .sort(cacheFileOrder)
        .flatMap((name) => {
            const file = statSync(join(status.cache, name));
            return [name, file.size, file.mtimeMs];
        });
    const stateDigest = createHash('sha256').update(encodeNativeState(state)).digest('hex');
    return [status.binary, binary.size, binary.mtimeMs, status.cache, ...cacheFiles,
        project.revision || '', interfaceId, width, height, stateDigest].join(':');
}

/** Encode the state object shared by the browser preview into canonical bytes. */
export function encodeNativeState(state = {}) {
    if( !state || typeof state !== 'object' || Array.isArray(state) )
        throw new Error('native preview state must be an object');

    const records = [];
    for( const [key, value] of Object.entries(state) ) {
        const match = /^(varp|varbit|varc|varcstr|stat):(\d+)$/.exec(key);
        /* Inventory and other host slices remain in the diagnostic DOM state;
         * the native bridge only serializes stores it can seed faithfully. */
        if( !match ) continue;
        const kindName = match[1];
        const id = Number.parseInt(match[2], 10);
        if( id < 0 || id > MAX_STATE_ID || (kindName === 'stat' && id > 24) )
            throw new Error(`native preview state id is out of range: ${key}`);

        let payload;
        if( kindName === 'varcstr' ) {
            if( typeof value !== 'string' )
                throw new Error(`native preview ${key} must be a string`);
            if( value.includes('\0') )
                throw new Error(`native preview ${key} contains NUL`);
            payload = Buffer.from(value, 'utf8');
            if( payload.length > MAX_STATE_STRING )
                throw new Error(`native preview ${key} exceeds ${MAX_STATE_STRING} UTF-8 bytes`);
        } else {
            if( !Number.isInteger(value) || value < -0x80000000 || value > 0x7fffffff )
                throw new Error(`native preview ${key} must be a signed 32-bit integer`);
            payload = Buffer.alloc(4);
            payload.writeInt32LE(value);
        }
        records.push({ kind: STATE_KIND[kindName], id, payload });
    }
    records.sort((a, b) => a.kind - b.kind || a.id - b.id);
    if( records.length > MAX_STATE_RECORDS )
        throw new Error(`native preview state exceeds ${MAX_STATE_RECORDS} records`);

    const size = 12 + records.reduce((sum, record) => sum + 12 + record.payload.length, 0);
    if( size > MAX_STATE_BYTES )
        throw new Error(`native preview state exceeds ${MAX_STATE_BYTES} bytes`);
    const packet = Buffer.alloc(size);
    packet.write('C2STATE1', 0, 'ascii');
    packet.writeUInt32LE(records.length, 8);
    let at = 12;
    for( const record of records ) {
        packet[at] = record.kind;
        packet.writeInt32LE(record.id, at + 4);
        packet.writeUInt32LE(record.payload.length, at + 8);
        record.payload.copy(packet, at + 12);
        at += 12 + record.payload.length;
    }
    return packet;
}

function cacheFileOrder(a, b) {
    if( a.endsWith('.dat2') ) return b.endsWith('.dat2') ? 0 : -1;
    if( b.endsWith('.dat2') ) return 1;
    return Number.parseInt(a.slice(a.lastIndexOf('idx') + 3), 10) -
        Number.parseInt(b.slice(b.lastIndexOf('idx') + 3), 10);
}

function discoverManifest(revision) {
    const candidate = join(REPO, 'manifests', `manifest_${revision}.ini`);
    return existsSync(candidate) ? candidate : null;
}

function inferRevision(cache) {
    const match = /(?:^|[._-])(osrs\d+|xrsps\d+|kronos|rs\d+|\d+)(?:[._-]|$)/i.exec(basename(cache));
    const name = match?.[1].toLowerCase() || null;
    return name?.startsWith('rs') && /^rs\d+$/.test(name) ? name.slice(2) : name;
}

function runBounded(command, args, { cwd, env, timeoutMs }) {
    return new Promise((resolvePromise, rejectPromise) => {
        const child = spawn(command, args, { cwd, env, stdio: ['ignore', 'pipe', 'pipe'] });
        let stdout = Buffer.alloc(0);
        let stderr = Buffer.alloc(0);
        let overflow = false;
        let timedOut = false;

        const append = (current, chunk) => {
            const remaining = MAX_LOG_BYTES - current.length;
            if( remaining <= 0 ) { overflow = true; return current; }
            if( chunk.length > remaining ) overflow = true;
            return Buffer.concat([current, chunk.subarray(0, remaining)]);
        };
        child.stdout.on('data', (chunk) => { stdout = append(stdout, chunk); });
        child.stderr.on('data', (chunk) => { stderr = append(stderr, chunk); });
        child.on('error', rejectPromise);

        const timer = setTimeout(() => {
            timedOut = true;
            child.kill('SIGKILL');
        }, timeoutMs);
        child.on('close', (code, signal) => {
            clearTimeout(timer);
            resolvePromise({
                code, signal, timedOut, overflow,
                stdout: stdout.toString('utf8'),
                stderr: stderr.toString('utf8'),
            });
        });
    });
}

function nativeFailure(result, command, args) {
    if( result.timedOut ) return `native preview timed out` + logSuffix(result);
    if( result.overflow ) return `native preview exceeded its diagnostic limit` + logSuffix(result);
    return `native preview failed (${command} ${args.join(' ')}, exit ${result.code ?? result.signal})` +
        logSuffix(result);
}

function logSuffix(result) {
    const detail = (result.stderr || result.stdout || '').trim();
    return detail ? `\n${detail}` : '';
}
