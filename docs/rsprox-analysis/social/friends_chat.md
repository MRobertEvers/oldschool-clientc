# Friends chat (chat-channel tab 7, setup panel 94, side channels 707)

Built 2026-11-20 from RSProx. Engine: `src/torirsserver/torirs_server_friendchat.{h,c}`.
Content: `interface_friends/scripts/friendchat.rs2` and `configs/friendchat.{constant,varp}`.
Test: `TORIRSSERVER_SELFTEST_FRIENDCHAT_ONLY=1 TORIRSSERVER_REV=osrs242 torirsserver --selftest`
(`src/torirsserver/test/friendchat_selftest.u.h`, 39 checks, two players, on 239/241/242).

## Reading the recordings

**The default transcripts hide every social packet.** `rsprox_index.py get` writes them with
RSProx's default filters, which drop `friendchat_*`, `update_friendchat_*`,
`message_friendchannel`, `clanchannel_*`, `clansettings_*`, `varclan*` and `message_public`.
Transcribe with every filter on (the `home-wire` home):

```sh
H=~/.rsprox-index-cache/home-wire; A=$H/.rsprox/binary/archive
python3 tools/rsprox/rsprox_index.py get <id>          # fetches the .bin
ln -sf ~/.rsprox-index-cache/bin/idx<id>.bin $A/
java -Duser.home=$H -cp "$(cat ~/rsprox-index-data/rsprox-c.classpath)" \
  net.rsprox.proxy.cli.TranscribeCommandKt -name archive/idx<id>
```

Sessions: the owner's two-account test series **index 745..770** (rev 233, Z-Kris owns the
channel, Blurite Rock joins; each side recorded), **236** (rev 227, 148 channel lines),
**4567, 4392, 4294, 4284** (rev 241/242 logins and rejoins), **111, 4237, 4257** (clans).
Find more with `search message_game "Attempting to join chat-channel"` (2,080 sessions).

## The protocol, recorded -> ours

