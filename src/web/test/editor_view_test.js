/*
 * The page half of a map-editor VIEW tab (docs/MAPED_DESIGN.html #windows):
 * what ?editor_view=1&maped=...&group=N boots as, and the URL "spawn view"
 * opens.
 *
 * Node only, like session_resume_test.js: the declarations are LIFTED out of
 * the shipped torirs_host.js, so a rewrite of the page's policy has to change
 * these expectations too. The command line it builds is the one the launch
 * service starts natively (platform/launch_sessions.c verb_view) and main.c
 * parses (--editor-view, --maped, --maped-group, --camera).
 */
const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const SRC = fs.readFileSync(path.join(__dirname, '..', 'torirs_host.js'), 'utf8');

function declaration(needle) {
  const start = SRC.indexOf(needle);
  assert.notStrictEqual(start, -1, `torirs_host.js no longer contains: ${needle}`);
  const firstBrace = SRC.indexOf('{', start);
  let depth = 0;
  for (let i = firstBrace; i < SRC.length; i++) {
    if (SRC[i] === '{') { depth++; }
    else if (SRC[i] === '}') {
      depth--;
      if (depth === 0) { return `${SRC.substring(start, i + 1)};`; }
    }
  }
  assert.fail(`unbalanced braces after: ${needle}`);
}

function constant(needle) {
  const start = SRC.indexOf(needle);
  assert.notStrictEqual(start, -1, `torirs_host.js no longer contains: ${needle}`);
  return SRC.substring(start, SRC.indexOf(';', start) + 1);
}

const context = { URL, URLSearchParams };
vm.runInNewContext(
  [
    constant('const EDITOR_VIEW_MANIFEST'),
    declaration('function resolveSocketUrl('),
    declaration('function editorViewBoot('),
    declaration('function editorViewUrl('),
    'this.EDITOR_VIEW_MANIFEST = EDITOR_VIEW_MANIFEST;',
    'this.editorViewBoot = editorViewBoot;',
    'this.editorViewUrl = editorViewUrl;',
  ].join('\n'),
  context,
  { filename: 'torirs_host.js' });
const { editorViewBoot, editorViewUrl, EDITOR_VIEW_MANIFEST } = context;

const HTTPS = { protocol: 'https:', host: 'example.org', href: 'https://example.org/torirs/index.html' };
const HTTP = { protocol: 'http:', host: 'localhost:8088', href: 'http://localhost:8088/index.html' };
const DEFAULT = ['--manifest', 'manifests/manifest_rs254lc.ini', '--offline'];
let passed = 0;
const q = s => new URLSearchParams(s);

/* 1. Not a view: the page's command line is untouched. */
{
  const boot = editorViewBoot(q('arg=--manifest&arg=x.ini'), ['--manifest', 'x.ini'], HTTP);
  assert.deepStrictEqual(Array.from(boot.args), ['--manifest', 'x.ini']);
  assert.strictEqual(boot.socketUrl, null);
  assert.strictEqual(boot.error, null);
  passed++;
}

/* 2. A view on a wss daemon: the editor manifest replaces the default game
 *    boot, and the viewer flags follow; the URL is also the socket's. */
{
  const boot = editorViewBoot(
    q('editor_view=1&maped=wss://example.org/maped&group=7&camera=3222.5,3218,0,512,300,1200'),
    DEFAULT, HTTPS);
  assert.deepStrictEqual(Array.from(boot.args), [
    '--manifest', EDITOR_VIEW_MANIFEST, '--editor-view', '--maped', 'wss://example.org/maped',
    '--maped-group', '7', '--camera', '3222.5,3218,0,512,300,1200']);
  assert.strictEqual(boot.socketUrl, 'wss://example.org/maped');
  assert.strictEqual(boot.error, null);
  assert.ok(!boot.args.includes('--offline'), 'the default game boot is replaced, not extended');
  passed++;
}

/* 3. A relative maped resolves like ws= (wss on an https page). */
{
  const boot = editorViewBoot(q('editor_view=1&maped=maped&group=2'), DEFAULT, HTTPS);
  assert.strictEqual(boot.socketUrl, 'wss://example.org/torirs/maped');
  assert.ok(boot.args.includes('wss://example.org/torirs/maped'));
  passed++;
}

/* 4. tcp:// passes through as written and names no socket URL. */
{
  const boot = editorViewBoot(q('editor_view=1&maped=tcp://127.0.0.1:43610&group=3'), DEFAULT, HTTP);
  assert.strictEqual(boot.socketUrl, null);
  assert.deepStrictEqual(Array.from(boot.args).slice(2),
    ['--editor-view', '--maped', 'tcp://127.0.0.1:43610', '--maped-group', '3']);
  passed++;
}

