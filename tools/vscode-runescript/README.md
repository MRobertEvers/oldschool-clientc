# RuneScript for VS Code

Syntax highlighting and intellisense for `.rs2` (ServerScript), `.cs2`
(ClientScript), and the content tree's declaration files — `.npc`, `.obj`,
`.loc`, `.varp`, `.enum`, `.dbrow`, `.dbtable`, `.param`, `.struct`, `.inv`,
`.if`, `.constant`, `.spawn`, and the `.alloc` / `.pack` / `.compack` ledgers.

Highlighting is driven by a tree-sitter parse resolved against a workspace
index, so `bronze_bar` colours as an obj, `%bankpin_code` as the varp
`pack/varp.alloc` allocated, and a name nothing declares stays plain. The
server behind it is [`tools/runescript-lsp`](../runescript-lsp).

---

## Install

```bash
tools/vscode-runescript/scripts/install.sh          # build + package + install
tools/vscode-runescript/scripts/install.sh --link   # symlink, for developing it
tools/vscode-runescript/scripts/install.sh --uninstall
```

Windows:

```powershell
powershell -ExecutionPolicy Bypass -File tools\vscode-runescript\scripts\install.ps1
powershell -ExecutionPolicy Bypass -File tools\vscode-runescript\scripts\install.ps1 -Link
```

Then reload the window (**Developer: Reload Window**).

To build the `.vsix` without installing it:

```bash
tools/vscode-runescript/scripts/build.sh            # -> dist/toridraw.runescript-<version>.vsix
tools/vscode-runescript/scripts/build.sh --no-server
```

`build.sh` writes the VSIX itself rather than calling `vsce`, so packaging
needs nothing from npm — just `node`, `zip`, and a compiler for the server.
The PowerShell script uses the .NET zip API and needs only PowerShell and
CMake.

---

## Settings

| setting | default | what it does |
| --- | --- | --- |
| `runescript.serverPath` | `""` | Path to `runescript-lsp`. Empty searches `tools/runescript-lsp/`, `build-lsp/`, then `PATH`. |
| `runescript.contentRoots` | `[]` | Extra directories to index, for a content tree kept outside the open folder. |
| `runescript.diagnostics.unknownSymbols` | `true` | Report a `~proc`, `@label`, `^constant`, `%var` or command nothing declares. |
| `runescript.diagnostics.unknownLocals` | `true` | Report a `$local` the script never declares. |
| `runescript.diagnostics.unknownNames` | `false` | Report a bare name that resolves to nothing. Off by default — a tree indexed without its cache packs would light up end to end. |
| `runescript.trace.server` | `"off"` | Log LSP traffic to the RuneScript output channel. |

Commands: **RuneScript: Restart Language Server**, **RuneScript: Show Language
Server Output**.

### When an answer looks stale

The server binary lives inside the workspace it is serving, so `make` replaces
it while VS Code is still holding the old process open — and the answers stay
as they were, which reads as "the fix did not work" rather than "you are
talking to yesterday's build". The extension watches the binary and restarts
itself when it changes. If it ever does not, **RuneScript: Restart Language
Server** does it by hand, and **Show Language Server Output** prints the path
it launched and when that file was built.

---

## What you get

Hover a name for its namespace, id, declaring file and doc comment.
Go-to-definition on `%bankpin_code` offers the `.varp` record, the
`pack/varp.alloc` line that allocated the id, and the `configs/all.varp.compack`
entry the cache knows it by — because which one you wanted depends on what you
were about to change. Find-all-references works across scripts and configs
alike. Completion is sigil-aware; inside a config file it offers the keys that
file type actually uses. Signature help quotes the callee's declared arguments.

---

## How it is put together

The extension is a language client with no npm dependencies. The usual way to
write one is `vscode-languageclient`, which would drag a `node_modules` tree
into the repo or make packaging need the network; the protocol this server
speaks is about three hundred lines of client, so `extension.js` implements it
directly — framed stdio transport, an initialize handshake, document
synchronisation, and one thin forwarder per provider.

There are TextMate grammars in `syntaxes/` as well. They are the fallback that
paints a file in the instant before the server answers, and whenever the server
is missing, so they deliberately colour only what is decidable from the text
alone. Everything that needs to know whether a name *exists* comes from the
server's semantic tokens.

If the server cannot be found you get a message saying so, and the file still
opens with the fallback colouring.
