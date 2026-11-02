# Hosidius fruit stall (hos_fruit_stall_02, loc 28823) -- RSProx index 4448

Session: world 369, rev 241, recorder LoveTori, thieving 27. Steals at the
town-square stalls 1795,3607 and 1801,3607 from [213]. The transcript has 401
`oploc2` lines on the stall but only **120 steals** (120 `human_pickuptable`,
120 "You steal ..." lines): the rest are re-clicks on a stall that was empty
(client still showed the full form for a tick), the "Your inventory is too full
to hold any more." mesbox ([714], 2x) and walk-in clicks. The fruit is dropped
as they go (120 `put_down` 2739).

Ids in our cache (rev 241): seq `human_pickuptable` 832; synth `pick` 2581
(2x per steal), `put_down` 2739 (the recorder's drop, not content); loc
`hos_fruit_stall_02` 28823 (full, op2 "Steal-from"), `hos_fruit_stall` 27537
(empty), `hos_fruit_stall_01` has no ops (decor).

## Shape per steal (O = op tick = L+1 after the walk; [213]/[216], [219]/[221], [224]/[226])

| step | R (4448) | ours (test/thieving/fruit_stall.lua tick log + TRACE_OUT) | verdict |
|---|---|---|---|
| O | `[facing] coord=` stall (sizez=2), seq 832 delay 0, synth 2581 | `p_arrivedelay`, `anim(human_pickuptable)`, `pick` (tick 17 / TRACE 16) | match |
| O+1 | spam line "You steal <a/some X>.", `update_inv_partial` slot, synth 2581 again, `loc_add_change hos_fruit_stall` shape 10 (rot 3 at 1795, rot 1 at 1801), `update_stat_v2` thieving | MESSAGE_GAME (now `mes_spam`), second `pick`, UPDATE_INV_PARTIAL, UPDATE_STAT_V2, loc_set hos_fruit_stall (tick 18) | match (message type fixed to spam) |
| O+4 | `loc_add_change hos_fruit_stall_02` back (respawn 3 ticks after the empty form; 120/120: 217->220, 222->225, 227->230 ...) | `loc_change(hos_fruit_stall, 3)`, back at tick 21 = empty+3 | match |
| xp | stat xp reads 28/29 alternately (60/60) = 28.5 per steal; wiki 28.5 | `experience=285` (tenths) | match |
| level | wiki 25; recorder 27 so no gate line recorded | `~mesbox` "You need to be level 25 to steal from the fruit stall." (shared proc); fired in test | wiki, UNVERIFIED by recording |
| fail | no failure/stun ever (stall thieving cannot fail), no guard or owner reaction | no owner/guard columns | match |
| full pack | mesbox "Your inventory is too full to hold any more." (2x) | shared label | match |
| extras | `kourend_easy_count` / diary varp on the first steal | not ported | OPEN |

## Loot, 120 steals (counts are the "You steal" lines; the inventory adds differ by 1-2 where the add scrolled out of my parse window)

| item (obj) | recorded | wiki rate | verdict |
|---|---|---|---|
| cooking_apple 1955 | 52/120 | 40/100 | consistent (binomial z ~ +1.3) |
| banana 1963 | 26/120 | 20/100 | consistent |
| strawberry 5504 | 6/120 | 7/100 | consistent |
| lemon 2102 | 6/120 | 5/100 | consistent |
| redberries 1951 | 6/120 | 5/100 | consistent |
| pineapple 2114 | 6/120 | 5/100 | consistent |
| lime 2120 | 5/120 | 5/100 | consistent |
| jangerberries 247 | 5/120 | 5/100 | consistent |
| macro_triffidfruit 464 ("Strange fruit") | 4/120 | 5/100 | consistent |
| papaya 5972 | 3/120 | 1/100 | consistent (small n) |
| golovanova_top 19653 ("the Golovanova fruit top") | 1/120 | 2/100 | consistent |

Note: `macro_triffidfruit` is the strange fruit; the golovanova fruit top is
`golovanova_top`. Used the wiki's rates (total 100); every count is within
sampling noise of them, so the 120 rolls cannot improve on the wiki.

## Open

- Guard dogs (wiki: attack a thief in the main square): none in the recording
  reacted; not built.
- Kourend easy diary task (varp 2085 / `kourend_easy_count` 7933) on the first
  steal: not built.
- Stalls at 1767,3599/3597 and Kourend Castle share loc 28823 per the wiki, so
  the one binding covers them; not recorded.
- Level-25 gate and the 100-tick-free respawn are the shared proc's / wiki's.