/* 5. manifest= picks the editor manifest; explicit arg= is kept as given. */
{
  let boot = editorViewBoot(q('editor_view=1&maped=ws://h:43610&manifest=manifests/manifest_osrs239_mapeditor.ini'),
    DEFAULT, HTTP);
  assert.strictEqual(boot.args[1], 'manifests/manifest_osrs239_mapeditor.ini');
  boot = editorViewBoot(q('editor_view=1&maped=ws://h:43610&arg=--manifest&arg=mine.ini'),
    ['--manifest', 'mine.ini'], HTTP);
  assert.deepStrictEqual(Array.from(boot.args).slice(0, 3), ['--manifest', 'mine.ini', '--editor-view']);
  passed++;
}

/* 6. A bad group is dropped (main.c would refuse it); no maped is an error. */
{
  let boot = editorViewBoot(q('editor_view=1&maped=ws://h:1&group=0'), DEFAULT, HTTP);
  assert.ok(!boot.args.includes('--maped-group'));
  boot = editorViewBoot(q('editor_view=1&maped=ws://h:1&group=7;rm'), DEFAULT, HTTP);
  assert.ok(!boot.args.includes('--maped-group'));
  boot = editorViewBoot(q('editor_view=1&group=7'), DEFAULT, HTTP);
  assert.ok(boot.error && /maped=/.test(boot.error));
  assert.ok(!boot.args.includes('--editor-view'));
  passed++;
}

/* 7. "spawn view": this page with the view query and its servers carried over,
 *    nothing else (no game args, no replay). */
{
  const href = 'https://example.org/torirs/index.html?arg=--manifest&arg=m.ini&io=/io2&js5_port=443&replay=x.bin#frag';
  const url = new URL(editorViewUrl(href, new URL(href).searchParams, 'm.ini',
    'wss://example.org/maped', 5, '3222.5,3218,0,512,300,1200'));
  assert.strictEqual(url.origin + url.pathname, 'https://example.org/torirs/index.html');
  assert.strictEqual(url.searchParams.get('editor_view'), '1');
  assert.strictEqual(url.searchParams.get('maped'), 'wss://example.org/maped');
  assert.strictEqual(url.searchParams.get('group'), '5');
  assert.strictEqual(url.searchParams.get('camera'), '3222.5,3218,0,512,300,1200');
  assert.strictEqual(url.searchParams.get('manifest'), 'm.ini');
  assert.strictEqual(url.searchParams.get('io'), '/io2');
  assert.strictEqual(url.searchParams.get('js5_port'), '443');
  assert.ok(!url.searchParams.has('replay') && !url.searchParams.has('arg'));
  assert.strictEqual(url.hash, '');
  /* And the tab it opens boots as the view it names. */
  const boot = editorViewBoot(url.searchParams, DEFAULT, HTTPS);
  assert.deepStrictEqual(Array.from(boot.args), ['--manifest', 'm.ini', '--editor-view', '--maped',
    'wss://example.org/maped', '--maped-group', '5', '--camera', '3222.5,3218,0,512,300,1200']);
  passed++;
}

/* 7b. A popped-out panel: the tab URL carries panel=, and the tab boots
 *     --editor-panel <module>; "world" and anything that is not a module word
 *     add nothing. */
{
  const href = 'https://example.org/torirs/index.html?io=/io2';
  const url = new URL(editorViewUrl(href, new URL(href).searchParams, 'm.ini',
    'wss://example.org/maped', 5, '3222,3218,0,512,300,1200', 'terrain'));
  assert.strictEqual(url.searchParams.get('panel'), 'terrain');
  const boot = editorViewBoot(url.searchParams, DEFAULT, HTTPS);
  assert.deepStrictEqual(Array.from(boot.args).slice(-2), ['--editor-panel', 'terrain']);
  const world = new URL(editorViewUrl(href, new URL(href).searchParams, 'm.ini',
    'wss://example.org/maped', 5, '', 'world'));
  assert.ok(!world.searchParams.has('panel'));
  let bad = editorViewBoot(q('editor_view=1&maped=ws://h:1&panel=../x'), DEFAULT, HTTP);
  assert.ok(!bad.args.includes('--editor-panel'));
  bad = editorViewBoot(q('editor_view=1&maped=ws://h:1&panel=world'), DEFAULT, HTTP);
  assert.ok(!bad.args.includes('--editor-panel'));
  passed++;
}

/* 8. The page wiring exists: the spawn function, the button, the socket URL. */
{
  assert.ok(SRC.includes('window.torirsSpawnEditorView ='));
  assert.ok(SRC.includes("Module._torirs_editor_view_spawn"));
  assert.ok(SRC.includes('else if (editorView.socketUrl) { Module.websocket = { url: editorView.socketUrl }; }'));
  const html = fs.readFileSync(path.join(__dirname, '..', 'index.html'), 'utf8');
  assert.ok(/id="editorview" hidden/.test(html) && /id="btn-spawn-view"/.test(html));
  passed++;
}

console.log(`editor view page: ${passed} cases ok`);
