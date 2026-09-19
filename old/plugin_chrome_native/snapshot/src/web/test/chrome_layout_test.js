/* Static integration checks for the application-owned web chrome slot. */
const assert = require('assert');
const fs = require('fs');
const path = require('path');

const web = path.join(__dirname, '..');
const html = fs.readFileSync(path.join(web, 'index.html'), 'utf8');
const chrome = fs.readFileSync(path.join(web, 'torirs_chrome.js'), 'utf8');
const execWeb = fs.readFileSync(path.join(web, '..', 'ui', 'torirs_chrome_exec_web.c'), 'utf8');
const execWebHeader = fs.readFileSync(path.join(web, '..', 'ui', 'torirs_chrome_exec_web.h'), 'utf8');

assert.match(html, /<div id="torirs-app">[\s\S]*<main id="app-content">/,
  'the page has one fullscreenable application root');
assert.match(html, /<div id="game-region">[\s\S]*<\/div>\s*(?:<!--[\s\S]*?-->\s*)?<aside id="plugin-chrome-mount"/,
  'game and plugin chrome have separate sibling layout regions');
assert.match(html, /requestFullscreen\(app\)/,
  'fullscreen targets the application root so the rail and pane remain present');
assert.doesNotMatch(html, /requestFullscreen\((?:canvas|document\.documentElement)\)/,
  'fullscreen never targets only the game or the document outside the app root');

assert.doesNotMatch(chrome, /createElement\(['"]iframe['"]\)/,
  'plugin chrome uses native page DOM, not an iframe document');
assert.doesNotMatch(chrome, /\.torirs-chrome\.floating\s*\{/,
  'there is no overlay fallback stylesheet');
assert.match(chrome, /gameMin\s*\+\s*LAYOUT\.railW\s*\+\s*LAYOUT\.panelMin/,
  'the presenter derives split/exclusive from available width');
assert.match(chrome, /this\.gameRegion\.hidden\s*=\s*mode\s*===\s*['"]exclusive['"]/,
  'exclusive mode replaces the game region rather than covering it');
assert.match(chrome, /mode\s*=\s*['"]collapsed['"][\s\S]*LAYOUT\.railW/,
  'collapsed mode retains only the narrow rail');
assert.match(chrome, /_ToriRSChromeExecWeb_RequestSelect/,
  'the collapsed rail queues a concrete destination after the widget executor stops polling');
assert.match(execWeb, /EMSCRIPTEN_KEEPALIVE[\s\S]*ToriRSChromeExecWeb_RequestSelect/,
  'the rail selection setter is retained as a wasm export');
assert.match(execWebHeader,
  /ToriRSChromeExecWeb_RequestSelect\([\s\S]*plugin_index[\s\S]*selection_generation/,
  'the frame-thread side receives a destination and stale-work generation');
assert.match(chrome, /torirsChromeRailSync[\s\S]*torirsChromeRailIcon/,
  'the page exposes separate retained registry and revisioned icon hooks');
assert.match(chrome, /_ToriRSChromeExecWeb_RequestLayout/,
  'the DOM presenter reports its real split or exclusive allocation');

console.log('chrome layout integration checks passed');
