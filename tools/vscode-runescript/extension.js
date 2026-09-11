/*
 * RuneScript for VS Code — a language client with no dependencies.
 *
 * The usual way to write this is `vscode-languageclient`, which is an npm
 * install away and would drag a node_modules tree into the repo (or make
 * building the extension need the network). The protocol this server speaks is
 * about three hundred lines of client, so it is written out here instead: a
 * framed stdio transport, an initialize handshake, document synchronisation,
 * and one thin forwarder per provider.
 *
 * Highlighting comes from the server's semantic tokens, which are computed
 * from a tree-sitter parse and resolved against the workspace index — that is
 * what lets `bronze_bar` colour as an obj and `bronze_bra` not colour at all.
 * The TextMate grammars in syntaxes/ are the fallback that paints the file in
 * the instant before the server answers, and whenever it cannot.
 */

'use strict';

const cp = require('child_process');
const fs = require('fs');
const path = require('path');
const vscode = require('vscode');

const SELECTOR = [{ language: 'runescript' }, { language: 'runeconfig' }];

let client = null;
let output = null;
let diagnostics = null;
let disposables = [];
let watchedBinary = null;
let restartTimer = null;
let restarting = false;

/* ------------------------------------------------------------------ */
/* Transport                                                           */
/* ------------------------------------------------------------------ */

class Client {
  constructor(binary, args, cwd) {
    this.process = cp.spawn(binary, args, { cwd, stdio: ['pipe', 'pipe', 'pipe'] });
    this.nextId = 1;
    this.pending = new Map();
    this.handlers = new Map();
    this.buffer = Buffer.alloc(0);
    this.capabilities = {};
    this.trace = false;

    this.process.stdout.on('data', (chunk) => this._onData(chunk));
    this.process.stderr.on('data', (chunk) => output.append(chunk.toString()));
    this.process.on('exit', (code, signal) => {
      output.appendLine(`server exited (code ${code}, signal ${signal})`);
      for (const { reject } of this.pending.values()) {
        reject(new Error('language server exited'));
      }
      this.pending.clear();
    });
    this.process.on('error', (error) => {
      vscode.window.showErrorMessage(`RuneScript: cannot start the language server — ${error.message}`);
    });
  }

  _onData(chunk) {
    this.buffer = Buffer.concat([this.buffer, chunk]);
    for (;;) {
      const separator = this.buffer.indexOf('\r\n\r\n');
      if (separator < 0) return;

      const header = this.buffer.slice(0, separator).toString('ascii');
      const match = /Content-Length:\s*(\d+)/i.exec(header);
      if (!match) {
        // Unparseable header: drop it rather than stall on it forever.
        this.buffer = this.buffer.slice(separator + 4);
        continue;
      }

      const length = parseInt(match[1], 10);
      const start = separator + 4;
      if (this.buffer.length < start + length) return;

      const body = this.buffer.slice(start, start + length).toString('utf8');
      this.buffer = this.buffer.slice(start + length);

      let message;
      try {
        message = JSON.parse(body);
      } catch (error) {
        output.appendLine(`dropped an unparseable message: ${error.message}`);
        continue;
      }
      this._dispatch(message);
    }
  }

  _dispatch(message) {
    if (this.trace) output.appendLine(`<- ${JSON.stringify(message).slice(0, 800)}`);

    if (message.id !== undefined && message.method === undefined) {
      const entry = this.pending.get(message.id);
      if (!entry) return;
      this.pending.delete(message.id);
      if (message.error) entry.reject(new Error(message.error.message || 'request failed'));
      else entry.resolve(message.result);
      return;
    }

    if (message.method) {
      const handler = this.handlers.get(message.method);
      if (handler) handler(message.params);
      // A server-to-client request we do not implement still needs an answer.
      else if (message.id !== undefined) this._send({ jsonrpc: '2.0', id: message.id, result: null });
    }
  }

  _send(message) {
    if (!this.process.stdin.writable) return;
    const body = Buffer.from(JSON.stringify(message), 'utf8');
    if (this.trace) output.appendLine(`-> ${body.toString('utf8').slice(0, 800)}`);
    this.process.stdin.write(`Content-Length: ${body.length}\r\n\r\n`);
    this.process.stdin.write(body);
  }

