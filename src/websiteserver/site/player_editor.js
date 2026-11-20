/*
 * The staff player editor (src/websiteserver/website_site_editor.c is the server half
 * and says how an edit lands: the save when the player is offline, a cheat
 * through the game when online). This draws the account from the page's
 * #pe-state JSON and the icon art beside it (objs.json, icons_<n>.png,
 * skill_<stat>.png, slot_<wearpos>.png, varps.json, invs.json -- rendered by
 * tools/entity_viewer/ev_icons), and posts each edit to
 * /admin/players/<key>/set, redrawing from the state that comes back.
 */
(function () {
  'use strict';

  var ART = '/admin-art/';
  var root = document.getElementById('pe');
  var S = JSON.parse(document.getElementById('pe-state').textContent);
  var KEY = S.key;

  /* stat id -> name (pack/stat.pack order). */
  var SKILL = ['Attack', 'Defence', 'Strength', 'Hitpoints', 'Ranged', 'Prayer', 'Magic', 'Cooking',
    'Woodcutting', 'Fletching', 'Fishing', 'Firemaking', 'Crafting', 'Smithing', 'Mining', 'Herblore',
    'Agility', 'Thieving', 'Slayer', 'Farming', 'Runecraft', 'Hunter', 'Construction', 'Sailing'];
  /* The skills tab's layout, row by row. */
  var SKILL_GRID = [0, 3, 14, 2, 16, 13, 1, 15, 10, 4, 17, 7, 5, 12, 11, 6, 9, 8, 20, 18, 19, 22, 21, 23];
  /* The worn-items layout: wearpos per cell, null for none. */
  var EQUIP = [null, 0, null, 1, 2, 13, 3, 4, 5, null, 7, null, 9, 10, 12];
  var SLOT = { 0: 'Head', 1: 'Cape', 2: 'Neck', 3: 'Weapon', 4: 'Body', 5: 'Shield', 7: 'Legs',
    9: 'Hands', 10: 'Feet', 12: 'Ring', 13: 'Ammo' };

  var OBJS = null;          /* [[id, name, pack, flags, wearpos, icon], ...] */
  var BY = {};              /* id -> row */
  var GRIDINFO = { cell: [36, 32], cols: 32, per_sheet: 1024 };
  var VARP_NAME = {};       /* id -> name */
  var VARPS = [];           /* [[id, name], ...] */
  var INV_NAME = {};        /* inv id -> name */
  var tab = 'skills';
  try { tab = localStorage.getItem('pe.tab') || 'skills'; } catch (e) { }

  /* ---- small helpers ---------------------------------------------------- */

  function h(tag, attrs, kids) {
    var el = document.createElement(tag);
    if (attrs) {
      for (var k in attrs) {
        var v = attrs[k];
        if (v === null || v === undefined || v === false) continue;
        if (k === 'class') el.className = v;
        else if (k === 'text') el.textContent = v;
        else if (k === 'style') el.setAttribute('style', v);
        else if (k.slice(0, 2) === 'on') el.addEventListener(k.slice(2), v);
        else el.setAttribute(k, v === true ? '' : v);
      }
    }
    (kids || []).forEach(function (c) {
      if (c === null || c === undefined || c === false) return;
      el.appendChild(typeof c === 'string' || typeof c === 'number' ? document.createTextNode(String(c)) : c);
    });
    return el;
  }

  function fmt(n) { return Number(n).toLocaleString('en-US'); }

  /* The game's experience table. */
  var XP = [0, 0];
  (function () {
    var points = 0;
    for (var lvl = 1; lvl < 99; lvl++) {
      points += Math.floor(lvl + 300 * Math.pow(2, lvl / 7));
      XP[lvl + 1] = Math.floor(points / 4);
    }
  })();
  function levelFor(xp) {
    var lvl = 1;
    while (lvl < 99 && xp >= XP[lvl + 1]) lvl++;
    return lvl;
  }

  /* The inventory's stack text: yellow, then thousands in white, then
   * millions in green. */
  function stackText(n) {
    if (n >= 10000000) return [Math.floor(n / 1000000) + 'M', 'm'];
    if (n >= 100000) return [Math.floor(n / 1000) + 'K', 'k'];
    return [String(n), ''];
  }

  function objName(id) { var r = BY[id]; return r ? r[1] : 'obj ' + id; }

  /* The atlas cell a stack of `count` draws as: the record's count
   * variants (coins become a pile), else its own. */
  function cellFor(r, count) {
    var cell = r[5];
    if (r[6] && count) r[6].forEach(function (v) { if (count >= v[0]) cell = v[1]; });
    return cell;
  }

  /* An item's icon at 2x (or 1x with `small`), from its atlas cell; `count`
   * picks the stack's look. */
  function icon(id, small, count) {
    var r = BY[id];
    var cls = 'pe-icon' + (small ? ' sm' : '');
    if (!r || r[5] < 0) return h('div', { class: cls + ' none', text: r ? r[1] : '#' + id });
    var n = cellFor(r, count), s = Math.floor(n / GRIDINFO.per_sheet), c = n % GRIDINFO.per_sheet;
    var cw = GRIDINFO.cell[0], ch = GRIDINFO.cell[1], k = small ? 1 : 2;
    var x = (c % GRIDINFO.cols) * cw * k, y = Math.floor(c / GRIDINFO.cols) * ch * k;
    return h('div', {
      class: cls, title: r[1],
      style: 'background-image:url(' + ART + 'icons_' + s + '.png);background-position:-' + x + 'px -' + y +
        'px;background-size:' + (GRIDINFO.cols * cw * k) + 'px ' + (Math.ceil(GRIDINFO.per_sheet / GRIDINFO.cols) * ch * k) + 'px'
    });
  }

  function skillIcon(stat) { return h('img', { src: ART + 'skill_' + stat + '.png', alt: '' }); }

  var toasts = h('div', { class: 'pe-toasts' });
  document.body.appendChild(toasts);
  function toast(text, bad) {
    if (!text) return;
    var t = h('div', { class: 'pe-toast' + (bad ? ' bad' : ''), text: text });
    toasts.appendChild(t);
    setTimeout(function () { t.remove(); }, bad ? 7000 : 3500);
  }

  function post(path, fields) {
    var body = new URLSearchParams(fields || {});
    root.classList.add('pe-busy');
    return fetch('/admin/players/' + encodeURIComponent(KEY) + '/' + path, {
      method: 'POST', credentials: 'same-origin', body: body,
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' }
    }).then(function (r) {
      if (!r.ok) throw new Error('The site answered ' + r.status + '.');
      return r.json();
    }).then(function (res) {
      if (res.state) S = res.state;
      if (res.message) toast(res.message, !res.ok);
      render();
      return res;
    }).catch(function (e) {
      toast(e.message || String(e), true);
      return { ok: false };
    }).finally(function () { root.classList.remove('pe-busy'); });
  }

  function set(k, v, n) { return post('set', { k: k, v: v === undefined ? '' : String(v), n: n || '' }); }

  /* Online edits are the game's cheats: these can, the rest wait for a logout. */
  function canOnline(k, v) {
    if (/^stat\.\d+\.level$/.test(k)) return true;
    if (/^varp\.\d+$/.test(k)) return true;
    if (/^item\.(inv|worn)\.\d+$/.test(k)) return v !== '';
    return k === 'clear.inv';
  }
  function editable(k, v) { return !S.online || canOnline(k, v); }
  var ONLINE_WHY = 'Log the player out to change this: online, only levels, varps, items given to ' +
    'the backpack or worn, and an emptied backpack go through the game.';

  /* ---- dialogs ------------------------------------------------------------ */

  var open = null;
  function closeDialog() { if (open) { open.remove(); open = null; } }
  document.addEventListener('keydown', function (e) { if (e.key === 'Escape') closeDialog(); });

  function dialog(title, content, buttons, side) {
    closeDialog();
    var box = h('div', { class: 'pe-dialog', role: 'dialog', 'aria-label': title }, [
      h('header', null, [side || null, h('h2', { text: title }),
        h('button', { class: 'pe-close', 'aria-label': 'Close', onclick: closeDialog, text: '×' })]),
      h('div', { class: 'content' }, content),
      h('footer', null, buttons)
    ]);
    open = h('div', { class: 'pe-modal', onclick: function (e) { if (e.target === open) closeDialog(); } }, [box]);
    document.body.appendChild(open);
    var first = box.querySelector('input');
    if (first) setTimeout(function () { first.focus(); first.select && first.select(); }, 0);
    return box;
  }

  /* Pick an item for `section` slot `slot`. */
  function itemDialog(section, slot, current) {
    var sectionName = section === 'inv' ? 'Backpack' : section === 'worn' ? 'Equipment' :
      section === 'bank' ? 'Bank' : invLabel(+section.split('.')[1]);
    var wearpos = section === 'worn' ? slot : null;
    var key = 'item.' + section + '.' + slot;
    var chosen = current ? current[0] : null;
    var q = h('input', { type: 'search', placeholder: 'Search by name, pack name or #id', autocomplete: 'off' });
    var anySlot = h('input', { type: 'checkbox' });
    var count = h('input', { type: 'number', min: 1, max: 2147483647, value: current ? current[1] : 1 });
    var list = h('div', { class: 'pe-results', role: 'listbox' });
    var put = h('button', { class: 'btn', text: current ? 'Replace' : 'Put in slot' });
    var empty = h('button', { class: 'btn quiet', text: 'Empty slot', disabled: !current });

    function results() {
      list.textContent = '';
      if (!OBJS) { list.appendChild(h('div', { class: 'pe-empty', text: 'The item list is still loading…' })); return; }
      var text = q.value.trim().toLowerCase();
      var hits = [];
      var byId = /^#?(\d+)$/.exec(text);
      for (var i = 0; i < OBJS.length && hits.length < 400; i++) {
        var r = OBJS[i];
        if (wearpos !== null && !anySlot.checked && r[4] !== wearpos) continue;
        if (byId) { if (String(r[0]).indexOf(byId[1]) !== 0) continue; }
        else if (text && r[1].toLowerCase().indexOf(text) < 0 && r[2].indexOf(text.replace(/ /g, '_')) < 0) continue;
        hits.push(r);
      }
      /* Names that start with the text first, then the shorter. */
      hits.sort(function (a, b) {
        var as = a[1].toLowerCase().indexOf(text) === 0 ? 0 : 1, bs = b[1].toLowerCase().indexOf(text) === 0 ? 0 : 1;
        return as - bs || a[1].length - b[1].length || a[0] - b[0];
      });
      hits.slice(0, 80).forEach(function (r) {
        var row = h('div', {
          class: 'pe-result' + (r[0] === chosen ? ' on' : ''), role: 'option',
          onclick: function () { chosen = r[0]; results(); put.disabled = false; },
          ondblclick: function () { chosen = r[0]; submit(); }
        }, [icon(r[0], true), h('div', { class: 'nm' }, [h('b', { text: r[1] }),
          h('small', { text: '#' + r[0] + (r[2] ? '  ·  ' + r[2] : '') })]),
          (r[3] & 2) ? h('span', { class: 'pe-tag note', text: 'note' }) : null,
          (r[3] & 1) && !(r[3] & 2) ? h('span', { class: 'pe-tag', text: 'stack' }) : null,
          r[4] >= 0 && SLOT[r[4]] ? h('span', { class: 'pe-tag', text: SLOT[r[4]] }) : null]);
        list.appendChild(row);
      });
      if (!hits.length) list.appendChild(h('div', { class: 'pe-empty', text: 'Nothing matches.' }));
      put.disabled = chosen === null;
    }
    function submit() {
      if (chosen === null) return;
      var n = Math.max(1, parseInt(count.value, 10) || 1);
      closeDialog();
      set(key, chosen + ' ' + n);
    }
    q.addEventListener('input', results);
    q.addEventListener('keydown', function (e) {
      if (e.key === 'Enter') {
        var first = list.querySelector('.pe-result');
        if (chosen === null && first) first.click();
        submit();
      }
    });
    anySlot.addEventListener('change', results);
    put.addEventListener('click', submit);
    empty.addEventListener('click', function () { closeDialog(); set(key, ''); });
    if (S.online) {
      empty.disabled = true;
      empty.title = ONLINE_WHY;
      if (section !== 'inv' && section !== 'worn') { put.disabled = true; put.title = ONLINE_WHY; }
    }

    var content = [];
    if (current) content.push(h('div', { class: 'pe-current' }, [icon(current[0]), h('div', null, [
      h('b', { text: objName(current[0]) }), h('div', { text: 'x ' + fmt(current[1]) + '   ·   #' + current[0] })])]));
    if (S.online && (section === 'inv' || section === 'worn'))
      content.push(h('p', { class: 'note', text: section === 'inv' ?
        'Online, the item is given to the first free backpack slot.' :
        'Online, the item is given to the backpack and then worn.' }));
    content.push(q);
    if (wearpos !== null) content.push(h('label', { class: 'check', style: 'margin:8px 0 0' }, [anySlot, ' Show items for any slot']));
    content.push(list);
    dialog(sectionName + ' · slot ' + (wearpos !== null ? (SLOT[slot] || slot) : slot),
      content,
      [h('label', { style: 'margin:0' }, ['Count ', count]), h('span', { class: 'grow' }), empty, put]);
    results();
  }

  function skillDialog(stat) {
    var st = S.stats[stat];
    var xp = Math.floor(st[1] / 10);
    var lvl = levelFor(xp);
    var level = h('input', { type: 'number', min: 1, max: 99, value: lvl });
    var exp = h('input', { type: 'number', min: 0, max: 200000000, value: xp });
    var setLevel = h('button', { class: 'btn', text: 'Set level' });
    var setXp = h('button', { class: 'btn quiet', text: 'Set experience' });
    var info = h('div', { class: 'pe-xp' });
    function refresh() {
      var l = Math.min(99, Math.max(1, parseInt(level.value, 10) || 1));
      info.textContent = 'Level ' + l + ' starts at ' + fmt(XP[l]) + ' xp' +
        (l < 99 ? ' · level ' + (l + 1) + ' at ' + fmt(XP[l + 1]) : '');
    }
    level.addEventListener('input', refresh);
    refresh();
    function go(k, v) { closeDialog(); set(k, v); }
    setLevel.addEventListener('click', function () { go('stat.' + stat + '.level', level.value); });
    setXp.addEventListener('click', function () { go('stat.' + stat + '.xp', exp.value); });
    level.addEventListener('keydown', function (e) { if (e.key === 'Enter') setLevel.click(); });
    exp.addEventListener('keydown', function (e) { if (e.key === 'Enter') setXp.click(); });
    if (S.online) { setXp.disabled = true; setXp.title = 'Online, a level can be set but exact experience cannot: log the player out first.'; }
    var quick = h('div', { class: 'pe-quick' }, [1, 10, 50, 70, 85, 99].map(function (n) {
      return h('button', { class: 'btn quiet', text: String(n), onclick: function () { level.value = n; refresh(); } });
    }));
    dialog(SKILL[stat], [
      h('div', { class: 'pe-field' }, [h('label', null, ['Level', level]), h('label', null, ['Experience', exp])]),
      quick, h('p', { style: 'margin:12px 0 0' }, [info])
    ], [h('span', { class: 'grow pe-xp', text: 'Now ' + fmt(xp) + ' xp, level ' + lvl + (st[0] !== lvl ? ' (boosted ' + st[0] + ')' : '') }),
      setXp, setLevel], h('div', { class: 'pe-skill-head' }, [h('img', { src: ART + 'skill_' + stat + '.png', alt: '' })]));
  }

  function posDialog() {
    var p = S.online ? [S.online.x, S.online.z, S.online.level] : S.pos;
    var x = h('input', { type: 'number', min: 0, max: 16383, value: p[0] });
    var z = h('input', { type: 'number', min: 0, max: 16383, value: p[1] });
    var lv = h('input', { type: 'number', min: 0, max: 3, value: p[2] });
    var go = h('button', { class: 'btn', text: 'Move', onclick: function () {
      closeDialog(); set('pos', x.value + ' ' + z.value + ' ' + lv.value);
    } });
    var presets = h('div', { class: 'pe-quick' }, [['Lumbridge', 3222, 3218, 0], ['Varrock', 3213, 3424, 0],
      ['Falador', 2964, 3378, 0], ['Edgeville', 3093, 3493, 0], ['Grand Exchange', 3164, 3486, 0]].map(function (t) {
      return h('button', { class: 'btn quiet', text: t[0], onclick: function () { x.value = t[1]; z.value = t[2]; lv.value = t[3]; } });
    }));
    dialog('Where they log in', [
      h('div', { class: 'pe-field' }, [h('label', null, ['x', x]), h('label', null, ['z', z]), h('label', null, ['Level', lv])]),
      presets], [h('span', { class: 'grow' }), go]);
  }

  function addItemTo(section, items) {
    var used = {};
    items.forEach(function (it) { used[it[0]] = 1; });
    var slot = 0;
    while (used[slot]) slot++;
    itemDialog(section, slot, null);
  }

  /* ---- the panels ---------------------------------------------------------- */

  function invLabel(inv) {
    var n = INV_NAME[inv];
    return n ? n.replace(/_/g, ' ') : 'Container ' + inv;
  }

  function itemsBy(list) { var m = {}; list.forEach(function (it) { m[it[0]] = it; }); return m; }

  function slotCell(section, slot, it) {
    var kids = [];
    if (it) {
      var t = stackText(it[2]);
      kids.push(icon(it[1], false, it[2]));
      if (it[2] > 1 || (BY[it[1]] && (BY[it[1]][3] & 1))) kids.push(h('span', { class: 'num ' + t[1], text: t[0] }));
    }
    return h('div', {
      class: 'pe-slot' + (it ? '' : ' empty'),
      title: it ? objName(it[1]) + ' x ' + fmt(it[2]) + ' (#' + it[1] + ')' : 'Empty slot ' + slot,
      onclick: function () { itemDialog(section, slot, it ? [it[1], it[2]] : null); }
    }, kids);
  }

  function skillsPanel() {
    var total = 0, totalXp = 0;
    for (var s = 0; s < 24; s++) { var x = Math.floor(S.stats[s][1] / 10); total += levelFor(x); totalXp += x; }
    var grid = h('div', { class: 'pe-skills pe-stone' }, SKILL_GRID.map(function (stat) {
      var st = S.stats[stat], xp = Math.floor(st[1] / 10), lvl = levelFor(xp);
      var frac = lvl >= 99 ? 1 : (xp - XP[lvl]) / (XP[lvl + 1] - XP[lvl]);
      return h('div', { class: 'pe-skill', title: SKILL[stat] + ': ' + fmt(xp) + ' xp', onclick: function () { skillDialog(stat); } }, [
        skillIcon(stat),
        h('div', { class: 'lv' }, [String(st[0]), h('small', { text: '/' + lvl }),
          h('span', { class: 'pe-prog' }, [h('i', { style: 'width:' + Math.round(frac * 100) + '%' })])])
      ]);
    }).concat([h('div', { class: 'pe-total' }, [h('span', { text: 'Total level: ' + total }), h('span', { text: fmt(totalXp) + ' xp' })])]));
    var side = h('section', { class: 'panel' }, [h('h2', { text: 'Character' }), h('dl', { class: 'pe-kv' }, [
      h('dt', { text: 'Logs in at' }), h('dd', null, [S.pos.join(', ') + ' ',
        h('button', { class: 'linkbtn', style: 'color:#7a2a12', text: 'move', disabled: !!S.online,
          title: S.online ? ONLINE_WHY : null, onclick: posDialog })]),
      S.online ? h('dt', { text: 'Standing at' }) : null,
      S.online ? h('dd', { text: S.online.x + ', ' + S.online.z + ', level ' + S.online.level }) : null,
      h('dt', { text: 'Combat level' }), h('dd', { text: String(combat()) }),
      h('dt', { text: 'Total experience' }), h('dd', { text: fmt(totalXp) })
    ]), h('p', { class: 'note', style: 'margin-top:14px', text: 'Click a skill to set its level or experience. ' +
      'Setting a level puts the experience at that level’s start and clears any boost.' })]);
    return h('div', { class: 'pe-skill-layout' }, [grid, side]);
  }

  function combat() {
    var L = function (s) { return levelFor(Math.floor(S.stats[s][1] / 10)); };
    var base = 0.25 * (L(1) + L(3) + Math.floor(L(5) / 2));
    var melee = 0.325 * (L(0) + L(2)), range = 0.325 * Math.floor(L(4) * 1.5), mage = 0.325 * Math.floor(L(6) * 1.5);
    return Math.floor(base + Math.max(melee, range, mage));
  }

  function inventoryPanel() {
    var inv = itemsBy(S.inv), worn = itemsBy(S.worn);
    var invGrid = h('div', { class: 'pe-items inv pe-stone' });
    for (var i = 0; i < 28; i++) invGrid.appendChild(slotCell('inv', i, inv[i]));
    var equip = h('div', { class: 'pe-equip pe-stone' }, EQUIP.map(function (pos) {
      if (pos === null) return h('div');
      var it = worn[pos];
      return h('div', {
        class: 'pe-eslot', title: it ? objName(it[1]) + ' x ' + fmt(it[2]) : SLOT[pos] + ' (empty)',
        onclick: function () { itemDialog('worn', pos, it ? [it[1], it[2]] : null); }
      }, [it ? icon(it[1], false, it[2]) : h('img', { class: 'bg', src: ART + 'slot_' + pos + '.png', alt: '' }),
        it && it[2] > 1 ? h('span', { class: 'num ' + stackText(it[2])[1], style: 'position:absolute;left:4px;top:3px;font:bold 13px Verdana;color:#ff0;text-shadow:1px 1px 0 #000', text: stackText(it[2])[0] }) : null,
        h('span', { class: 'lbl', text: SLOT[pos] })]);
    }));
    function clearBtn(section, label) {
      var k = 'clear.' + section;
      return h('button', { class: 'btn quiet', text: label, disabled: !editable(k), title: editable(k) ? null : ONLINE_WHY,
        onclick: function () { if (confirmText('Empty the ' + (section === 'inv' ? 'backpack' : 'equipment') + '?')) set(k); } });
    }
    return h('div', { class: 'pe-inv-layout' }, [
      h('div', { class: 'pe-col' }, [h('div', { class: 'pe-toolbar' }, [h('h3', { text: 'Backpack', style: 'margin:0' }), h('span', { class: 'grow' }),
        h('span', { class: 'note', text: S.inv.length + ' / 28' }), clearBtn('inv', 'Empty')]), invGrid]),
      h('div', { class: 'pe-col' }, [h('div', { class: 'pe-toolbar' }, [h('h3', { text: 'Equipment', style: 'margin:0' }), h('span', { class: 'grow' }),
        clearBtn('worn', 'Unequip all')]), equip])
    ]);
  }

  /* A confirm that does not block the page: a tiny dialog would be nicer,
   * but emptying is rare and the browser's own is clear. */
  function confirmText(text) { return window.confirm(text); }

  function bankLike(section, items, title) {
    var filter = h('input', { type: 'search', placeholder: 'Filter' });
    var grid = h('div', { class: 'pe-items bank pe-stone' });
    function draw() {
      grid.textContent = '';
      var text = filter.value.trim().toLowerCase();
      var shown = items.filter(function (it) { return !text || objName(it[1]).toLowerCase().indexOf(text) >= 0 || String(it[1]) === text; });
      shown.forEach(function (it) { grid.appendChild(slotCell(section, it[0], it)); });
      if (!shown.length) grid.appendChild(h('div', { class: 'pe-empty', style: 'grid-column:1/-1', text: items.length ? 'Nothing matches.' : 'Empty.' }));
    }
    filter.addEventListener('input', draw);
    draw();
    var k = 'clear.' + section;
    return h('div', null, [h('div', { class: 'pe-toolbar' }, [title ? h('h3', { text: title, style: 'margin:0' }) : null, filter,
      h('span', { class: 'grow' }), h('span', { class: 'note', text: items.length + ' item' + (items.length === 1 ? '' : 's') + ' · ' +
        fmt(items.reduce(function (a, it) { return a + it[2]; }, 0)) + ' total' }),
      h('button', { class: 'btn quiet', text: 'Empty', disabled: !editable(k), title: editable(k) ? null : ONLINE_WHY,
        onclick: function () { if (confirmText('Empty ' + (title || 'the bank') + '?')) set(k); } }),
      h('button', { class: 'btn', text: '+ Add item', disabled: S.online, title: S.online ? ONLINE_WHY : null,
        onclick: function () { addItemTo(section, items); } })]), grid]);
  }

  function containersPanel() {
    if (!S.containers.length) return h('div', { class: 'pe-empty', text: 'No other containers in this save.' });
    return h('div', { class: 'stack' }, S.containers.map(function (c) {
      return bankLike('container.' + c.inv, c.items, invLabel(c.inv) + '  (#' + c.inv + ')');
    }));
  }

  function varpsPanel() {
    var filter = h('input', { type: 'search', placeholder: 'Find a varp by name or id' });
    var body = h('tbody');
    var table = h('div', { class: 'scroll' }, [h('table', { class: 'pe-varps' }, [
      h('thead', null, [h('tr', null, [h('th', { text: 'Id' }), h('th', { text: 'Name' }), h('th', { text: 'Value' }), h('th')])]), body])]);
    var have = {};
    S.varps.forEach(function (v) { have[v[0]] = v[1]; });
    function row(id, value, isNew) {
      var input = h('input', { type: 'number', value: value });
      var tr;
      var save = h('button', { class: 'btn quiet', text: isNew ? 'Add' : 'Save', onclick: function () {
        set('varp.' + id, input.value, VARP_NAME[id] || '');
      } });
      input.addEventListener('input', function () { tr.classList.toggle('changed', String(input.value) !== String(value)); });
      input.addEventListener('keydown', function (e) { if (e.key === 'Enter') save.click(); });
      tr = h('tr', null, [h('td', { class: 'id', text: String(id) }), h('td', { class: 'name', text: VARP_NAME[id] || '' }),
        h('td', null, [input]), h('td', { class: 'row-actions' }, [save, isNew ? null : h('button', {
          class: 'pe-x', title: 'Set to 0 (removes it from the save)', text: '×',
          onclick: function () { set('varp.' + id, 0, VARP_NAME[id] || ''); } })])]);
      if (S.online && !VARP_NAME[id]) { save.disabled = true; save.title = 'Online, a varp is set by its name, and this one has none.'; }
      return tr;
    }
    function draw() {
      body.textContent = '';
      var text = filter.value.trim().toLowerCase();
      var ids = Object.keys(have).map(Number).sort(function (a, b) { return a - b; });
      var shown = 0;
      ids.forEach(function (id) {
        var name = (VARP_NAME[id] || '').toLowerCase();
        if (text && name.indexOf(text) < 0 && String(id).indexOf(text) !== 0) return;
        body.appendChild(row(id, have[id], false));
        shown++;
      });
      /* Searching also offers varps the save does not hold yet. */
      if (text) {
        var extra = 0;
        for (var i = 0; i < VARPS.length && extra < 25; i++) {
          var v = VARPS[i];
          if (have[v[0]] !== undefined) continue;
          if (v[1].toLowerCase().indexOf(text) < 0 && String(v[0]) !== text) continue;
          body.appendChild(row(v[0], 0, true));
          extra++;
        }
        shown += extra;
      }
      if (!shown) body.appendChild(h('tr', null, [h('td', { colspan: 4, class: 'pe-empty', text: 'Nothing matches.' })]));
    }
    filter.addEventListener('input', draw);
    draw();
    return h('div', null, [h('div', { class: 'pe-toolbar' }, [filter, h('span', { class: 'grow' }),
      h('span', { class: 'note', text: S.varps.length + ' set in the ' + (S.snapshot ? 'live snapshot' : 'save') })]),
      h('p', { class: 'note', text: S.snapshot ?
        'This is the game’s live state, so it includes temporary varps; only those a varp’s config marks permanent survive a logout.' :
        'The save holds only permanent varps. Search to add one it does not have yet; 0 removes it.' }), table]);
  }

  /* ---- the page ------------------------------------------------------------ */

  function header() {
    var refresh = h('button', { class: 'btn quiet', text: 'Refresh', onclick: function () { post('state'); } });
    var logout = S.online ? h('button', { class: 'btn', text: 'Log out', title: 'End their session (saved first) so everything can be edited',
      onclick: function () { if (confirmText('Log ' + S.name + ' out of the game?')) post('logout'); } }) : null;
    return h('div', { class: 'pe-head' }, [
      h('h1', { text: S.name }),
      h('div', { class: 'pe-meta' }, [
        h('span', { class: 'pe-pill' }, [h('span', { class: 'pe-dot' + (S.online ? ' on' : '') }),
          S.online ? 'Online' : (S.ctl === 'down' ? 'Offline (game not running)' : 'Offline')]),
        S.admin ? h('span', { class: 'pe-pill' }, [h('b', { text: 'Admin' })]) : null,
        h('span', { class: 'pe-pill' }, S.linked ? ['Linked to ', h('b', { text: S.linked })] : ['Not linked']),
        h('a', { class: 'pe-pill', href: '/players/' + encodeURIComponent(KEY), text: 'Public page' })
      ]),
      h('div', { class: 'pe-actions' }, [refresh, logout])
    ]);
  }

  function banner() {
    if (S.ctl === 'refused') return h('div', { class: 'pe-banner warn' }, [h('strong', { text: 'The game server did not answer. ' }),
      'Whether the player is online is unknown, so edits are refused until it does.']);
    if (!S.save && !S.snapshot) return h('div', { class: 'pe-banner warn' }, [h('strong', { text: 'No save yet. ' }),
      'This account has never logged in, so there is nothing to edit until it has.']);
    if (S.online) return h('div', { class: 'pe-banner' }, [h('strong', { text: 'Online. ' }),
      'You are seeing the live game. Levels, varps, items given to the backpack or worn, and emptying the backpack apply at once; ' +
      'everything else needs them logged out first (the button above saves and logs them out).']);
    return null;
  }

  var TABS = [
    ['skills', 'Skills', function () { return ART + 'skill_3.png'; }, function () { return null; }],
    ['inventory', 'Inventory', function () { return ART + 'slot_4.png'; }, function () { return S.inv.length + S.worn.length; }],
    ['bank', 'Bank', function () { return null; }, function () { return S.bank.length; }],
    ['containers', 'Containers', function () { return null; }, function () { return S.containers.length; }],
    ['varps', 'Varps', function () { return null; }, function () { return S.varps.length; }]
  ];

  function render() {
    root.textContent = '';
    root.appendChild(header());
    var b = banner();
    if (b) root.appendChild(b);
    if (!S.save && !S.snapshot) return;
    root.appendChild(h('div', { class: 'pe-tabs', role: 'tablist' }, TABS.map(function (t) {
      var img = t[2](), n = t[3]();
      return h('button', { class: 'pe-tab' + (tab === t[0] ? ' on' : ''), role: 'tab', 'aria-selected': tab === t[0] ? 'true' : 'false',
        onclick: function () { tab = t[0]; try { localStorage.setItem('pe.tab', tab); } catch (e) { } render(); } },
        [img ? h('img', { src: img, alt: '' }) : null, t[1], n !== null ? h('span', { class: 'n', text: String(n) }) : null]);
    })));
    var panel = tab === 'inventory' ? inventoryPanel() : tab === 'bank' ? bankLike('bank', S.bank, null) :
      tab === 'containers' ? containersPanel() : tab === 'varps' ? varpsPanel() : skillsPanel();
    root.appendChild(h('div', { class: 'pe-body' }, [h('section', { class: 'panel' }, [panel])]));
  }

  render();

  /* The art's indexes: names and icons for items, names for varps and
   * containers. The page works without them, with ids for names. */
  function getJson(file) {
    return fetch(ART + file, { credentials: 'same-origin' }).then(function (r) { return r.ok ? r.json() : null; }).catch(function () { return null; });
  }
  Promise.all([getJson('objs.json'), getJson('varps.json'), getJson('invs.json')]).then(function (all) {
    if (all[0]) {
      OBJS = all[0].objs;
      GRIDINFO = all[0];
      OBJS.forEach(function (r) { BY[r[0]] = r; });
    } else toast('The item icons are not in this build, so items show as ids.', true);
    if (all[1]) { VARPS = all[1].varps; VARPS.forEach(function (v) { VARP_NAME[v[0]] = v[1]; }); }
    if (all[2]) all[2].invs.forEach(function (v) { INV_NAME[v[0]] = v[1]; });
    if (!open) render();
  });
})();
