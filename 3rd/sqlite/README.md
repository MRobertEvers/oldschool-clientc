# SQLite 3.54.1, vendored

The official amalgamation, unmodified: `sqlite3.c`, `sqlite3.h` and
`sqlite3ext.h` from https://www.sqlite.org/2026/sqlite-amalgamation-3540100.zip
(2,990,966 bytes, SHA3-256
`92281c86e2957f9b9305890f9ac04204caa7b677cc868dc326d942689df28e30`, as
listed on https://www.sqlite.org/download.html). Public domain.

Built by `src/makefile` as `$(OBJ_DIR)/sqlite3.o` (see `SQLITE_DEFINES` there)
and linked into the processes that keep a database: io_server (the gate's
links, the account site, the 2D client's sheet store) and torirsserver (the
account database, `src/torirsserver/torirs_server_accounts.c`). Nothing links
the system's libsqlite3.

To upgrade: replace the three files with a newer amalgamation's, update the
version, URL and hash above, and rebuild.