  request(method, params) {
    const id = this.nextId++;
    return new Promise((resolve, reject) => {
      this.pending.set(id, { resolve, reject });
      this._send({ jsonrpc: '2.0', id, method, params });
    });
  }

  notify(method, params) {
    this._send({ jsonrpc: '2.0', method, params });
  }

  on(method, handler) {
    this.handlers.set(method, handler);
  }

  stop() {
    try {
      this.notify('exit', {});
      this.process.stdin.end();
    } catch (error) {
      /* the process is already gone */
    }
    setTimeout(() => {
      if (!this.process.killed) this.process.kill();
    }, 1000);
  }
}

/* ------------------------------------------------------------------ */
/* LSP <-> VS Code conversions                                          */
/* ------------------------------------------------------------------ */

const toRange = (range) =>
  new vscode.Range(
    range.start.line,
    range.start.character,
    range.end.line,
    range.end.character,
  );

const toPosition = (position) => ({ line: position.line, character: position.character });

const textDocumentId = (document) => ({ uri: document.uri.toString() });

const positionParams = (document, position) => ({
  textDocument: textDocumentId(document),
  position: toPosition(position),
});

function toLocation(entry) {
  return new vscode.Location(vscode.Uri.parse(entry.uri), toRange(entry.range));
}

/* ------------------------------------------------------------------ */
/* Finding the server                                                  */
/* ------------------------------------------------------------------ */

function serverCandidates(workspaceRoot) {
  const exe = process.platform === 'win32' ? '.exe' : '';
  const names = [
    ['tools', 'runescript-lsp', `runescript-lsp${exe}`],
    ['build-lsp', `runescript-lsp${exe}`],
    ['build-lsp', 'Release', `runescript-lsp${exe}`],
    ['build-lsp', 'Debug', `runescript-lsp${exe}`],
  ];
  const candidates = [];

  if (workspaceRoot) {
    for (const parts of names) candidates.push(path.join(workspaceRoot, ...parts));
  }
  // The extension is usually installed beside the tree it was built from.
  candidates.push(path.join(__dirname, '..', 'runescript-lsp', `runescript-lsp${exe}`));
  return candidates;
}

function resolveServerPath(workspaceRoot) {
  const configured = vscode.workspace.getConfiguration('runescript').get('serverPath', '');
  if (configured) {
    const resolved = path.isAbsolute(configured) || !workspaceRoot
      ? configured
      : path.join(workspaceRoot, configured);
    if (fs.existsSync(resolved)) return resolved;
    vscode.window.showWarningMessage(
      `RuneScript: runescript.serverPath points at ${resolved}, which does not exist.`,
    );
  }

  for (const candidate of serverCandidates(workspaceRoot)) {
    if (fs.existsSync(candidate)) return candidate;
  }
  // Nothing built in the tree: fall back to the name and let PATH decide.
  return `runescript-lsp${process.platform === 'win32' ? '.exe' : ''}`;
}

/* ------------------------------------------------------------------ */
/* Providers                                                           */
/* ------------------------------------------------------------------ */

