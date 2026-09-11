(function (root, factory) {
    var codec = factory();
    if (typeof module !== "undefined" && module.exports) {
        module.exports = codec;
    }
    if (root) {
        root.ToriRSPluginChromeCodec = codec;
    }
}(typeof window !== "undefined" ? window : this, function () {
    "use strict";

    function parse(source) {
        var input = String(source);
        var at = 0;

        function fail() { throw new Error("invalid plugin chrome message"); }
        function white() {
            while (at < input.length && /[\x20\t\r\n]/.test(input.charAt(at))) {
                at += 1;
            }
        }
        function stringValue() {
            var out = "";
            var ch;
            var esc;
            var hex;
            if (input.charAt(at) !== "\"") { fail(); }
            at += 1;
            while (at < input.length) {
                ch = input.charAt(at++);
                if (ch === "\"") { return out; }
                if (ch !== "\\") {
                    if (ch < " ") { fail(); }
                    out += ch;
                    continue;
                }
                if (at >= input.length) { fail(); }
                esc = input.charAt(at++);
                if (esc === "\"" || esc === "\\" || esc === "/") { out += esc; }
                else if (esc === "b") { out += "\b"; }
                else if (esc === "f") { out += "\f"; }
                else if (esc === "n") { out += "\n"; }
                else if (esc === "r") { out += "\r"; }
                else if (esc === "t") { out += "\t"; }
                else if (esc === "u") {
                    hex = input.substr(at, 4);
                    if (!/^[0-9a-fA-F]{4}$/.test(hex)) { fail(); }
                    out += String.fromCharCode(parseInt(hex, 16));
                    at += 4;
                } else { fail(); }
            }
            fail();
            return out;
        }
        function numberValue() {
            var start = at;
            var answer;
            if (input.charAt(at) === "-") { at += 1; }
            if (input.charAt(at) === "0") { at += 1; }
            else {
                if (!/[1-9]/.test(input.charAt(at))) { fail(); }
                while (/[0-9]/.test(input.charAt(at))) { at += 1; }
            }
            if (input.charAt(at) === ".") {
                at += 1;
                if (!/[0-9]/.test(input.charAt(at))) { fail(); }
                while (/[0-9]/.test(input.charAt(at))) { at += 1; }
            }
            if (input.charAt(at) === "e" || input.charAt(at) === "E") {
                at += 1;
                if (input.charAt(at) === "+" || input.charAt(at) === "-") { at += 1; }
                if (!/[0-9]/.test(input.charAt(at))) { fail(); }
                while (/[0-9]/.test(input.charAt(at))) { at += 1; }
            }
            answer = Number(input.substring(start, at));
            if (!isFinite(answer)) { fail(); }
            return answer;
        }
        function value() {
            var answer;
            var key;
            white();
            if (input.charAt(at) === "\"") { return stringValue(); }
            if (input.charAt(at) === "[") {
                answer = [];
                at += 1;
                white();
                if (input.charAt(at) === "]") { at += 1; return answer; }
                while (at < input.length) {
                    answer[answer.length] = value();
                    white();
                    if (input.charAt(at) === "]") { at += 1; return answer; }
                    if (input.charAt(at++) !== ",") { fail(); }
                }
                fail();
            }
            if (input.charAt(at) === "{") {
                answer = {};
                at += 1;
                white();
                if (input.charAt(at) === "}") { at += 1; return answer; }
                while (at < input.length) {
                    white();
                    key = stringValue();
                    white();
                    if (input.charAt(at++) !== ":") { fail(); }
                    answer[key] = value();
                    white();
                    if (input.charAt(at) === "}") { at += 1; return answer; }
                    if (input.charAt(at++) !== ",") { fail(); }
                }
                fail();
            }
            if (input.substr(at, 4) === "true") { at += 4; return true; }
            if (input.substr(at, 5) === "false") { at += 5; return false; }
            if (input.substr(at, 4) === "null") { at += 4; return null; }
            return numberValue();
        }

        white();
        var result = value();
        white();
        if (at !== input.length) { fail(); }
        return result;
    }

    function quote(value) {
        var input = String(value);
        var out = "\"";
        var ch;
        var code;
        var hex;
        var i;
        for (i = 0; i < input.length; i += 1) {
            ch = input.charAt(i);
            code = input.charCodeAt(i);
            if (ch === "\"" || ch === "\\") { out += "\\" + ch; }
            else if (ch === "\b") { out += "\\b"; }
            else if (ch === "\f") { out += "\\f"; }
            else if (ch === "\n") { out += "\\n"; }
            else if (ch === "\r") { out += "\\r"; }
            else if (ch === "\t") { out += "\\t"; }
            else if (code < 32) {
                hex = code.toString(16);
                out += "\\u" + "0000".substr(hex.length) + hex;
            } else { out += ch; }
        }
        return out + "\"";
    }

    function stringify(value) {
        var out;
        var first;
        var key;
        var i;
        if (value === null) { return "null"; }
        if (typeof value === "string") { return quote(value); }
        if (typeof value === "number") { return isFinite(value) ? String(value) : "null"; }
        if (typeof value === "boolean") { return value ? "true" : "false"; }
        if (Object.prototype.toString.call(value) === "[object Array]") {
            out = "[";
            for (i = 0; i < value.length; i += 1) {
                if (i) { out += ","; }
                out += stringify(value[i]);
            }
            return out + "]";
        }
        if (typeof value === "object") {
            out = "{";
            first = true;
            for (key in value) {
                if (Object.prototype.hasOwnProperty.call(value, key) &&
                        typeof value[key] !== "undefined" && typeof value[key] !== "function") {
                    if (!first) { out += ","; }
                    first = false;
                    out += quote(key) + ":" + stringify(value[key]);
                }
            }
            return out + "}";
        }
        return "null";
    }

    return { parse: parse, stringify: stringify };
}));
