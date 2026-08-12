# NPC death: what fades the corpse, and who owns it

A corpse that pops out of existence at the end of its death animation looks
wrong, and the instinct is that the client should fade it. It does not — not on
its own. The rev-239 client has no automatic death fade anywhere in it. What it
has is a general **server-driven transparency ramp**, new in that revision, and
a server that wants a corpse to dissolve must drive it.

This document is the record of what each reference actually does, so the next
person does not go looking for a client-side fade that was never there.

Sources read for this: the official rev-239 gamepack deob
(`~/Documents/git_repos/Deobfuscator/src_osrs239_rl1_12_33`, RuneLite 1.12.33),
RSProt's osrs-239 module (`~/Documents/git_repos/rsprot`), LostCity
(`~/Documents/git_repos/LostCity_Server`, rev 254 / 2004 lineage), and rsmod
(`~/Documents/git_repos/rsmod`) as a second modern server.

---

## 1. The deob (rev 239)

### 1.1 Removal is instant, and there is no grace period

NPC_INFO high-resolution decoding is `Statics.method9992` (line 47066). Each
npc in the index list reads one bit, then two:

| bits | meaning | kept in the index list? |
|---|---|---|
| `0` | no update | yes |
| `1,0` | extended info only | yes |
| `1,1` | one walk step | yes |
| `1,2` | run / teleport | yes |
| `1,3` | **removed** | **no** — pushed to `client.field956`, `field1244 = true` |

The same `field1244 = true` marking happens for every index past the new count
when the high-resolution count shrinks (line ~47077).

`Statics.method13029` (line 47043) is the caller, and it reaps immediately after
the three decode passes:

```java
for (int var4 = 0; var4 < client.field798 * 1246434723; var4++) {
    class86 var6 = (class86) var0.field1411.method13058(client.field956[var4]);
    if (var6.field1244) {
        var6.method2900(null);   // post NpcDespawned
        var6.field1237 = null;   // drop the NpcType
        var6.method12141();
    }
}
```

Clearing `field1237` is what stops the draw: `class86.method2930` (line 364)
returns `null` for a model the moment the type is gone. So the corpse is gone on
the very cycle the server stops sending it. **There is no client-side lingering,
no despawn animation, and no fade tied to death, to the death animation, or to
removal.** Every mention of a fade in this client is the mask below.

### 1.2 The transparency tween — `class657`

The whole fade mechanism is one 70-line class, held per actor as
`class105.field1467` (the field lives on the shared Actor base, so players and
npcs both have it):

```java
public class class657 {
    byte field7006;   // current transparency (the tween's live value)
    int  field7008;   // start cycle
    int  field7007;   // end cycle
    byte field7009;   // start transparency
    byte field7010;   // end transparency

    void method14216(int nowCycle, int start, int end, byte from, byte to, boolean useFrom) {
        this.field7008 = start;
        this.field7007 = end;
        this.field7010 = to;
        this.field7009 = (useFrom || this.field7008 < nowCycle) ? from : this.field7006;
    }

    byte method14218(int nowCycle) {                       // sample
        if (nowCycle < field7008) return field7006;        // not started
        if (nowCycle >= field7007) return field7006 = field7010;   // finished, latch
        int a = field7009 & 0xFF, b = field7010 & 0xFF;
        float t = (float)(nowCycle - field7008) / (field7007 - field7008);
        return field7006 = (byte)((b - a) * t + a);        // linear
    }

    boolean method14219(int nowCycle) { ... }              // "is a tween live/non-zero"
}
```

Three things in there matter and none of them are guessable:

- **It is sampled per client cycle (20 ms), not per game tick.** `method14218`
  is called from the model build every frame with `client.field742`, so the ramp
  is smooth, not stepped at 600 ms.
- **`useFrom == false` means "continue from wherever I am now"** (`field7006`,
  the live value), so back-to-back ramps chain without a visible jump. It only
  falls back to the explicit start value when the ramp begins in the past.
- **It latches.** Past `end` the value sticks at `endTransparency` forever —
  a fade to 255 leaves the actor permanently invisible until another ramp or a
  respawn. The client never clears it on its own except in
  `class105`'s reset (`field1467 = null`, line 247).

### 1.3 How the tween reaches the model

`class86.method2930` (the npc model builder), lines 455–462:

```java
byte t = (this.field1467 == null) ? 0 : this.field1467.method14218(client.field742);
var11.method4830(t);          // class144.field2180 = t
```

`class60` (Player) does exactly the same at lines 812–819.

`field2180` is a **model-wide transparency**, applied on top of per-face alpha
in `class144.method4912` (line 2073) / `method4901` (line 2088):

