# The servers a browser run needs

> Build them together: `make -C src servers`. Both web targets (`web`,
> `web-debug`) depend on it, so a build that produces the module also produces
> the processes that feed it.

A desktop client opens a cache directory and a socket. A browser tab can do
neither. The cache moves into the browser -- one record per archive in
IndexedDB, filled on demand by a *producer* that speaks the cache's own
protocol -- and everything the tab cannot reach directly is answered by a
native process.

```
              ┌────────────────── the browser tab ──────────────────┐
              │ index.html ── torirs_host.js ── torirs.wasm         │
              │       torirs_hostio.js  (files, boot, one archive)  │
              │       torirs_idb.js     (IndexedDB: groups/files)   │
              │       torirs_js5.js     torirs_ondemand.js          │
              └──────┬───────────────────┬────────────────┬─────────┘
   GET /             │        JS5 over   │     emscripten │  game
   GET /boot/<path>  │        WebSocket  │     socket     │  (WebSocket)
   GET /cache/dat1/… │                   │                │
                     ▼                   ▼                ▼
             ┌──────────────┐    ┌───────────────┐  ┌────────────────┐
             │  io_server   │    │ JS5 source:   │  │ game server    │
             │  HTTP        │    │ torirsserver  │  │ torirsserver / │
             │              │    │ (game port)   │  │ LostCity       │
             │              │    │ or js5_server │  │                │
             └──────┬───────┘    └───────┬───────┘  └────────────────┘
                    └── the cache on disk ┘
```

Who answers what, by kind of read:

| the client asks for | where it comes from |
| --- | --- |
| a dat2 group or reference table (osrs239, osrs230) | IndexedDB, else **JS5** over a WebSocket -- `torirsserver` serves JS5 on its game port (first byte picks), or a standalone `js5_server` |
| a dat1 container or map square (LostCity rs254 / rs289) | IndexedDB, else **io_server's dat1 proxy**, `GET /cache/dat1/…`: a page cannot speak the 2004 on-demand protocol (raw TCP) and the jag archives have no CORS |
| the manifest, RevConfig INIs, plugin scripts | IndexedDB with a validator, revalidated against **io_server** `GET /boot/<path>` |
| the player's own files | IndexedDB only |

The page and the module come from `io_server` too. So a run always has
`io_server` plus a game server, and for a dat2 world the game server is also
the JS5 source. There is one web lane; the "wire" lane, in which the browser
held no cache and every read was a `POST /io` to io_server, was removed (see
the note in `src/platform/platform.mk`). io_server still answers `POST /io`
and `make -C src test-io-wire` still checks the codec, but no client build
sends to it.

---

## io_server

```sh
./src/build/io_server --manifest manifests/manifest_osrs239.ini    # http://localhost:8088/
./src/build/io_server --root build-web --boot-root . --port 8099
```

| route | method | what it does |
| --- | --- | --- |
| `/` and everything else | GET | static files under `--root` (default `build-web`) |
| `/boot/<path>` | GET | a file the client opens by name, under `--boot-root`, with a validator |
| `/cache/dat1/<table>/<archive>` | GET | one raw dat1 container, proxied off the LostCity server the manifest names |
| `/cache/dat1/batch` | POST | several of those, pipelined on one connection |
| `/stats` | GET | one line: which caches are open, what was served |
| `/status` | GET | the same, as a page |
| `/io` | POST | an `IOWire` batch; kept for `test-io-wire`, unused by the client |

`--boot-root` is separate from `--root` on purpose: one is build output, the
other is the source tree the manifests live in, and a server that conflated
them would serve either the wrong file or the whole repository.

### Caches and worlds are opened on demand

`--manifest` is only a preopen. A dat1 proxy request names the manifest it is
booting -- the same path the page fetched through `/boot/` moments earlier --
and the server opens that world's on-demand connection on first use and keeps
it. One process serves every world at once, and changing the manifest in the
page's URL needs no restart. Cache directories arrive from another process, so
they are treated as input: resolved under `--boot-root`, and rejected if
absolute or escaping it.

### Staleness: conditional GETs

Boot files are the client's *configuration* — the manifest and the RevConfig
INIs it names — and they are edited by hand between runs. A page that trusted
its stored copy would boot yesterday's configuration; a page that re-downloaded
every file every time would work but would make an offline start impossible.
The answer is the one HTTP already has.

Every file response carries a validator:

```
ETag: "1786554865-5590"          mtime and size
Cache-Control: no-cache          store it, but ask every time
Access-Control-Expose-Headers: ETag
```

