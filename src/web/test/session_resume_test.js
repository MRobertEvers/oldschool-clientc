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
const TOKEN = '11,-22,33,-44,5';
let passed = 0;

/* 1. A tab that was in the world and was reloaded comes back asking for THAT
 *    session -- the name it belongs to and the key it authenticated on, at the
 *    END of the command line, the only position that outranks both argv layers
 *    main.c applies. No password, because none is kept: the client shows a
 *    reconnect, not a login. */
{
  const storage = fakeStorage();
  const first = createResumeSession(storage);
  assert.deepStrictEqual(first.boot(MANIFEST), MANIFEST, 'a first boot has nothing to resume');
  assert.strictEqual(first.remember('zezima', TOKEN), true);

  const reload = createResumeSession(storage);
  assert.deepStrictEqual(
    reload.boot(MANIFEST),
    MANIFEST.concat(['--user', 'zezima', '--resume', TOKEN]),
    'the reload did not resume the session');
  /* Whatever the tab holds, it is not a password. Checked on the stored text
   * rather than on the command line, because a password that never reaches
   * argv but sits in sessionStorage is still a password being kept. */
  assert.ok(
    !/hunter2|password|pass/.test(storage.entries.get('torirs.session')),
    'the tab kept a password');
  passed++;
}

/* 2. A logout is the player ending that session. The next reload opens on an
 *    empty form, which is what they asked for. */
{
  const storage = fakeStorage();
  const live = createResumeSession(storage);
  live.boot(MANIFEST);
  live.remember('zezima', TOKEN);
  live.forget();
  assert.deepStrictEqual(
    createResumeSession(storage).boot(MANIFEST), MANIFEST, 'the logout was not honoured');
  /* And nothing is left behind to be read by anything else. */
  assert.strictEqual(storage.entries.size, 0, 'the entry was emptied rather than removed');
  passed++;
}

/* 3. A refused reconnect drops it, and that is the page's half of the rule the
 *    client states: the key names a session the server no longer has, so a
 *    second refresh must present nothing and get the login form rather than
 *    replaying a dead key. The client calls forget() on that path
 *    (app_title_resume_to_login); here it is the same call a logout makes. */
{
  const storage = fakeStorage();
  const live = createResumeSession(storage);
  live.boot(MANIFEST);
  live.remember('zezima', TOKEN);

  const reload = createResumeSession(storage);
  assert.deepStrictEqual(
    reload.boot(MANIFEST),
    MANIFEST.concat(['--user', 'zezima', '--resume', TOKEN]),
    'the first reload did not resume');
  reload.forget();
  assert.deepStrictEqual(
    createResumeSession(storage).boot(MANIFEST), MANIFEST,
    'a retired key survived the refusal and would be presented again');
  passed++;
}

/* 4. An explicit credential on the page's command line is the operator's, and
 *    outranks the tab -- including when the two name different accounts, which
 *    is somebody deliberately logging in as somebody else. */
{
  const storage = fakeStorage();
  const live = createResumeSession(storage);
  live.boot(MANIFEST);
  live.remember('zezima', TOKEN);

  const explicit = MANIFEST.concat(['--user', 'asdf', '--pass', 'a']);
  assert.deepStrictEqual(
    createResumeSession(storage).boot(explicit), explicit,
    'the page command line was overridden by the tab');
  passed++;
}

/* 5. Another profile is another world. One world's session key means nothing
 *    to another's server, and the entry survives for the profile it belongs
 *    to. */
{
  const storage = fakeStorage();
  const live = createResumeSession(storage);
  live.boot(MANIFEST);
  live.remember('zezima', TOKEN);

  const other = ['--manifest', 'manifests/manifest_rs254lc.ini'];
  assert.deepStrictEqual(
    createResumeSession(storage).boot(other), other, 'a session crossed profiles');
  assert.deepStrictEqual(
    createResumeSession(storage).boot(MANIFEST),
    MANIFEST.concat(['--user', 'zezima', '--resume', TOKEN]),
    'the visit to another profile spent the entry');
  passed++;
}

