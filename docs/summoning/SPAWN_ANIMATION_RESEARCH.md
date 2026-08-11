# Summoning spawn-animation research

Research date: 10 August 2026

## Answer

Revision-530 Summoning does have a visible spawn-in effect for an ordinary
familiar. It is not a skeletal animation played by the player, and it is not a
special spawn animation in the familiar's NPC animation set. It is an animated
blue spot graphic attached to the newly spawned or called familiar:

- graphic `1314` for a familiar whose NPC size is `1`; or
- graphic `1315` for a familiar whose NPC size is greater than `1`.

2009Scape also plays sound `188`. Its ordinary pouch path does not animate the
player. The same familiar-attached graphic is used when the player presses
**Call familiar**.

Pets are a separate path. When a pet item is released and consumed, 2009Scape
plays player animation `827`, then spawns the pet. The generic blue familiar
graphic is deliberately excluded for objects of type `Pet`.

This distinction explains the apparently conflicting terminology in old and
new sources. Wiki prose sometimes calls the event a portal or a summoning
animation, but the period visual evidence shows the player standing still
while the animated effect materializes the creature beside them.

## Findings by action

| Action | Player animation | Familiar/NPC graphic | Sound | Confidence |
| --- | --- | --- | --- | --- |
| Summon an ordinary familiar from a pouch | None | `1314` if size 1; `1315` if size >1 | `188` | High |
| Call an existing ordinary familiar | None | The same `1314`/`1315` graphic after relocation | `188` | High |
| Renew an existing familiar with another pouch | None in the 2009Scape path; its timer is refreshed rather than spawning it again | None | None identified | High for 2009Scape |
| Release a pet from an item | `827` when the item is consumed | None of the generic blue graphics | None identified | High for 2009Scape |
| Use a familiar special move | `7660` plus player graphic `1316` in the generic special-move path | Familiar-specific behavior may follow | Varies | High; this is **not** spawning |

For the three playable familiars in this port, the size rule gives:

- Spirit wolf: size `2`, so large graphic `1315`;
- Dreadfowl: size `1`, so small graphic `1314`; and
- Spirit terrorbird: size `2`, so large graphic `1315`.

The rule should remain size-based rather than being hard-coded by familiar.

## 2009Scape implementation evidence