```java
int method4912(int faceAlpha) {
    if (this.field2180 == -1) return 253;              // 0xFF -> fully invisible
    int t = this.field2180 & 0xFF;
    if (t > 0 && faceAlpha < 253) return faceAlpha + (t * (253 - faceAlpha) >> 8);
    return faceAlpha;
}
```

So the scale is **0 = opaque, 255 = invisible**, it is a *transparency* not an
alpha, and it pushes each face's existing alpha toward the 253 "gone" cap rather
than replacing it — a model with per-face glass keeps its relative
translucency the whole way down. At 255 (`== -1` as a signed byte)
`method4901` returns before drawing the face at all.

One consequence to carry into any port: `class86.method942` / `method948`
(lines 284, 340) return `true` when `field1467.method14219(cycle)` is live.
Those are the "this actor needs the translucent path" predicates. **An actor
mid-fade must be routed into the alpha-blended pass, not the opaque one**, or
the ramp does nothing visible.

### 1.4 The wire — NPC_INFO extended info, mask `0x400`

Decoded in `Statics.method10109` (the npc extended-info pass, line 47270) at
line 47621:

```java
if ((var6 & 0x400) != 0) {
    int start = client.field742 + var1.method13179();   // g2, cycles from now
    int end   = client.field742 + var1.method13132();   // g2, cycles from now
    byte from = method13222(var1);                      // g1
    byte to   = var1.method13168();                     // g1
    boolean useFrom = var1.method13137() != 0;          // g1
    if (var5.field1467 == null) var5.field1467 = new class657();
    var5.field1467.method14216(client.field742, start, end, from, to, useFrom);
}
```

RSProt names it and confirms the accessor variants
(`NpcTransparencyEncoder.kt`):

```kotlin
buffer.p2Alt3(start); buffer.p2(end)
buffer.p1Alt1(startTransparency); buffer.p1Alt2(endTransparency)
buffer.p1Alt2(if (useStartTransparency) 1 else 0)
```

The player equivalent is mask `0x100000` with `p2Alt2 / p2 / p1Alt3 × 3`.

**Both are brand new at 239.** `3rd/rsprot/gen/packet_versions.txt` lists
`NpcTransparencyEncoder` and `PlayerTransparencyEncoder` at version 1, revision
239 only — every other extended-info encoder there runs 221→239. Nothing before
239 can express a fade at all.

`start` and `end` are **signed shorts and may be negative** (RSProt's
`setTransparency` validates against `SIGNED_SHORT_RANGE` and documents it), which
is the "ramp that began in the past" case `useStartTransparency` exists for.

### 1.5 What the deob does *not* do

- It does not fade on death. Nothing keys off the death animation, the seq, or
  hitpoints — the client does not know an npc died, only that it stopped
  arriving in NPC_INFO.
- It does not defer removal to let an animation or a fade finish.
- It does not clear a finished tween. Sending a fade and *not* deleting the npc
  leaves an invisible, still-clickable npc standing there.

---

## 2. LostCity (rev 254 / 2004 lineage)

### 2.1 The script — `content/scripts/skill_combat/scripts/npc/npc_death.rs2`

Nine lines, three of which suspend:

```
[proc,npc_death]
npc_walk(npc_coord);
npc_setmode(none);
npc_arrivedelay;                        // up to two ticks
if (finduid(%npc_aggressive_player) = true) {
    if (npc_param(death_sound) ! null) {
        ~sound_within_distance(npc_param(death_sound), 0, npc_coord, 12);
    }
    %lastcombat = null;
}
npc_anim(npc_param(death_anim), 0);
npc_delay(1);                           // "osrs has an extra tick of delay here.
                                        //  I think this delay can vary from npc to npc"
npc_del;
```

`npc_delay(1)` parks until `currentTick + 1 + 1`, i.e. **two ticks**, and that is
the entire corpse lifetime — it is a fixed constant, deliberately not the length
of the death animation. The comment above it is LostCity conceding it does not
know the real rule.

### 2.2 The engine

`NpcOps.ts:93` — `npc_del` is unconditional and immediate:

```ts
[ScriptOpcode.NPC_DEL]: state => {
    World.removeNpc(state.activeNpc, check(state.activeNpc.type, NpcTypeValid).respawnrate);
},
```

`World.removeNpc` (World.ts:1298) drops the npc and schedules the respawn. There
is no fade phase, no transparency, no deferred despawn.

### 2.3 There is no fade to send

LostCity's whole npc extended-info set (`engine/src/network/rsbuf/prot.ts:41`)
is eight masks:

```
DAMAGE2 = 0x1, ANIM = 0x2, FACE_ENTITY = 0x4, SAY, DAMAGE, CHANGE_TYPE,
SPOT_ANIM, FACE_COORD
```

No transparency, no tinting. **A fade is not expressible in this protocol**, so
LostCity's behaviour is not a shortcut or an omission — it is the only thing
2004 could do, and copying LostCity's death verbatim onto a 239 client
necessarily copies the pop.