/* 6. Anything that is not an entry this file wrote is not a session: a
 *    truncated string, another script's key, the shape a page from last week
 *    wrote. Each one boots the client into an ordinary login rather than
 *    appending half a reconnect -- `--resume undefined` is a boot the client
 *    logs as broken, having already decided not to show a form. The junk
 *    carries THIS boot's manifest, because an entry naming another profile is
 *    turned away before its shape is ever looked at (5). */
{
  const m = MANIFEST[1];
  for (const junk of ['', 'not json', 'null', '{}',
                      `{"manifest":"${m}","user":"zezima"}`,
                      `{"manifest":"${m}","resume":"${TOKEN}"}`,
                      `{"manifest":"${m}","user":"","resume":"${TOKEN}"}`,
                      `{"manifest":"${m}","user":["zezima"],"resume":"${TOKEN}"}`,
                      `{"manifest":"${m}","user":"zezima","resume":""}`,
                      `{"manifest":"${m}","user":"zezima","resume":"1,2,3,4"}`,
                      `{"manifest":"${m}","user":"zezima","resume":"1,2,3,4,5,6"}`,
                      `{"manifest":"${m}","user":"zezima","resume":"hunter2"}`,
                      `{"manifest":"${m}","user":"zezima","resume":"1,2,3,4,x"}`,
                      /* The shape the build before this one wrote: a password
                       * where the key should be. Read as a token it is junk,
                       * and the entry is not a session. */
                      `{"manifest":"${m}","user":"zezima","password":"hunter2"}`,
                      `["${m}","zezima","${TOKEN}"]`]) {
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
  assert.strictEqual(createResumeSession(null).remember('zezima', TOKEN), false);
  createResumeSession(null).forget();

  const storage = fakeStorage();
  storage.throwOnWrite = true;
  const live = createResumeSession(storage);
  live.boot(MANIFEST);
  assert.strictEqual(live.remember('zezima', TOKEN), false, 'a refused write reported success');

  storage.throwOnWrite = false;
  live.remember('zezima', TOKEN);
  storage.throwOnRead = true;
  assert.deepStrictEqual(
    createResumeSession(storage).boot(MANIFEST), MANIFEST, 'a throwing read broke the boot');
  passed++;
}

/* 8. A session with no name is not a session, and neither is one with no key.
 *    The client asserts on both (app_session_resume.c) and a page from another
 *    build must not write one for a later boot to present: a reconnect is the
 *    one boot that shows no login form, so one that cannot be performed is a
 *    loading bar the player can only close the tab out of. */
{
  const storage = fakeStorage();
  const live = createResumeSession(storage);
  live.boot(MANIFEST);
  assert.strictEqual(live.remember('', TOKEN), false);
  assert.strictEqual(live.remember(undefined, TOKEN), false);
  assert.strictEqual(live.remember('zezima', ''), false);
  assert.strictEqual(live.remember('zezima', undefined), false);
  /* A revision whose login has no seed reconnect produces no token at all --
   * the client forgets rather than writing one of these, and if it did write
   * one the page refuses it. */
  assert.strictEqual(live.remember('zezima', 'hunter2'), false, 'a password was taken as a key');
  assert.strictEqual(storage.entries.size, 0);
  passed++;
}

/* 9. The argv line the page logs is the first thing to ask about a boot, and a
 *    session key in it is as good as a password until the session ends. */
{
  assert.deepStrictEqual(
    redactArgs(MANIFEST.concat(['--user', 'zezima', '--resume', TOKEN])),
    MANIFEST.concat(['--user', 'zezima', '--resume', '****']));
  /* An operator may still type a password on the page's own command line. */
  assert.deepStrictEqual(
    redactArgs(['--user', 'zezima', '--pass', 'hunter2']),
    ['--user', 'zezima', '--pass', '****']);
  /* A trailing flag names nothing; the line is still printed. */
  assert.deepStrictEqual(redactArgs(['--pass']), ['--pass']);
  passed++;
}

/* 10. Every handshake re-keys the session, and the newest key is the one a
 *     reload presents. Presenting a retired one is a reconnect the server
 *     refuses, which costs the player the world they were standing in. */
{
  const storage = fakeStorage();
  const live = createResumeSession(storage);
  live.boot(MANIFEST);
  live.remember('zezima', TOKEN);
  live.remember('zezima', '1,2,3,4,5');
  assert.deepStrictEqual(
    createResumeSession(storage).boot(MANIFEST).slice(-2), ['--resume', '1,2,3,4,5'],
    'a retired key was presented');
  passed++;
}

console.log(`session_resume_test: ${passed} passed`);
