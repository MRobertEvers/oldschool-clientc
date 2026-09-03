(function (global, factory) {
    'use strict';
    var exported = { createRuntime: factory, protocol: 1 };
    if (typeof module !== 'undefined' && module.exports)
        module.exports = exported;
    if (!global || !global.document)
        return;
    var runtime = factory(global, global.document);
    if (runtime)
        global.ToriRSPluginChrome = runtime;
})(typeof window !== 'undefined' ? window :
    (typeof global !== 'undefined' ? global : this), function createRuntime(global, document) {
    'use strict';
    var PROTOCOL = 1;
    var MAX_MESSAGES = 64;
    var MAX_ENTRIES = 33;
    var MAX_OPTIONS = 4096;
    var MAX_TEXT = 191;
    var Codec = global.ToriRSPluginChromeCodec;
    if (!Codec)
        return null;
    var CMD = {
        SYNC_BEGIN: 1, SYNC_END: 2,
        PANEL_OPEN: 3, PANEL_CLOSE: 4, PANEL_TITLE: 5, PANEL_RECT: 6, PANEL_TAB: 7,
        WIDGET_ADD: 8, WIDGET_REMOVE: 9, WIDGET_LABEL: 10, WIDGET_TEXT: 11,
        WIDGET_CHECKED: 12, WIDGET_HIDDEN: 13, WIDGET_COLOR: 14,
        WIDGET_SELECTED: 15, WIDGET_FOCUS: 16, WIDGET_OPTIONS: 17,
        WIDGET_OPTION: 18, CHECK_STYLE: 19
    };
    var W = {
        LABEL: 0, CHECKBOX: 1, TEXTINPUT: 2, SEPARATOR: 3, MENUITEM: 4,
        DROPDOWN: 5, MODELVIEW: 6, BUTTON: 7, TABSTRIP: 8, LISTROW: 9,
        COLORPICK: 10, TEXTAREA: 11, CUSTOM: 12, FREE: 13
    };
    var INTENT = {
        ACTIVATE: 1, ACTION: 2, TOGGLE: 3, TEXT: 4, PICK: 5, TAB: 6,
        CLOSE: 7, CUSTOM_ACTIVATE: 8
    };
    var ROW_ACTION = 0x1;
    var ROW_LOCKED = 0x2;
    /* Chrome 39 has no dependable keyed-collection implementation on every API-22 image.
     * Handles are small integers, so a prefixed own-property store is enough. */
    function Store() {
        this.values = {};
        this.count = 0;
    }
    Store.prototype.key = function (key) { return "$".concat(key); };
    Store.prototype.get = function (key) { return this.values[this.key(key)]; };
    Store.prototype.set = function (key, value) {
        var name = this.key(key);
        if (!Object.prototype.hasOwnProperty.call(this.values, name))
            this.count++;
        this.values[name] = value;
    };
    Store.prototype.drop = function (key) {
        var name = this.key(key);
        if (!Object.prototype.hasOwnProperty.call(this.values, name))
            return;
        delete this.values[name];
        this.count--;
    };
    Store.prototype.clear = function () { this.values = {}; this.count = 0; };
    Store.prototype.each = function (fn) {
        for (var name_1 in this.values)
            if (Object.prototype.hasOwnProperty.call(this.values, name_1))
                fn(this.values[name_1]);
    };
    var shell = document.getElementById('tpc-shell');
    var pane = document.getElementById('tpc-pane');
    var title = document.getElementById('tpc-title');
    var close = document.getElementById('tpc-close');
    var tabs = document.getElementById('tpc-tabs');
    var content = document.getElementById('tpc-content');
    var rail = document.getElementById('tpc-rail-list');
    var status = document.getElementById('tpc-status');
    if (!shell || !pane || !title || !close || !tabs || !content || !rail)
        return null;
    var legacy = shell.getAttribute && shell.getAttribute('data-tpc-legacy') === '1';
    var legacyPixels = legacy && !global.ToriRSAndroid;
    toggleClass(shell, 'tpc-android-fonts', legacy && !legacyPixels);
    var state = {
        rail: {
            registryRevision: 0, selectionGeneration: 0, pageGeneration: 0,
            activePlugin: -1, lastSelectedPlugin: -1, selectedEntry: -1,
            expanded: false, entries: []
        },
        railNodes: new Store(),
        icons: new Store(),
        themeRevision: 0,
        theme: {},
        pageGeneration: 0,
        panel: -1,
        activeTab: 0,
        checkStyle: 0,
        widgets: new Store(),
        sequence: 0,
        messages: [],
        intents: [],
        lastLayout: '',
        applying: false,
        editorFocused: false,
        focusToken: 0
    };
    /* Packaged Android cannot inject inline code under this bundle's CSP. Its
     * JavascriptInterface is therefore discovered here, in trusted local code. */
    if (typeof global.torirsChromeIntentPosted !== 'function' &&
        global.ToriRSAndroid && typeof global.ToriRSAndroid.intent === 'function') {
        global.torirsChromeIntentPosted = function (copy) {
            global.ToriRSAndroid.intent(copy.k, copy.p, copy.w, copy.v, copy.text, copy.x, copy.y, copy.g, copy.s);
        };
    }
    function integer(value, fallback) {
        return typeof value === 'number' && isFinite(value) && Math.floor(value) === value
            ? value : fallback;
    }
    function unsigned(value) {
        return Number(value) >>> 0;
    }
    function text(value, maximum) {
        if (maximum === void 0) { maximum = MAX_TEXT; }
        return String(value == null ? '' : value).slice(0, maximum);
    }
    function array(value) {
        return Object.prototype.toString.call(value) === '[object Array]';
    }
    function trimmed(value) {
        return text(value, 4096).replace(/^\s+|\s+$/g, '');
    }
    function bind(node, kind, handler) {
        node["on".concat(kind)] = handler;
    }
    function hasClass(node, name) {
        return new RegExp("(^|\\s)".concat(name, "(\\s|$)")).test(node.className || '');
    }
    function toggleClass(node, name, enabled) {
        var current = node.className || '';
        if (enabled && !hasClass(node, name))
            node.className = trimmed("".concat(current, " ").concat(name));
        else if (!enabled && hasClass(node, name))
            node.className = trimmed(current.replace(new RegExp("(^|\\s)".concat(name, "(?=\\s|$)"), 'g'), ' '));
    }
    function hidden(node, value) {
        node.hidden = !!value;
        node.style.display = value ? 'none' : '';
    }
    function clear(node) {
        while (node.firstChild)
            node.removeChild(node.firstChild);
    }
    function setText(node, value) {
        if (node)
            node.innerText = text(value, 4096);
    }
    function nextSequence() {
        state.sequence = (state.sequence + 1) >>> 0;
        if (!state.sequence)
            state.sequence = 1;
        return state.sequence;
    }
    function safeUrl(value, allowBlob) {
        if (allowBlob === void 0) { allowBlob = true; }
        var url = trimmed(value);
        if (!url || /[\u0000-\u001f]/.test(url))
            return '';
        if (/^(?:\.\.?\/|[A-Za-z0-9_.-]+\/)/.test(url) && !/\.\./.test(url))
            return url;
        if (/^torirs:\/\//i.test(url))
            return url;
        if (/^https:\/\/torirs\.local(?:\/|$)/i.test(url))
            return url;
        if (!legacyPixels && /^data:image\/(?:png|gif|bmp|webp);base64,[A-Za-z0-9+/=]+$/i.test(url))
            return url;
        if (!legacyPixels && allowBlob && /^blob:/i.test(url))
            return url;
        return '';
    }
    function cssUrl(url) {
        return url ? "url(\"".concat(url.replace(/["\\]/g, '\\$&'), "\")") : 'none';
    }
    function skinButton(button) {
        if (!button || legacyPixels)
            return;
        var left = safeUrl(state.theme.buttonLeft);
        var middle = safeUrl(state.theme.buttonMiddle);
        var right = safeUrl(state.theme.buttonRight);
        if (left && middle && right) {
            button.style.backgroundImage = "".concat(cssUrl(left), ",").concat(cssUrl(right), ",").concat(cssUrl(middle));
            button.style.backgroundRepeat = 'no-repeat,no-repeat,repeat-x';
            button.style.backgroundPosition = 'left center,right center,center';
            /* The source art is the 2x (36px-high) bake. Chrome geometry is in the
             * authored 1x units; DPR=2 displays these pixels one-for-one. */
            button.style.backgroundSize = '18px 18px,18px 18px,10px 18px';
        }
        else if (middle) {
            button.style.backgroundImage = cssUrl(middle);
            button.style.backgroundRepeat = 'repeat-x';
            button.style.backgroundPosition = 'center';
            button.style.backgroundSize = '10px 18px';
        }
    }
    function skinField(control) {
        if (!control || legacyPixels)
            return;
        var body = safeUrl(state.theme.dropdownBody) || 'skin/DropdownBody.png';
        var isSelect = String(control.tagName || '').toLowerCase() === 'select';
        var arrow = isSelect && (safeUrl(state.theme.scrollDown) || 'skin/ScrollDown.png');
        if (body && arrow) {
            control.style.backgroundImage = "".concat(cssUrl(arrow), ",").concat(cssUrl(body));
            control.style.backgroundPosition = 'right 2px center,left top';
            control.style.backgroundRepeat = 'no-repeat,repeat';
            control.style.backgroundSize = '14px 14px,auto';
        }
        else if (body) {
            control.style.backgroundImage = cssUrl(body);
            control.style.backgroundRepeat = 'repeat';
        }
    }
    function setTabsVisible(visible) {
        hidden(tabs, !visible);
        toggleClass(pane, 'tpc-no-tabs', !visible);
    }
    function sizeLegacyViewport() {
        if (!legacy)
            return;
        var doc = document.documentElement || {};
        var body = document.body || {};
        var height = doc.clientHeight || body.clientHeight || 0;
        if (height > 0) {
            shell.style.height = "".concat(height, "px");
            pane.style.height = "".concat(height, "px");
            rail.style.height = "".concat(height, "px");
        }
    }
    function normalizeIntent(input) {
        return {
            k: integer(input && input.k, 0),
            p: integer(input && input.p, -1),
            w: integer(input && input.w, -1),
            v: integer(input && input.v, 0),
            text: text(input && input.text),
            x: integer(input && input.x, 0),
            y: integer(input && input.y, 0),
            g: unsigned(input && input.g),
            s: unsigned(input && input.s)
        };
    }
    function queueEnvelope(envelope, widgetIntent) {
        var copy = Codec.parse(Codec.stringify(envelope));
        if (state.messages.length >= MAX_MESSAGES)
            state.messages.shift();
        state.messages.push(copy);
        if (widgetIntent) {
            var raw = normalizeIntent(widgetIntent);
            if (state.intents.length >= MAX_MESSAGES)
                state.intents.shift();
            state.intents.push(raw);
            if (typeof global.torirsChromeIntentPosted === 'function') {
                try {
                    global.torirsChromeIntentPosted({
                        k: raw.k, p: raw.p, w: raw.w, v: raw.v, text: raw.text,
                        x: raw.x, y: raw.y, g: raw.g, s: raw.s
                    });
                }
                catch (error) { /* Host failure cannot break retained UI. */ }
                return;
            }
        }
        var encoded = Codec.stringify(copy);
        var post = typeof global.torirsPluginChromePostMessage === 'function'
            ? global.torirsPluginChromePostMessage
            : (global.ToriRSAndroid && typeof global.ToriRSAndroid.postMessage === 'function'
                ? function (value) { return global.ToriRSAndroid.postMessage(value); }
                : null);
        if (post) {
            try {
                post(encoded);
            }
            catch (error) { /* Pull queue remains the fallback. */ }
        }
    }
    function postRailSelect(node) {
        var pluginIndex = integer(node && node._tpcPluginIndex, -1);
        var generation = unsigned(node && node._tpcSelectionGeneration);
        if (pluginIndex === -1 || generation === 0)
            return;
        queueEnvelope({
            protocol: PROTOCOL,
            type: 'rail.select',
            sequence: nextSequence(),
            pluginIndex: pluginIndex,
            selectionGeneration: generation
        });
    }
    function postWidget(record, kind, value, newText, x, y) {
        if (value === void 0) { value = 0; }
        if (newText === void 0) { newText = ''; }
        if (x === void 0) { x = 0; }
        if (y === void 0) { y = 0; }
        if (!record || state.widgets.get(record.handle) !== record ||
            state.pageGeneration !== record.generation || state.panel !== record.panel)
            return;
        var intent = normalizeIntent({
            k: kind, p: record.panel, w: record.handle, v: value, text: newText,
            x: x,
            y: y,
            g: record.generation, s: record.serial
        });
        queueEnvelope({
            protocol: PROTOCOL,
            type: 'widget.intent',
            sequence: nextSequence(),
            intent: intent
        }, intent);
    }
    function editable(node) {
        if (!node || !node.tagName)
            return false;
        var tag = String(node.tagName).toUpperCase();
        return tag === 'INPUT' || tag === 'SELECT' || tag === 'TEXTAREA';
    }
    function postEditorFocus(focused) {
        focused = !!focused;
        if (state.editorFocused === focused)
            return;
        state.editorFocused = focused;
        queueEnvelope({
            protocol: PROTOCOL,
            type: 'editor.focus',
            sequence: nextSequence(),
            focused: focused,
            pageGeneration: state.pageGeneration
        });
    }
    function decodeRgba(width, height, encoded) {
        var pixels = width * height;
        if (legacyPixels || !global.atob || !global.Uint8ClampedArray || !global.ImageData ||
            width <= 0 || height <= 0 || width > 4096 || height > 4096 ||
            pixels > 4194304)
            return '';
        try {
            var binary = global.atob(text(encoded, pixels * 6));
            if (binary.length !== pixels * 4)
                return '';
            var bytes = new global.Uint8ClampedArray(binary.length);
            for (var i = 0; i < binary.length; i++)
                bytes[i] = binary.charCodeAt(i) & 255;
            var canvas = document.createElement('canvas');
            canvas.width = width;
            canvas.height = height;
            var context = canvas.getContext && canvas.getContext('2d');
            if (!context)
                return '';
            context.putImageData(new global.ImageData(bytes, width, height), 0, 0);
            return canvas.toDataURL('image/png');
        }
        catch (error) {
            return '';
        }
    }
    function appendImage(parent, url, className, alt) {
        if (!url)
            return false;
        if (legacyPixels && /\.png(?:[?#]|$)/i.test(url)) {
            var transparent = document.createElement('span');
            transparent.className = className;
            transparent.title = alt || '';
            transparent.style.display = 'block';
            transparent.style.filter = "progid:DXImageTransform.Microsoft.AlphaImageLoader(src='" +
                url.replace(/'/g, '%27') + "',sizingMethod='scale')";
            parent.appendChild(transparent);
            return true;
        }
        var image = document.createElement('img');
        image.className = className;
        image.alt = alt || '';
        image.src = url;
        parent.appendChild(image);
        return true;
    }
    function messageBitmapUrl(message) {
        var direct = safeUrl(message && message.url);
        if (direct)
            return direct;
        if (message && message.rgbaBase64)
            return decodeRgba(integer(message.width, 0), integer(message.height, 0), message.rgbaBase64);
        return '';
    }
    function fallbackIcon(entry) {
        var themed = safeUrl(state.theme.pluginIcon);
        if (themed)
            return themed;
        return '';
    }
    function renderRailButton(entry, button) {
        var selected = entry.pluginIndex === state.rail.selectedEntry;
        var expanded = selected && state.rail.expanded;
        var cached = state.icons.get(entry.pluginIndex);
        var iconUrl = cached && cached.url ? cached.url : fallbackIcon(entry);
        button._tpcPluginIndex = entry.pluginIndex;
        button._tpcSelectionGeneration = state.rail.selectionGeneration;
        button.title = entry.title;
        button.setAttribute('aria-label', entry.title +
            (entry.badge ? ", ".concat(entry.badge) : '') +
            (entry.attention ? ', needs attention' : ''));
        button.setAttribute('aria-selected', selected ? 'true' : 'false');
        button.setAttribute('aria-expanded', expanded ? 'true' : 'false');
        toggleClass(button, 'tpc-attention', entry.attention);
        clear(button);
        if (iconUrl) {
            appendImage(button, iconUrl, 'tpc-rail-icon', '');
        }
        else {
            var fallback = document.createElement('span');
            fallback.className = 'tpc-rail-fallback';
            setText(fallback, trimmed(entry.title).slice(0, 1).toUpperCase() || 'P');
            button.appendChild(fallback);
        }
        if (entry.badge) {
            var badge = document.createElement('span');
            badge.className = 'tpc-badge';
            setText(badge, entry.badge);
            button.appendChild(badge);
        }
        if (entry.attention) {
            var attention = document.createElement('i');
            attention.className = 'tpc-attention-dot';
            attention.setAttribute('aria-hidden', 'true');
            button.appendChild(attention);
        }
    }
    function rebuildRail() {
        clear(rail);
        state.railNodes.clear();
        var _loop_1 = function (index) {
            var entry = state.rail.entries[index];
            var button = document.createElement('button');
            button.type = 'button';
            button.className = 'tpc-rail-entry';
            bind(button, 'click', function () { return postRailSelect(button); });
            state.railNodes.set(entry.pluginIndex, button);
            rail.appendChild(button);
            renderRailButton(entry, button);
        };
        for (var index = 0; index < state.rail.entries.length; index++) {
            _loop_1(index);
        }
    }
    function updateRailNodes() {
        for (var index = 0; index < state.rail.entries.length; index++) {
            var entry = state.rail.entries[index];
            var button = state.railNodes.get(entry.pluginIndex);
            if (button)
                renderRailButton(entry, button);
        }
    }
    function sanitizeRailEntry(input) {
        var kind = integer(input && input.kind, 0);
        var pluginIndex = integer(input && input.pluginIndex, -1);
        if ((kind !== 1 && kind !== 2) || (kind === 1 ? pluginIndex !== -2 : pluginIndex < 0))
            return null;
        return {
            kind: kind,
            pluginIndex: pluginIndex,
            preferredWidth: integer(input.preferredWidth, 320),
            title: text(input.title || (kind === 1 ? 'Manage Plugins' : 'Plugin'), 63),
            iconAsset: text(input.iconAsset, 63),
            badge: text(input.badge, 23),
            attention: !!input.attention
        };
    }
    function applyRailSnapshot(message) {
        var entries = [];
        var source = array(message.entries) ? message.entries : [];
        for (var i = 0; i < source.length && entries.length < MAX_ENTRIES; i++) {
            var entry = sanitizeRailEntry(source[i]);
            if (entry)
                entries.push(entry);
        }
        var revision = unsigned(message.registryRevision);
        var rebuild = revision !== state.rail.registryRevision ||
            entries.length !== state.rail.entries.length || state.railNodes.count !== entries.length;
        state.rail = {
            registryRevision: revision,
            selectionGeneration: unsigned(message.selectionGeneration),
            pageGeneration: unsigned(message.pageGeneration),
            activePlugin: integer(message.activePlugin, -1),
            lastSelectedPlugin: integer(message.lastSelectedPlugin, -1),
            selectedEntry: integer(message.selectedEntry, -1),
            expanded: !!message.expanded,
            entries: entries
        };
        toggleClass(shell, 'tpc-collapsed', !state.rail.expanded);
        if (rebuild)
            rebuildRail();
        else
            updateRailNodes();
        if (!state.rail.expanded)
            clearPage(false);
        reportLayout();
    }
    function applyRailIcon(message) {
        var pluginIndex = integer(message.pluginIndex, -1);
        var revision = unsigned(message.revision);
        var width = integer(message.width, 0);
        var height = integer(message.height, 0);
        if (pluginIndex < 0 || !revision || width < 0 || height < 0 || width > 64 || height > 64)
            return;
        var current = state.icons.get(pluginIndex);
        if (current && current.revision === revision)
            return;
        var url = width && height ? messageBitmapUrl(message) : '';
        state.icons.set(pluginIndex, { revision: revision, width: width, height: height, url: url });
        var entry = null;
        for (var i = 0; i < state.rail.entries.length; i++)
            if (state.rail.entries[i].pluginIndex === pluginIndex) {
                entry = state.rail.entries[i];
                break;
            }
        var button = state.railNodes.get(pluginIndex);
        if (entry && button)
            renderRailButton(entry, button);
    }
    function applyTheme(message) {
        var revision = unsigned(message.revision);
        if (!revision || revision === state.themeRevision)
            return;
        var assets = message.assets && typeof message.assets === 'object' ? message.assets : {};
        var allowed = [
            'panelBody', 'pluginIcon', 'buttonLeft', 'buttonMiddle', 'buttonRight',
            'checkOn', 'checkOff', 'checkBoxOn', 'checkBoxOff', 'dropdownBody',
            'scrollUp', 'scrollDown', 'scrollTrack', 'scrollGripTop', 'scrollGripMiddle',
            'scrollGripBottom', 'close', 'frameTopLeft', 'frameTop', 'frameTopRight',
            'frameLeft', 'frameRight', 'frameBottomLeft', 'frameBottom', 'frameBottomRight'
        ];
        var next = {};
        for (var i = 0; i < allowed.length; i++)
            next[allowed[i]] = safeUrl(assets[allowed[i]]);
        state.theme = next;
        state.themeRevision = revision;
        pane.style.backgroundImage = cssUrl(next.panelBody);
        if (rail.parentNode)
            rail.parentNode.style.backgroundImage = cssUrl(next.panelBody);
        close.style.backgroundImage = cssUrl(next.close);
        var frameIds = ['tl', 't', 'tr', 'l', 'r', 'bl', 'b', 'br'];
        var frameAssets = ['frameTopLeft', 'frameTop', 'frameTopRight', 'frameLeft',
            'frameRight', 'frameBottomLeft', 'frameBottom', 'frameBottomRight'];
        for (var i = 0; i < frameIds.length; i++) {
            var node = document.getElementById("tpc-frame-".concat(frameIds[i]));
            if (node)
                node.style.backgroundImage = cssUrl(next[frameAssets[i]]);
        }
        updateRailNodes();
        state.widgets.each(renderWidget);
    }
    function clearPage(report) {
        if (report === void 0) { report = true; }
        postEditorFocus(false);
        state.widgets.clear();
        clear(content);
        clear(tabs);
        setTabsVisible(false);
        hidden(pane, true);
        state.pageGeneration = 0;
        state.panel = -1;
        state.activeTab = 0;
        setText(title, 'Plugins');
        if (report)
            reportLayout();
    }
    function normalizeCommand(input) {
        return {
            k: integer(input && input.k, 0), p: integer(input && input.p, -1),
            w: integer(input && input.w, -1), tab: integer(input && input.tab, -1),
            v: integer(input && input.v, 0), c: unsigned(input && input.c),
            x: integer(input && input.x, 0), y: integer(input && input.y, 0),
            cw: integer(input && input.cw, 0), ch: integer(input && input.ch, 0),
            label: text(input && input.label, 63), text: text(input && input.text),
            s: unsigned(input && input.s)
        };
    }
    function labelNode(labelValue) {
        var label = document.createElement('span');
        label.className = 'tpc-label';
        setText(label, labelValue);
        return label;
    }
    function attachLabel(row, record, control) {
        if (record.label)
            row.appendChild(labelNode(record.label));
        row.appendChild(control);
    }
    function createWidget(command) {
        if (command.p !== state.panel || command.w < 0 || command.v < 0 || command.v > W.FREE)
            return;
        removeWidget(command.w);
        var row = document.createElement('div');
        row.className = 'tpc-row';
        var record = {
            handle: command.w, panel: command.p, generation: state.pageGeneration,
            serial: command.s, kind: command.v, tab: command.tab, shape: command.cw,
            rows: command.ch, label: command.label, text: command.text, color: command.c,
            checked: false, selected: -1, hidden: false, focused: false,
            options: [], optionsRevision: 0, customRevision: 0, customScale: 1000,
            row: row,
            control: null
        };
        row._tpcRecord = record;
        switch (record.kind) {
            case W.LABEL: {
                var value = document.createElement('span');
                value.className = 'tpc-value';
                row.appendChild(value);
                record.control = value;
                break;
            }
            case W.CHECKBOX: {
                var box_1 = document.createElement('input');
                box_1.type = 'checkbox';
                box_1.className = 'tpc-check';
                bind(box_1, 'change', function () { return postWidget(record, INTENT.TOGGLE, box_1.checked ? 1 : 0); });
                row.appendChild(box_1);
                var caption = document.createElement('span');
                row.appendChild(caption);
                record.control = box_1;
                record.caption = caption;
                break;
            }
            case W.TEXTINPUT: {
                var input_1 = document.createElement('input');
                input_1.type = 'text';
                input_1.className = 'tpc-field tpc-text-field';
                input_1.maxLength = MAX_TEXT;
                bind(input_1, 'change', function () { return postWidget(record, INTENT.TEXT, 0, input_1.value); });
                attachLabel(row, record, input_1);
                record.control = input_1;
                break;
            }
            case W.TEXTAREA: {
                row.className += ' tpc-tall-row';
                var area_1 = document.createElement('textarea');
                area_1.className = 'tpc-field tpc-textarea';
                area_1.maxLength = MAX_TEXT;
                area_1.rows = record.rows > 0 ? Math.min(record.rows, 32) : 4;
                bind(area_1, 'change', function () { return postWidget(record, INTENT.TEXT, 0, area_1.value); });
                attachLabel(row, record, area_1);
                record.control = area_1;
                break;
            }
            case W.SEPARATOR: {
                row.className += ' tpc-separator';
                var line = document.createElement('span');
                line.className = 'tpc-separator-line';
                row.appendChild(line);
                break;
            }
            case W.MENUITEM:
            case W.BUTTON: {
                var button = document.createElement('button');
                button.type = 'button';
                button.className = record.kind === W.BUTTON ? 'tpc-button' : 'tpc-menu';
                bind(button, 'click', function () { return postWidget(record, INTENT.ACTIVATE); });
                row.appendChild(button);
                record.control = button;
                break;
            }
            case W.DROPDOWN: {
                var select_1 = document.createElement('select');
                select_1.className = 'tpc-field tpc-select';
                bind(select_1, 'change', function () { return postWidget(record, INTENT.PICK, select_1.selectedIndex); });
                attachLabel(row, record, select_1);
                record.control = select_1;
                break;
            }
            case W.MODELVIEW: {
                var model = document.createElement('div');
                model.className = 'tpc-model';
                row.appendChild(model);
                record.control = model;
                break;
            }
            case W.TABSTRIP:
                hidden(row, true);
                record.control = tabs;
                break;
            case W.LISTROW: {
                var holder = document.createElement('div');
                holder.className = 'tpc-list-row';
                var action = document.createElement('button');
                action.type = 'button';
                action.className = 'tpc-row-action';
                bind(action, 'click', function () { return postWidget(record, (record.shape & ROW_ACTION) ? INTENT.ACTION : INTENT.ACTIVATE); });
                holder.appendChild(action);
                record.caption = action;
                if (!(record.shape & ROW_LOCKED)) {
                    var toggle_1 = document.createElement('input');
                    toggle_1.type = 'checkbox';
                    toggle_1.className = 'tpc-check';
                    bind(toggle_1, 'change', function () { return postWidget(record, INTENT.TOGGLE, toggle_1.checked ? 1 : 0); });
                    holder.appendChild(toggle_1);
                    record.toggle = toggle_1;
                }
                row.appendChild(holder);
                record.control = holder;
                break;
            }
            case W.COLORPICK: {
                var holder = document.createElement('span');
                holder.className = 'tpc-color';
                var swatch_1 = document.createElement('input');
                swatch_1.type = legacy ? 'text' : 'color';
                swatch_1.className = 'tpc-field tpc-color-swatch';
                var hex_1 = document.createElement('input');
                hex_1.type = 'text';
                hex_1.className = 'tpc-field tpc-color-text';
                hex_1.maxLength = 7;
                bind(swatch_1, 'change', function () {
                    hex_1.value = swatch_1.value;
                    postWidget(record, INTENT.TEXT, 0, swatch_1.value);
                });
                bind(hex_1, 'change', function () { return postWidget(record, INTENT.TEXT, 0, hex_1.value); });
                holder.appendChild(swatch_1);
                holder.appendChild(hex_1);
                attachLabel(row, record, holder);
                record.control = holder;
                record.swatch = swatch_1;
                record.hex = hex_1;
                break;
            }
            case W.CUSTOM: {
                row.className += ' tpc-tall-row';
                var custom_1 = document.createElement('div');
                custom_1.className = 'tpc-custom';
                custom_1.style.height = "".concat(Math.max(48, Math.min(512, record.rows || 120)), "px");
                custom_1.tabIndex = 0;
                bind(custom_1, 'mousedown', function (event) {
                    record.pointer = { x: event.clientX, y: event.clientY };
                });
                bind(custom_1, 'mouseup', function (event) {
                    if (!record.pointer)
                        return;
                    record.pointer = null;
                    var bounds = custom_1.getBoundingClientRect();
                    var boundsWidth = bounds.width || (bounds.right - bounds.left);
                    var boundsHeight = bounds.height || (bounds.bottom - bounds.top);
                    if (!boundsWidth || !boundsHeight)
                        return;
                    var px = Math.max(0, Math.min(record.bitmapWidth - 1, Math.floor((event.clientX - bounds.left) * record.bitmapWidth / boundsWidth)));
                    var py = Math.max(0, Math.min(record.bitmapHeight - 1, Math.floor((event.clientY - bounds.top) * record.bitmapHeight / boundsHeight)));
                    postWidget(record, INTENT.CUSTOM_ACTIVATE, 0, '', Math.floor(px * 1000 / Math.max(1, record.customScale)), Math.floor(py * 1000 / Math.max(1, record.customScale)));
                });
                bind(custom_1, 'keydown', function (event) {
                    var code = event.keyCode || event.which || 0;
                    if (code === 13 || code === 32)
                        postWidget(record, INTENT.CUSTOM_ACTIVATE);
                });
                if (record.label)
                    row.appendChild(labelNode(record.label));
                row.appendChild(custom_1);
                record.control = custom_1;
                break;
            }
            case W.FREE:
            default:
                hidden(row, true);
                break;
        }
        content.appendChild(row);
        state.widgets.set(record.handle, record);
        renderWidget(record);
        reconcileTabsAndVisibility();
    }
    function removeWidget(handle) {
        var record = state.widgets.get(handle);
        if (!record)
            return;
        if (record.row.parentNode)
            record.row.parentNode.removeChild(record.row);
        if (record.kind === W.TABSTRIP)
            setTabsVisible(false);
        state.widgets.drop(handle);
        reconcileTabsAndVisibility();
    }
    function renderOptions(record) {
        if (!record || !record.control)
            return;
        if (record.kind === W.DROPDOWN) {
            var select = record.control;
            clear(select);
            for (var index = 0; index < record.options.length; index++) {
                var option = document.createElement('option');
                setText(option, record.options[index]);
                select.appendChild(option);
            }
            if (record.selected >= 0 && record.selected < record.options.length)
                select.selectedIndex = record.selected;
        }
        else if (record.kind === W.TABSTRIP) {
            clear(tabs);
            var _loop_2 = function (index) {
                var button = document.createElement('button');
                button.type = 'button';
                setText(button, record.options[index]);
                button.setAttribute('aria-selected', index === state.activeTab ? 'true' : 'false');
                bind(button, 'click', function () { return postWidget(record, INTENT.TAB, index); });
                tabs.appendChild(button);
            };
            for (var index = 0; index < record.options.length; index++) {
                _loop_2(index);
            }
            setTabsVisible(record.options.length !== 0);
        }
    }
    function renderWidget(record) {
        if (!record || state.applying)
            return;
        switch (record.kind) {
            case W.LABEL:
                setText(record.control, record.text || record.label);
                if (record.color) {
                    var raw = (record.color & 0xffffff).toString(16);
                    record.control.style.color = "#".concat('000000'.slice(raw.length)).concat(raw);
                }
                break;
            case W.CHECKBOX:
                record.control.checked = !!record.checked;
                if (!legacyPixels) {
                    var square = state.checkStyle === 1;
                    var on = safeUrl(square ? state.theme.checkBoxOn : state.theme.checkOn) ||
                        (square ? 'skin/CheckBoxOn.png' : 'skin/CheckOn.png');
                    var off = safeUrl(square ? state.theme.checkBoxOff : state.theme.checkOff) ||
                        (square ? 'skin/CheckBoxOff.png' : 'skin/CheckOff.png');
                    var side = square ? 18 : 17;
                    record.control.style.width = "".concat(side, "px");
                    record.control.style.height = "".concat(side, "px");
                    record.control.style.minWidth = "".concat(side, "px");
                    record.control.style.backgroundSize = "".concat(side, "px ").concat(side, "px");
                    record.control.style.backgroundImage = cssUrl(record.checked ? on : off);
                }
                setText(record.caption, record.label || record.text);
                break;
            case W.TEXTINPUT:
            case W.TEXTAREA:
                skinField(record.control);
                if (record.control.value !== record.text)
                    record.control.value = record.text;
                break;
            case W.MENUITEM:
            case W.BUTTON:
                skinButton(record.control);
                setText(record.control, record.text || record.label);
                break;
            case W.DROPDOWN:
                skinField(record.control);
                renderOptions(record);
                break;
            case W.TABSTRIP:
                renderOptions(record);
                break;
            case W.MODELVIEW:
                setText(record.control, record.label || record.text || 'Model preview');
                break;
            case W.LISTROW:
                skinButton(record.caption);
                setText(record.caption, record.label || record.text);
                if (record.toggle)
                    record.toggle.checked = !!record.checked;
                break;
            case W.COLORPICK: {
                var value = /^#[0-9a-f]{6}$/i.test(record.text) ? record.text : '#000000';
                record.swatch.value = value;
                record.hex.value = record.text || value;
                break;
            }
            default:
                break;
        }
        hidden(record.row, !!record.hidden || (record.tab >= 0 && record.tab !== state.activeTab) ||
            record.kind === W.TABSTRIP || record.kind === W.FREE);
        if (record.focused && record.control && record.control.focus) {
            try {
                record.control.focus({ preventScroll: true });
            }
            catch (error) {
                record.control.focus();
            }
        }
    }
    function reconcileTabsAndVisibility() {
        if (state.applying)
            return;
        state.widgets.each(renderWidget);
    }
    function applyCommand(raw) {
        var command = normalizeCommand(raw);
        if (command.k === CMD.CHECK_STYLE) {
            state.checkStyle = command.v;
            toggleClass(shell, 'tpc-check-square', command.v === 1);
            reconcileTabsAndVisibility();
            return;
        }
        if (command.k === CMD.PANEL_OPEN) {
            if (state.panel < 0)
                state.panel = command.p;
            if (command.p === state.panel && command.text)
                setText(title, command.text);
            return;
        }
        if (command.p !== state.panel)
            return;
        var record = state.widgets.get(command.w);
        switch (command.k) {
            case CMD.PANEL_CLOSE:
                clearPage();
                break;
            case CMD.PANEL_TITLE:
                setText(title, command.text);
                break;
            case CMD.PANEL_TAB:
                state.activeTab = command.v;
                reconcileTabsAndVisibility();
                break;
            case CMD.PANEL_RECT:
                break;
            case CMD.WIDGET_ADD:
                createWidget(command);
                break;
            case CMD.WIDGET_REMOVE:
                removeWidget(command.w);
                break;
            case CMD.WIDGET_LABEL:
                if (record) {
                    record.label = command.label;
                    renderWidget(record);
                }
                break;
            case CMD.WIDGET_TEXT:
                if (record) {
                    record.text = command.text;
                    renderWidget(record);
                }
                break;
            case CMD.WIDGET_CHECKED:
                if (record) {
                    record.checked = !!command.v;
                    renderWidget(record);
                }
                break;
            case CMD.WIDGET_HIDDEN:
                if (record) {
                    record.hidden = !!command.v;
                    renderWidget(record);
                }
                break;
            case CMD.WIDGET_COLOR:
                if (record) {
                    record.color = command.c;
                    renderWidget(record);
                }
                break;
            case CMD.WIDGET_SELECTED:
                if (record) {
                    record.selected = command.v;
                    renderWidget(record);
                }
                break;
            case CMD.WIDGET_FOCUS:
                if (record) {
                    record.focused = !!command.v;
                    renderWidget(record);
                }
                break;
            case CMD.WIDGET_OPTIONS:
                if (record) {
                    var count = Math.max(0, Math.min(MAX_OPTIONS, command.v));
                    record.options = new Array(count);
                    for (var i = 0; i < count; i++)
                        record.options[i] = '';
                    record.optionsRevision++;
                    renderWidget(record);
                }
                break;
            case CMD.WIDGET_OPTION:
                if (record && command.v >= 0 && command.v < record.options.length) {
                    record.options[command.v] = command.text;
                    record.optionsRevision++;
                    renderWidget(record);
                }
                break;
            default:
                break;
        }
    }
    function applyPageSnapshot(message) {
        var generation = unsigned(message.pageGeneration);
        var panel = integer(message.panel, -1);
        if (!generation || panel < 0)
            return;
        clearPage(false);
        state.pageGeneration = generation;
        state.panel = panel;
        state.activeTab = 0;
        state.checkStyle = integer(message.checkStyle, 0);
        setText(title, message.title || 'Plugins');
        hidden(pane, false);
        toggleClass(shell, 'tpc-collapsed', false);
        var commands = array(message.commands) ? message.commands : [];
        state.applying = true;
        try {
            for (var i = 0; i < commands.length; i++)
                applyCommand(commands[i]);
        }
        finally {
            state.applying = false;
        }
        reconcileTabsAndVisibility();
        reportLayout();
    }
    function applyPageDelta(message) {
        if (!state.pageGeneration || unsigned(message.pageGeneration) !== state.pageGeneration)
            return;
        var commands = array(message.commands) ? message.commands : [];
        state.applying = true;
        try {
            for (var i = 0; i < commands.length; i++)
                applyCommand(commands[i]);
        }
        finally {
            state.applying = false;
        }
        reconcileTabsAndVisibility();
    }
    function applyCustomBitmap(message) {
        var generation = unsigned(message.pageGeneration);
        var panel = integer(message.panel, -1);
        var handle = integer(message.widget, -1);
        var serial = unsigned(message.widgetSerial);
        var revision = unsigned(message.revision);
        var width = integer(message.width, 0);
        var height = integer(message.height, 0);
        var record = state.widgets.get(handle);
        if (!record || record.kind !== W.CUSTOM || generation !== state.pageGeneration ||
            panel !== state.panel || record.panel !== panel || record.serial !== serial ||
            !revision || revision === record.customRevision || width <= 0 || height <= 0 ||
            width > 4096 || height > 4096)
            return;
        var url = messageBitmapUrl(message);
        if (!url)
            return;
        clear(record.control);
        if (!appendImage(record.control, url, '', record.label || ''))
            return;
        record.customRevision = revision;
        record.customScale = Math.max(1, integer(message.scaleMilli, 1000));
        record.bitmapWidth = width;
        record.bitmapHeight = height;
    }
    function reportLayout() {
        sizeLegacyViewport();
        var visible = pane.style.display !== 'none' && !!state.pageGeneration;
        var width = visible ? Math.max(0, pane.clientWidth || 0) : 0;
        var height = visible ? Math.max(0, pane.clientHeight || 0) : 0;
        var scaleMilli = Math.max(1, Math.round(Number(global.devicePixelRatio || 1) * 1000));
        var sizeClass = width < 320 ? 0 : (width >= 480 ? 2 : 1);
        var key = [state.rail.selectionGeneration, state.pageGeneration, width, height,
            scaleMilli, sizeClass, visible ? 1 : 0].join(':');
        if (key === state.lastLayout)
            return;
        state.lastLayout = key;
        queueEnvelope({
            protocol: PROTOCOL,
            type: 'layout',
            sequence: nextSequence(),
            selectionGeneration: state.rail.selectionGeneration,
            pageGeneration: state.pageGeneration,
            width: width,
            height: height,
            scaleMilli: scaleMilli,
            sizeClass: sizeClass,
            visible: visible,
            gameVisible: true
        });
    }
    function receive(input) {
        var message = input;
        if (typeof input === 'string') {
            try {
                message = Codec.parse(input);
            }
            catch (error) {
                return false;
            }
        }
        if (!message || typeof message !== 'object' || integer(message.protocol, 0) !== PROTOCOL)
            return false;
        switch (message.type) {
            case 'rail.snapshot':
                applyRailSnapshot(message);
                break;
            case 'rail.icon':
                applyRailIcon(message);
                break;
            case 'theme':
                applyTheme(message);
                break;
            case 'page.snapshot':
                applyPageSnapshot(message);
                break;
            case 'page.delta':
                applyPageDelta(message);
                break;
            case 'page.close':
                if (!message.pageGeneration || unsigned(message.pageGeneration) === state.pageGeneration)
                    clearPage();
                break;
            case 'custom.bitmap':
                applyCustomBitmap(message);
                break;
            default: return false;
        }
        return true;
    }
    function takeMessage() {
        return state.messages.length ? Codec.stringify(state.messages.shift()) : '';
    }
    function takeIntent() {
        return state.intents.length ? Codec.stringify(state.intents.shift()) : '';
    }
    bind(close, 'click', function () {
        if (state.panel < 0 || !state.pageGeneration)
            return;
        var intent = normalizeIntent({
            k: INTENT.CLOSE, p: state.panel, w: -1,
            g: state.pageGeneration, s: 0
        });
        queueEnvelope({
            protocol: PROTOCOL, type: 'widget.intent', sequence: nextSequence(),
            intent: intent
        }, intent);
    });
    bind(content, 'focusin', function (event) {
        event = event || global.event || {};
        if (!editable(event.target || event.srcElement))
            return;
        state.focusToken++;
        postEditorFocus(true);
    });
    bind(content, 'focusout', function (event) {
        event = event || global.event || {};
        if (!editable(event.target || event.srcElement))
            return;
        var token = ++state.focusToken;
        var settle = function () {
            if (token !== state.focusToken)
                return;
            var active = document.activeElement;
            if (editable(active) && content.contains && content.contains(active))
                return;
            postEditorFocus(false);
        };
        if (typeof global.setTimeout === 'function')
            global.setTimeout(settle, 0);
        else
            settle();
    });
    global.onresize = reportLayout;
    sizeLegacyViewport();
    return {
        protocol: PROTOCOL,
        receive: receive,
        takeMessage: takeMessage,
        takeIntent: takeIntent,
        inspect: function () {
            return {
                railEntries: state.rail.entries.length,
                selectedEntry: state.rail.selectedEntry,
                expanded: state.rail.expanded,
                pageGeneration: state.pageGeneration,
                panel: state.panel,
                widgetCount: state.widgets.count,
                queuedMessages: state.messages.length
            };
        },
        constants: { CMD: CMD, W: W, INTENT: INTENT }
    };
});