| exchange | recorded | ours | verdict |
|---|---|---|---|
| login burst | empty `update_friendchat_channel_full_v2` (753 [0]) | `ToriRSServer_FriendchatLogin` | matched |
| rejoin at login | `chatchannel_login("Das Idle")` clientscript, client sends the join (4567 [7..8]) | `~friendchat_login` when `chatchannel_on_logout`=1 | matched |
| join | `friendchat_join_leave name=`; "Attempting to join chat-channel..." same tick; +1/+2 "Now talking in chat-channel <prefix>", "To talk, start each line of chat with the / symbol.", FULL (753 [7..9], 762 [7..8]) | +1 (`^friendchat_join_delay`) | matched (the shorter recorded delay) |
| member list | FULL in join order, owner rank 7, friends 0..6, others -1 (758 [8], 755 [35]) | same | matched |
| others told | SINGLEUSER `add` to every member (757 [19]) | same | matched |
| leave | no name; "You have left the channel." + empty FULL same tick; others SINGLEUSER rank -128 (753 [954], 761 [266]) | same | matched |
| say | client sends `message_public type=2 "/text"`; every member gets `message_friendchannel` "Text", slash gone, first letter upper (236 [131155..131217], 756 [20] "/red:lol" -> "Red:lol") | same; never said aloud | matched |
| talk refused | "You are not allowed to talk in this friendschat channel." (757 [149], 760 [36]) | same | matched |
| unknown channel | "The channel you tried to join does not exist." with no attempt line (753 [1049..1050]) | same | matched |
| disabled channel of a known player | attempt line, then "does not exist" (753 [1246..1247]) | same | matched |
| enter rank | "You do not have a high enough rank to join this chat-channel." (752 [1898]) | same | matched |
| not allowed | "You are not allowed to join this user's chat-channel." (752 [2713]) | the owner's ignore list | cause unverified |
| kick | the client itself sends `/-Attempting to kick player from chat-channel...` and `friendchat_kick`; kicker +2 "Your request to kick/ban this user was successful.", kicked "You have been kicked from the channel." + empty FULL, others SINGLEUSER -128 (761 [96..98], 762 [44]) | same tick | matched (tick +0, not +2) |
| ban | rejoin: attempt line + "You are temporarily banned from this chat-channel." (762 [56..57]) | same | matched |
| ban ends | the owner left (channel empty) and rejoined; the kicked player got straight back in (761 [248..258], 762 [203]) | bans die with the channel; else `^friendchat_ban_ticks` 6000 | matched; the hour is the wiki's |
| setup open | four `if_settext` (enter, talk, kick, prefix) then `toplevel_mainmodal_open(-1,-1)` + modal 94 (753 [20]) | same | matched |
| setup ranks | op N = rank N-2 (op1 Anyone .. op9 Only me), kick op4..op9 (745 [16626..16832]) | same | matched |
| prefix | op1: `meslayer_mode9("Enter chat prefix:", -1)`, `resume_p_stringdialog`, kept as base 37: "Test FC Name Weeeeeeeee" -> "Test Fc Name" (745 [49..68]); op2 Disable -> "Chat disabled" (745 [37]) | same | matched |
| changes window | first change: "Changes will take effect on your friends channel in the next 60 seconds." (type friendnotification); later changes in the window silent (753 [25] vs [75]); live 100 ticks later (753 [146]->[247], [375]->[475], 769 [222]->[323]) | `^friendchat_settings_delay` 100 | matched |
| flush | FULL to every member on a settings change (758 [126]); SINGLEUSER `add` on a rank change (754 [186]); a member now below the entry rank put out (769 [323]) | same | matched |
| enable | immediate: "Your chat-channel has now been enabled!", "Join your channel by clicking 'Join Chat' and typing: <name>" (753 [753]) | same | matched |
| disable | at the flush the owner "Your chat-channel has now been disabled!", a tick later every member "You have been removed from this channel." + empty FULL (753 [475..476], 752 [1288..1289]) | same | matched |
| set rank | `friendchat_setrank`; the changes line; `update_friendlist` row with the new rank straight away (759 [3957..3979]) | same; UPDATE_FRIENDLIST now carries the rank on every wire | matched |
| logout | "You have left the channel." + empty FULL; `chatchannel_on_logout` 1 (4567 [53613..53616]) | others told; flag set by `~friendchat_logout`; the leaver's own lines are not sent (the session is gone) | matched for everyone else |
| tabs | `if_buttonx side_channels:tab_N` -> varbit `side_channels_tab_selected` + `if_opensub side_channels:contents` (7 / 701 / 702 / 76) same tick; login mounts the saved tab (4567 [0], [1233..1236]) | `[if_button,side_channels:tab_N]`, `[if_open,side_channels]`; varp 1054 now `scope=perm` | matched |
| clan tabs | `clan_affinitystatus`=1, `clan_guest_affinitystatus`=1 (4567 [6]) | `~friendchat_login`; varp 3019 now `transmit=yes` (was never sent, so both tabs said "Connecting...") | matched |

## Named differences

- Member and friend rows carry an empty world name; Jagex sends "Old School 67". No
  clientscript the panels run reads it.
- A full channel (`^friendchat_member_max` 100, wiki) refuses the join silently: no recording
  shows the line.
- A refused kick (rank too low, a higher target) says nothing: only success is recorded.
- A new account's defaults (disabled, Anyone / Anyone / Only me) are unverified.
- `varclan_disable` in the login burst (4567 [0]) is not sent.

## Open

- **Clans are not built.** `//` and `///` lines (MESSAGE_PUBLIC type 3) are now dropped. They
  used to be said aloud. Clan channels, settings, ranks, guest channels, creation (705) and the
  clan interfaces (690..706) remain greenfield. Recordings: 111, 4237 and 4257 carry
  `clansettings_full` / `clanchannel_full` / `clanchannel_delta` / `message_clanchannel(_system)`.
  No session opens the creation panel 705. See `docs/clan_chat_server_reqs.md`.
- The settings, the prefix and the rejoin are saved (`[friendchat]`); ranks live with the
  friend lists, which are still in-memory only (docs/FRIENDS_PRIVATE_CHAT.md §6).
- Not yet driven in a live client.
