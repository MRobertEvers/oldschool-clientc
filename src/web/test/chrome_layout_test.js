/* Static integration gates for the one canonical web document and its wasm
 * protocol adapter. Runtime behavior is covered by chrome_dom_test.js. */
'use strict';

const assert = require('assert');
const fs = require('fs');
const path = require('path');

const web = path.join(__dirname, '..');
const src = path.join(web, '..');
const html = fs.readFileSync(path.join(web, 'index.html'), 'utf8');
const adapter = fs.readFileSync(path.join(web, 'torirs_chrome.js'), 'utf8');
const execWeb = fs.readFileSync(path.join(src, 'ui', 'torirs_chrome_exec_web.c'), 'utf8');
const execWebHeader = fs.readFileSync(path.join(src, 'ui', 'torirs_chrome_exec_web.h'), 'utf8');
const makefile = fs.readFileSync(path.join(src, 'Makefile'), 'utf8');
const canonicalHtml = fs.readFileSync(path.join(src, 'plugin_chrome', 'modern.html'), 'utf8');
const canonicalCss = fs.readFileSync(path.join(src, 'plugin_chrome', 'modern.css'), 'utf8');
const httpServer = fs.readFileSync(path.join(src, 'ioserver', 'http_server.c'), 'utf8');

assert.match(html, /<div id="torirs-app">[\s\S]*<main id="app-content">/,
  'the page has one fullscreenable application root');
assert.match(html,
  /<div id="game-region">[\s\S]*<\/div>\s*(?:<!--[\s\S]*?-->\s*)?<aside id="plugin-chrome-mount"/,
  'game and plugin chrome have separate sibling layout regions');
assert.match(html, /requestFullscreen\(app\)/,
  'fullscreen targets the application root so rail and pane remain present');
assert.doesNotMatch(html, /requestFullscreen\((?:canvas|document\.documentElement)\)/,
  'fullscreen never targets only the game');

assert.match(adapter, /createElement\(['"]iframe['"]\)/,
  'the adapter creates its one app-owned browser document');
assert.match(adapter, /frame\.src\s*=\s*['"]plugin_chrome\/modern\.html['"]/,
  'the document is the canonical modern bundle, not duplicated DOM');
assert.match(adapter, /allow-scripts allow-same-origin/,
  'the local frame has only the capabilities required by its bridge');
assert.doesNotMatch(adapter, /global\.open\s*\(|window\.open\s*\(/,
  'the web host cannot create auxiliary or per-plugin windows');
assert.match(adapter, /this\.frame\s*\|\|[\s\S]*return !!this\.frame/,
  'ensureFrame retains the original iframe');
assert.match(adapter, /entries\.slice\(0, 33\)/,
  'one rail can retain Manage plus all 32 plugins');
assert.match(adapter, /const RAIL_WIDTH = 42;/,
  'the outer allocation reserves the modern rail width');
assert.match(canonicalCss, /grid-template-columns:\s*minmax\(0, 1fr\) 42px/,
  'the canonical modern document consumes the same 42px rail width');
assert.match(adapter, /GAME_MIN \+ RAIL_WIDTH \+ PANEL_MIN/,
  'split/exclusive mode is derived from available width');
assert.match(adapter, /game\.hidden\s*=\s*mode\s*===\s*['"]exclusive['"]/,
  'exclusive mode replaces the game instead of covering it');
assert.match(adapter, /mode\s*=\s*['"]collapsed['"][\s\S]*RAIL_WIDTH/,
  'collapsed mode retains only the narrow rail');
assert.match(adapter, /type:\s*['"]page\.snapshot['"][\s\S]*commands:\s*initial/,
  'a complete C sync becomes one atomic canonical snapshot');
assert.match(adapter, /type:\s*['"]page\.delta['"][\s\S]*commands/,
  'later C syncs become retained canonical deltas');
assert.match(adapter, /torirsChromeReceive/,
  'new wasm glue has one direct protocol-1 receive hook');
assert.match(adapter, /_ToriRSChromeExecWeb_RequestSelect/,
  'rail destinations return to the frame thread with generation fencing');
assert.match(adapter, /_ToriRSChromeExecWeb_RequestLayout/,
  'the presenter reports its actual split/exclusive allocation');

assert.match(execWeb, /EMSCRIPTEN_KEEPALIVE[\s\S]*ToriRSChromeExecWeb_RequestSelect/,
  'the rail selection setter is retained as a wasm export');
assert.match(execWebHeader,
  /ToriRSChromeExecWeb_RequestSelect\([\s\S]*plugin_index[\s\S]*selection_generation/,
  'the C side receives destination plus stale-work generation');
assert.match(execWeb,
  /\\\"protocol\\\":1,\\\"type\\\":\\\"rail\.snapshot\\\"/,
  'C publishes the registry using protocol-1 field names');
assert.match(execWeb, /web_chrome_apply_batch[\s\S]*chrome_web_batch_begin[\s\S]*chrome_web_batch_end/,
  'C crosses Wasm to JS once for each atomic retained transaction');
assert.match(adapter, /torirsChromeApplyBatch[\s\S]*Array\.isArray[\s\S]*host\.apply/,
  'the page drains that batch through the canonical retained command path');

assert.match(makefile, /WEB_PLUGIN_CHROME\s*=[\s\S]*plugin_chrome\/modern\.html[\s\S]*plugin_chrome\/runtime\.js/,
  'web staging names the canonical page and runtime');
assert.match(makefile, /WEB_PLUGIN_CHROME_SKIN[\s\S]*res\/plugin_chrome\/skin\/\*\.png/,
  'web staging includes the baked ToriRSChrome skin');
assert.match(makefile, /build-web\/plugin_chrome\/skin/,
  'the build preserves the bundle-relative skin directory');
assert.match(makefile, /WEB_PLUGIN_CHROME_FONT[\s\S]*res\/plugin_chrome\/font/,
  'the build stages every generated cache-font format beside the page');
assert.match(httpServer, /strcmp\(dot, "\.woff"\)[\s\S]*return "font\/woff"/,
  'the development server serves WOFF with a font MIME type');
assert.doesNotMatch(canonicalHtml, /<script(?![^>]+src=)[^>]*>/i,
  'the framed document contains no plugin-authored or inline script');

console.log('chrome layout integration checks passed');