and a request that presents `If-None-Match` with a matching tag gets `304 Not
Modified` with no body.

Three details that are load-bearing:

- **`no-cache`, not `no-store`.** `no-store` forbids the browser from keeping
  the copy it would revalidate, which defeats the whole mechanism. Responses
  without a validator — a proxied dat1 container, `/stats` — still say
  `no-store`, because those genuinely may not be reused.
- **`Access-Control-Expose-Headers`.** A cross-origin response's `ETag` is
  hidden from script unless it is exposed, and a validator nobody can read is
  the same as no validator.
- **mtime + size is not a content hash** and does not claim to be. A file
  rewritten within the same second at exactly the same length is missed. For a
  manifest someone is editing, that is a rounding error against re-reading it
  on every boot.

The host's side of this is in [WEB_CACHE_INDEXEDDB.md](WEB_CACHE_INDEXEDDB.md).

---

## JS5: torirsserver's game port, or js5_server

```sh
./src/build_opt/torirsserver 43594 --rev osrs239          # game + JS5 on one socket
./src/build_opt/js5_server --cache cache.osrs239 --revision 239 --port 43594
```

The osrs239 launch profile has no separate JS5 process: `torirsserver` serves
JS5 on its game port, and the page's default JS5 port is that game port.
`js5_server` is the standalone form of the same service -- a read-only
revision-239 cache service. The protocol is documented in full in
[JS5_SERVER.md](JS5_SERVER.md); what matters here is how it fits the browser.

### One port, two framings

The first byte decides, and nothing else does: an HTTP upgrade opens with `'G'`,
a JS5 stream with opcode 15. So a desktop client and a browser reach the same
port, and the session state machine never learns which transport it is on.

The WebSocket branch is not a convenience. Emscripten implements BSD sockets as
WebSockets, so the browser build's `connect()` *is* an upgrade request — a
server that speaks only raw TCP is unreachable from a page, with no bridge in
front of it.

The framing lives in `js5/server/js5_server_conn.[ch]`, which has no socket in
it. That is what lets the same connection type be hosted by a different loop
later without duplicating the handshake.

### Staleness: the cache can change underneath it

Everything the server answers with is a snapshot taken when the cache was
opened — the master index, the CRC and version each archive is validated
against, and the decoded reference tables. Repack the cache while the server
runs and that snapshot describes a file that is no longer there: it would hand
out a master its own dat2 disagrees with, and every group read would fail its
CRC with nothing saying why.

So the reactor re-stamps the cache once a second (two `stat` calls on the dat2
and idx255 — every archive write touches one, every reference-table write the
other) and reloads when they move:

```
js5_server: cache.osrs239 changed on disk — reloaded, master=205 bytes
```

Three properties worth knowing:

- **A failed reload is not fatal.** A repack in progress genuinely looks
  corrupt — the writer is mid-file — so the rebuild goes into a scratch store
  and is swapped in only on success. On failure the previous contents keep
  being served and the next check tries again. Verified by truncating a dat2
  under a running server: it says so once, keeps answering, and reloads cleanly
  when the file is restored.
- **The swap is in place.** A session holds a pointer to the store for its
  lifetime, so the contents are replaced inside the object rather than the
  object being replaced. Blobs already handed to a session are owned copies.
- **Sessions mid-download are not told.** JS5 has no message for "the cache
  moved". A client holding the previous master fails a CRC on its next group,
  drops the connection and re-primes — which is exactly the recovery path a
  corrupt local copy already takes.

### What it does not do

No authentication, no origin check, no encryption, and it exposes every group
in the cache. Accepting WebSockets means a browser can reach it directly, which
makes the loopback default matter more rather than less: any page the browser
loads can open a socket to it. `--bind 0.0.0.0` only where the surrounding host
controls who can reach it.

---

## Running it

```sh
./launch run osrs239-web        # torirsserver + io_server, then the page URL
```

By hand, the same two processes:

```sh
make -C src web                                  # module + both servers
TORIRSSERVER_CACHE=cache.osrs239 ./src/build_opt/torirsserver 43594 --rev osrs239 &
./src/build/io_server --manifest manifests/manifest_osrs239.ini --root build-web --boot-root .
```

```
http://localhost:8088/?args=--manifest,manifests/manifest_osrs239.ini
```

`run-live.sh web <manifest> …` drives the same pair from a script, starting
`io_server` as its own child so a stopped script does not leave a process
holding the port, and a native `ToriRSServer` for a local live world.

## Ports and where the page dials

