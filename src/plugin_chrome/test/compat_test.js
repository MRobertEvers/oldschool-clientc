"use strict";

var assert = require("assert");
var fs = require("fs");
var path = require("path");
var root = path.join(__dirname, "..");
var modernRuntime = fs.readFileSync(path.join(root, "runtime.js"), "utf8");
var legacyRuntime = fs.readFileSync(path.join(root, "runtime-ie8.js"), "utf8");
var codec = fs.readFileSync(path.join(root, "codec-es3.js"), "utf8");
var modernHtml = fs.readFileSync(path.join(root, "modern.html"), "utf8");
var legacyHtml = fs.readFileSync(path.join(root, "legacy-ie8.html"), "utf8");
var modernCss = fs.readFileSync(path.join(root, "modern.css"), "utf8");
var legacyCss = fs.readFileSync(path.join(root, "legacy-ie8.css"), "utf8");
var fontRoot = path.join(root, "..", "..", "res", "plugin_chrome", "font");

var forbiddenRuntime = [
    [/=>/, "arrow functions"],
    [/`/, "template literals"],
    [/\b(?:const|let|class|Map|Promise|ResizeObserver|globalThis)\b/, "post-Chrome39 syntax/global"],
    [/Number\.isInteger|Array\.isArray|Object\.assign/, "newer static helpers"],
    [/\.forEach\s*\(|\.find\s*\(|\.fill\s*\(/, "newer array methods"],
    [/addEventListener|querySelector|querySelectorAll/, "IE8-only/newer DOM event or selector APIs"],
    [/\bJSON\s*\./, "native JSON dependency"],
    [/pointerdown|pointerup|setPointerCapture/, "pointer-event dependency"],
    [/\.delete\s*\(/, "reserved-word method access"],
    [/\beval\s*\(|new\s+Function\s*\(|\.innerHTML\s*=/, "code/HTML injection primitive"]
];

forbiddenRuntime.forEach(function (entry) {
    assert.doesNotMatch(modernRuntime, entry[0], "Chrome39 runtime must not use " + entry[1]);
    assert.doesNotMatch(legacyRuntime, entry[0], "XP runtime must not use " + entry[1]);
});
assert.doesNotMatch(codec, /\bJSON\s*\.|=>|`|\b(?:const|let|class|globalThis)\b/,
    "the bundled codec is ES3 and independent of native JSON");
assert.doesNotMatch(codec, /,\s*[}\]]/, "the ES3 codec has no trailing literal commas");
assert.strictEqual(modernRuntime, legacyRuntime,
    "both pages execute the same downlevel state machine; the legacy marker selects safe pixels");

