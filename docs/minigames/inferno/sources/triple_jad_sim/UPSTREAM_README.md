# Triple Jad Simulator

A browser simulator for Old School RuneScape's **Inferno wave 68 (triple Jads)**: three JalTok-Jads and their Yt-HurKot healers, tick for tick, in the real Inferno arena.

**Play it:** https://davidsaad-git.github.io/triple-jad-sim/

The Jads, healers, client and practice tools are modelled on the TzKal-Zuk simulator at [scim.gg](https://scim.gg) (its Zuk-fight Jad), with the real wave-68 layout: three Jads attacking every 9 ticks, staggered 3 ticks apart, three healers each at half health, no pillars. Everything runs in your browser from the real game cache, so the arena, models, animations, projectiles and sounds are the game's own.

Unofficial fan project, not affiliated with Jagex or scim.gg. Old School RuneScape and all game assets are the property of Jagex Limited. See [NOTICE.md](NOTICE.md) for third-party credits.

## How to play

- **Walk:** left-click the floor. **Attack:** left-click a Jad or healer. Right-click for more options.
- **Prayers:** prayer tab (F5); the prayer orb toggles quick prayers. Watch the Jad's animation and switch protection before its attack lands (the prayer is checked when the attack is released, 3 ticks after the animation starts).
- **Camera:** W A S D, middle-mouse drag, mouse wheel. Click the compass to face north.
- **Pause:** press **P** (or the Pause button next to HUD / Practice).
- **Restart:** Ctrl+R. **Encounters / loadouts:** Ctrl+K, then Configure (presets: Max Tbow, Bowfa, Atlatl / RCB, Budget, Mage Tank; custom gear, stats, infinite health).
- **Practice panel:** bottom left: spawn healers, set Jad HP, infinite health / special attack.
- **Settings / Plugins:** top-left and top-right buttons (playback speed, input lag, layout, resource packs, tick counter, prayer flick helper, tile markers and more).

## Run it locally

```bash
npm install
npm run dev
```

Open http://localhost:5173. The app loads the trimmed game cache committed in `public/osrs-cache/trimmed/` (about 7 MB).

## Development

```bash
npx vitest run    # tests
npx tsc -b        # type check
npm run build     # production build
```

Rebuilding the trimmed cache or the UI assets needs the full OpenRS2 cache 2720 at `public/osrs-cache/disk.zip`:

```bash
curl -L -o public/osrs-cache/disk.zip https://archive.openrs2.org/caches/runescape/2720/disk.zip
npx tsx scripts/trim-cache.ts
```

Layout: `src/sim` (deterministic tick engine, combat, Jads), `src/render` (WebGL2 renderer), `src/audio` (cache sound synthesis), `src/input` (clock, input lag, menus), `src/ui` (client, menus, HUD, plugins), `src/cache` (game cache reader).