function registerProviders(context, legend) {
  const register = (disposable) => {
    disposables.push(disposable);
    context.subscriptions.push(disposable);
  };

  register(
    vscode.languages.registerHoverProvider(SELECTOR, {
      async provideHover(document, position) {
        const result = await client.request('textDocument/hover', positionParams(document, position));
        if (!result || !result.contents) return null;
        const markdown = new vscode.MarkdownString(result.contents.value || '');
        markdown.isTrusted = false;
        return new vscode.Hover(markdown, result.range ? toRange(result.range) : undefined);
      },
    }),
  );

  register(
    vscode.languages.registerDefinitionProvider(SELECTOR, {
      async provideDefinition(document, position) {
        const result = await client.request(
          'textDocument/definition',
          positionParams(document, position),
        );
        if (!result) return null;
        return (Array.isArray(result) ? result : [result]).map(toLocation);
      },
    }),
  );

  register(
    vscode.languages.registerReferenceProvider(SELECTOR, {
      async provideReferences(document, position, context_) {
        const result = await client.request('textDocument/references', {
          ...positionParams(document, position),
          context: { includeDeclaration: context_.includeDeclaration },
        });
        if (!result) return null;
        return result.map(toLocation);
      },
    }),
  );

  register(
    vscode.languages.registerDocumentSymbolProvider(SELECTOR, {
      async provideDocumentSymbols(document) {
        const result = await client.request('textDocument/documentSymbol', {
          textDocument: textDocumentId(document),
        });
        if (!result) return null;
        return result.map((entry) => {
          const symbol = new vscode.DocumentSymbol(
            entry.name,
            entry.detail || '',
            (entry.kind || 12) - 1,
            toRange(entry.range),
            toRange(entry.selectionRange || entry.range),
          );
          return symbol;
        });
      },
    }),
  );

  register(
    vscode.languages.registerWorkspaceSymbolProvider({
      async provideWorkspaceSymbols(query) {
        const result = await client.request('workspace/symbol', { query });
        if (!result) return null;
        return result.map(
          (entry) =>
            new vscode.SymbolInformation(
              entry.name,
              (entry.kind || 12) - 1,
              entry.containerName || '',
              toLocation(entry.location),
            ),
        );
      },
    }),
  );

  register(
    vscode.languages.registerCompletionItemProvider(
      SELECTOR,
      {
        async provideCompletionItems(document, position) {
          const result = await client.request(
            'textDocument/completion',
            positionParams(document, position),
          );
          const items = (result && result.items) || [];
          return items.map((entry) => {
            const item = new vscode.CompletionItem(entry.label, (entry.kind || 1) - 1);
            if (entry.detail) item.detail = entry.detail;
            if (entry.documentation) {
              item.documentation = new vscode.MarkdownString(entry.documentation.value || '');
            }
            return item;
          });
        },
      },
      '~', '@', '^', '%', '$', ',', '(', '=',
    ),
  );

  register(
    vscode.languages.registerSignatureHelpProvider(
      SELECTOR,
      {
        async provideSignatureHelp(document, position) {
          const result = await client.request(
            'textDocument/signatureHelp',
            positionParams(document, position),
          );
          if (!result || !result.signatures || !result.signatures.length) return null;

          const help = new vscode.SignatureHelp();
          help.signatures = result.signatures.map((entry) => {
            const signature = new vscode.SignatureInformation(entry.label);
            if (entry.documentation) {
              signature.documentation = new vscode.MarkdownString(entry.documentation.value || '');
            }
            // The server does not split the label into parameters, so the
            // active-parameter highlight is left off rather than pointed at
            // the wrong span.
            signature.parameters = [];
            return signature;
          });
          help.activeSignature = result.activeSignature || 0;
          help.activeParameter = result.activeParameter || 0;
          return help;
        },
      },
      '(', ',',
    ),
  );

  register(
    vscode.languages.registerDocumentHighlightProvider(SELECTOR, {
      async provideDocumentHighlights(document, position) {
        const result = await client.request(
          'textDocument/documentHighlight',
          positionParams(document, position),
        );
        if (!result) return null;
        return result.map((entry) => new vscode.DocumentHighlight(toRange(entry.range)));
      },
    }),
  );

  register(
    vscode.languages.registerFoldingRangeProvider(SELECTOR, {
      async provideFoldingRanges(document) {
        const result = await client.request('textDocument/foldingRange', {
          textDocument: textDocumentId(document),
        });
        if (!result) return null;
        return result.map((entry) => new vscode.FoldingRange(entry.startLine, entry.endLine));
      },
    }),
  );

  if (legend) {
    register(
      vscode.languages.registerDocumentSemanticTokensProvider(
        SELECTOR,
        {
          async provideDocumentSemanticTokens(document) {
            const result = await client.request('textDocument/semanticTokens/full', {
              textDocument: textDocumentId(document),
            });
            if (!result || !result.data) return null;
            // The server emits the protocol's own encoding, which is the same
            // five-integer encoding this constructor takes.
            return new vscode.SemanticTokens(Uint32Array.from(result.data));
          },
        },
        legend,
      ),
    );
  }
}

