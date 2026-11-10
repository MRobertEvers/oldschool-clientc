# Animal Magnetism: wiki transcript updates from RSProx recordings

Source: RSProx index 4511, 4513 and 4514. Revision 242, recorded 2026-11-10, world 496.
Together they are a full playthrough of Animal Magnetism, plus a visit to Droalak and Veronica.
Every line below is the server's own page text: the `<br>` breaks are joined back into one line, and the speaker and mood come from the chathead.
`[n]` is the tick in the session.

Compared against `Transcript:Animal_Magnetism` as served on 2026-11-10 (`?action=raw`).
Of the 306 distinct recorded pages and menu rows:

| Result | Count |
|---|---|
| Already exact in the transcript | 241 |
| In the transcript with different wording | 35 |
| Missing from the transcript | 30 |

The missing count includes 10 rows that belong to other pages. They are listed in the last section.

`Transcript:Making_History` already has Droalak's post-quest lines exactly. `Transcript:Veronica` already has the lamp hand-out exactly.

Each draft below is wikitext ready to paste over (or into) the named section.

---

## Transcript:Animal_Magnetism

### 1. ==Starting out== / ===Talking to Ava=== (replace)

The opening line has changed, and the quest now starts behind the standard start confirm.
The "What's a nice girl like you..." option was not offered (4513 [160], [173], [224]).
Ava's chicken request is two pages, not one ([197], [210]).

```
===Talking to Ava===
* '''Ava:''' Hello and welcome to my humble abode. It's sadly rather more humble than I'd like, to be honest. Perhaps you can help with that?
* {{tselect|Start the Animal Magnetism quest?}}
* {{topt|Yes.}}
** '''Player:''' I would be happy to make your home a better place.
** '''Ava:''' Yay, I didn't even have to talk about a reward; you're more gullible than most adventurers, that's for sure.
** '''Ava:''' Don't worry, though; I just need you to help fix this vile old bed for me. Then I'll find a suitable reward for you.
** '''Player:''' Great, will I be able to take a nap in it?
** '''Ava:''' Don't be silly; everyone knows that true warriors don't ever sleep...or perform many other bodily functions, for that matter. I'll come up with something, though.
** '''Player:''' I'm not convinced by just a vague something; can you be a slight bit more inspiring in your offer?
** '''Ava:''' What I need is simple: a couple of undead chickens. You should be able to pick some up at the farm near Port Phasmatys.
** '''Ava:''' I'll use one for my bed, then see what I can make from the other in the way of a reward. I have some ideas involving infinite feathers.
** {{tact|[[Animal Magnetism]] is started.}}
** '''Player:''' Very well then, I shall await my mystery prize with bated breath.
** {{tact|end}}
* {{topt|No.}}
** '''Player:''' I'm not much into interior design, to tell the truth.
** '''Ava:''' Very well, I suppose I'll just have to resort to the Professor's patented sleeping aid in order to get some rest. It does so bruise my head, though.
** {{tact|end}}
```

Note: the quest starts on the click of "I'll use one for my bed..." ([233]), before the player's last line.

### 2. New section, after "Trying to trade Ava": Talking to Ava without the requirements (4511 [131..136])

```
===Talking to Ava without the requirements===
* '''Ava:''' Hello and welcome to my humble abode. It's sadly rather more humble than I'd like, to be honest.
* '''Player:''' Well maybe I could help with that?
* '''Ava:''' Tempting, but I don't think you're skilled enough unfortunately.
* {{tbox|You do not meet all of the requirements to start the Animal Magnetism quest.}}
* {{tact|end}}
```

"Trying to trade Ava" is also what **Devices** gives before the quest is finished (4511 [125], 4514 [884]):

```
===Trying to trade Ava or view her devices===
```

### 3. ===Using items on Ava=== (add to "Other items", and fix its punctuation)

The catch-all line uses a semicolon ([3168], [3185], 4514 [905], [909], [930], [1825], [1840]). Change it to:

```
* '''Ava:''' I'm not sure what you are trying to do; are you sure you aren't slightly deranged?
```

Add these replies by item:

```
====Undead chicken (after both are handed over)====
* '''Ava:''' What would I want these for now; I already have two?
====Selected iron====
* '''Ava:''' What is this? Whatever it is, I have no use for it.
====Undead twigs (after handing them over)====
* '''Ava:''' What makes you think I want more twigs?
====Research notes====
* '''Ava:''' I gave you those and have plenty of copies; I need them to be translated.
====Polished buttons or hard leather====
* '''Ava:''' Why do you think I want these?
```

Sources: 4513 [2776]; 4514 [914] (chicken). 4513 [3161], [3175], [3179] (selected iron). 4514 [1596] (twigs), [1635] (notes), [924], [1819] (buttons, leather).

### 4. ====A matter of love==== / Talking to Alice

Alice names her husband (4513 [373]):

```
* '''Alice:''' You need to talk to my husband Malcolm, though - not that I can these days.
```

### 5. ====A matter of love==== / Talking to Alice again

The player names Malcolm, and Alice has an extra page (4513 [487..509]):

