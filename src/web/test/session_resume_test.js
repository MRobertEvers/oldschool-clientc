/*
 * What a reloaded page logs back in as.
 *
 * The page half of the resume: which boots may have a held session, what is
 * written and when it is dropped. The client half -- WHICH events say
 * remember and forget -- is app/app_session_resume.c and its two call sites,
 * tested from the app layer.
 *
 * Node only: no browser, no wasm, no build step, matching
 * host_plugin_files_test.js, and like it the declarations are LIFTED out of
 * the shipped torirs_host.js rather than copied here, so a rewrite of the
 * shipped policy has to also change these expectations.
 *
 * The storage is a fake with a switch on it, because three of its behaviours
 * are real: a browser that refuses site data throws on the property itself, a
 * full one throws on the write, and both have to leave the client booting
 * exactly as it did before any of this existed.
 */
const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const SRC = fs.readFileSync(path.join(__dirname, '..', 'torirs_host.js'), 'utf8');

/* Same lift as host_plugin_files_test.js: the declaration, brace-matched. */
function declaration(needle) {
  const start = SRC.indexOf(needle);
  assert.notStrictEqual(start, -1, `torirs_host.js no longer contains: ${needle}`);
  const firstBrace = SRC.indexOf('{', start);
  assert.notStrictEqual(firstBrace, -1, `torirs_host.js has no body after: ${needle}`);
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

const context = { console };
vm.runInNewContext(
  [declaration('function createResumeSession('), declaration('function redactArgs(')].join('\n'),
  context,
  { filename: 'torirs_host.js' });
const { createResumeSession, redactArgs } = context;

/* A sessionStorage that can be made to behave like the two browsers that
 * refuse one. `entries` is the tab's storage seen from outside -- what a
 * reload finds still sitting there. */
function fakeStorage() {
  return {
    entries: new Map(),
    throwOnRead: false,
    throwOnWrite: false,
    getItem(key) {
      if (this.throwOnRead) { throw new Error('storage disabled'); }
      return this.entries.has(key) ? this.entries.get(key) : null;
    },
    setItem(key, value) {
      if (this.throwOnWrite) { throw new Error('quota exceeded'); }
      this.entries.set(key, String(value));
    },
    removeItem(key) { this.entries.delete(key); }
  };
}

const MANIFEST = ['--manifest', 'manifests/manifest_osrs239.ini'];
let passed = 0;

/* 1. A tab that logged in and was reloaded comes back with its credentials on
 *    the command line, at the END of it -- the only position that outranks
 *    both argv layers main.c applies. */
{
  const storage = fakeStorage();
  const first = createResumeSession(storage);
  assert.deepStrictEqual(first.boot(MANIFEST), MANIFEST, 'a first boot has nothing to resume');
  assert.strictEqual(first.remember('zezima', 'hunter2'), true);

  const reload = createResumeSession(storage);
  assert.deepStrictEqual(
    reload.boot(MANIFEST),
    MANIFEST.concat(['--user', 'zezima', '--pass', 'hunter2']),
    'the reload did not resume the session');
  passed++;
}

/* 2. A logout is the player ending that session. The next reload opens on an
 *    empty form, which is what they asked for. */
{
  const storage = fakeStorage();
  const live = createResumeSession(storage);
  live.boot(MANIFEST);
  live.remember('zezima', 'hunter2');
  live.forget();
  assert.deepStrictEqual(
    createResumeSession(storage).boot(MANIFEST), MANIFEST, 'the logout was not honoured');
  /* And nothing is left behind to be read by anything else. */
  assert.strictEqual(storage.entries.size, 0, 'the entry was emptied rather than removed');
  passed++;
}

/* 3. A rejected login must NOT drop it. "This world is full" and "your account
 *    is still logged in" are the two most likely answers a reloading client
 *    gets, and the credentials are still the ones that worked -- the page is
 *    never told otherwise, so the only way to lose them is a logout or the
 *    tab closing. */
{
  const storage = fakeStorage();
  const live = createResumeSession(storage);
  live.boot(MANIFEST);
  live.remember('zezima', 'hunter2');

  const rejected = createResumeSession(storage);
  assert.deepStrictEqual(
    rejected.boot(MANIFEST),
    MANIFEST.concat(['--user', 'zezima', '--pass', 'hunter2']),
    'the first reload did not resume');
  /* The login that boot armed was refused, the player is back on the form,
   * and they press F5 again. */
  assert.deepStrictEqual(
    createResumeSession(storage).boot(MANIFEST),
    MANIFEST.concat(['--user', 'zezima', '--pass', 'hunter2']),
    'a refused login lost a password that works');
  passed++;
}

/* 4. An explicit credential on the page's command line is the operator's, and
 *    outranks the tab -- including when the two name different accounts, which
 *    is somebody deliberately logging in as somebody else. */
{
  const storage = fakeStorage();
  const live = createResumeSession(storage);
  live.boot(MANIFEST);
  live.remember('zezima', 'hunter2');

  const explicit = MANIFEST.concat(['--user', 'asdf', '--pass', 'a']);
  assert.deepStrictEqual(
    createResumeSession(storage).boot(explicit), explicit,
    'the page command line was overridden by the tab');
  passed++;
}

/* 5. Another profile is another world. One world's credentials do not go to
 *    another's server, and the entry survives for the profile it belongs to. */
{
  const storage = fakeStorage();
  const live = createResumeSession(storage);
  live.boot(MANIFEST);
  live.remember('zezima', 'hunter2');

  const other = ['--manifest', 'manifests/manifest_rs254lc.ini'];
  assert.deepStrictEqual(
    createResumeSession(storage).boot(other), other, 'credentials crossed profiles');
  assert.deepStrictEqual(
    createResumeSession(storage).boot(MANIFEST),
    MANIFEST.concat(['--user', 'zezima', '--pass', 'hunter2']),
    'the visit to another profile spent the entry');
  passed++;
}

/* 6. Anything that is not an entry this file wrote is not a session: a
 *    truncated string, another script's key, an older shape from a page this
 *    tab loaded last week. Each one boots the client rather than appending
 *    half a login -- `--pass undefined` is a password, and the server would be
 *    told it. The junk carries THIS boot's manifest, because an entry that
 *    names another profile is turned away before its shape is ever looked at
 *    (5), and it is the one written here under the right name that gets as far
 *    as the command line. */
{
  const m = MANIFEST[1];
  for (const junk of ['', 'not json', 'null', '{}',
                      `{"manifest":"${m}","user":"zezima"}`,
                      `{"manifest":"${m}","password":"hunter2"}`,
                      `{"manifest":"${m}","user":"","password":"hunter2"}`,
                      `{"manifest":"${m}","user":["zezima"],"password":"hunter2"}`,
                      `["${m}","zezima","hunter2"]`]) {
    const storage = fakeStorage();
    storage.entries.set('torirs.session', junk);
    assert.deepStrictEqual(
      createResumeSession(storage).boot(MANIFEST), MANIFEST, `resumed from junk: ${junk}`);
  }
  passed++;
}

/* 7. A browser that refuses site data. The property throws, the write throws,
 *    and there is no storage object at all -- all three leave a client that
 *    boots exactly as it did before the resume existed. */
{
  assert.deepStrictEqual(
    createResumeSession(null).boot(MANIFEST), MANIFEST, 'no storage broke the boot');
  assert.strictEqual(createResumeSession(null).remember('zezima', 'hunter2'), false);
  createResumeSession(null).forget();

  const storage = fakeStorage();
  storage.throwOnWrite = true;
  const live = createResumeSession(storage);
  live.boot(MANIFEST);
  assert.strictEqual(live.remember('zezima', 'hunter2'), false, 'a refused write reported success');

  storage.throwOnWrite = false;
  live.remember('zezima', 'hunter2');
  storage.throwOnRead = true;
  assert.deepStrictEqual(
    createResumeSession(storage).boot(MANIFEST), MANIFEST, 'a throwing read broke the boot');
  passed++;
}

/* 8. A session with no name is not a session. The client asserts on one
 *    (app_session_resume.c) and a page from another build must not write one
 *    for a later boot to submit. */
{
  const storage = fakeStorage();
  const live = createResumeSession(storage);
  live.boot(MANIFEST);
  assert.strictEqual(live.remember('', 'hunter2'), false);
  assert.strictEqual(live.remember(undefined, 'hunter2'), false);
  assert.strictEqual(storage.entries.size, 0);
  /* A password is allowed to be empty -- servers here accept one. */
  assert.strictEqual(live.remember('zezima', ''), true);
  assert.deepStrictEqual(
    createResumeSession(storage).boot(MANIFEST),
    MANIFEST.concat(['--user', 'zezima', '--pass', '']));
  passed++;
}

/* 9. The argv line the page logs is the first thing to ask about a boot, and
 *    a password in it is a password on a screenshot. */
{
  assert.deepStrictEqual(
    redactArgs(MANIFEST.concat(['--user', 'zezima', '--pass', 'hunter2'])),
    MANIFEST.concat(['--user', 'zezima', '--pass', '****']));
  /* A trailing --pass names nothing; the line is still printed. */
  assert.deepStrictEqual(redactArgs(['--pass']), ['--pass']);
  passed++;
}

console.log(`session_resume_test: ${passed} passed`);
