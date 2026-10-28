# PokeSprite (deprecated)

A 2D client that played the game with Pokemon-overworld sprite sheets instead of models, and the offline pipeline that generated those sheets. It is archived here and no longer built or served.

| here | was | what |
|---|---|---|
| `pokeclient/` | `pokeclient/` | the 2D client: its own Makefile, linking shared `src/` objects by path |
| `src/` | `src/pokesprite/` | the `.pss` sheet format and the sqlite-indexed sheet store |
| `tools/` | `tools/pokesprite/` | the offline generator (`pokesprite.py`, `region.py`, `layers.py`, `locs.py`, `store.py`) |
| `assets/` | `assets/pokesprite/` | the generated store, its own git repo (`pokesprite-assets`): Lumbridge, map squares 49-51 × 49-51 |

What was taken out of the live tree:

- **io_server:** the `GET /pokesprite/<key>` route, the `--pokesprite DIR` flag, the lazily opened store, and the two `pokesprite_*` counters on `/stats`. The two store sources are gone from `IO_SERVER_SRCS`. sqlite stays, because the gate's links use it.
- **torirsserver:** `ToriRSServer_PlayerIsPokesprite`, which recognised the client by login client type 31 (`POKESPRITE_CLIENT_TYPE`), and the login line it printed. A client of type 31 is now an ordinary desktop client.

What was kept: `tools/entity_viewer/ev_sheet`, the generator's renderer. It is the entity viewer's general "npc/player + seq → PNG frames" tool and has other users. Its `--layer` and `--materials` modes exist for this pipeline.

**This does not build from here as it is.**

- **Paths:** `pokeclient/Makefile` and `gen_shared_mk.py` find the repository as their parent directory (`ROOT := $(abspath ..)`), and `tools/pokesprite.py` as two levels up. Both assume their old locations.
- **Sources:** they expect `src/pokesprite/` and the io_server route above.

To revive it, move the four directories back, restore the io_server route, flag and sources, and restore the server's client-type check. The last state that built and ran is branch `pokesprite` (tag `pokesprite-baseline` for the humanoid art), 2026-10-14.
