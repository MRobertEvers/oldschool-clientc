"use strict";

var assert = require("assert");
var path = require("path");
var codec = require(path.join("..", "codec-es3.js"));
var createRuntime = require(path.join("..", "runtime-ie8.js")).createRuntime;

function makeNode(tag) {
    var item = {
        tagName: String(tag).toUpperCase(),
        children: [], parentNode: null, firstChild: null,
        className: "", style: {}, title: "", value: "", checked: false,
        selectedIndex: -1, clientWidth: 320, clientHeight: 500,
        offsetWidth: 320, offsetHeight: 500, _attrs: {}
    };
    item.appendChild = function (child) {
        child.parentNode = item;
        item.children[item.children.length] = child;
        item.firstChild = item.children.length ? item.children[0] : null;
        return child;
    };
    item.removeChild = function (child) {
        var i;
        for (i = 0; i < item.children.length; i += 1) {
            if (item.children[i] === child) {
                item.children.splice(i, 1);
                child.parentNode = null;
                break;
            }
        }
        item.firstChild = item.children.length ? item.children[0] : null;
        return child;
    };
    item.setAttribute = function (name, value) { item._attrs[name] = String(value); };
    item.getAttribute = function (name) { return item._attrs[name] || null; };
    item.fire = function (name, event) {
        var handler = item["on" + name];
        if (typeof handler === "function") {
            handler(event || { srcElement: item, target: item, clientX: 10, clientY: 10 });
        }
    };
    item.focus = function () {};
    /* IE6/7 shape: edges but no width/height members. */
    item.getBoundingClientRect = function () {
        return { left: 0, top: 0, right: 200, bottom: 100 };
    };
    if (item.tagName === "CANVAS") {
        item.getContext = function () { return null; };
    }
    return item;
}

function fixture() {
    var ids = {};
    var document = {
        activeElement: null,
        documentElement: makeNode("html"),
        createElement: makeNode,
        getElementById: function (id) { return ids[id] || null; }
    };
    function add(id, tag, parent) {
        var item = makeNode(tag);
        item.id = id;
        ids[id] = item;
        if (parent) { parent.appendChild(item); }
        return item;
    }
    var shell = add("tpc-shell", "table", null);
    shell.className = "tpc-shell tpc-collapsed";
    shell.setAttribute("data-tpc-legacy", "1");
    var pane = add("tpc-pane", "div", shell);
    var frameIds = ["tl", "t", "tr", "l", "r", "bl", "b", "br"];
    var i;
    for (i = 0; i < frameIds.length; i += 1) {
        add("tpc-frame-" + frameIds[i], "div", pane);
    }
    add("tpc-title", "h1", pane);
    add("tpc-close", "button", pane);
    add("tpc-tabs", "div", pane);
    add("tpc-content", "div", pane);
    var railHost = makeNode("td");
    shell.appendChild(railHost);
    add("tpc-rail-list", "div", railHost);
    add("tpc-status", "div", null);
    return { document: document, ids: ids };
}

function command(kind, extra) {
    var result = {
        k: kind, p: 3, w: -1, tab: -1, v: 0, c: 0,
        x: 0, y: 0, cw: 0, ch: 0, label: "", text: "", s: 0
    };
    var key;
    for (key in extra) { if (Object.prototype.hasOwnProperty.call(extra, key)) { result[key] = extra[key]; } }
    return result;
}

var built = fixture();
var posted = [];
var timers = [];
var root = {
    document: built.document,
    ToriRSPluginChromeCodec: codec,
    torirsPluginChromePostMessage: function (value) { posted[posted.length] = codec.parse(value); },
    setTimeout: function (fn) { timers[timers.length] = fn; return timers.length; }
};
var runtime = createRuntime(root, built.document);
assert(runtime, "legacy runtime boots without modern DOM APIs");

var railMessage = {
    protocol: 1, type: "rail.snapshot", registryRevision: 1,
    selectionGeneration: 5, pageGeneration: 1, activePlugin: -1,
    lastSelectedPlugin: -1, selectedEntry: -2, expanded: false,
    entries: [
        { kind: 1, pluginIndex: -2, preferredWidth: 320, title: "Manage Plugins", iconAsset: "", badge: "", attention: false },
        { kind: 2, pluginIndex: 0, preferredWidth: 320, title: "Plugin", iconAsset: "icon.png", badge: "7", attention: true }
    ]
};
assert(runtime.receive(codec.stringify(railMessage)), "bundled parser accepts host JSON text");
assert.strictEqual(runtime.inspect().railEntries, 2);
built.ids["tpc-rail-list"].children[0].fire("click");
assert.strictEqual(posted[posted.length - 1].type, "rail.select");
assert.strictEqual(posted[posted.length - 1].pluginIndex, -2);
assert.strictEqual(posted[posted.length - 1].selectionGeneration, 5);