```
* '''Player:''' I have a message from Malcolm. He wants you to know that he still loves you, despite his ghostly state.
* '''Alice:''' The curse of undeath was so cruel; all the men out here succumbed, but Lyra and I were left alive.
* '''Alice:''' Ever since that day, I've not been able to speak to him.
* '''Alice:''' Tell him I love him but I can't find our savings. I know he had our collection of gold and 'prize cow' rosettes just before the curse struck.
* '''Player:''' I'll have a word with him, then; magic has its uses I suppose.
```

### 6. ====A matter of savings==== / Talking to Malcolm

"til", not "till" (4513 [575]):

```
* '''Malcolm:''' A warning to ya, too: annoy her and I'll haunt ya til yer hair turns white.
```

### 7. ====A matter of savings==== / Talking to Alice, and ====A matter of bank accounts==== / Talking to Alice again

The player says "Malcolm" (4513 [607], [704]):

```
* '''Player:''' Malcolm says he put the cash in the bank.
```
```
* '''Player:''' Malcolm says he won't trust me with the bank pass.
```

The last line of "A matter of bank accounts / Talking to Alice again" ends with a trailing space in the game text. It does not need to be shown.

### 8. New section after "Talking to Alice before obtaining the amulet": Speaking to Malcolm before obtaining the amulet (4513 [759..771], stage 70)

```
====Speaking to Malcolm before obtaining the amulet====
* '''Player:''' I talked to your wife and thought that if you had a special amulet, you could speak to her and sort out the bank situation without me being involved.
* '''Malcolm:''' Arr, that makes far more sense than I was expecting from a muscle-head like you. My wife's a clever one.
* '''Player:''' Well...oh, never mind. I'm working on getting the amulet anyway.
* {{tact|end}}
```

### 9. ===Obtaining the amulet=== / Speaking to the old crone

A player who has finished [[Ghosts Ahoy]] first gets a menu (4513 [891], [904], [989], [1016], [1031], [1815]):

```
* {{tselect|Select an option}}
* {{topt|I'm here about Necrovarus.}}
** '''Player:''' I'm here about Necrovarus.
** '''Player:''' Good news - the enchantment worked!
** '''Old crone:''' So Necrovarus has let the ghosts go free.... You have achieved many wonderful things, my young friend.
** {{tact|end}}
* {{topt|I'm here about the farmers east of here.}}
** '''Player:''' I'm here about the farmers east of here.
** ''(continues as below)''
```

Three crone lines name Malcolm (4513 [908], [960], [1818]):

```
* '''Player:''' Alice and her husband Malcolm are having trouble talking to one another and said you might be able to help.
```
```
* '''Old crone:''' The second amulet will be useful for the purpose you desire, though it won't work for any other ghost or human other than Malcolm and his wife.
```
```
* '''Player:''' Hello there, just dropping in to thank you again. It all went perfectly. Alice and Malcolm can communicate again.
```

Typo fix in "after losing the amulet" (4513 [1036], [1344]): "lucklily" should be **luckily**.

### 10. ====Speaking to Malcolm==== (Returning to Alice): handing over an amulet you no longer carry (4513 [1184..1185])

```
* {{topt|Okay, you need it more than I do, I suppose.}}
** '''Player:''' Okay, you need it more than I do, I suppose.
** {{tcond|If the player does not have the crone-made amulet}}
*** '''Malcolm:''' Ya din't seem to 'ave it.
*** {{tact|end}}
```

### 11. ====Speaking to Malcolm after catching the chicken====

The menu option is "Where can I get these ecto-tokens?". The player then says this (4513 [2418]):

```
** '''Player:''' Where can I get ecto-tokens?
```

Buying one chicken has the player line too (4513 [2448]):

```
** '''Player:''' Could I buy 1 chicken now?
```

### 12. ===Helpful Witch=== / Talking to the Witch after losing the selected iron

No "that" (4513 [3129], [3135], [3507]):

```
* '''Witch:''' I can't say I didn't expect this; you'd lose your head if it weren't glued on. Lucky I made some replacements. Take this one and leave me in peace.
```

New section after the magnet is handed in (4514 [841..850], [1538..1540]):

```
====Talking to the Witch after handing over the magnet====
* '''Witch:''' Hello my ickle...oh, it's you. What do you want?
* '''Player:''' I wondered whether you had anything interesting to say.
* '''Witch:''' Clearly not.
* {{tact|end}}
```

### 13. Using the hammer on the selected iron

These are game messages, not boxes (4513 [3329..3711], 32 tries). Every try plays the hammering animation and sound. Remove the doubled "the":

```
* {{mes|You aren't in the right area for this to work.}}
```
```
* {{mes|You think that facing North might work better.}}
```

### 14. Reporting the axe bounce to Ava

"any more", two words (4514 [323], [362]):

```
* '''Ava:''' Of course, you won't be able to pick your nose any more, so I suppose you'll want to try the second option.
```

This talk does not advance the quest. The quest advances when Turael explains the blessed axe (4514 [475]).

### 15. Turael

When Turael is short of the axe at the first attempt (stage 170), he says "a holy symbol". Later attempts say "holy symbol" (4514 [524] vs [542], [1019], [1061]):

