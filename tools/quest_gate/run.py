#!/usr/bin/env python3
"""Run one quest test per client process, headless, and collect its artefacts.

UNIMPLEMENTED -- owner: tests (docs/ARCHITECT.md).

The shape it must have, so that nothing about it is re-litigated later:

  one process per quest, never one process per two quests: a quest test leaves
  server saves, varps and a scene behind it, and the next quest starting from
  that is a test whose failures nobody can attribute.

  env, all of it private to the session directory:
    TORIRS_CONTENT_TEST=<session>        the virtual clock and the artefact dir
    TORIRS_QUEST_SCRIPT=<test .lua>      which quest
    TORIRS_PLUGINS=1
    TORIRS_PLUGIN_MANIFEST=script/plugins/quest_driver.ini
    TORIRS_PLUGIN_PREFS=<session>/plugin_prefs.ini
    TORIRS_PREFS=<session>/preferences.ini
    TORIRSSERVER_SAVES=<session>/saves
    TORIRSSERVER_STAFF_LEVEL=2
    TORIRSSERVER_HOME=3222,3218          skip Tutorial Island
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
    TORIRS_STDERR_UNBUFFERED=1 TORIRS_PLUGIN_LOG=1
  argv: --soft3d --window 765x503, manifest manifests/manifest_osrs239.ini
  rewritten to transport=embed the way tools/content_selftest.py:62-94 does.

  binary: OPT=1 EMBED_SERVER=1 PLATFORM_OBJ_BASE=build_questtest
          PLATFORM_TARGET=torirs_questtest -- its OWN objdir, because several
          sessions build from this checkout at once.

  output: build/quest_gate/<quest>/{ledger.tsv, shots/NN-name.png, result}

It exits non-zero until it is written: a runner that prints nothing and
returns 0 is a green gate that tests nothing, which is the one outcome worse
than a red one.
"""

import sys

print("tools/quest_gate/run.py: UNIMPLEMENTED (owner: tests)", file=sys.stderr)
sys.exit(2)