| | default | changed with |
| --- | --- | --- |
| `io_server` HTTP | 8088 | `--port`; the page derives `/boot`, `/stats`, `/cache/dat1` from its own URL, `?io=` overrides |
| JS5 | `ws://<page host>:43594` | `?js5_host=`, `?js5_port=`, or `?js5_url=` |
| game socket | the manifest's `ws_host:ws_port` | `?ws=` |

Two WebSockets leave the page, JS5 and the game, and both default to a plain
`ws://host:port`. That is right on a LAN and wrong behind anything that
terminates TLS: an `https:` page may not open `ws:` (mixed content, refused
before connecting), and a reverse proxy reaches a server by path, not port.
`?ws=<url-or-path>` names the game socket; JS5 then dials the same URL, since
the server picks JS5 or game off the first byte. `?js5_url=` names JS5
separately if the two ever split.

---

## Deploying it: one zip

```sh
make -C src deploy-osrs239          # -> build/deploy/torirs-osrs239-<date>-<sha>.zip
python tools/deploy/build_osrs239_package.py --skip-build   # restage what is built
```

`tools/deploy/build_osrs239_package.py` builds the script pack (if stale), the
web client and both servers, then stages `bin/`, `build-web/`,
`cache.osrs239/`, the slice of `OSRS-Content/osrs-content` torirsserver
opens (not the 11 GB selftest corpus, and only the `.jm2` maps), the manifest
rewritten for that layout, `revconfig/`, `script/` and start scripts, and zips
it. The servers are native binaries, so run it on the OS that will host the
package. The zip's README covers running it; `install-windows-task.ps1`
registers a startup task and the firewall rules on Windows.

Behind a TLS-terminating reverse proxy: `ws=ws` on `https://host/torirs/`
sets the game socket to `wss://host/torirs/ws`, and JS5 follows it; the proxy
pipes that path's upgrade raw to the game port, where the server terminates
the WebSocket itself. Every HTTP default (`/boot`, `/stats`) is relative to the
page's directory, so a prefix mount needs no other configuration. This was
learned the hard way: the first public deployment routed the game socket and
not JS5, and every cache read came back empty.


`--world osrs241` packages the current revision instead (its cache, manifest
and revconfig); the default is still `osrs239`.

---

## The sign-in gate

A world on the internet runs both servers with `--gate`, and then **nothing on
either answers without the gate's cookie**:

```sh
TORIRSSERVER_SAVES=saves torirsserver 43594 --rev osrs241 --gate
TORIRSSERVER_SAVES=saves io_server --manifest manifests/manifest_osrs241.ini \
    --root build-web --boot-root . --port 8088 \
    --gate '/?args=--manifest,manifests/manifest_osrs241.ini&ws=ws'
```

| server | without a good cookie |
| --- | --- |
| io_server, any route | GET: `303 /login`; anything else: `401` |
| torirsserver, WebSocket upgrade (game and JS5) | `403 Forbidden`, closed |
| torirsserver, raw TCP | closed at the first byte (no cookie can ride on it) |

io_server serves the gate's own pages, plain HTML with nothing else loaded:
`/login`, `/invite/<code>` and `/logout` (`src/ioserver/io_gate.c`). Signing
in sets `torirs_gate=<name>.<expiry>.<hmac>` (30 days, `HttpOnly`,
`SameSite=Lax`, `Secure` when `X-Forwarded-Proto: https`) and lands on the
URL given to `--gate`. torirsserver reads the same cookie off the upgrade
request; the two processes share only the key, `<saves>/gate/gate.key`, which
whichever starts first writes.

**Two accounts, unrelated.** The gate's login is a *web login*, kept by the
account server (below) with the invites, and made only from an invite or by
the operator:

```sh
TORIRSSERVER_SAVES=saves TORIRSSERVER_GATE_URL=https://torirs.example.com \
    torirsserver --account invite [name]      # prints the link; one use
torirsserver --account revoke <login>         # it cannot sign in again
```

The database's first open imports what the files held before it --
`<saves>/gate/<name>.ini`, `gate/invites/<code>.ini`, every save's stats and
each `history/<name>.log` -- and nothing reads those files again.

Past the page, the game asks for a *player account* (`<saves>/accounts/`) the
usual way: the first login claims a name. Both are held to the same password
rules -- 8 to 20 characters, a letter and a digit, no spaces, not one character
repeated, not containing the name (`ToriRSServer_AuthPasswordProblem`). Five
wrong passwords for one web login lock it for ten minutes.