/* ------------------------------------------------------------------ */
/* Document synchronisation                                            */
/* ------------------------------------------------------------------ */

function isOurs(document) {
  return document.languageId === 'runescript' || document.languageId === 'runeconfig';
}

function syncOpen(document) {
  if (!isOurs(document)) return;
  client.notify('textDocument/didOpen', {
    textDocument: {
      uri: document.uri.toString(),
      languageId: document.languageId,
      version: document.version,
      text: document.getText(),
    },
  });
}

function registerSync(context) {
  const register = (disposable) => {
    disposables.push(disposable);
    context.subscriptions.push(disposable);
  };

  for (const document of vscode.workspace.textDocuments) syncOpen(document);

  register(vscode.workspace.onDidOpenTextDocument(syncOpen));

  register(
    vscode.workspace.onDidChangeTextDocument((event) => {
      if (!isOurs(event.document)) return;
      // Full sync: the server advertises TextDocumentSyncKind.Full, so one
      // change carrying the whole text is what it expects.
      client.notify('textDocument/didChange', {
        textDocument: {
          uri: event.document.uri.toString(),
          version: event.document.version,
        },
        contentChanges: [{ text: event.document.getText() }],
      });
    }),
  );

  register(
    vscode.workspace.onDidSaveTextDocument((document) => {
      if (!isOurs(document)) return;
      client.notify('textDocument/didSave', {
        textDocument: textDocumentId(document),
        text: document.getText(),
      });
    }),
  );

  register(
    vscode.workspace.onDidCloseTextDocument((document) => {
      if (!isOurs(document)) return;
      client.notify('textDocument/didClose', { textDocument: textDocumentId(document) });
    }),
  );

  register(
    vscode.workspace.onDidChangeConfiguration((event) => {
      if (!event.affectsConfiguration('runescript')) return;
      client.notify('workspace/didChangeConfiguration', {
        settings: { runescript: currentSettings() },
      });
    }),
  );
}

function currentSettings() {
  const configuration = vscode.workspace.getConfiguration('runescript');
  return {
    diagnostics: {
      unknownSymbols: configuration.get('diagnostics.unknownSymbols', true),
      unknownLocals: configuration.get('diagnostics.unknownLocals', true),
      unknownNames: configuration.get('diagnostics.unknownNames', false),
    },
  };
}

/* ------------------------------------------------------------------ */
/* Picking up a rebuilt server                                         */
/* ------------------------------------------------------------------ */

/*
 * The server binary usually lives inside the workspace it is serving —
 * `tools/runescript-lsp/runescript-lsp` — so `make` replaces it while VS Code
 * is holding the old process open. Nothing about that is visible from the
 * editor: the answers just stay as they were, which reads as "the fix did not
 * work" rather than "you are talking to yesterday's build".
 *
 * So the binary is watched, and a new one restarts the client. Polling one
 * file every two seconds costs nothing, and unlike a workspace FileSystemWatcher
 * it still fires for a path the user has excluded from watching or gitignored,
 * which a build output usually is.
 */
function watchBinary(context, binary) {
  unwatchBinary();
  watchedBinary = binary;

  fs.watchFile(binary, { interval: 2000 }, (current, previous) => {
    if (current.mtimeMs === previous.mtimeMs && current.size === previous.size) return;
    if (restarting) return;

    // A link step writes the file in pieces; restarting mid-write launches a
    // truncated binary. Wait for the writes to stop before acting.
    if (restartTimer) clearTimeout(restartTimer);
    restartTimer = setTimeout(async () => {
      restartTimer = null;
      restarting = true;
      output.appendLine('server binary changed on disk — restarting');
      try {
        stop();
        await start(context);
      } finally {
        restarting = false;
      }
    }, 750);
  });

  context.subscriptions.push({ dispose: unwatchBinary });
}

