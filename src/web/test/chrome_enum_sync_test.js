/*
 * The page's copies of four C enums must match the C.
 *
 * torirs_chrome.js hand-copies `ToriRSChromeWidgetKind`, `ToriRSChromeIntentKind`,
 * `ToriRSChromeCmdKind` and `ToriRSChromeSkinSlot` as JS objects, because the
 * browser cannot include a header. Hand-copies rot: LISTROW and COLORPICK were added to the widget enum
 * and ACTION to the intent enum, and the page kept the old numbering. Nothing
 * failed loudly -- the roster's rows fell through to the generic branch and
 * rendered as bare text, and every control reported the intent one past the one
 * it meant, so a checkbox toggle arrived as an ACTION and a text edit as a
 * TOGGLE. Both look like sloppy UI rather than like a broken contract, which is
 * why this is a test and not a comment.
 *
 * Reads the enumerators straight out of the headers, so it cannot itself go
 * stale the way a second hand-copy would.
 */
'use strict';

const fs = require('fs');
const path = require('path');

const ROOT = path.join(__dirname, '..', '..');
let failures = 0;

function check(cond, what) {
  if (cond) return;
  console.log('FAIL: ' + what);
  failures++;
}

/*
 * Enumerator names in declaration order, with C's implicit numbering applied.
 *
 * Only the two forms these enums actually use are honoured: a bare name, and a
 * name with a literal `= N` initialiser. Anything else (an expression, a
 * duplicate value) is refused rather than guessed at -- a wrong answer here
 * would be a test that passes while the page is broken.
 */
function readEnum(file, tag) {
  const src = fs.readFileSync(path.join(ROOT, file), 'utf8');
  const at = src.indexOf('enum ' + tag);
  if (at < 0) throw new Error('no enum ' + tag + ' in ' + file);
  const open = src.indexOf('{', at);
  const close = src.indexOf('};', open);
  if (open < 0 || close < 0) throw new Error('unterminated enum ' + tag);

  const body = src
    .slice(open + 1, close)
    /* Comments carry commas and braces of their own. */
    .replace(/\/\*[\s\S]*?\*\//g, '')
    .replace(/\/\/[^\n]*/g, '');

  const out = {};
  let next = 0;
  for (const raw of body.split(',')) {
    const item = raw.trim();
    if (!item) continue;
    const m = /^([A-Za-z_][A-Za-z0-9_]*)\s*(?:=\s*(-?\d+)\s*)?$/.exec(item);
    if (!m) throw new Error('unparsed enumerator in ' + tag + ': ' + item);
    if (m[2] !== undefined) next = parseInt(m[2], 10);
    out[m[1]] = next;
    next++;
  }
  return out;
}

/** The JS table object literals, read as data rather than executed. */
function readJsTable(name) {
  const src = fs.readFileSync(path.join(ROOT, 'web', 'torirs_chrome.js'), 'utf8');
  const at = src.indexOf('var ' + name + ' = {');
  if (at < 0) throw new Error('no table ' + name + ' in torirs_chrome.js');
  const open = src.indexOf('{', at);
  const close = src.indexOf('}', open);
  const out = {};
  for (const raw of src.slice(open + 1, close).split(',')) {
    const item = raw.trim();
    if (!item) continue;
    const m = /^([A-Za-z_][A-Za-z0-9_]*)\s*:\s*(-?\d+)$/.exec(item);
    if (!m) throw new Error('unparsed entry in ' + name + ': ' + item);
    out[m[1]] = parseInt(m[2], 10);
  }
  return out;
}

/*
 * Compared BOTH ways on purpose.
 *
 * A missing entry is the failure that actually happened -- the page not knowing
 * a kind exists -- and a value mismatch is the failure it caused. An extra
 * entry on the page is also a failure: it means a kind was removed from C and
 * the page is still switching on a number that now means something else.
 */
function compare(label, cEnum, prefix, jsTable) {
  const expect = {};
  for (const name of Object.keys(cEnum)) {
    if (!name.startsWith(prefix)) continue;
    expect[name.slice(prefix.length)] = cEnum[name];
  }

  for (const key of Object.keys(expect)) {
    check(
      Object.prototype.hasOwnProperty.call(jsTable, key),
      label + ': the page is missing ' + key + ' (C says ' + expect[key] + ')');
    if (Object.prototype.hasOwnProperty.call(jsTable, key)) {
      check(
        jsTable[key] === expect[key],
        label + ': ' + key + ' is ' + jsTable[key] + ' on the page, ' +
          expect[key] + ' in C');
    }
  }
  for (const key of Object.keys(jsTable)) {
    check(
      Object.prototype.hasOwnProperty.call(expect, key),
      label + ': the page has ' + key + ', which C no longer declares');
  }
}

compare(
  'widget kinds',
  readEnum('ui/uitree_debug_overlay.h', 'ToriRSChromeWidgetKind'),
  'TORIRS_CHROME_W_',
  readJsTable('W'));

compare(
  'intent kinds',
  readEnum('ui/torirs_chrome_exec.h', 'ToriRSChromeIntentKind'),
  'TORIRS_CHROME_INTENT_',
  readJsTable('INTENT'));

compare(
  'command kinds',
  readEnum('ui/torirs_chrome_exec.h', 'ToriRSChromeCmdKind'),
  'TORIRS_CHROME_CMD_',
  readJsTable('CMD'));

/*
 * The skin slots, which fail in a quieter way than the three above.
 *
 * A slot is an INDEX into the bake, and the page turns each one into a data:
 * URL it then names in a stylesheet. Get the numbering wrong and nothing
 * errors: the window comes up wearing a scrollbar arrow where its checkbox
 * should be, or the tradebacking stretched into a 17x17 box. The C side of the
 * same coupling is pinned by the static assertions in ui/torirs_chrome_skin.h;
 * this is the page's half of it.
 *
 * SLOT_COUNT is dropped, not compared: it is the enum's terminator rather than
 * a slot, and the page has no use for it.
 */
{
  const slots = readEnum('ui/uitree_debug_overlay.h', 'ToriRSChromeSkinSlot');
  delete slots.TORIRS_CHROME_SKIN_SLOT_COUNT;
  compare('skin slots', slots, 'TORIRS_CHROME_SKIN_', readJsTable('SKIN'));
}

if (failures > 0) {
  console.log(failures + ' failure(s)');
  process.exit(1);
}
console.log('chrome enum sync: the page agrees with the headers.');
