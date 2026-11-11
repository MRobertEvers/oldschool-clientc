# Another Slice of H.A.M.: wiki transcript updates from an RSProx recording

Source: RSProx index 4524. Revision 242, recorded 2026-11-11, world 370.
It is a full playthrough of Another Slice of H.A.M., plus some unrelated talk around Dorgesh-Kaan, the Goblin Village, the Varrock Museum and an Evil Bob random event.
Every line below is the server's own page text: the `<br>` breaks are joined back into one line (a break after a hyphen joins with no space), and the speaker comes from the chathead.
`[n]` is the tick in the session.

Compared against `Transcript:Another_Slice_of_H.A.M.` as served on 2026-11-11 (`?action=raw`).
Of the 254 distinct recorded pages and menu rows that belong to this quest:

| Result | Count |
|---|---|
| Already exact in the transcript | 242 |
| In the transcript with different wording | 11 |
| Missing from the transcript | 1 |

7 of the 11 "different wording" rows are menus: the header is "Select an option", the page has "Select an Option". The option text and order of every recorded menu already match.
119 more recorded rows belong to other pages. They are listed in the last section.

The quest already starts behind the standard confirm on the page, and both branches are exact: "Start the Another Slice of H.A.M. quest?" Yes./No. ([631], [667]); "No." gives "Do your own digging!" / "Oh well. We will continue at our own pace." ([633..634]); "Yes." gives "I'd love to help!" and the two lines after it ([669..671]).

Each draft below is wikitext ready to paste over the named lines. The whole page with every draft applied is in `wiki_new.wikitext` (see the end).

---

## Transcript:Another_Slice_of_H.A.M.

### 1. ==Starting off== / ===Talking to Ur-tag or Ambassador Alivjar=== (heading, menu, one line)

Only Ur-tag gives this menu. Talking to Ambassador Alvijar goes straight to "What are you talking about?" with no menu ([616..618], [659..661]), as the page's own "Talking to Ambassador Alvijar" section already says. Talking to Ur-tag gives the menu ([638..640], [651..653]). The heading also misspells Alvijar.

```
===Talking to Ur-tag===
```
```
* {{tselect|Select an option}}
```

The "south-<br>west" in Ur-tag's reply is one hyphenated word split at a line break, like "steam-powered" and "war-like" elsewhere on the page ([670]):

```
*** '''Ur-tag:''' Splendid! The path to the train tunnel is in the south-west of the city.
```

### 2. ====Talking to Ur-tag after starting the quest==== (menu header)

[683], [1781]:

```
* {{tselect|Select an option}}
```

### 3. ===Talking to Tegdak with an artefact=== (Zanik's last line)

The line ends with "!" ([2152]):

```
*** '''Zanik:''' Sure thing! Come on, [player name], let's go talk to the scribe!
```

### 4. ==The goblin scribe== / ===Talking to Zanik=== (menu header)

[2177], [2318], [2325]:

```
* {{tselect|Select an option}}
```

### 5. ==The goblin scribe== / ===Talking to Ur-tag=== (menu header)

[2302], [2308], [3524]:

```
* {{tselect|Select an option}}
```

### 6. ===Talking to the goblin scribe with Zanik=== (pronoun)

The page says "she dug up". The recording, for a male player, says "he dug up" ([2334]). The word follows the player's gender:

```
* '''Zanik:''' [player name] helped us to finish the dig! Look what [he/she] dug up!
```

### 7. ===Talking with Zanik in Goblin Village=== and ===Getting Zanik from the railway=== (menu headers)

[2446] and [2735]:

```
* {{tselect|Select an option}}
```

### 8. ===Talking to the generals=== (menu header)

[3413], [3428], [3432]. The map option (`quest=no`) was not offered, which fits its condition.

```
* {{tselect|Select an option}}
```

### 9. ==Talking to the Sergeants in the Lumbridge Swamp== (missing last line)

Mossfists has one more page after Slimetoes' "Grubfoot say pink robe man..." ([3618]):