The most direct executable reference is 2009Scape's
[`Familiar.java`](https://gitlab.com/2009scape/2009scape/-/blob/master/Server/src/main/content/global/skill/summoning/familiar/Familiar.java):

- it declares `SMALL_SUMMON_GRAPHIC` as graphics ID `1314` and
  `LARGE_SUMMON_GRAPHIC` as `1315`;
- its no-argument `init()` calls `init(getSpawnLocation(), true)`;
- that `true` causes `init(Location, boolean)` to call `call()`;
- `call()` teleports the familiar to its spawn location, plays sound `188`,
  and applies `1315` when `size() > 1`, otherwise `1314`; and
- `call()` contains no call to animate the owner/player.

The pouch route in
[`FamiliarManager.java`](https://gitlab.com/2009scape/2009scape/-/blob/master/Server/src/main/content/global/skill/summoning/familiar/FamiliarManager.java)
constructs the familiar and invokes `spawnFamiliar()`. That invokes
`familiar.init()`, closing the initial-pouch chain into the same `call()` method
used by the Call button. This matters: the graphic is not merely a recall
effect accidentally mistaken for the initial summon effect.

The same manager's pet route is explicitly different. `summonPet(...)` calls
`player.animate(new Animation(827))` before removing a consumed pet item and
spawning the pet. `Familiar.call()` wraps sound `188` and graphics `1314/1315`
in `if (!(this instanceof Pet))`, so a pet release does not get both effects.

The source also declares player animation `7660` and graphic `1316` as the
generic familiar special-move presentation. Those identifiers occur beside
the summoning graphics in the class, which makes them easy to misclassify, but
they are not part of `init()` or `call()`.

2009Scape is a reconstruction, not Jagex's original server source. It is still
strong evidence for the intended revision-530 behavior because its control
flow and cache identifiers agree with both the cache dump and contemporary
visual evidence below.

## Contemporary wiki visual evidence

The RuneScape Wiki/Fandom article revision from 30 January 2009 embeds
`Summoning Familiar.gif` with the caption “A player summoning a familiar with
the Summoning skill.” See the
[`Summoning` revision 897436](https://runescape.fandom.com/wiki/Summoning?oldid=897436).
The companion [`Summoning familiars` revision
887889](https://runescape.fandom.com/wiki/Summoning_familiars?oldid=887889),
dated 27 January 2009, documents the familiar list and points back to the main
Summoning article.

The GIF's
[`file history`](https://runescape.fandom.com/wiki/File:Summoning_Familiar.gif)
contains captures uploaded on 27 July 2008 and 2 February 2009. I downloaded
both historical revisions, decoded every animation frame, and inspected them
frame by frame rather than relying on the current thumbnail:

### 27 July 2008 capture

- Dimensions reported by the file-history API: `204x152`.
- 47 decoded frames.
- The player remains in the same standing pose throughout.
- White/blue shards or streaks appear on the ground beside the player.
- The familiar becomes visible inside that effect and remains when it clears.
- No player arm, torso, weapon, or stance animation is visible.

### 2 February 2009 capture

- Dimensions reported by the file-history API: `176x168`.
- 97 decoded frames.
- The player again remains still.
- A small familiar materializes beside the player inside vertical blue/white
  streaks.
- The effect clears while the familiar remains, before the GIF loops.
- No player body animation is visible.

These captures are the strongest historical evidence found because they show
the live presentation from the relevant period. They agree with the
familiar-attached blue graphic in 2009Scape and contradict adding an invented
player casting animation to the ordinary pouch path.

The file metadata can be reproduced with Fandom's
[`imageinfo` API](https://runescape.fandom.com/api.php?action=query&format=json&prop=imageinfo&titles=File%3ASummoning%20Familiar.gif&iiprop=timestamp%7Curl%7Csize%7Csha1&iilimit=max).
The old article revision and both historical uploads predate revision 530's
February 2009 timeframe closely enough to be directly relevant.

## Wiki and guide language

Later copies of the original game-guide wording describe opening a pouch as
briefly activating a portal that pulls the familiar into RuneScape. That prose
is consistent with the materialization effect in the archived GIF; it does not
say the player performs a casting gesture. A surviving mirror is the
[`Summoning - The Basics` knowledge-base
page](https://www.2011.rs/kb/summoning_the_basics).

The modern RuneScape Wiki records a 5 January 2015 patch stating that “the
animation for summoning a follower” was slightly adjusted. See the
[`Summoning familiars` update
history](https://runescape.wiki/w/Summoning_familiars). This confirms that the
overall spawn presentation is reasonably called an animation, but it is six
years after revision 530 and does not identify whether a player skeleton, NPC
skeleton, or spot graphic was changed. It cannot override the 2008–2009 visual
and revision-530 code evidence.

Contemporary fan guides also distinguish **Call Familiar** as the control that
makes the already-summoned creature appear beside the player; for example,
Sal's 2008-era
[`Summoning` guide](https://runescape.salmoneus.net/skills/summoning.html).
That supports treating pouch summon and call as separate actions even though
2009Scape intentionally gives both the same arrival graphic.

## Cache-identifier evidence

2009Scape's revision-530
[`gfxs.txt`](https://gitlab.com/2009scape/2009scape/-/blob/master/dumps/530/gfxs.txt)
labels the relevant consecutive graphic IDs:

- `1314`: small blue summon-familiar graphic;
- `1315`: big blue summon-familiar graphic; and
- `1316`: a familiar special graphic, not the generic spawn graphic.

The local revision-530 reconnaissance independently resolved spotanim `1314`
to model `31388` and sequence `7663`; see
[`docs/summoning_port/AGENT_RECON.md`](../summoning_port/AGENT_RECON.md).
The complete dependency closure for both `1314` and `1315` still needs to be
transcoded and verified before either can be packed into the target cache.

## What is missing from the current port

The current summon procedure in
[`summoning_spirit_wolf.rs2`](../../OSRS-Content/osrs239-content/server/scripts/ported_scape2009_summoning/scripts/summoning_spirit_wolf.rs2)
adds the NPC, assigns its owner, puts it into player-follow mode, consumes the
pouch, updates state, and sends the message. It never calls `spotanim_npc`,
plays a sound, or animates the player. Therefore the observed instantaneous
appearance is the script's present behavior; it is no longer explained by the
earlier model-rendering failure.

The marked Summoning overlay currently contains only the already verified
renew-points effect, source graphic `1308` mapped to target spotanim `20000`.
Its [`spotanim.client`](../../OSRS-Content/osrs239-content/ported/scape2009_summoning/pack/spotanim.client)
membership file remains empty, and there is no imported target mapping for
source graphics `1314` or `1315`.

The mock server already implements `spotanim_npc`: it applies the spotanim to
the active NPC and marks the NPC spotanim update mask. No renderer or network
feature needs to be invented for this effect. The missing work is content and
asset wiring:

1. Extract and transcode source spotanims `1314` and `1315`, including their
   models, sequences, frame archives, framemaps, and any material dependencies.
2. Give them collision-free target IDs and add only those overlay-owned assets
   to the Summoning manifest/cache lane.
3. After `npc_add`, while the new familiar is still the active NPC, call
   `spotanim_npc` with the mapped small or large graphic based on NPC size.
4. Use the same effect when Call familiar is implemented as an actual NPC
   relocation.
5. Add sound `188` if the target's area/audio path supports the source sound.
6. Do **not** add a player animation to ordinary pouch summoning.
7. Keep pet release distinct; if source-faithful pet presentation is in scope,
   map player animation `827` and do not also apply `1314/1315` to the pet.

This work can remain entirely inside the marked Summoning overlay. It does not
require editing original target cache content.

## Confidence and limitations

The ordinary-familiar conclusion is high confidence because three independent
forms of evidence agree: 2009Scape control flow, revision-530 graphic labels,
and two period wiki animation captures. The pet result is high confidence for
2009Scape behavior, but I did not find an equally clear period pet-release GIF
that independently proves Jagex used animation `827` in February 2009.

I did not find an original Jagex server script, a revision-530 packet capture,
or a surviving official page that enumerates animation IDs. Exact spotanim
height, delay, sound radius, and the full `1315` asset dependency graph should
therefore be verified against the source cache during implementation. None of
those uncertainties changes the central finding: normal familiar arrival is
an animated graphic on the familiar, while the player remains idle.