```
* '''Turael:''' You'll need to hand over both a mithril axe and a holy symbol of Saradomin. You don't have an axe in your pack, so I'm not able to help.
```

"Goodbye, then." takes a comma (4514 [993], [1035]):

```
* '''Turael:''' Goodbye, then. You may, of course, return if you decide you want a new axe.
```

Turael's full menu has five rows. The quest row reads "I'm here about a quest." (stage 160), "Hello, I'm here about those trees again." (170), and **"I'm here about blessed axes again."** once the axe has been given:

```
* '''Turael:''' 'Ello, and what are you after then?
* {{tselect|Select an option}}
* {{topt|I need another assignment.}}
* {{topt|Have you any rewards for me, or anything to trade?}}
* {{topt|Let's talk about the difficulty of my assignments.}}
* {{topt|I'm here about blessed axes again.}}
* {{topt|Er... Nothing...}}
```

New section: asking for a replacement axe (4514 [1013..1035], [1056..1061], [1265..1270]):

```
====Asking Turael for a replacement axe====
* {{topt|I'm here about blessed axes again.}}
** '''Player:''' Hello, I'm here about the blessed axe again.
** {{tcond|If the player has a blessed axe in their inventory}}
*** '''Turael:''' You already have an axe in your bag but you want another?
** {{tcond|Otherwise}}
*** '''Turael:''' I suppose you want a new axe?
** {{tselect|Select an option}}
** {{topt|Actually, yes.}}
*** '''Player:''' Actually, yes.
*** {{tcond|If the player does not have a mithril axe}}
**** '''Turael:''' You'll need to hand over both a mithril axe and holy symbol of Saradomin. You don't have an axe in your pack, so I'm not able to help.
*** {{tcond|If the player has both}}
**** '''Turael:''' Here's the replacement; be more careful with this one, eh?
** {{topt|I can do without for now, thanks.}}
*** '''Player:''' Not right now.
*** '''Turael:''' Goodbye, then. You may, of course, return if you decide you want a new axe.
```

The wiki's open comment on "You're still hunting [task monster]..." can be resolved. With his own task running, Turael's Assignment says (4514 [1050]):

```
* '''Turael:''' You're still hunting goblins, you have 27 to go. Come back when you've finished your task.
```

### 16. The research notes

Translate gives a game message, which the transcript lacks (4514 [1626], [1653], [1684], [1706]):

```
* {{mes|You fiddle with the notes.}}
```

"It suddenly all makes sense." is a game message, not a box ([1661], [1758]).

Ava, when the notes are held but not translated: "in case you forgot**!**" ([1644]).

New section, losing the translated notes (4514 [1679..1681]). Ava hands over the **garbled** notes again, to be translated again:

```
====Talking to Ava after losing the translated notes====
* '''Ava:''' Let me guess, you've managed to mislay the notes after you translated them?
* '''Player:''' I can't say I'm proud of it, but you aren't wrong.
* '''Ava:''' Luckily, I assumed you were terminally careless, so I can supply these copies.
* {{tact|receives=Research notes}}
* {{tact|end}}
```

Losing the pattern (4514 [1802..1806]): "my pack...not my fault" has no space, and "aren't you." ends with a full stop:

```
* '''Player:''' The pattern seems to have vanished from my pack...not my fault, of course.
* '''Ava:''' You're pretty careless, aren't you. I assume you're the type who leaves a trail of arrows and knives behind them when they train?
* '''Ava:''' Here's a replacement; perhaps if I charged for them, you'd be more careful.
```

Typo fix: "Your short-term memory loss worries me.  Combine" has a double space. The game has one ([1813]).

Typo fix: "You almoust remove a suitable twig" should be **almost** (4514 [1502], [1510]).

### 17. Quest complete scroll (4514 [1862])

The rewards are listed in this order: 1 Quest Point, 2,500 Woodcutting XP, 1,000 Crafting XP, 1,000 Fletching XP, 1,000 Slayer XP, An Ava's Device.
The scroll shows the accumulator's model, even when the attractor is given.

---

## Not this transcript

These rows were missing from the Animal Magnetism comparison because they belong to other pages:

- **Veronica**, lamp and post-quest dialogue (4513 [91..93], [2519..2520]): already exact in `Transcript:Veronica`.
- **Antique lamp (Ernest the Chicken)**: 500 xp in the chosen skill (4513 [108]), as `Antique lamp (Ernest the Chicken)` says.
- **Droalak** (Making History, post-quest, 4513 [1935..1940]): already exact in `Transcript:Making_History`, lines 591-594. The recording gives every page as chathap1. The scroll-delivered state also fades him: varbit 1388 goes 0 to 1 on the click of "Bye!".
- **Ghost disciple** (Ectofuntus): "Can I have the tokens I have earned?" / "Certainly, mortal. Here's 35 ectotokens." (4513 [2371..2373]).
- **Giles** (Certer random event): "Thank you, I hope you like your present. I must be leaving now though." The reward line is "Your reward is: 1 x Uncut sapphire." (4513 [3002..3006]).
- **Porcine of Interest** tracking tree: "Something pretty big must have come running through here to knock this over." (4514 [136]).