```
==Talking to the Sergeants in the Lumbridge Swamp==
* '''Sergeant Slimetoes:''' You [player name]?
* '''Player:''' Yes.
* '''Sergeant Mossfists:''' Generals say we help you rescue Chosen Commander.
* '''Sergeant Slimetoes:''' It too important for puny human to do on own. You just get killed.
* '''Sergeant Mossfists:''' So we come to look after you so you not die.
* '''Sergeant Slimetoes:''' Grubfoot say pink robe man take Chosen Commander down this hole into new tunnel.
* '''Sergeant Mossfists:''' We distract guards for you. If we see them, we attack them so you get past while we fighting!
* {{tact|end}}
```

### 10. "Talking to the Sergeants on the surface after talking to them in the cave" (heading)

The sergeants say "What we waiting for?..." on the surface before the player has been into the cave at all. Mossfists says it at [3627], right after the first talk, and the player only goes down at [3632]. Slimetoes says it again at [3704]. The heading should not depend on the cave:

```
===Talking to the Sergeants on the surface again===
```

### 11. ===Coordinating with the sergeants=== (speaker and menu headers)

The sergeant you click answers "Okay.". It is not tied to the option:

- Slimetoes clicked, "Both of you follow me", Slimetoes answers ([3674..3679]).
- Mossfists clicked, "One of you wait here", Mossfists answers ([3869..3873], [4117..4120]).
- Mossfists clicked, "Wait here", Mossfists answers ([3889..3892]).
- "Follow me", Slimetoes answers ([4032..4035]).

"Both of you wait here" was not picked, but it is in the same menu. Both headers are "Select an option".

```
===Coordinating with the sergeants===
====With both sergeants====
* {{tselect|Select an option}}
* {{topt|Both of you follow me}}
** '''Player:''' Both of you follow me.
** '''Sergeant Mossfists/Slimetoes:''' Okay.
** {{tact|end}}
* {{topt|One of you wait here}}
** '''Player:''' One of you wait here.
** '''Sergeant Mossfists/Slimetoes:''' Okay.
** {{tact|end}}
* {{topt|Both of you wait here}}
** '''Player:''' Both of you wait here.
** '''Sergeant Mossfists/Slimetoes:''' Okay.
** {{tact|end}}

====With one sergeant====
* {{tselect|Select an option}}
* {{topt|Wait here}}
** '''Player:''' Wait here.
** '''Sergeant Mossfists/Slimetoes:''' Okay.
* {{topt|Follow me}}
** '''Player:''' Follow me.
** '''Sergeant Mossfists/Slimetoes:''' Okay.
```

### 12. Overheads (from the recording's overhead packets, not chat pages)

These are not counted in the table above. The guards have two more overheads ("Goblins!" [3743], [3830], [3881], [4111]; "Die goblin!" [3905], [4126], [4159]) besides "Goblin!" ([3789], [3901]) and "Kill it!" ([3903]):

```
===Guards engaging in combat with the sergeants===
* '''Guard:''' {{overhead|Goblin!}}
* '''Guard:''' {{overhead|Kill it!}}
* '''Guard:''' {{overhead|Goblins!}}
* '''Guard:''' {{overhead|Die goblin!}}
```

Sigmund's Magic overhead has a capital M and a "!" ([4195], hit with Earth Strike). The melee one is exact ([4200]):

```
* '''Sigmund:''' {{overhead|Saradomin protect me from Magic!}}
```

"You there!", "Intruder!", "Got you!" and the sergeants' "Quick! Me keep human busy!" / "Ha ha ha!" / "Take that!" are already exact.

### Checked and left alone

