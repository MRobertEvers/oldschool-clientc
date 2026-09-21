# quest-driver: design context (fixed decisions)

This file is the design the implementation plan (docs/QUEST_DRIVER_PLAN.md) was
written against. The plan is authoritative on mechanism; this file is the
"why" and the decisions that are not up for re-litigation.

## Goal
A quest test is a Lua script that plays a quest through the real client against
the embedded server, headless, and leaves behind a numbered screenshot for every
interaction plus a pass/fail verdict per step (a ledger). The script reads like
the play-through: talk to Hans, wait for the dialog, continue, choose, check the
varp, screenshot.

## Decisions (owner, 2026-09-18/19)
- One Lua coroutine per quest test, owned by a Lua plugin "quest-driver"
  (script/plugins/quest_driver.lua + script/plugins/quest_driver.ini), resumed by a
  C scheduler on the content-test pump (src/game/content_test.c, enabled by
  TORIRS_CONTENT_TEST; virtual clock, 20 ms per frame, 30 frames per server tick,
  settle predicates, PNG capture via App_RequestScreenshot). The mailbox stays for
  interactive use; the driver runs in-process because the events a quest test
  waits for happen between mailbox polls.
- The scheduler owns lua_resume. Scripts only get a C `await` primitive that
  yields. lcorolib stays OUT of lua_unity.c: the sandbox removed pcall on
  purpose (an instruction-budget error must not be catchable) and coroutine.resume
  would hand it back. Re-arm the budget hook with lua_sethook on the coroutine's
  own lua_State at EVERY resume (lua_newthread copies the hook only at creation).
- api.drive is a test-only module registered only when ContentTest_Enabled()
  (same gate as content_test.c); capability name "drive". It must be listed in
  script/plugins/plugin_api.meta.lua (the inventory test pins the surface) with
  a @testonly tag the check accepts.
- PRIMARY action: drive.click_minimenu(target, option) = project the target,
  hover, confirm the pickset holds it (else `covered`), right-click, find the row
  by ACTION ID + target identity (never by row text), left-click the row, so the
  client's own dispatcher (app_minimenu_run_option) runs: walk, latch, packet.
  drive.op(target, option) (fabricated one-row menu) is a LOGGED bypass, never
  the default; every use is a ledger note.
- Dialog clicks (continue, choose) drive the resume-pausebutton seam, because a
  chatmenu row carries no cache op (see plan D1).
- Every verb returns (result, detail); result in {ok, timeout, not_found,
  refused, covered, no_row, not_visible, closed, unsupported}. Every await has a
  deadline in server ticks. Awaits are EDGE + LEVEL: satisfied if the predicate
  already holds when registered, or when a matching event arrives and the
  predicate then holds.
- NO numeric interface/component ids and NO client op strings in the driver or
  in tests. Names come from: revconfig roles (profile data), content symbols via
  the embedded server (ToriRSServer_ContentSymbol), engine identities
  (REVCONFIG_MINIMENU_* action ids, inventory kinds), derived roles, or the test
  author's own data (option text, message substring). Never key on a lane name.
- Assume the embedded server and osrs239 only. Do not test other lanes now.
- inv_op `ok` means dispatched; effects are asserted separately.
- The test-only module may bypass api_tab_select's dispatch_event gate.
- chat.drain photographs every page by default (shots = false to skip).
- Ledger: <session>/ledger.tsv (header quest-ledger-v1; columns index, step,
  verdict, ticks, shots, detail; trailing summary row) plus a stderr mirror
  `QUEST <quest> PASS|FAIL <step> ticks=<n> shots=<a,b> [why=<text>]`.
  t.finish(code) ends the process with that code.
- Runner: tools/quest_gate/run.py <quest> launches one private-session process
  per quest (env: TORIRS_CONTENT_TEST=<session>, TORIRS_QUEST_SCRIPT=<lua>,
  TORIRS_PLUGINS=1, TORIRS_PLUGIN_MANIFEST=script/plugins/quest_driver.ini,
  TORIRS_PLUGIN_PREFS=<session>/plugin_prefs.ini, TORIRS_PREFS=<session>/preferences.ini,
  TORIRSSERVER_SAVES=<session>/saves, TORIRSSERVER_STAFF_LEVEL=2,
  SDL_VIDEODRIVER=dummy, SDL_AUDIODRIVER=dummy, TORIRS_STDERR_UNBUFFERED=1,
  TORIRS_PLUGIN_LOG=1; binary built with OPT=1 EMBED_SERVER=1
  PLATFORM_OBJ_BASE=build_questtest PLATFORM_TARGET=torirs_questtest; --soft3d
  --window 765x503), collects shots/NN-name.png and ledger.tsv under
  build/quest_gate/<quest>/. tools/quest_gate/gate.py is red on any FAIL row,
  missing/undersized shot, or duplicate shot MD5.
- Tests live in test/quests/<quest>.lua returning { id, fixture, setup = {cheats},
  run = function(t) ... end }; fixtures in test/quests/fixtures/*.ini (server
  saves; [varps] holds only scope=perm vars, which is exactly the quest set).

## Project rules that apply (see CLAUDE.md, which is authoritative)
- assert() contract violations, never return early; one assert per condition.
- Allocation failure is an assert.
- No `switch` inside a protothread body (PT_BEGIN..PT_END).
- Never mutate source in a shared tree to prove a test fails: use a throwaway
  worktree for mutation checks.
- Build with a private objdir/target (PLATFORM_OBJ_BASE/PLATFORM_TARGET) so
  concurrent builds never share objects.