---

## 3. rsmod (second modern reference)

`api/death/src/main/kotlin/org/rsmod/api/death/NpcDeath.kt`:

```kotlin
walk(coords); noneMode(); hideAllOps(); arriveDelay()
// ... death sound to the aggressive player, %lastcombat = 0
val deathAnim = param(params.death_anim)
anim(deathAnim)
delay(seqTypes[deathAnim])              // <-- the FULL length of the death seq
if (npc.respawns) { npcRepo.despawn(npc, npc.type.respawnRate); return }
npcRepo.del(npc, Int.MAX_VALUE)
```

Two differences from LostCity worth having:

1. **`delay(seqTypes[deathAnim])` — the corpse lies for exactly as long as its
   death animation runs**, per npc, derived from the seq. This is the answer to
   LostCity's "I think this delay can vary from npc to npc". It requires the
   server to know seq durations.
2. `hideAllOps()` — a corpse is not clickable while it dies.

rsmod does **not** call `setTransparency` anywhere; a grep for it across the
tree returns only unrelated interface-transparency uses. So neither open server
reference drives the fade, and RSProt exposes the API without a consumer.

---

## 4. What this tree does today

**Server** (`src/net/mock/mock230_combat.c`, `npc_death_step`, line 1160) is a
faithful port of LostCity's schedule, run in the engine rather than in content
(because `[ai_queue3]` is an exclusive trigger and 494 drop tables bind it):

```
D            hitpoints hit 0 -> flinch, death queued
D+1          MOCK230_DEATH_QUEUED  — stop, clear mode, arrivedelay (0/1/2 ticks)
D+1+a        MOCK230_DEATH_ARRIVE  — death sound, death animation
D+1+a+delay  MOCK230_DEATH_CORPSE  — drop table, then npc_del
```

`death_delay` defaults to **2** (`mock230_content.c:3314`) — LostCity's
`npc_delay(1)`, overridable per npc. Most death seqs at 239 run longer than
two ticks, so **the corpse is reaped mid-animation**: that, not the missing
fade, is the first thing visibly wrong.

**Client** parses the mask and throws it away, on both entities
(`src/net/rev/osrs239/osrs239_entity_info.c`):

- `V5_NPC_TRANSPARENCY 0x400` — line 1520, five reads, no op emitted
- `V5_PLAYER_TRANSPARENCY 0x100000` — line 1025, same

The accessor variants match RSProt exactly, so the framing is already correct;
only the effect is absent.

**Renderer** has per-face alpha (`ToriDraw_Model.face_alphas` /
`original_face_alphas`, `3rd/toridraw/toridraw_types.h:135`) but **no
model-wide transparency** — there is no equivalent of `class144.field2180`,
and therefore no equivalent of `method4912`'s "push each face toward 253".

**Server-side seq durations do not exist.** `SS_OP_SEQLENGTH` (1020) is declared
in `src/serverscript/ss_opcode.h:69` and implemented nowhere, and no seq table is
packed into the server band. rsmod's rule cannot be copied without adding one.

---

## 5. The gap, in the order it has to be closed

1. **Corpse lifetime.** Either pack seq durations into the server band and use
   rsmod's rule (`delay = length(death_anim)`), or set `death_delay` per npc in
   the generated combat ledger (`docs/DEATH_ATK_DEF_ANIMS.md` — the pipeline that
   already picks `death_anim` per npc knows the seq, so it can emit its length).
   Until this is right, a fade would only make the pop smoother, not correct.
2. **Model-wide transparency in the renderer.** Add the `field2180` equivalent
   plus `method4912`'s blend, and route an actor with a live tween into the
   translucent pass (the `method942` / `method948` predicate).
3. **Client: consume mask `0x400`.** Emit an op, hold a `class657`-shaped tween
   on the entity, sample it per *client cycle* (not per tick) in the model build.
   Mind the latch (§1.2) — a finished ramp sticks.
4. **Server: send it.** At `MOCK230_DEATH_ARRIVE`, alongside the death animation,
   send `transparency(start=<corpse lifetime - fade>, end=<corpse lifetime>,
   0 -> 255, useStart=true)` in cycles (30 cycles per tick), so the ramp
   completes on the tick `npc_del` lands. Nothing clears a tween on its own, so
   the delete and the end of the ramp must be the same tick.
5. **`hideAllOps` on the corpse** (rsmod does it, LostCity does not) — a
   half-transparent npc that still takes clicks is worse than one that pops.

An honest caveat on step 4: neither open server reference sends this mask, so
the exact ramp Jagex uses on death — its length, whether it starts with the
animation or at its end, whether it applies to players too — is not established
by any source read here. What *is* established is that the client can only fade
because a server told it to, and the shape of what the server must say.