**Accounts from the shell.** The package ships `torirsctl`, which wraps
`torirsserver --account` with no-echo password prompts (the password reaches
the binary on stdin, never as an argument): `create-admin <name>`,
`create-player <name>`, `admin <name> on|off`, `password <name>`,
`unlink <name>`, `create-login <name>` (a web login without an invite),
`revoke-login <name>`, `invite [name]` and `list`. It runs beside the live servers: every record is
read again at each login.

From anywhere else, `tools/deploy/torirsctl <command>` runs the NUC's copy
over SSH: to `mrobertevers.com` (the cloud proxy), and through it over
WireGuard to the NUC (`--lan` goes straight to its LAN address instead). The
password prompt happens on the NUC through the SSH terminal; piped stdin is
passed through. Hosts and keys default to this deployment's and are
overridable (`TORIRSCTL_PROXY`, `TORIRSCTL_NUC`, ... -- see the script's
header).

**Staff.** `torirsserver --account admin <name> on` sets `admin = 1` on a
player account; the next login is staff level 2 -- the Jagex moderator's gold
crown on public chat and private messages, and permission to cheat. Wherever
passwords are checked, a `::command` (CLIENT_CHEAT) or a world-map click
teleport from anyone else is refused and logged (`ToriRSServer_PlayerMayCheat`).
A development server lets everyone cheat, since a name proves nothing there.

### Player accounts over OAuth: Play Now

Behind the gate, io_server is also the account server for PLAYER accounts
(`src/torirsserver/torirs_server_oauth.h`), and every endpoint needs the gate
cookie like everything else:

| route | what it does |
| --- | --- |
| `POST /oauth/token` | RFC 6749: `grant_type=password`, `refresh_token` (rotating; a reused one revokes its family) or `authorization_code` (PKCE S256 only). Public client `torirs-client`, scope `gamesso.token.create`. Answers JWT access (10 min) and refresh (30 days) tokens, HS256 under `<saves>/oauth/signing.key`. |
| `GET\|POST /oauth/authorize` | The code flow's player-account sign-in page, then a redirect to a same-origin path or a loopback URI with `?code=&state=`. |
| `POST /oauth/revoke` | RFC 7009. |
| `POST /game/play` | The deob's play call: a Bearer access token (header or `access_token=`) for `{"token": <one-time login>}`, good for 60 s. |
| `GET /.well-known/oauth-authorization-server` | RFC 8414 metadata. |

The game never sees a JWT. As in the deob's launcher login, the client trades
its access token at `/game/play` for a one-time login token and sends THAT in
the login block's RSA section with auth type TOKEN; torirsserver spends it
(`ToriRSServer_OAuthRedeemOtl`). A 1024-bit RSA block has no room for a JWT, and
the deob does not try.

A world whose manifest says `[net:boot] auth=oauth` (package:
`--auth oauth`) opens on the device's saved accounts, one Play Now card each,
laid out by count (`app_title_layout_accounts`); "Log in with another account"
opens the password form, which then signs in with the password grant (a new
name is claimed there, under the password rules), and the account icon in the
form's corner goes back to the cards. The accounts file (`TORIRS_ACCOUNTS`,
default `accounts.ini`) is the client's own and travels as FILE_READ/WRITE, so
on the browser it lives in IndexedDB. The client's requests are SCRIPT items
named `auth/token` and `auth/play` that lend a form body, the launch service's
convention: the browser executor POSTs them to the page's own origin, where the
cookie rides along; the desktop one to `TORIRS_AUTH_URL` with
`TORIRS_GATE_COOKIE` (plain http, so a LAN gate only).

### Player accounts linked to the web login

A web login (the "user account") can take player accounts under it. Behind
the gate a page always has a web login, so on an `auth=oauth` world the title
asks the gate who it is (`POST /account/players`) and puts that login's linked
accounts on Play Now cards ahead of the device's own.

| route | what it does |
| --- | --- |
| `POST /account/players` | `{"user": <web login>, "players": "A\nB"}`: the linked accounts, newline separated |
| `POST /account/link` | `username` + `password` of an existing player account: proves it is the linker's, links it, and answers a one-time login (`{"token","name"}`) so linking also plays. A name with no account is refused; accounts are made by logging in to the game |
| `POST /account/play` | `player`: a one-time login for a linked account, on this session |

Submitting the password form while signed in to the gate opens a prompt:
**Link account** (the call above) or **Log in** (the ordinary password grant,
leaving the account unlinked). A name that is already linked to this login
skips the prompt and plays.

A link mints a random **link token**. The web login's links database,
`<saves>/gate/links.sqlite` (mode 0600; one row per player account), keeps the
token; the player's own record, `<saves>/accounts/<name>.ini`, gains
`managed_by = <web login>` and `link_hash = SHA-256(token)`. `/account/play`
mints a one-time login only when the record names the session's login AND the
stored token hashes to `link_hash`. So:

- **Only an existing account.** A link never creates a player account: a name
  with no record in `<saves>/accounts/` is refused (`There is no player
  account by that name.`). Log in to the game once to make it, then link it.
- **One owner.** Another login cannot link the account even with its password
  (`That account is already linked to another login.`).
- **One live link.** Linking again, even by the owner, mints a new token; the
  old one is dead. That is also how an owner whose database row was lost gets
  the account back.
- **Nothing else signs it in.** Its password is refused everywhere (game login
  reply 3, the password grant, the authorize page); every refresh token it had
  is revoked at the link; an access token or one-time login minted before the
  link is refused.
- **The record is the authority.** A database row the record no longer agrees
  with is dropped the next time the list is read.

`torirsserver --account unlink <name>` clears `managed_by` and `link_hash`,
which gives the account its own password back. With no gate in front (a LAN or
development world) there is no web login: `/account/players` is never answered
and the form signs in with passwords as before, but a linked account still
refuses its password there.

### The account site

Signed in, the gate lands on its own small website (`src/ioserver/io_site.c`)
rather than straight in the game. It is server-rendered HTML with the styling
inline, and it loads nothing else: no script, no image, no font. The game is
still the landing URL given to `--gate` (`/?args=...`), which every page's
**Play** button opens.

| page | what it shows |
| --- | --- |
| `/` | Play Now, this login's linked accounts, the top five players |
| `/hiscores[?skill=N&page=P]` | everyone ranked overall (total level, then XP) or by one skill, 50 to a page, with a player search |
| `/players/<name>` | combat and total level, total XP, quest points, overall rank, last played, every skill's level, XP and rank, and the adventure log (quests completed). For an account linked to the viewer, also total XP over time (SVG), XP gained over the last day, week and month, and the recent sessions with their length, XP gained and most-trained skill |
| `/account` | the linked accounts (Stats, Unlink) and a form to link another one with its name and password, under the same rules as the game's Link account |

### The account server

```sh
TORIRSSERVER_SAVES=saves ./src/build_opt/account_server [--port 8091]   # make -C src account-server
```

One process owns `<saves>/accounts.sqlite` (`src/accountserver/`): web
logins, invites, the hiscores, experience history, sessions and the adventure
log. It listens on 127.0.0.1 only -- it checks passwords and takes records on
no credential of its own -- and answers lines of tab-separated fields, so curl
reads it (`account_server.h` lists the routes). Everything else is its client:

- **The game server sends and never waits.** A world that checks passwords
  queues a record when a login finishes, once a minute per player when the
  stats moved (compared, not hooked, so every writer of experience is seen), at
  the logout save, and for each `adventure_log` line content writes (a
  completed quest, from `~quest_complete_rewards_at`). The socket host appends
  them to `<saves>/account_spool.tsv` once a tick (buffered, never synced) and
  sends them from its select loop on a non-blocking socket
  (`torirs_server_accountlink.h`). The account server applies each record once
  by its sequence number and acknowledges it; the spool keeps whatever is not
  acknowledged, across restarts of either side, so an account server that is
  down costs the world nothing and loses nothing. The game server links no
  sqlite.
- **io_server asks** (`account_client.h`): the gate's sign-in and invites, and
  one request per site page -- a player page is a single view with the ranks,
  the gains, the chart, the sessions and the log. When the account server does
  not answer, the gate and the site say so (503), never "wrong password".
- **`torirsserver --account`** asks it for web logins, invites and the admin
  flag's effect on the hiscores; player accounts stay files the game server
  reads.

`TORIRS_ACCOUNT_SERVER=host:port` points the game server and io_server
elsewhere (default 127.0.0.1:8091); `TORIRS_WORLD_NAME` names a world's
records (default `world`). The database's first open imports the files it
replaced; schema 1 (when the game server opened it itself) is upgraded in
place.

A session dropped before its scene loaded records nothing. The site's reads
are prepared queries -- a page of the hiscores is a range of one index, a rank
a count along it -- and nothing lists the saves.

Staff (`admin = 1` in the account record) are not ranked: they appear on no
hiscore list and push nobody down, and their adventure log shows only on their
own page. The flag is read at every login, and `--account admin` and `--account
create ... admin` apply it at once. Experience over time is kept per day: a
stat's row for each day it changed, so a gain over a day, a week or a month is
one lookup per skill. Session history is private to the login its account is
linked to.