assert.match(legacyHtml, /http-equiv=["']X-UA-Compatible["'][^>]+IE=8/i,
    "XP asks for IE8 standards mode when that engine is installed");
assert.match(legacyHtml, /data-tpc-legacy=["']1["']/,
    "legacy page selects URL/AlphaImageLoader behavior");
assert.match(legacyHtml, /name=["']viewport["'][^>]+width=device-width/i,
    "API22 maps its narrow host view to the same CSS-pixel rail width");
assert.match(legacyHtml, /codec-es3\.js[\s\S]*runtime-ie8\.js/,
    "legacy codec loads before the runtime");
assert.match(modernHtml, /codec-es3\.js[\s\S]*runtime\.js/,
    "modern codec loads before the runtime");

[modernHtml, legacyHtml].forEach(function (html) {
    assert.match(html, /Content-Security-Policy/i, "each page carries a local-only CSP");
    assert.doesNotMatch(html, /unsafe-inline|unsafe-eval/i, "CSP enables no inline code or eval");
    assert.doesNotMatch(html, /<script(?![^>]+src=)[^>]*>/i, "all scripts are local external files");
    assert.doesNotMatch(html, /<(?:iframe|object|embed)\b/i, "bundle embeds no foreign active content");
    assert.match(html, /font-src\s+'self'/i, "local cache-derived fonts are allowed by CSP");
});

assert.match(modernCss,
    /font-family:\s*"ToriRS Chrome";[\s\S]*ToriRSBody\.woff[\s\S]*ToriRSBody\.ttf/,
    "modern browsers use the cache-derived Body WOFF/TTF");
assert.match(modernCss,
    /font-family:\s*"ToriRS Chrome";[\s\S]*ToriRSMenu\.woff[\s\S]*font-weight:\s*700/,
    "headings and buttons can select the cache-derived Menu face by weight");
assert.match(modernCss, /ToriRS Chrome Small[\s\S]*ToriRSSmall\.woff/,
    "badges use the authored 10px/12px Small face");
assert.match(legacyCss,
    /font-family:\s*"ToriRS Chrome";\s*src:\s*url\("font\/ToriRSBody\.eot"\)/,
    "XP has a direct local-file EOT Body source");
assert.match(legacyCss,
    /font-family:\s*"ToriRS Chrome";\s*src:\s*url\("font\/ToriRSMenu\.eot"\)/,
    "XP has a direct local-file EOT Menu source");
assert.match(legacyCss,
    /font-family:\s*"ToriRS Chrome Small";\s*src:\s*url\("font\/ToriRSSmall\.eot"\)/,
    "XP has a direct local-file EOT Small source");
assert.doesNotMatch(legacyCss, /\.eot\?#iefix/i,
    "XP EOT URLs preserve their file:/// root string");
assert.match(legacyCss, /tpc-android-fonts[\s\S]*ToriRS Chrome Android/,
    "Chrome39 selects a distinct WOFF/TTF alias without overriding XP EOT");
assert.match(legacyRuntime, /tpc-android-fonts/,
    "the trusted bridge selects the Android font alias without user-agent guessing");
["ToriRSBody", "ToriRSMenu", "ToriRSSmall"].forEach(function (stem) {
    ["eot", "woff", "ttf"].forEach(function (extension) {
        assert(fs.existsSync(path.join(fontRoot, stem + "." + extension)),
            "font bundle contains " + stem + "." + extension);
    });
});

assert.doesNotMatch(legacyCss, /display\s*:\s*(?:flex|grid)|var\s*\(|--[a-z]|\bgap\s*:|\binset\s*:/i,
    "XP CSS uses table/absolute layout without modern CSS dependencies");
assert.match(legacyCss, /\.tpc-shell\s*\{[^}]*table-layout\s*:\s*fixed/i,
    "XP shell is a fixed table layout");
assert.match(legacyHtml, /id=["']tpc-shell-body["'][\s\S]*id=["']tpc-shell-row["']/,
    "legacy shell authors explicit full-height table sections for old MSHTML");
assert.match(legacyCss, /#tpc-shell-body\s*,\s*#tpc-shell-row\s*\{[^}]*height\s*:\s*100%/i,
    "legacy table body and row fill the embedded host height");
assert.match(legacyCss, /\.tpc-pane\s*\{[^}]*box-sizing\s*:\s*border-box/i,
    "pane border remains inside the host allocation on Chrome39/IE8");
assert.match(legacyCss, /\.tpc-content\s*\{[^}]*position\s*:\s*absolute/i,
    "XP page content uses absolute geometry");
assert.match(legacyCss, /\.tpc-row\s*\{[^}]*min-height\s*:\s*18px[^}]*margin\s*:\s*0 0 3px/i,
    "legacy rows use the authored 18px height and 3px pitch gap");
assert.match(legacyCss, /\.tpc-row \.tpc-label\s*\{[^}]*inline-block[^}]*width\s*:\s*104px/i,
    "legacy labels reserve the authored 104px column");
assert.match(legacyCss, /\.tpc-row > \.tpc-field\s*\{[^}]*width\s*:\s*55%;\s*width\s*:\s*calc\(/i,
    "old MSHTML gets a percentage field fallback before Chrome39 fills the remaining width");
assert.match(legacyCss, /#tpc-close\s*\{[^}]*width\s*:\s*16px[^}]*height\s*:\s*16px[^}]*background-repeat\s*:\s*no-repeat/i,
    "the 16px close sprite is centered once instead of tiled");
assert.match(legacyCss, /\.tpc-separator\s*\{[^}]*height\s*:\s*18px[^}]*background\s*:\s*transparent/i,
    "legacy separator keeps row pitch without painting a solid slab");
assert.match(legacyCss, /\.tpc-custom img\s*,\s*\.tpc-custom canvas\s*\{[^}]*height\s*:\s*100%/i,
    "legacy custom pixels fill the host-selected region height");

assert.match(modernCss, /display:\s*-webkit-box;[\s\S]*display:\s*flex;[\s\S]*display:\s*grid;/,
    "modern CSS supplies Chrome39 flex before optional grid enhancement");
assert.doesNotMatch(modernCss, /\bgap\s*:/, "Chrome39 layout does not depend on flex gap");
assert.match(modernCss, /background:\s*#0e0e0c;\s*background:\s*var\(--tpc-deep\)/,
    "literal palette fallback precedes CSS variables");
assert.match(modernCss, /left:\s*-6px;\s*right:\s*-6px;\s*top:\s*-6px;\s*bottom:\s*-6px;\s*inset:/,
    "edge geometry precedes the unsupported inset shorthand");
assert.match(modernCss, /\.tpc-content\s*\{[^}]*padding\s*:\s*6px/i,
    "browser content uses the shared authored panel padding");
assert.match(modernCss, /\.tpc-row\s*\{[^}]*min-height\s*:\s*18px[^}]*margin-bottom\s*:\s*3px/i,
    "browser rows use the shared 18px height and 3px pitch gap");
assert.match(modernCss, /\.tpc-label\s*\{[^}]*0 0 104px/i,
    "browser labels use the shared 104px column");
assert.match(modernCss, /\.tpc-tabs\s*\{[^}]*height\s*:\s*20px/i,
    "browser tabs use the shared 20px height");
assert.match(modernCss, /\.tpc-check\s*\{[^}]*width\s*:\s*17px[^}]*height\s*:\s*17px/i,
    "the default checkbox is rendered at its baked 17px size");
assert.match(modernCss, /\.tpc-button[^}]*\{[^}]*height\s*:\s*18px[^}]*background-size\s*:\s*18px 18px/i,
    "button art is fitted to the authored 18px control row");
