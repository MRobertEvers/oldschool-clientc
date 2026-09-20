#!/usr/bin/env python3
"""Audit the two shared GPU cores against the GL ceilings they are written to.

`make -C src lane-check PLATFORM=web` runs this. Two questions:

  --es2 FILE...        does the ES2 core stay inside OpenGL ES 2.0 core? It
                       is the code behind BOTH ES 2.0 renderers -- the
                       browser's WebGL1 (platform_web_renderer_webgl1.c) and
                       Android's GLES2 (platform_androidarmv7_renderer_opengles2.c) -- so a
                       GLES3 call in it breaks a 2013 phone as surely as a
                       browser. It used to be the link that answered
                       this -- the web lane pinned -sMAX_WEBGL_VERSION=1, so
                       a GLES3 entry point could not resolve to a working
                       context and a mistake failed on that build. The module
                       now carries a WebGL2 renderer as well, so the runtime
                       must be able to make either context and the pin is
                       gone. The WebGL1 lane still RUNS on a WebGL1 context
                       (platform_gl_context_sdl.c asks for one by name), but
                       nothing at link time would catch a GLES3 call in the
                       core any more. This does.

  --no-extensions FILE...
                       does either core reach for a GL extension? Neither may:
                       the web lane links with
                       GL_SUPPORT_AUTOMATIC_ENABLE_EXTENSIONS=0, so one that
                       did would fail in a browser, and the Android lane
                       targets a driver generation where an extension is a
                       per-device gamble.

Comments are stripped before the search and string literals are not, because
a shader's `#version 300 es` is source the GPU compiles, while a comment
saying "ES2 has no GL_UNPACK_ROW_LENGTH" is documentation of exactly the rule
being checked and must not trip it.
"""

import re
import sys

# Every ES 3.0 entry point and token the WebGL2 renderer uses. A name here is
# a name the ES2 renderer may not use.
ES3_ONLY = [
    "glBindVertexArray",
    "glGenVertexArrays",
    "glDeleteVertexArrays",
    "glVertexAttribIPointer",
    "glVertexAttribDivisor",
    "glDrawRangeElements",
    "glDrawElementsInstanced",
    "glDrawArraysInstanced",
    "glGetUniformBlockIndex",
    "glUniformBlockBinding",
    "glBindBufferBase",
    "glInvalidateFramebuffer",
    "glBlitFramebuffer",
    "glMapBufferRange",
    "glFenceSync",
    "glTexStorage2D",
    "glReadBuffer",
    "glDrawBuffers",
    "GL_UNPACK_ROW_LENGTH",
    "GL_PACK_ROW_LENGTH",
    "GL_TEXTURE_SWIZZLE_R",
    "GL_TEXTURE_SWIZZLE_A",
    "GL_DEPTH_COMPONENT24",
    "GL_RGBA8",
    "GL_R8",
    "GL_RG8",
    "GL_UNSIGNED_INT_2_10_10_10_REV",
    "GL_HALF_FLOAT",
    "version 300 es",
]

EXTENSION_PATTERN = re.compile(
    r"GL_OES_|GL_EXT_|GL_ANGLE_|glGetString\s*\(\s*GL_EXTENSIONS|emscripten_webgl_enable")


def strip_comments(text):
    """Blank out /* */ and // comments, keeping line numbering intact."""
    out = []
    i = 0
    n = len(text)
    while i < n:
        two = text[i:i + 2]
        if two == "/*":
            end = text.find("*/", i + 2)
            end = n if end < 0 else end + 2
            out.append("".join(c if c == "\n" else " " for c in text[i:end]))
            i = end
        elif two == "//":
            end = text.find("\n", i)
            end = n if end < 0 else end
            out.append(" " * (end - i))
            i = end
        elif text[i] == '"':
            # A string literal is source, not commentary: keep it whole.
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            j = min(j + 1, n)
            out.append(text[i:j])
            i = j
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def scan(paths, needles, describe):
    findings = 0
    for path in paths:
        with open(path, "r", encoding="utf-8") as handle:
            lines = strip_comments(handle.read()).splitlines()
        for number, line in enumerate(lines, 1):
            for needle in needles:
                if hasattr(needle, "search"):
                    hit = needle.search(line)
                    text = hit.group(0) if hit else None
                else:
                    text = needle if needle in line else None
                if text:
                    print("%s:%d: %s: %s" % (path, number, describe, text), file=sys.stderr)
                    findings += 1
    return findings


def main(argv):
    if len(argv) < 3 or argv[1] not in ("--es2", "--no-extensions"):
        print(__doc__, file=sys.stderr)
        return 2
    mode, paths = argv[1], argv[2:]
    if mode == "--es2":
        found = scan(paths, ES3_ONLY, "OpenGL ES 3.0 only")
        if found:
            print(
                "webgl_lane_audit: the ES2 core must stay inside OpenGL ES 2.0 "
                "(WEB-GL1-001 in docs/platform_quirks.md)",
                file=sys.stderr)
            return 1
        print("webgl_lane_audit: ES2 core is ES 2.0 only, total 0")
        return 0
    found = scan(paths, [EXTENSION_PATTERN], "GL extension")
    if found:
        print(
            "webgl_lane_audit: neither GPU core may require a GL extension; "
            "the web lane links with GL_SUPPORT_AUTOMATIC_ENABLE_EXTENSIONS=0",
            file=sys.stderr)
        return 1
    print("webgl_lane_audit: no GL extensions in either core, total 0")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