function unwatchBinary() {
  if (restartTimer) {
    clearTimeout(restartTimer);
    restartTimer = null;
  }
  if (watchedBinary) {
    fs.unwatchFile(watchedBinary);
    watchedBinary = null;
  }
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

async function start(context) {
  const folders = vscode.workspace.workspaceFolders || [];
  const workspaceRoot = folders.length ? folders[0].uri.fsPath : undefined;
  const binary = resolveServerPath(workspaceRoot);

  // The exact path and its build time, so "am I running the build I just
  // made?" is answerable from the output channel rather than by guessing.
  let stamp = '';
  try {
    stamp = ` (built ${fs.statSync(binary).mtime.toISOString()})`;
  } catch (error) {
    stamp = ' (not on disk — will fall back to PATH)';
  }
  output.appendLine(`starting ${binary}${stamp}`);

  client = new Client(binary, [], workspaceRoot);
  client.trace =
    vscode.workspace.getConfiguration('runescript').get('trace.server', 'off') !== 'off';

  client.on('textDocument/publishDiagnostics', (params) => {
    const uri = vscode.Uri.parse(params.uri);
    diagnostics.set(
      uri,
      (params.diagnostics || []).map((entry) => {
        const diagnostic = new vscode.Diagnostic(
          toRange(entry.range),
          entry.message,
          (entry.severity || 1) - 1,
        );
        diagnostic.source = entry.source || 'runescript';
        return diagnostic;
      }),
    );
  });

  client.on('window/logMessage', (params) => output.appendLine(params.message));
  client.on('window/showMessage', (params) => {
    if (params.type === 1) vscode.window.showErrorMessage(params.message);
    else if (params.type === 2) vscode.window.showWarningMessage(params.message);
  });

  const contentRoots = vscode.workspace
    .getConfiguration('runescript')
    .get('contentRoots', [])
    .map((root) => (path.isAbsolute(root) || !workspaceRoot ? root : path.join(workspaceRoot, root)));

  let result;
  try {
    result = await client.request('initialize', {
      processId: process.pid,
      clientInfo: { name: 'vscode-runescript', version: '0.1.0' },
      rootUri: workspaceRoot ? vscode.Uri.file(workspaceRoot).toString() : null,
      workspaceFolders: folders.map((folder) => ({
        uri: folder.uri.toString(),
        name: folder.name,
      })),
      initializationOptions: { ...currentSettings(), contentRoots },
      capabilities: {
        textDocument: {
          synchronization: { didSave: true },
          semanticTokens: { formats: ['relative'] },
        },
      },
    });
  } catch (error) {
    vscode.window.showErrorMessage(
      `RuneScript: the language server did not start (${error.message}). ` +
        'Build it with `make -C tools/runescript-lsp`, or set runescript.serverPath.',
    );
    return;
  }

  client.capabilities = (result && result.capabilities) || {};
  client.notify('initialized', {});

  let legend = null;
  const provider = client.capabilities.semanticTokensProvider;
  if (provider && provider.legend) {
    legend = new vscode.SemanticTokensLegend(
      provider.legend.tokenTypes,
      provider.legend.tokenModifiers,
    );
  }

  registerProviders(context, legend);
  registerSync(context);
  watchBinary(context, binary);
  output.appendLine('language server ready');
}

function stop() {
  unwatchBinary();
  for (const disposable of disposables) disposable.dispose();
  disposables = [];
  if (diagnostics) diagnostics.clear();
  if (client) {
    client.stop();
    client = null;
  }
}

function activate(context) {
  output = vscode.window.createOutputChannel('RuneScript');
  diagnostics = vscode.languages.createDiagnosticCollection('runescript');
  context.subscriptions.push(output, diagnostics);

  context.subscriptions.push(
    vscode.commands.registerCommand('runescript.restartServer', async () => {
      stop();
      await start(context);
      vscode.window.showInformationMessage('RuneScript: language server restarted.');
    }),
  );
  context.subscriptions.push(
    vscode.commands.registerCommand('runescript.showOutput', () => output.show()),
  );

  start(context);
}

function deactivate() {
  stop();
}

module.exports = { activate, deactivate };