assert.match(modernCss, /\.tpc-row select\s*\{[^}]*right 2px center[^}]*14px 14px/i,
    "dropdown arrow honors the authored two-pixel field inset");
assert.match(modernCss, /::\-webkit-scrollbar\s*\{[^}]*width\s*:\s*16px/i,
    "browser scrollbar uses the shared 16px strip");
assert.match(modernCss, /\.tpc-custom img\s*,\s*\.tpc-custom canvas\s*\{[^}]*height\s*:\s*100%/i,
    "custom pixels fill the host-selected region height");

assert.match(legacyRuntime, /AlphaImageLoader/, "XP transparent PNG fallback is bundled");
assert.match(legacyRuntime, /ToriRSAndroid[\s\S]*rgbaBase64/,
    "the same downlevel runtime retains the API22 typed bridge and optional RGBA path");
assert.match(legacyRuntime, /type:\s*['"]editor\.focus['"]/,
    "editor focus ownership is part of the downlevel runtime");
assert.match(legacyRuntime, /ScrollDown\.png[\s\S]*backgroundSize = ['"]14px 14px,auto['"]/,
    "runtime theme application preserves the skinned dropdown arrow");
assert.match(legacyRuntime, /style\.height = [^;]*Math\.max\(48, Math\.min\(512,/,
    "custom region height is bounded and driven by the semantic command");

console.log("plugin chrome compatibility gates: ok");