- **Pages sent twice.** "Aaaaah! My eyes!" ([3352], [3354]) and the guard's "Oh no! Goblins!" ([3656] / [3659], and at every later entry) each appear twice. The second copy re-opens the same page with no click in between. It is one page, so the page's single line is right.
- **Tegdak's "Thank you, [player name]!" twice** ([1921], [1926]). The first talk was cut short when the player dropped the artefact ([1924]).
- **The generals' menu three times** ([3413], [3428], [3432]). The player clicked Wartface in the middle of the answer ([3431]), so the menu reopened. This is not a dialogue difference.
- **Leaving Goblin Village** ([2572]). Zanik says only "I'll meet you back in Dorgesh-Kaan, [player name]." The page has "Where are we going?" before it. That line may come from a different trigger, so it was not removed.
- **Menus the recording never opened** keep "Select an Option" (line numbers are in `wiki_raw.wikitext`): the railway Yes/No/Where menus (lines 262, 275, 334), "I can't believe we're going to the Goblin Village..." (325), and the 1,000-coin mace menu (525). They are probably "Select an option" too, but this recording does not show them.
- **Ur-tag's "What are you arguing about?"** was never picked, so its "What are you talking about?" reply is unconfirmed.
- The heading "Talking to Zanik in Dorgesh-Kaa" is a typo, and its first line ("Let's go then!") is out of place. The recording says nothing about either.

---

## Belongs to other pages

These 119 rows came up in the session but are not Another Slice of H.A.M. dialogue. Nothing from them goes into the quest transcript.

- **Cave goblin guard** ([12]): "I'm keeping an eye on you, surface-dweller." Present in `Transcript:Cave goblin guard`.
- **Barlak** (bone seller, [221..281]): every line is present in `Transcript:Barlak`.
- **Ur-meg** ([422..449]): every line is present in `Transcript:Ur-meg`.
- **Oldak**, Buy-sphere before he has made the Goblin Village sphere ([510..513], `opnpc3`): "I can make you a moving-over-distance sphere for two law runes and a piece of molten glass." / Player: "I'll have to go and get some then." **Missing** from `Transcript:Oldak`. That page's Buy-sphere section only has the later "Where do you want a sphere to?" menu.
- **Oldak**, Talk-to ([517..561], [3477..3479]) and Buy-sphere with materials ([2881..2904]; "I have 20 law runes and 4 pieces of molten glass here!", "Excellent! I can make you 4 moving-over-distance spheres..."): present in `Transcript:Oldak` (the second as its `[X]` template).
- **Goblin scribe**, generic greeting and menu ([592..593], [767..768]): present in `Transcript:Goblin scribe`.
- **Museum guard** ([1370..1372]): "Welcome! Would you like to go into the Dig Site archaeology cleaning area?" Present in `Transcript:Museum guard`.
- **Varrock Museum cleaning area**: the mesbox "You will need a full archaeology kit to clean your finds. Leather gloves and boots, rock pick, trowel and specimen brush. You can find them on the tool rack." ([1381], [1396], [1418]) and the menu "Take cleaning kit?" Yes./No. ([1390]). A wiki search found neither. They likely belong on the museum's cleaning-area page (tool rack or specimen table).
- **Terry Balando** ([1606..1614]): present in `Transcript:Terry Balando`.
- **Evil Bob / Prison Pete** random event ([2018..2143]): present in `Transcript:Evil Bob` and `Transcript:Prison Pete`.
- **Cave goblin** (Dorgesh-Kaan, [2215..2220]): "What's it like on the surface?..." Present in `Transcript:Cave goblin (Dorgesh-Kaan)`.
- **Grubfoot**, generic ([2500..2502], [3420..3427]): "Me wish me weren't so small...", "Grubfoot do this! Grubfoot do that!...", "Don't talk to me!" / "Why not?" / "Me not allowed to talk..." / "Shut up Grubfoot!". Present in `Transcript:Grubfoot`.
- **General Wartface / General Bentnoze**, generic, without Zanik ([2598..2609]): the worm stew argument and the "Shut up" chain ending "Shut up goblins!". Present in `Transcript:General Wartface` / `Transcript:General Bentnoze`.
- **Goblin (Goblin Village)** ([3187..3188]): "Brown armour best!" and its menu. Present in `Transcript:Goblin (Goblin Village)`.

---

Outputs (session scratchpad, not in the repo): `wiki_raw.wikitext` (the page as fetched), `wiki_new.wikitext` (the whole page with drafts 1-12 applied), `wiki.diff` (raw to new), `wiki_summary.txt` (edit summary).
