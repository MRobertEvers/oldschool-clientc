# The Thistlewick Howl: the scripts

The files of the quest in [QUEST_TUTORIAL.md](QUEST_TUTORIAL.md), as they stand in the `thistlewick` lane
(`build/lanes/thistlewick/content/server/scripts/...`). The map, the copied records, the spawns, the stamps and
the camera path are made in the editor and saved by it; these are the files the editor started and that were
finished in VS Code (the last is entirely the editor's).

## Stages and places

`server/scripts/quests/quest_thistlewick/configs/quest_thistlewick.constant`

```
// Thistlewick: the values the quest's variable takes, named so a script reads
// ^thistlewick_started and not 1. Add a line for each step between started and complete,
// and keep ^thistlewick_complete the highest.
^thistlewick_not_started = 0
// Elder Bramwen asked for help: talk to Odo.
^thistlewick_started = 1
// Odo told where the wolf goes, and where he hid the cellar key.
^thistlewick_heard = 2
// The cellar door is unlocked for good: the den can be entered again without the key.
^thistlewick_unlocked = 3
// Greyfang is dead: take his pelt to the elder.
^thistlewick_slain = 4
^thistlewick_complete = 5

// Where the player goes. 0_<level>_<square x>_<square z>_<tile x>_<tile z>: the
// Inspector and the status bar show a tile's square and its x,z inside it.
^thistlewick_village = 0_72_56_63_63

// ::thistlewick_test's level 50, as experience in tenths (101,333 xp).
^thistlewick_test_xp = 1013330

// Greyfang, the boss in the den (thistle_den.rs2).
^greyfang_bites_per_pounce = 4
^greyfang_pounce_min = 10
^greyfang_pounce_max = 18
^greyfang_reg_bites = 0
^greyfang_reg_target_x = 1
^greyfang_reg_target_z = 2
```

## The quest's variable (written by Triggers > New quest)

`server/scripts/quests/quest_thistlewick/configs/quest_thistlewick.varp`

```
// Thistlewick: the quest's progress, one number for each player.
//   0 not started, 1 started, 2 complete (named in quest_thistlewick.constant).
// scope=perm keeps it across logins. transmit=no: only the server reads it;
// write transmit=yes when a client script must see it.
//
// A script reads and writes it as %varp9517_thistlewick_quest
// A variable's name carries its kind and its id. One written with neither, [my_name],
// is given both by the first RUN, here and in every script of the lane that reads it.
[varp9517_thistlewick_quest]
scope=perm
clientcode=default
transmit=no
protect=no
wholewrite=deny
wholeread=deny
```

## Dialogue, triggers, cutscene, journal, rewards, cheats

`server/scripts/quests/quest_thistlewick/scripts/quest_thistlewick.rs2`

```
// The Thistlewick Howl: a custom quest in the village of Thistlewick, the four
// squares m72_56 .. m73_57 (built in ToriRSEd; Triggers > New quest made this file).
//
// THE STORY
//   Thistlewick's mill was built over the mouth of an old cave. Bramwen's
//   grandmother said a great grey wolf, Greyfang, once denned in it; the
//   villagers walled the cave off behind the mill's cellar and forgot it.
//   The miller died last winter and the mill stands shut. This spring the
//   cellar wall gave way, the wolf's line came back, and now sheep vanish at
//   night and a howl comes up out of the ground.
//
//   Elder Bramwen (the hall)      tells the legend, asks the player to help,
//                                 sends them to Odo
//   Odo (walks round the pen)     followed the tracks into the shut mill, to
//                                 the cellar door; he keeps its key, hidden
//                                 in the hay bales by the mill to keep the
//                                 village children out of the cellar
//   the hay bales (by the mill)   Search: the cellar key, where Odo said
//   the cellar door (in the mill) the key unlocks it for good; down into the
//                                 den -- a private copy of the template
//                                 m0_100 -- and a cutscene finds Greyfang on
//                                 his bed of stolen wool (thistle_den.rs2)
//   Greyfang (in the den)         the boss (thistle_den.rs2); drops his pelt
//   Elder Bramwen                 takes the pelt: the legend was true
//
// %varp9517_thistlewick_quest takes the ^thistlewick_* stages in
// ../configs/quest_thistlewick.constant.
//
// Every npc, loc and obj named here is a Catalog "Copy to new" of a cache
// record, so it looks like its source but answers only to these triggers.

// --------------------------------------------------------- Elder Bramwen ----

[opnpc1,thistle_elder]
if (%varp9517_thistlewick_quest = ^thistlewick_not_started) {
    @thistle_elder_offer;
} else if (%varp9517_thistlewick_quest = ^thistlewick_slain) {
    @thistle_elder_finish;
} else if (%varp9517_thistlewick_quest = ^thistlewick_complete) {
    ~chatnpc_anim(^chat_happy, "Not a sheep lost since you went down there. Thistlewick owes you, friend.");
    return;
}
if (%varp9517_thistlewick_quest = ^thistlewick_started) {
    ~chatnpc_anim(^chat_quiz, "Have you spoken to Odo? He sat up with the flock last night.");
    ~chatnpc_anim(^chat_neutral, "You'll find him walking round the sheep pen, just east of the old mill.");
    return;
}
~chatnpc_anim(^chat_worried, "Into the old mill's cellar? Then it's the cave under it, just as Grandmother said.");
~chatnpc_anim(^chat_neutral, "Be careful down there. Whatever has been taking our flock is no ordinary wolf.");

[label,thistle_elder_offer]
~chatnpc_anim(^chat_sad, "Welcome to Thistlewick, though you find us in a sorry state. Three sheep gone in a week.");
~chatnpc_anim(^chat_worried, "And every night a howl that seems to come up out of the ground itself.");
switch_int (~p_choice3("Out of the ground?", 1, "What can I do?", 2, "I'm only passing through.", 3)) {
    case 3 :
        ~chatplayer_anim(^chat_neutral, "I'm only passing through.");
        ~chatnpc_anim(^chat_sad, "Then pass quickly, and don't walk the lanes after dark.");
        return;
    case 1 :
        ~chatplayer_anim(^chat_quiz, "Out of the ground?");
        ~chatnpc_anim(^chat_neutral, "My grandmother told of a great grey wolf, Greyfang, that denned in a cave under this hill.");
        ~chatnpc_anim(^chat_neutral, "The cave was walled up when the mill was built over it. I always thought it a story for frightening children.");
}
~chatnpc_anim(^chat_quiz, "Will you find out what is taking our sheep, and put an end to it?");
if (~p_choice2_header("Yes.", 1, "No.", 2, "Start The Thistlewick Howl?") = 1) {
    %varp9517_thistlewick_quest = ^thistlewick_started;
    mes("You've started a new quest: @q_mes@The Thistlewick Howl</col>");
    ~chatplayer_anim(^chat_happy, "I'll find your sheep-thief.");
    ~chatnpc_anim(^chat_happy, "Bless you. Start with Odo, our shepherd. He sat up with the flock last night.");
    return;
}
~chatplayer_anim(^chat_neutral, "Not just now.");

[label,thistle_elder_finish]
if (inv_total(inv, thistle_pelt) < 1) {
    ~chatnpc_anim(^chat_quiz, "The howling has stopped! Was it the wolf? Bring me something to show the village and I'll believe it.");
    mes("Greyfang's pelt is still in the den. The cellar door in the mill stays unlocked.");
    return;
}
~chatplayer_anim(^chat_happy, "There was a cave under the mill, and a wolf the size of a pony living in it. This is his pelt.");
~chatnpc_anim(^chat_shock, "That grey... Greyfang! Grandmother's stories were true, then.");
~chatnpc_anim(^chat_happy, "We'll wall that cellar up properly this time. Take this, with the whole village's thanks.");
inv_del(inv, thistle_pelt, 1);
%varp9517_thistlewick_quest = ^thistlewick_complete;
~thistlewick_rewards;

// ------------------------------------------------------------------ Odo ----

[opnpc1,thistle_shepherd]
if (%varp9517_thistlewick_quest = ^thistlewick_not_started) {
    ~chatnpc_anim(^chat_neutral, "Can't stop to chat. I've the flock to count, and there are fewer sheep in that pen every morning.");
    return;
}
if (%varp9517_thistlewick_quest = ^thistlewick_started) {
    ~chatplayer_anim(^chat_quiz, "The elder says you sat up with the flock last night.");
    ~chatnpc_anim(^chat_worried, "I did. Near midnight something grey came over the wall, quick as you like, and took a ewe by the neck.");
    ~chatnpc_anim(^chat_worried, "I followed its tracks in the mud. Paws as wide as my hand, straight to the old mill and in under the back door.");
    ~chatnpc_anim(^chat_neutral, "Inside, the tracks go to the cellar door and stop. The mill's been shut since the miller died; there's nothing down there but his old cellar.");
    ~chatplayer_anim(^chat_quiz, "Can I get into the cellar?");
    ~chatnpc_anim(^chat_neutral, "The miller left me the key. I hid it in the hay bales by the mill, so the children couldn't go poking about down there.");
    ~chatnpc_anim(^chat_worried, "Dig it out if you're brave enough. I'm not going down there.");
    %varp9517_thistlewick_quest = ^thistlewick_heard;
    return;
}
if (%varp9517_thistlewick_quest = ^thistlewick_heard) {
    if (inv_total(inv, thistle_cellar_key) > 0) {
        ~chatnpc_anim(^chat_worried, "You found it, then. The cellar door is inside the mill. Mind yourself.");
        return;
    }
    ~chatnpc_anim(^chat_neutral, "The key's in the hay bales by the mill door. Right at the bottom, where no child would dig.");
    return;
}
if (%varp9517_thistlewick_quest = ^thistlewick_unlocked) {
    ~chatnpc_anim(^chat_worried, "Still hear it at night, I do. Whatever's down there, it's still down there.");
    return;
}
if (%varp9517_thistlewick_quest = ^thistlewick_slain) {
    ~chatnpc_anim(^chat_shock, "You went down there and came back? Then the elder will want to see you, and whatever you found.");
    return;
}
~chatnpc_anim(^chat_happy, "Every one of them counted this morning, and every morning since. Thank you.");

// ------------------------------------------------------------- hay bales ----

[oploc1,thistle_haybales]
if (%varp9517_thistlewick_quest ! ^thistlewick_heard) {
    mes("Just hay.");
    return;
}
if (inv_total(inv, thistle_cellar_key) > 0) {
    mes("You find nothing else in the hay.");
    return;
}
anim(human_pickupfloor, 0);
p_delay(1);
if (inv_freespace(inv) < 1) {
    mes("You feel a key at the bottom of the hay, but you have no room to take it.");
    return;
}
inv_add(inv, thistle_cellar_key, 1);
~objbox(thistle_cellar_key, "Right at the bottom of the hay, where Odo said, you find the miller's old iron key.");

// ------------------------------------------------------------ cellar door ----

[oploc1,thistle_cellar_door]
if (map_instance_find(coord) ! ^map_instance_none) {
    return;
}
if (%varp9517_thistlewick_quest < ^thistlewick_heard) {
    mes("The cellar door is locked. Deep scratches score the boards around it.");
    return;
}
if (%varp9517_thistlewick_quest = ^thistlewick_heard) {
    if (inv_total(inv, thistle_cellar_key) < 1) {
        mes("The cellar door is locked. Odo hid the key in the hay bales by the mill.");
        return;
    }
    inv_del(inv, thistle_cellar_key, 1);
    %varp9517_thistlewick_quest = ^thistlewick_unlocked;
    mes("The old key turns stiffly. Cold air and the smell of wet fur come up from below.");
    ~thistle_den_enter(true);
    return;
}
if (%varp9517_thistlewick_quest >= ^thistlewick_slain) {
    mes("You climb down into the empty den.");
}
~thistle_den_enter(false);

// ---------------------------------------------------- journal and rewards ----

// A custom quest has no row in the client's quest list (that table is the
// cache's), so the journal opens with ::thistlewick_journal.
[proc,thistlewick_journal]
def_string $text;
if (%varp9517_thistlewick_quest = ^thistlewick_not_started) {
    $text = append(^journal_todo, "I can start this quest by speaking to ");
    $text = append($text, ^journal_highlight);
    $text = append($text, "Elder Bramwen");
    $text = append($text, ^journal_todo);
    $text = append($text, " in the hall at Thistlewick.");
} else if (%varp9517_thistlewick_quest = ^thistlewick_started) {
    $text = append(^journal_done, "Something takes Thistlewick's sheep at night, and a howl comes up out of the ground.|");
    $text = append($text, ^journal_todo);
    $text = append($text, "Elder Bramwen asked me to find out what. I should ask Odo the shepherd, who sat up with the flock.");
} else if (%varp9517_thistlewick_quest = ^thistlewick_heard) {
    $text = append(^journal_done, "Odo followed huge paw prints into the shut mill, to its cellar door.|");
    $text = append($text, ^journal_todo);
    $text = append($text, "He hid the cellar key at the bottom of the hay bales by the mill. I should find it and see what is down there.");
} else if (%varp9517_thistlewick_quest = ^thistlewick_unlocked) {
    $text = append(^journal_done, "I unlocked the mill's cellar. Beyond it is a cave: Greyfang's old den.|");
    $text = append($text, ^journal_todo);
    $text = append($text, "Something still lives down there. I should go back down and deal with it.");
} else if (%varp9517_thistlewick_quest = ^thistlewick_slain) {
    $text = append(^journal_done, "I killed Greyfang, the wolf in the cave under the mill.|");
    $text = append($text, ^journal_todo);
    $text = append($text, "I should take his pelt to Elder Bramwen.");
} else {
    $text = append(^journal_done, "Greyfang of Grandmother Bramwen's stories was real, and I rid Thistlewick of him.|");
    $text = append($text, ^journal_complete);
    $text = append($text, "QUEST COMPLETE!");
}
~quest_journal("The Thistlewick Howl", $text);

// The scroll without a quest row of its own: it names Cook's Assistant's row
// only for what the scroll reads from it (the Novice completion jingle).
[proc,thistlewick_rewards]
stat_advance(attack, 15000);
stat_advance(defence, 15000);
inv_add(inv, coins, 500);
~quest_award_points(1);
mes("Congratulations, you've completed a quest: @q_mes@The Thistlewick Howl</col>");
~quest_scroll_paint_titled(quest_cooksassistant, "You have completed The Thistlewick Howl!", append(~quest_points_reward(1), "1,500 Attack XP|1,500 Defence XP|500 coins"), coins_250, ^questscroll_model_x, ^questscroll_model_y);

// ------------------------------------------------------------------ cheats ----
// Typed in the play client's chat box: ::thistlewick (reset), ::thistlewick_start,
// ::thistlewick_done, ::thistlewick_journal, ::thistlewick_tele, ::thistlewick_test.

[debugproc,thistlewick]
%varp9517_thistlewick_quest = ^thistlewick_not_started;
mes("Thistlewick: not started.");

[debugproc,thistlewick_start]
%varp9517_thistlewick_quest = ^thistlewick_started;
mes("Thistlewick: started.");

[debugproc,thistlewick_done]
%varp9517_thistlewick_quest = ^thistlewick_complete;
mes("Thistlewick: complete.");

[debugproc,thistlewick_journal]
~thistlewick_journal;

[debugproc,thistlewick_tele]
p_telejump(^thistlewick_village);
mes("Thistlewick.");

// Ready to play the quest through: attack, strength, defence and hitpoints at 50
// (Greyfang is a level-64 wolf: attack 50, defence 52, 69 hitpoints), an adamant
// scimitar and five lobsters, and the village. The quest's stage is left as it is
// (::thistlewick resets it).
[debugproc,thistlewick_test]
~thistlewick_test_stat(attack, 50, ^thistlewick_test_xp);
~thistlewick_test_stat(strength, 50, ^thistlewick_test_xp);
~thistlewick_test_stat(defence, 50, ^thistlewick_test_xp);
~thistlewick_test_stat(hitpoints, 50, ^thistlewick_test_xp);
if (inv_total(inv, adamant_scimitar) = 0 & inv_total(worn, adamant_scimitar) = 0 & inv_freespace(inv) > 0) {
    inv_add(inv, adamant_scimitar, 1);
}
if (inv_total(inv, lobster) < 5 & inv_freespace(inv) >= calc(5 - inv_total(inv, lobster))) {
    inv_add(inv, lobster, calc(5 - inv_total(inv, lobster)));
}
p_telejump(^thistlewick_village);
mes("Thistlewick test: attack, strength, defence and hitpoints 50; an adamant scimitar (wield it) and 5 lobsters.");

// A stat to exactly `$level`: the base raised to it with the experience it lacks
// (stat_advance takes tenths, as stat_xp gives them; there is no command that sets
// a level), and the current level drained or healed to it. A base already above
// `$level` keeps its experience: only the current level comes down, and it creeps
// back up a point a minute.
[proc,thistlewick_test_stat](stat $stat, int $level, int $xp)
if (stat_base($stat) < $level) {
    stat_advance($stat, calc($xp - stat_xp($stat)));
}
if (stat($stat) > $level) {
    stat_drain($stat, calc(stat($stat) - $level), 0);
} else if (stat($stat) < $level) {
    stat_heal($stat, calc($level - stat($stat)), 0);
}
```

## The den (written by File > New instance, then edited) and Greyfang

`server/scripts/edited/thistle_den/scripts/thistle_den.rs2`

```
// server/scripts/edited/thistle_den/scripts/thistle_den.rs2: written by the ToriRSEd instance helper
// (File > New instance...) for the instance thistle_den. It is yours to edit from here.
//
// WHERE THINGS ARE
//   Template   m0_100, in instance space: ordinary map squares that no
//              world-map area claims. Edit them in ToriRSEd: Squares > Instance space,
//              or Scenes > instance:thistle_den.
//   At play    each player who enters gets a private copy stamped from the template
//              (~map_instance_from_square_block: terrain and locs, every level),
//              and its npcs (map_instance_spawnnpcs, below).
//              The copy lands in the instance pool, map square x >= 100, and a tile of
//              it is map_instance_coord($handle, dx, dz, level): dx, dz count tiles
//              from the template's south-west corner 0_0_100_0_0.
//   Edits      to the template show on the next entry, after Save and RUN.
//   Reference  general/scripts/misc/map_instance_procs.rs2, docs/map_instances.md.
//
// Edited for The Thistlewick Howl: the helper wrote ENTER as [oploc1,thistle_cellar_door];
// the quest's own [oploc1,thistle_cellar_door] (quest_thistlewick.rs2) checks the stage and
// the key, then calls this proc, which plays the cutscene inside the copy. Greyfang's fight
// is below.

// ENTER: called by the cellar door once the player may go down. $first: the door was
// unlocked just now, and the den is seen for the first time (the cutscene plays).
[proc,thistle_den_enter](boolean $first)
def_int $handle = ~map_instance_from_square_block(0_0_100_0_0, 1, 1);
if ($handle = ^map_instance_none) {
    mes("There is no room for another instance just now.");
    return;
}
%varp6600_map_instance_handle = $handle;
p_telejump(map_instance_coord($handle, 32, 20, 0));
// The template's own npcs (its spawn rows) stand in this copy too, each on its
// tile; they go with the copy. Delete the line for an empty copy.
map_instance_spawnnpcs($handle);

// Greyfang is dead: the den is empty, and a pelt not yet taken lies on his bed.
if (%varp9517_thistlewick_quest >= ^thistlewick_slain) {
    if (%varp9517_thistlewick_quest = ^thistlewick_slain & inv_total(inv, thistle_pelt) < 1) {
        obj_add(map_instance_coord($handle, 32, 36, 0), thistle_pelt, 1, 200);
        mes("Greyfang's pelt still lies on his bed of straw.");
    }
    return;
}

// SPAWN ON ENTER: Greyfang on his bed, template tile 0_0_100_32_35 of this player's copy;
// he is removed with the copy. He waits there through the cutscene.
~greyfang_spawn(map_instance_coord($handle, 32, 35, 0));
if ($first = true) {
    // The Camera path panel's thistle_lair path (Script tab > Copy script), pasted below.
    ~thistle_lair_camera($handle);
}
// Then he wakes and comes for whoever came down.
if (npc_find(coord, thistle_greyfang, 20, 0) = true) {
    npc_say("Aroooooooo!");
    npc_queue(5, 0, 1);
}

// EXIT: the first option of thistle_den_ladder, standing inside a copy. Hands the copy back
// (~map_instance_release_here frees the instance the player stands in, so it goes
// before the teleport), then home to 0_73_56_13_52 on the real map.
[oploc1,thistle_den_ladder]
if (map_instance_find(coord) = ^map_instance_none) {
    // The same loc on the real map, or in the template itself: nothing to leave.
    return;
}
~map_instance_release_here;
p_telejump(0_73_56_13_52);

// --------------------------------------------------------------- Greyfang ----
//
// He bites like any monster of his kind (his fields came with Copy to new from
// the cache's wolf). Every ^greyfang_bites_per_pounce bites he crouches instead:
// "*Greyfang crouches low...*", the tile the player stands on is marked, and
// two ticks later he pounces on it. Whoever still stands there takes 10..18.
// Step off the mark and he lands on nothing.

[proc,greyfang_spawn](coord $at)
npc_add($at, thistle_greyfang, 0);
~greyfang_reg_set(^greyfang_reg_bites, 0);

// Woken (thistle_den_enter, once the cutscene is over): he goes for whoever came down.
[ai_queue5,thistle_greyfang]
huntall(npc_coord, 12, 0);
while (huntnext = true) {
    npc_setmode(opplayer2);
}

[ai_queue2,thistle_greyfang]
~npc_default_damage(last_int);

// Naming this trigger takes him out of the wildcard swing, so the ordinary bite
// is restated here (skill_combat/combat.rs2's [ai_opplayer2,_]).
[ai_opplayer2,thistle_greyfang]
if (npc_stat(hitpoints) = 0) {
    return;
}
~greyfang_reg_set(^greyfang_reg_bites, calc(~greyfang_reg(^greyfang_reg_bites) + 1));
if (~greyfang_reg(^greyfang_reg_bites) >= ^greyfang_bites_per_pounce) {
    ~greyfang_reg_set(^greyfang_reg_bites, 0);
    ~greyfang_pounce_aim;
    return;
}
npc_anim(npc_param(attack_anim), 0);
~npc_meleeattack;

// The telegraph: he stops, says so, and marks the target's tile.
[proc,greyfang_pounce_aim]
~greyfang_reg_set(^greyfang_reg_target_x, coordx(coord));
~greyfang_reg_set(^greyfang_reg_target_z, coordz(coord));
npc_bind(3);
npc_say("*Greyfang crouches low...*");
mes("Greyfang crouches low, eyes on you: move!");
npc_facesquare(coord);
spotanim_map(gargboss_debris_shadow_90, coord, 0, 0);
npc_attackdelay(4);
npc_queue(6, 0, 2);

// The pounce: whoever still stands on the marked tile is hit.
[ai_queue6,thistle_greyfang]
def_coord $target = movecoord(npc_coord, calc(~greyfang_reg(^greyfang_reg_target_x) - coordx(npc_coord)), 0,
    calc(~greyfang_reg(^greyfang_reg_target_z) - coordz(npc_coord)));
npc_anim(npc_param(attack_anim), 0);
spotanim_map(smokepuff, $target, 0, 0);
huntall(npc_coord, 12, 0);
while (huntnext = true) {
    if (coord = $target) {
        damage(uid, hitsplat_damage, ~random_range(^greyfang_pounce_min, ^greyfang_pounce_max));
        if (stat(hitpoints) = 0) {
            queue(player_death, 0, 0);
        }
    }
}

// One Greyfang per den: his fight state lives in the den's registers.
[proc,greyfang_reg](int $reg)(int)
return(map_instance_var_get(map_instance_find(npc_coord), $reg));

[proc,greyfang_reg_set](int $reg, int $value)
map_instance_var_set(map_instance_find(npc_coord), $reg, $value);

// The death: the quest moves on, and the pelt drops where he fell.
[ai_queue3,thistle_greyfang]
if (npc_findhero = true) {
    if (%varp9517_thistlewick_quest = ^thistlewick_unlocked) {
        %varp9517_thistlewick_quest = ^thistlewick_slain;
        mes("Greyfang falls. The howling under Thistlewick is over.");
        obj_add(npc_coord, thistle_pelt, 1, 200);
    }
}
~npc_default_death;

// ---------------------------------------------------------------- the cutscene ----
// The Camera path panel's thistle_lair path (Script tab > Copy script), pasted as it came.
[proc,thistle_lair_camera](int $handle)
// generated by the map editor's Camera path panel from editor/campaths.txt: thistle_lair
// ~thistle_lair_camera($handle); plays it in the map instance $handle, whose south-west square is a copy of m0_100
~fade_screen(0, 255, 0, 0);
p_delay(1);
~cutscene_lock(3, 0);
~cutscene_hud_lock;
p_delay(0);
cam_moveto(map_instance_coord($handle, 32, 18, 0), 1437, 232, 232);
cam_lookat(map_instance_coord($handle, 32, 25, 0), 0, 232, 232);
p_delay(0);
~fade_screen(0, 0, 0, 255);
p_delay(2);
cam_moveto(map_instance_coord($handle, 32, 25, 0), 990, 5, 5);
cam_lookat(map_instance_coord($handle, 32, 31, 0), 0, 5, 5);
p_delay(6);
cam_moveto(map_instance_coord($handle, 32, 31, 0), 542, 5, 5);
cam_lookat(map_instance_coord($handle, 32, 35, 0), 0, 5, 5);
p_delay(6);
~fade_screen(0, 255, 0, 0);
p_delay(1);
~cutscene_unlock;
~cutscene_hud_unlock;
p_delay(1);
~fade_screen(0, 0, 0, 255);
p_delay(1);
if_closesub(toplevel_osrs_stretch:overlay_atmosphere);
```

## Odo's record overlay with his patrol (written by the NPC editor's Movement tab)

`server/scripts/edited/thistle_shepherd/configs/thistle_shepherd.npc`

```
[thistle_shepherd]
death_delay=2
blockwalk=npc
blocksight=0
retaliate=yes
healthbar=healthbar_standard_30
hitsplat=yes
forcemulti=no
defaultmode=patrol
facing=south
param=attackrange,1
wanderrange=5
maxrange=12
param=huntrange,0
timer=0
moverestrict=normal
nomove=0
huntmode=none
givechase=yes
param=combat_xp_multiplier,1000
param=death_anim,human_death
param=attack_anim,human_farmersfork_stab
param=defend_anim,human_farmersfork_def
param=attack_sound,2517
param=defend_sound,513
param=death_sound,512
param=attackrate,6
hitpoints=12
attack=3
strength=4
defence=8
magic=1
ranged=1
respawnrate=25
param=attackrate,6
param=damagetype,0
param=stabattack,5
param=strengthbonus,6
param=magicattack,0
param=rangeattack,0
param=rangebonus,0
param=stabdefence,0
param=slashdefence,0
param=crushdefence,0
param=magicdefence,0
param=rangedefence,0
param=death_drop,bones
patrol1=0_73_56_20_56,5
patrol2=0_73_56_30_56,0
patrol3=0_73_56_30_44,5
patrol4=0_73_56_20_44,0
```
