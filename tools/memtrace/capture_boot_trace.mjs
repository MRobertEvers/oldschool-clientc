#!/usr/bin/env node
/**
 * Capture WASM boot memtrace.bin via Playwright.
 * Usage: node capture_boot_trace.mjs [url] [out_path] [wait_ms]
 */
import { chromium } from 'playwright';
import { mkdirSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const scriptDir = dirname(fileURLToPath(import.meta.url));
const defaultOut = join(scriptDir, 'bins', 'boot_memtrace.bin');

const url = process.argv[2] ?? 'http://127.0.0.1:8080/?runescape&soft3d';
const outPath = resolve(process.argv[3] ?? defaultOut);
const waitMs = Number(process.argv[4] ?? 10000);

const browser = await chromium.launch({ headless: true });
const page = await browser.newPage();

page.on('console', (msg) => {
  const text = msg.text();
  if (text.includes('error') || text.includes('Error') || text.includes('Exception')) {
    console.error('[page]', text);
  }
});

console.log(`navigate ${url}`);
await page.goto(url, { waitUntil: 'domcontentloaded', timeout: 120000 });

console.log(`wait ${waitMs}ms for boot...`);
await page.waitForTimeout(waitMs);

const bytes = await page.evaluate(async () => {
  const mod = window.Module;
  if (!mod) throw new Error('Module not ready');
  const flush =
    mod._LibToriPlatformEmscripten_JSHost_MemtraceFlush ??
    mod.LibToriPlatformEmscripten_JSHost_MemtraceFlush;
  const pathFn =
    mod._LibToriPlatformEmscripten_JSHost_MemtracePath ??
    mod.LibToriPlatformEmscripten_JSHost_MemtracePath;
  if (typeof flush !== 'function' || typeof pathFn !== 'function' || !mod.FS_readFile) {
    throw new Error('MEMTRACE exports missing — rebuild with MEMTRACE=1');
  }
  flush();
  const data = mod.FS_readFile('memtrace.bin');
  const chunk = 0x8000;
  let binary = '';
  for (let i = 0; i < data.length; i += chunk) {
    binary += String.fromCharCode.apply(null, data.subarray(i, i + chunk));
  }
  return btoa(binary);
});

mkdirSync(dirname(outPath), { recursive: true });
writeFileSync(outPath, Buffer.from(bytes, 'base64'));
const outSize = Buffer.from(bytes, 'base64').length;
console.log(`wrote ${outPath} (${outSize} bytes)`);
await browser.close();