runtime.receive({
    protocol: 1, type: "theme", revision: 1,
    assets: { panelBody: "skin/body.bmp", pluginIcon: "skin/wrench.png", close: "skin/close.bmp" }
});
var manageButton = built.ids["tpc-rail-list"].children[0];
assert(/AlphaImageLoader/.test(manageButton.children[0].style.filter || ""),
    "transparent fallback PNG uses the XP AlphaImageLoader path");
runtime.receive({
    protocol: 1, type: "rail.icon", pluginIndex: 0, revision: 2,
    width: 16, height: 16, url: "bitmap/plugin0.bmp"
});
assert.strictEqual(built.ids["tpc-rail-list"].children[1].children[0].tagName, "IMG",
    "host BMP icon uses an ordinary image element");

railMessage.selectionGeneration = 6;
railMessage.pageGeneration = 10;
railMessage.activePlugin = 0;
railMessage.lastSelectedPlugin = 0;
railMessage.selectedEntry = 0;
railMessage.expanded = true;
runtime.receive(railMessage);
runtime.receive({
    protocol: 1, type: "page.snapshot", pageGeneration: 10, panel: 3,
    title: "Plugin", commands: [
        command(3, { text: "Plugin" }),
        command(8, { w: 1, v: 1, label: "Enabled", s: 101 }),
        command(8, { w: 2, v: 2, label: "Text", text: "abc", s: 102 }),
        command(8, { w: 3, v: 12, label: "Chart", ch: 120, s: 103 })
    ]
});
assert.strictEqual(runtime.inspect().widgetCount, 3);
var oldCheckbox = built.ids["tpc-content"].children[0].children[0];
oldCheckbox.checked = true;
oldCheckbox.fire("change");
var raw = codec.parse(runtime.takeIntent());
assert.strictEqual(raw.g, 10);
assert.strictEqual(raw.s, 101, "legacy intent is generation and serial fenced");

runtime.receive({
    protocol: 1, type: "custom.bitmap", pageGeneration: 10,
    panel: 3, widget: 3, widgetSerial: 103, revision: 1,
    scaleMilli: 1000, width: 200, height: 100, url: "bitmap/chart.bmp"
});
var custom = built.ids["tpc-content"].children[2]._tpcRecord.control;
assert.strictEqual(custom.style.height, "120px",
    "legacy custom region obeys the command height rather than image aspect ratio");
assert.strictEqual(custom.children[0].tagName, "IMG", "custom region is host-URL/IMG based");
custom.fire("mousedown", { clientX: 50, clientY: 25 });
custom.fire("mouseup", { clientX: 50, clientY: 25 });
raw = codec.parse(runtime.takeIntent());
assert.strictEqual(raw.k, 8);
assert.strictEqual(raw.s, 103);
assert(raw.x >= 0 && raw.y >= 0, "edge-only IE rect still yields local custom coordinates");

runtime.receive({
    protocol: 1, type: "page.snapshot", pageGeneration: 11, panel: 3,
    title: "Replacement", commands: [
        command(3, { text: "Replacement" }),
        command(8, { w: 1, v: 1, label: "Enabled", s: 202 })
    ]
});
oldCheckbox.fire("change");
assert.strictEqual(runtime.takeIntent(), "", "stale XP DOM listener cannot retarget a recycled handle");

var editor = built.ids["tpc-content"].children[0].children[0];
built.document.activeElement = editor;
built.ids["tpc-content"].fire("focusin", { srcElement: editor });
assert.strictEqual(posted[posted.length - 1].type, "editor.focus");
assert.strictEqual(posted[posted.length - 1].focused, true);
built.ids["tpc-content"].fire("focusout", { srcElement: editor });
built.document.activeElement = null;
while (timers.length) { timers.shift()(); }
assert.strictEqual(posted[posted.length - 1].focused, false,
    "legacy editor blur returns IME ownership after debounce");

assert.deepStrictEqual(codec.parse(codec.stringify({ text: "a\nb", n: 3, yes: true })),
    { text: "a\nb", n: 3, yes: true }, "ES3 codec round-trips without native JSON");
assert.strictEqual(runtime.receive("{bad"), false, "malformed input is rejected safely");
console.log("legacy plugin chrome runtime: ok");
