/*
 * Script container binding and the host command seam.
 *
 * This is the only file that knows both `struct ToriRSServer` and the VM. The
 * VM stays engine-agnostic — it holds active entities as void* and never
 * dereferences them — so everything that touches game state funnels through
 * ToriRSServer_ScriptCommand below.
 *
 * The dispatch contract, which this header used to state the other way round:
 * **a trigger with no script does nothing.** Resolution is the reference's
 * (`ScriptProvider.getByTrigger`) — exact type, then category, then the bare `_`
 * wildcard, and nothing after that — and that wildcard is the only fallback the
 * design has.
 *
 * What survives of the C that used to answer a missed trigger is enumerated in
 * `enum ToriRSServerFallback` and gated on `ToriRSServer_ScriptsFallback`, which refuses
 * the two cases `if( !run_trigger(...) )` could not tell apart: a script that
 * *aborted* (a bug in content, not a gap in it) and a server with no script pack
 * at all (in which nothing is a gap, because everything is). The old promise —
 * "a missing script leaves every call site doing exactly what it did before
 * scripts existed" — was right while scripts were an experiment and is a second,
 * silently disagreeing implementation of the game now that they are not.
 */

#include <dirent.h>
#include <assert.h>

#include "torirs_server.h"

#include "torirs_server_container.h"
#include "torirs_server_content.h"
#include "torirs_server_friends.h"
#include "torirs_server_ids.h"
#include "torirs_server_scene.h"
#include "torirs_server_session.h"
#include "torirs_server_shop.h"

#include "ss_meta.h"
#include "ss_opcode.h"
#include "ssvm.h"
#include "ssvm_provider.h"
#include "net/jbase37.h"

/* Derived from the `case SS_OP_*:` labels in this file and in the VM core.
 * See gen_opcode_coverage.py for why it is generated. */
#include "torirs_server_opcode_coverage.gen.h"

/* index-11 archive id -> duration in ms, decoded offline from the cache
 * because ToriRSServer has no live cache access at script-command time. See
 * gen_jingle_lengths.py. */
#include "torirs_server_jingle_lengths.gen.h"

#include <stddef.h>
#include <stdio.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* MinGW exposes stat but not the POSIX lstat name. Windows directory entries
 * cannot introduce the Unix symlink recursion this scan avoids with lstat, so
 * the ordinary metadata call is the equivalent operation for that lane. */
#if defined(_WIN32)
#define scripts_lstat stat
#else
#define scripts_lstat lstat
#endif

/*
 * ToriRSServer_SendSynthSound is defined in torirs_server_encode.c (WEAPON_FX.md §6).
 * It is declared here rather than in torirs_server.h because torirs_server.h is out of
 * this lane's file ownership for the weapon-FX port
 * (WEAPON_FX_PORT_QUEUE.md §1: lane C owns packetin.h, torirs_server_encode.c,
 * torirs_server_scripts.c, embed_test.c — not torirs_server.h, which another lane has
 * uncommitted changes in). Move this prototype into torirs_server.h next to its
 * siblings (ToriRSServer_SendCamShake, ToriRSServer_SendRunWeight) the next time
 * that file is touched for an unrelated reason.
 */
struct ToriRSServerPlayer;
void
ToriRSServer_SendSynthSound(
    struct ToriRSServerPlayer* player,
    int id,
    int loops,
    int delay);

/* ------------------------------------------------------------------ */
/* Coordinates                                                         */
/* ------------------------------------------------------------------ */

/*
 * RuneScript packs a coord into one int as
 * (level << 28) | ((mx * 64 + lx) << 14) | (mz * 64 + lz).
 *
 * The compiler emits coord literals this way, so the host has to read them the
 * same way or `p_teleport(0_50_50_0_0)` lands somewhere unrelated. Both halves
 * of that agreement live in this repo, which is exactly why it is worth
 * spelling out in one place.
 */

static int32_t
coord_pack(int level, int x, int z)
{
    return (int32_t)(((uint32_t)level << 28) | ((uint32_t)x << 14) | (uint32_t)z);
}

static int
coord_level(int32_t coord)
{
    return (int)(((uint32_t)coord >> 28) & 0x3);
}

static int
coord_x(int32_t coord)
{
    return (int)(((uint32_t)coord >> 14) & 0x3fff);
}

static int
coord_z(int32_t coord)
{
    return (int)((uint32_t)coord & 0x3fff);
}

/* ------------------------------------------------------------------ */
/* Container                                                           */
/* ------------------------------------------------------------------ */

/* The source extensions the pack is compiled from. */
static int
scripts_is_source(const char* name)
{
    const char* dot = strrchr(name, '.');

    if( !dot )
        return 0;
    return strcmp(dot, ".rs2") == 0 || strcmp(dot, ".constant") == 0 ||
           strcmp(dot, ".dbrow") == 0 || strcmp(dot, ".dbtable") == 0 ||
           strcmp(dot, ".varp") == 0;
}

/*
 * First source file newer than `pack_mtime`, depth-first, stopping at the first
 * hit. Returns 1 and fills `out_path` / `out_delta`, or 0.
 */
static int
scripts_scan_newer(
    const char* dir,
    time_t pack_mtime,
    char* out_path,
    size_t out_len,
    long* out_delta)
{
    DIR* dirp = opendir(dir);
    struct dirent* ent;
    int stale = 0;

    if( !dirp )
        return 0;

    while( !stale && (ent = readdir(dirp)) != NULL )
    {
        char child[1024];
        struct stat sbuf;

        if( ent->d_name[0] == '.' )
            continue; /* `.`, `..`, and dotfiles the compiler does not read */
        /* `build` holds the pack itself; comparing it to itself proves nothing. */
        if( strcmp(ent->d_name, "build") == 0 )
            continue;

        snprintf(child, sizeof(child), "%s/%s", dir, ent->d_name);
        if( scripts_lstat(child, &sbuf) != 0 )
            continue;

        if( S_ISDIR(sbuf.st_mode) )
        {
            stale = scripts_scan_newer(child, pack_mtime, out_path, out_len,
                                       out_delta);
        }
        else if( S_ISREG(sbuf.st_mode) && scripts_is_source(ent->d_name) &&
                 sbuf.st_mtime > pack_mtime )
        {
            snprintf(out_path, out_len, "%s", child);
            *out_delta = (long)(sbuf.st_mtime - pack_mtime);
            stale = 1;
        }
    }

    closedir(dirp);
    return stale;
}

/*
 * Is the compiled pack older than the content it was compiled from?
 *
 * This exists because a whole session was spent chasing a `[debugproc]` that
 * "did not work" and was correct the entire time: the pack is a separate build
 * from the binary, `run-live.sh` built only the binary, and the running server
 * loaded a script.dat from days earlier. Nothing anywhere said so. The C was
 * current, the scripts were stale, and every symptom pointed at the content.
 *
 * One source newer than `script.dat` is already proof the pack does not
 * describe the tree, so the walk stops at the first one and names it.
 *
 * **This used to be a shell pipeline** — `find | xargs stat | sort -rn | head -1`
 * through popen — which is how it came to hang a login. Three things were wrong
 * with it and all three mattered:
 *
 *   - `sort` cannot emit until its input closes, so `head -1` does not shorten
 *     anything: the pipeline walked the entire content tree and forked a `stat`
 *     per batch every single time, on a path a player is waiting on.
 *   - `head` exiting killed `sort` mid-write, which is the `sort: Broken pipe`
 *     that appeared in the server log with nothing to attribute it to.
 *   - `pclose` then blocks in `wait4` for every member of that pipeline. The
 *     login response has already gone out at this point, so the client sits on
 *     "Connecting to server..." with no error at either end — and the server
 *     looks idle at 0% CPU, because it is: it is waiting on `sort`.
 *
 * A directory walk that returns at the first hit has none of that, and it is a
 * diagnostic, so it must never be the most expensive thing in the function it
 * decorates.
 *
 * Deliberately a warning and not a refusal — a stale pack still runs, and
 * someone deliberately testing an old pack against new C should be able to.
 * The point is that they know they are doing it.
 */
static int
scripts_newer_than_pack(const char* dir, char* out_path, size_t out_len, long* out_delta)
{
    char pack[1024];
    char tree[1024];
    struct stat pack_st;
    char* slash;

    snprintf(pack, sizeof(pack), "%s/script.dat", dir);
    if( stat(pack, &pack_st) != 0 )
        return 0; /* no pack at all is the louder banner below */

    /* `dir` is <tree>/build; the sources are its parent. */
    snprintf(tree, sizeof(tree), "%s", dir);
    slash = strrchr(tree, '/');
    if( slash )
        *slash = '\0';
    else
        snprintf(tree, sizeof(tree), "%s", ".");

    if( scripts_scan_newer(tree, pack_st.st_mtime, out_path, out_len, out_delta) )
        return 1;

    /*
     * The allocation ledgers, which live outside the script tree. Compiled
     * bytecode carries resolved ids — `(table << 12) | column` constants, enum
     * and varp numbers — so a renumbered `pack/<ns>.alloc` re-points every one
     * of them at the wrong record while the script sources' mtimes say nothing
     * changed. The tree is `<content>/server/scripts`; the ledgers sit two
     * levels up in `<content>/pack`.
     */
    {
        char packdir[1024];
        DIR* dirp;
        struct dirent* ent;
        int stale = 0;

        snprintf(packdir, sizeof(packdir), "%s/../../pack", tree);
        dirp = opendir(packdir);
        if( !dirp )
            return 0;
        while( !stale && (ent = readdir(dirp)) != NULL )
        {
            const char* dot = strrchr(ent->d_name, '.');
            char child[1200];
            struct stat sbuf;

            if( !dot || strcmp(dot, ".alloc") != 0 )
                continue;
            snprintf(child, sizeof(child), "%s/%s", packdir, ent->d_name);
            if( scripts_lstat(child, &sbuf) != 0 )
                continue;
            if( sbuf.st_mtime > pack_st.st_mtime )
            {
                snprintf(out_path, out_len, "%s", child);
                *out_delta = (long)(sbuf.st_mtime - pack_st.st_mtime);
                stale = 1;
            }
        }
        closedir(dirp);
        return stale;
    }
}

int
ToriRSServer_ScriptsLoad(
    struct ToriRSServer* srv,
    const char* dir)
{
    struct SSVM_Error err;

    /*
     * Once per world, however many players log in.
     *
     * Both hosts call this from their login path, and reloading on a second
     * login frees the env out from under the *first* player's parked script —
     * a use-after-free whose symptom is a crash several ticks later, in
     * whichever conversation happened to be open. Reloading content while
     * somebody is logged in is not a thing this server supports; the honest
     * spelling of that is to refuse rather than to corrupt.
     */
    if( srv->scripts_ok )
        return 1;

    ToriRSServer_ScriptsFree(srv);

    srv->scripts = (struct SSVM_Provider*)calloc(1, sizeof(struct SSVM_Provider));
    srv->script_env = (struct SSVM_Env*)calloc(1, sizeof(struct SSVM_Env));
    assert(srv->scripts);
    assert(srv->script_env);

    SSVM_ErrorClear(&err);
    if( !SSVM_ProviderLoadDir(srv->scripts, dir, &err) )
    {
        /*
         * A banner, not a line, because of what it now means.
         *
         * This used to be a one-line warning about a supported mode: no pack,
         * every trigger falls through to C, the mock still plays. That mode is
         * gone — `ToriRSServer_ScriptsFallback` refuses every engine fallback when
         * `scripts_ok` is 0 — so a server that reaches here does almost nothing
         * at all. Which is the point: the alternative was a second game running
         * silently beside the one the content tree describes, discoverable only
         * by finding a behaviour where the two disagreed.
         */
        fprintf(stderr,
                "torirsserver: ============================================================\n"
                "torirsserver: NO SCRIPT PACK at %s\n"
                "torirsserver:   %s\n"
                "torirsserver: The game's behaviour is content, not C. Without the pack the\n"
                "torirsserver: engine's fallbacks stay OFF and almost nothing will work —\n"
                "torirsserver: no interactions, no buttons, no dialogue, no drops.\n"
                "torirsserver: Build it:  make -C src torirsserver-scripts\n"
                "torirsserver: ============================================================\n",
                dir, err.message);
        ToriRSServer_ScriptsFree(srv);
        return 0;
    }

    SSVM_EnvInit(srv->script_env, srv->scripts);
    SSVM_EnvBindHost(srv->script_env, srv, ToriRSServer_ScriptCommand);
    /* Fixed seed so a session replays identically, which every deterministic
     * test downstream depends on. */
    SSVM_EnvSeed(srv->script_env, 0x5eed1234u);

    srv->scripts_ok = 1;
    fprintf(stderr, "torirsserver: %d scripts loaded from %s\n", srv->scripts->loaded, dir);

    /*
     * ── RESERVED QUEUE SLOTS ─────────────────────────────────────────────
     *
     * `[ai_queue1,_]` is the engine-wide RETALIATION rung — `~npc_retaliate`
     * arms `npc_queue(1, …)` on the tick a player's hitsplat lands on any npc,
     * and the default body is `npc_setmode(opplayer2)`. Trigger dispatch is
     * exact-type-first, so `[ai_queue1,<type>]` does not ADD an ending to that
     * npc: it REPLACES what happens when that npc is hit.
     *
     * A binding there that deletes the npc therefore deletes it on the first
     * hitsplat — before `npc_death_step` can reach DEATH_ARRIVE, so its death
     * animation is never played and never sent. That is a monster that
     * vanishes the instant you attack it, and it cost days to find on the
     * Theatre's Nylocas Matomenos (and again on Verzik's phase-2 reds) because
     * every symptom points at the animation pipeline and none of it points
     * here. The twelve legitimate `[ai_queue1,<type>]` bindings in this tree
     * are all `npc_setmode(applayer2)` — retaliation OVERRIDES, which is what
     * the slot is for.
     *
     * Checked mechanically because the reasoning is not discoverable from the
     * symptom: an encounter that wants its own delayed step has nine other
     * queue slots and must use one of them.
     */
    {
        int bad = 0;

        for( int i = 0; i < srv->scripts->count; i++ )
        {
            const struct SSVM_Script* sc = &srv->scripts->scripts[i];
            int trigger;
            int subject;

            if( sc->op_count <= 0 || sc->lookup_key < 0 )
                continue;
            trigger = sc->lookup_key & 0xff;
            /* `subject` is the type/category half of the key; `_` (the default
             * rung) encodes as no subject and is exactly the binding that is
             * SUPPOSED to live here. */
            subject = sc->lookup_key >> 10;
            if( trigger != SS_TRIGGER_AI_QUEUE1 || subject == 0 )
                continue;
            for( int pc = 0; pc < sc->op_count; pc++ )
            {
                char type_name[128];
                const char* comma;
                int npc_type;

                if( sc->opcodes[pc] != SS_OP_NPC_DEL )
                    continue;
                /*
                 * Only for an npc a player can ATTACK. Queue 1 fires off a
                 * hitsplat, so an npc with no Attack option can never reach
                 * this binding by being hit — a corpse form, a fading rubble
                 * marker, a death bat. Ninety-five of these exist and every one
                 * of them is fine; the two that were not (`maiden_elemental`,
                 * Verzik's phase-2 reds) are exactly the two the team fights.
                 * `ToriRSServer_CombatAttackable` is the client's own minimenu
                 * test, so this cannot disagree with what is clickable.
                 */
                comma = sc->name ? strchr(sc->name, ',') : NULL;
                if( !comma )
                    break;
                snprintf(type_name, sizeof(type_name), "%s", comma + 1);
                {
                    char* close = strchr(type_name, ']');
                    if( close )
                        *close = '\0';
                }
                npc_type = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, type_name);
                if( npc_type < 0 || !ToriRSServer_CombatAttackable(npc_type) )
                    break;
                fprintf(stderr,
                        "torirsserver: RESERVED QUEUE SLOT: %s calls npc_del, and %s is "
                        "ATTACKABLE. Queue 1 is the retaliation rung "
                        "(`[ai_queue1,_]`), armed when a player HITS this npc — so "
                        "this deletes it on the first hitsplat, before its death "
                        "animation can play. It will vanish when attacked instead "
                        "of dying. Use another queue slot.\n",
                        sc->name ? sc->name : "?", type_name);
                bad++;
                break;
            }
        }
        if( bad )
        {
            fprintf(stderr,
                    "torirsserver: ============================================================\n"
                    "torirsserver: %d script(s) bind npc_del to the retaliation queue.\n"
                    "torirsserver: Every npc they name vanishes when attacked instead of\n"
                    "torirsserver: dying. Refusing to run — this is not survivable content.\n"
                    "torirsserver: ============================================================\n",
                    bad);
            ToriRSServer_ScriptsFree(srv);
            return 0;
        }
    }
    {
        char newer[1024] = { 0 };
        long delta = 0;

        if( scripts_newer_than_pack(dir, newer, sizeof(newer), &delta) )
        {
            fprintf(stderr,
                    "torirsserver: ============================================================\n"
                    "torirsserver: STALE SCRIPT PACK — the tree is newer than script.dat\n"
                    "torirsserver:   %s\n"
                    "torirsserver:   is %ld second(s) newer than the compiled pack.\n"
                    "torirsserver:   pack: %s\n"
                    "torirsserver: This server would be running content that does NOT match\n"
                    "torirsserver: the tree. A script you just edited is not the one that\n"
                    "torirsserver: would run.\n"
                    "torirsserver: Rebuild it:  ./tools/tob_build_packs.sh\n"
                    "torirsserver:          or: make -C src torirsserver-scripts\n"
                    "torirsserver: Deliberately testing an old pack against new C?\n"
                    "torirsserver:   TORIRSSERVER_ALLOW_STALE_SCRIPTS=1\n"
                    "torirsserver: ============================================================\n",
                    newer, delta, dir);
            /*
             * A REFUSAL, not a warning.
             *
             * This was a warning for exactly the reason the comment on
             * `scripts_newer_than_pack` gives — someone testing an old pack
             * against new C should be able to — and that reason is still
             * honoured, by an env var, because it is a deliberate act and
             * should have to be spelled.
             *
             * What it cannot be is the default. A banner in a wall of boot
             * output is not a guarantee; it is something to scroll past, and it
             * was scrolled past for an entire session while every symptom was
             * blamed on the content it was announcing was not loaded. The whole
             * cost of that mistake is paid at boot, once, by a `stat` walk that
             * stops at the first hit.
             *
             * Every consumer is covered by putting it here rather than in the
             * launchers: `ToriRSServer`, `ToriRSServer --selftest`, the embedded server
             * inside the client, and anything else that ever loads a pack.
             */
            if( !getenv("TORIRSSERVER_ALLOW_STALE_SCRIPTS") )
            {
                fprintf(stderr,
                        "torirsserver: refusing to run on a stale script pack.\n");
                exit(1);
            }
            fprintf(stderr,
                    "torirsserver: TORIRSSERVER_ALLOW_STALE_SCRIPTS=1 — continuing anyway.\n");
        }
    }
    /* Before anything runs: an opcode this tree needs and the engine lacks is a
     * fact about the tree, not about whichever player eventually triggers it. */
    ToriRSServer_ScriptsReportGaps(srv);
    /* And the second list of the same kind: which behaviours are still answered
     * from C when content binds nothing. It shrinks; that is the schedule. */
    ToriRSServer_ScriptsReportFallbacks(srv);
    /* And the third: the fallbacks above are C standing in for content that
     * has not arrived. This is the opposite — content that arrived and took a
     * verb the engine still answers. See ToriRSServer_ScriptsReportShadowedOps. */
    ToriRSServer_ScriptsReportShadowedOps(srv);
    /* And the fourth, which is about the pack rather than about the engine: a
     * `settimer`/`queue` argument whose "script id" is not a script id. That one
     * cannot wait for a session to notice it — the timer has to fire first, and
     * a quest-completion queue does not fire until somebody finishes the quest. */
    ToriRSServer_ScriptsReportScriptIdArgs(srv);
    return srv->scripts->loaded;
}

/* ------------------------------------------------------------------ */
/* Coverage                                                            */
/* ------------------------------------------------------------------ */

/** Does anything — the VM core or the host seam — implement this opcode? */
static int
opcode_implemented(int opcode)
{
    int lo = 0;
    int hi = TORIRSSERVER_OPCODE_COVERAGE_COUNT - 1;

    while( lo <= hi )
    {
        int mid = lo + ((hi - lo) / 2);
        int value = (int)TORIRSSERVER_OPCODE_COVERAGE[mid];

        if( value == opcode )
            return 1;
        if( value < opcode )
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return 0;
}

/*
 * Opcodes content uses that this engine deliberately does not implement.
 *
 * Same shape and same discipline as `k_engine_fallbacks` above: a row is a
 * *stated* gap with a reason, not a tolerance. The gate checks it BOTH ways —
 * every reported gap must be on this list, and nothing on this list may be
 * implemented — so a row cannot quietly outlive the reason it was written for,
 * and a brand-new gap still fails the build. That is the property a bare
 * threshold does not have.
 *
 * The bar for a row is not "hard". It is that implementing the opcode alone
 * would produce a **silent no-op**: a server that sends a packet nothing reads
 * is strictly worse than a reported gap, because the gap has a symptom and the
 * no-op does not (PORTING_GUIDE §4.3).
 */
static const struct
{
    const char* name;
    const char* blocked_on;
} k_opcode_gap_allowed[] = {
    /*
     * EMPTY, and that is the state this table is for -- every opcode the
     * content tree names is implemented.
     *
     * The last two rows were HINT_NPC and HINT_STOP, both of whose notes said
     * they would "delete themselves the day the client's hint-arrow render lane
     * lands". It landed (`app_overlay_build_hint_arrow` in src/app.c), the four
     * `hint_*` opcodes are implemented below, and the rows are gone.
     *
     * The sentinel is not a row: `name` is NULL, so the lookup below skips it
     * and no opcode can match. It exists because ISO C has no zero-length array
     * and this table is meant to be empty most of the time -- deleting the last
     * real row should not require also editing the loops that read it.
     */
    { NULL, NULL },
};

#define TORIRSSERVER_OPCODE_GAP_ALLOWED_COUNT                                                           \
    ((int)(sizeof(k_opcode_gap_allowed) / sizeof(k_opcode_gap_allowed[0])))

/** Is this opcode a *stated* gap? Matched by name, because the numbers move
 *  between revisions and the name is what the row is about. */
static int
opcode_gap_allowed(int opcode)
{
    const char* name = SSVM_OpcodeName(opcode);

    if( !name )
        return 0;
    for( int i = 0; i < TORIRSSERVER_OPCODE_GAP_ALLOWED_COUNT; i++ )
    {
        if( !k_opcode_gap_allowed[i].name )
            continue;
        if( strcmp(k_opcode_gap_allowed[i].name, name) == 0 )
            return 1;
    }
    return 0;
}

/**
 * Report every opcode the loaded content uses that nothing implements.
 *
 * At **load** time, not at call time, and that is the whole value. The VM
 * already complains when an unimplemented opcode is reached — but only if a
 * player happens to trigger that script, which for content behind a quest step
 * or a rare drop may be never. Answering the question up front turns "the
 * server has 155 of 396 opcodes" into the far more useful "this content tree
 * needs these eleven, here is the first script that wants each".
 *
 * That list is the work queue for moving the remaining C behaviour into
 * content: an opcode nothing asks for is not worth implementing, and one that
 * three scripts ask for is blocking three scripts.
 *
 * Returns the number of distinct missing opcodes.
 */
int
ToriRSServer_ScriptsReportGaps(struct ToriRSServer* srv)
{
    /* One bit per opcode, so each is reported once however many scripts want
     * it. opcode values run to 10003 (they are sparse, not dense), so this is
     * sized by the generated value limit rather than by the opcode count. */
    static uint8_t seen[TORIRSSERVER_OPCODE_VALUE_LIMIT];
    /* Static, not automatic: 10,004 pointers is 80 KB, which is most of a
     * default thread stack. */
    static const char* first_user[TORIRSSERVER_OPCODE_VALUE_LIMIT];
    int missing = 0;

    if( !srv->scripts_ok || !srv->scripts )
        return 0;

    memset(seen, 0, sizeof(seen));
    memset(first_user, 0, sizeof(first_user));

    for( int i = 0; i < srv->scripts->count; i++ )
    {
        const struct SSVM_Script* script = &srv->scripts->scripts[i];

        /* Absent slots are zeroed; op_count marks them. */
        if( script->op_count <= 0 || !script->opcodes )
            continue;

        for( int op = 0; op < script->op_count; op++ )
        {
            int opcode = (int)script->opcodes[op];

            if( opcode < 0 || opcode >= TORIRSSERVER_OPCODE_VALUE_LIMIT )
                continue;
            if( seen[opcode] || opcode_implemented(opcode) )
                continue;
            seen[opcode] = 1;
            first_user[opcode] = script->name ? script->name : "?";
            if( !opcode_gap_allowed(opcode) )
                missing++;
        }
    }

    /*
     * The inverse check, and the reason this is a list rather than a number.
     *
     * A row whose opcode HAS been implemented is a stale statement about the
     * engine, and stale statements are how `k_engine_npc_verbs` came to claim a
     * verb dispatch had stopped honouring. Counted into the return value so it
     * fails the same gate: the fix is to delete the row, which is one line.
     */
    for( int i = 0; i < TORIRSSERVER_OPCODE_GAP_ALLOWED_COUNT; i++ )
    {
        for( int opcode = 0; opcode < TORIRSSERVER_OPCODE_VALUE_LIMIT; opcode++ )
        {
            const char* name = SSVM_OpcodeName(opcode);

            if( !k_opcode_gap_allowed[i].name )
                continue;
            if( !name || strcmp(name, k_opcode_gap_allowed[i].name) != 0 )
                continue;
            if( !opcode_implemented(opcode) )
                continue;
            fprintf(stderr,
                    "torirsserver: %s is implemented but still listed as a stated gap — "
                    "delete its row in k_opcode_gap_allowed\n",
                    name);
            missing++;
        }
    }

    for( int opcode = 0; opcode < TORIRSSERVER_OPCODE_VALUE_LIMIT; opcode++ )
    {
        if( !seen[opcode] || !opcode_gap_allowed(opcode) )
            continue;
        fprintf(stderr, "torirsserver: stated gap %-20s (first wanted by %s)\n",
                SSVM_OpcodeName(opcode), first_user[opcode]);
    }

    if( missing == 0 )
        return 0;

    fprintf(stderr, "torirsserver: %d opcode(s) this content uses are not implemented:\n",
            missing);
    for( int opcode = 0; opcode < TORIRSSERVER_OPCODE_VALUE_LIMIT; opcode++ )
    {
        if( !seen[opcode] || opcode_gap_allowed(opcode) )
            continue;
        fprintf(stderr, "  %-28s first wanted by %s\n", SSVM_OpcodeName(opcode),
                first_user[opcode]);
    }
    return missing;
}

/*
 * Remove every host-side owner of one parked state, then return it to the VM
 * pool while its environment is still alive.
 *
 * A state normally has exactly one owner, but clearing every matching slot
 * makes teardown safe even if an earlier parking bug left two references to
 * it.  In particular, world_delay states are not player- or npc-owned: merely
 * clearing active_script cannot reach them.
 */
static void
discard_parked_state(
    struct ToriRSServer* srv,
    struct SSVM_State* state)
{
    if( !state )
        return;

    for( int i = 0; i < TORIRSSERVER_PLAYER_MAX; i++ )
    {
        if( srv->players[i].active_script == state )
        {
            srv->players[i].active_script = NULL;
            srv->players[i].resume_button_count = 0;
        }
    }
    for( int i = 0; i < TORIRSSERVER_NPC_MAX; i++ )
    {
        if( srv->npcs[i].active_script == state )
            srv->npcs[i].active_script = NULL;
    }
    for( int i = 0; i < TORIRSSERVER_WORLD_QUEUE_MAX; i++ )
    {
        if( srv->world_queue[i].state == state )
        {
            srv->world_queue[i].state = NULL;
            srv->world_queue[i].delay = 0;
            srv->world_queue[i].active = 0;
        }
    }

    SSVM_StateRelease(state);
}

void
ToriRSServer_ScriptsFree(struct ToriRSServer* srv)
{
    /*
     * The parked script points into the env that is about to be freed.
     *
     * Nothing noticed while every trigger ran to completion; a conversation
     * that blocks on p_pausebutton is the first content that leaves a state
     * parked across a reload, and the next tick would then walk a freed
     * pointer. The resume buttons go with it — they only mean anything to the
     * script that registered them.
     *
     * Every owner class, not only the active player: this runs at load and at
     * shutdown, when there is no "whose turn it is" at all.  A crop respawn is
     * parked in world_queue, for example; leaving that slot live resumes a
     * state whose env and provider were freed here.  Adding an unrelated proc
     * merely changed the allocator layout enough to turn that latent UAF into
     * an invalid host-command call during a later world tick.
     */
    for( int i = 0; i < TORIRSSERVER_PLAYER_MAX; i++ )
    {
        discard_parked_state(srv, srv->players[i].active_script);
        srv->players[i].resume_button_count = 0;
    }
    for( int i = 0; i < TORIRSSERVER_NPC_MAX; i++ )
        discard_parked_state(srv, srv->npcs[i].active_script);
    for( int i = 0; i < TORIRSSERVER_WORLD_QUEUE_MAX; i++ )
        discard_parked_state(srv, srv->world_queue[i].state);

    if( srv->script_env )
    {
        SSVM_EnvFree(srv->script_env);
        free(srv->script_env);
        srv->script_env = NULL;
    }
    if( srv->scripts )
    {
        SSVM_ProviderFree(srv->scripts);
        free(srv->scripts);
        srv->scripts = NULL;
    }
    srv->scripts_ok = 0;
}

/*
 * A script the engine starts *by id* must be of the kind whose slot held the id.
 *
 * The number in a timer or queue slot is content's: it writes `settimer(poison,
 * 30)` and the compiler turns the name into an id. That name is resolved against
 * several namespaces, and when one of the others also spells it the wrong number
 * used to be emitted with nothing to catch it — `poison` is obj 273 as well as
 * `[timer,poison]`, so the engine armed a timer on script 273, which is
 * `[label,woman_im_looking_for_a_lady]`, and every 30 ticks the player got a
 * dialogue box from a quest they were nowhere near. Which line it was moved
 * whenever the tree grew, because the only thing choosing it was an unrelated
 * namespace's allocation order.
 *
 * `ssc_compile.c`'s `arg_is_script_name` is the fix; this is the guard, and it is
 * here rather than only there because the id survives a recompile in a save, a
 * test fixture or a hand-written packet, and because the failure is otherwise
 * indistinguishable from content doing it on purpose. Refuse rather than warn:
 * running an arbitrary script is the bug, not the report of it.
 *
 * `expect` is a comma-separated list of trigger words, matched against the word
 * between the brackets in `"[timer,poison]"`.
 */
static int
script_kind_matches(const struct SSVM_Script* script, const char* expect)
{
    const char* name = script->name;
    const char* comma;
    size_t word;

    if( !name || name[0] != '[' )
        return 1; /* nothing to check against */
    comma = strchr(name, ',');
    if( !comma )
        return 1;
    word = (size_t)(comma - (name + 1));

    for( const char* p = expect; *p; )
    {
        const char* end = strchr(p, ',');
        size_t len = end ? (size_t)(end - p) : strlen(p);

        if( len == word && strncmp(p, name + 1, len) == 0 )
            return 1;
        p = end ? end + 1 : p + len;
    }
    return 0;
}

static int
script_kind_allowed(const struct SSVM_Script* script, const char* expect)
{
    if( script_kind_matches(script, expect) )
        return 1;
    fprintf(stderr,
            "torirsserver: refusing to start %s by id %d — the slot holds a %s script; "
            "the id in it is not a script id\n",
            script->name, script->id, expect);
    return 0;
}

/*
 * Forget every parked-slot reference to this state, without ending it.
 *
 * A resumed script does not have to suspend the same way twice, and where it
 * parks the second time is not where it parked the first. `world_delay` inside
 * a proc that an earlier `p_delay` had parked on a player moves the state to
 * the world queue — and the player's slot, left pointing at it, holds the
 * player's one script seat for the whole of the delay.
 *
 * Firemaking is where that bit: `~push_player` p_delays, and on resume
 * `~firemaking_success` `world_delay`s for the fire's ~100-200 tick lifetime.
 * For all of those ticks the player looked busy, so every later `~push_player`
 * was dropped by the one-parked-script rule below and no fire was ever lit
 * again — "dropping [proc,push_player], which suspended while
 * [proc,firemaking_success] waits", once per attempt.
 *
 * The reference has the same clear: `Player.executeScript` assigns
 * `this.activeScript = script` only for SUSPENDED/PAUSEBUTTON/COUNTDIALOG and
 * sets it to null on WORLD_SUSPENDED and NPC_SUSPENDED.
 *
 * Searched rather than addressed: a state can be parked on any player, and the
 * one whose turn it is now is not necessarily the one it belongs to — a
 * world-queued script resumes in phase 1, before anybody's turn.
 */
static void
unpark(struct ToriRSServer* srv, struct SSVM_State* state)
{
    for( int i = 0; i < TORIRSSERVER_PLAYER_MAX; i++ )
    {
        if( srv->players[i].active_script == state )
            srv->players[i].active_script = NULL;
    }
    for( int i = 0; i < TORIRSSERVER_NPC_MAX; i++ )
    {
        if( srv->npcs[i].active_script == state )
            srv->npcs[i].active_script = NULL;
    }
}

/* Release a state and clear whichever slot was holding it. */
static void
release_parked(struct ToriRSServer* srv, struct SSVM_State* state)
{
    unpark(srv, state);
    SSVM_StateRelease(state);
}

void
ToriRSServer_ScriptsReleaseState(
    struct ToriRSServer* srv,
    struct SSVM_State* state)
{
    if( state )
        release_parked(srv, state);
}

/*
 * `Player.canAccess()` — may the engine hand this player a script right now?
 *
 * The reference is `!this.protect && !(this.delayed || containsModalInterface())`.
 * `protect` has no equivalent here: this engine's one-parked-script rule
 * (`run_or_park`) stands in for it, which is a stated divergence rather than an
 * omission — see osrs230_mockserver.md §3.19.
 *
 * This is what makes a queue a queue. Without it a `[queue]` armed by a dialogue
 * ran *through* the dialogue, and a normal timer fired while the player was
 * `p_delay`ed.
 */
static int
player_can_access(struct ToriRSServer* srv)
{
    struct ToriRSServerPlayer* player = srv->active_player;

    if( srv->tick < player->delayed_until )
        return 0;
    if( player->mainmodal_group > 0 || player->chatmodal_group > 0 )
        return 0;
    return 1;
}

/*
 * Report a script abort, once per site.
 *
 * Two `fprintf(stderr)` calls stood here, and that was the single most expensive
 * thing the embedded server did. It runs on the host's frame thread, where one
 * unbuffered stderr write costs about 6 ms on Windows (see
 * `app_world_spawn_npc_now`) — and a script that aborts aborts *every* time it
 * is dispatched, because nothing about the dispatch changed. A stale db table id
 * in `~player_combat_stat` therefore cost ~20 ms of report on every swing, which
 * is a dropped frame per attack. The abort was a content bug; the stutter was
 * this.
 *
 * So: one buffer, one write, and only the first time a given script dies at a
 * given instruction. The same script aborting at a different pc is a different
 * bug and still prints. The cap bounds the tracking itself — a server with that
 * many distinct dying scripts has already said everything it has to say.
 */
enum
{
    ABORT_SITE_MAX = 128
};

static struct
{
    const struct SSVM_Script* script;
    int pc;
} g_abort_sites[ABORT_SITE_MAX];
static int g_abort_site_count;
static int g_abort_sites_full;

static void
report_abort(struct SSVM_State* state)
{
    /* SSVM_Backtrace's own buffer is 1 KB and already ends in a newline, so the
     * context line appends to it and the pair leaves as one write. */
    char report[1400];

    for( int i = 0; i < g_abort_site_count; i++ )
        if( g_abort_sites[i].script == state->script &&
            g_abort_sites[i].pc == state->err.offset )
            return;

    if( g_abort_site_count >= ABORT_SITE_MAX )
    {
        if( g_abort_sites_full )
            return;
        g_abort_sites_full = 1;
        fprintf(stderr, "torirsserver: %d scripts have aborted at distinct sites; the rest "
                        "are silent\n",
                ABORT_SITE_MAX);
        return;
    }
    g_abort_sites[g_abort_site_count].script = state->script;
    g_abort_sites[g_abort_site_count].pc = state->err.offset;
    g_abort_site_count++;

    snprintf(report, sizeof(report),
             "torirsserver: %storirsserver: abort context host_tag=%d pointers=0x%x active_npc=%d\n",
             SSVM_Backtrace(state), (int)state->host_tag, (unsigned)state->pointers,
             (state->pointers & SSVM_PTR_ACTIVE_NPC) != 0);
    fputs(report, stderr);
}

/**
 * Run a state, and park it wherever its suspend status says it belongs.
 *
 * Returns 1 when a script ran (finished or parked), 0 when it aborted — the
 * caller treats 0 as "nothing happened" and falls back to its C behaviour.
 */
static int
run_or_park(struct ToriRSServer* srv, struct SSVM_State* state)
{
    int was_parked = srv->active_player->active_script == state;
    enum SSVM_Exec status = SSVM_Execute(state);

    /*
     * A parked script that finishes closes the chatbox behind it.
     *
     * `Player.executeScript`: when the state that just ended *was* the player's
     * active one, it clears the resume buttons and — if no MAIN modal is up —
     * calls `closeModal(false)`. This engine never did, which was invisible
     * until `canAccess()` existed and then fatal: a conversation that ended on a
     * `~mesbox` left the chat interface mounted, so the player stayed busy and
     * every later queue entry and normal timer was held forever.
     *
     * `false` is the whole reason `close_modal_ex` exists — a script may
     * `weakqueue` on its last line, and the automatic close must not discard it.
     */
    if( was_parked && (status == SSVM_FINISHED || status == SSVM_ABORTED) )
    {
        struct ToriRSServerPlayer* owner = srv->active_player;

        if( status == SSVM_ABORTED )
            report_abort(state);
        release_parked(srv, state);
        owner->resume_button_count = 0;
        if( owner->mainmodal_group <= 0 )
            ToriRSServer_WorldCloseModalEx(srv, 0);
        return status == SSVM_FINISHED;
    }

    switch( status )
    {
    case SSVM_FINISHED:
        release_parked(srv, state);
        return 1;

    case SSVM_ABORTED:
        report_abort(state);
        release_parked(srv, state);
        return 0;

    case SSVM_SUSPENDED:
    case SSVM_PAUSEBUTTON:
    case SSVM_COUNTDIALOG:
    case SSVM_NAMEDIALOG:
        /* One parked script per player. A second would need somewhere to live
         * and, more importantly, would let two scripts interleave writes to the
         * same player — the reference has the same single slot. */
        if( srv->active_player->active_script && srv->active_player->active_script != state )
        {
            fprintf(stderr,
                    "torirsserver: dropping %s, which suspended while %s waits\n",
                    state->script ? state->script->name : "?",
                    srv->active_player->active_script->script
                        ? srv->active_player->active_script->script->name
                        : "?");
            SSVM_StateRelease(state);
            return 0;
        }
        /* It may have been parked on an npc last time round — see `unpark`. */
        unpark(srv, state);
        srv->active_player->active_script = state;
        return 1;

    case SSVM_NPC_SUSPENDED:
    {
        int slot = (int)state->host_tag - 1;

        if( slot < 0 || slot >= TORIRSSERVER_NPC_MAX )
        {
            fprintf(stderr, "torirsserver: npc_delay with no active npc\n");
            SSVM_StateRelease(state);
            return 0;
        }
        if( srv->npcs[slot].active_script && srv->npcs[slot].active_script != state )
        {
            fprintf(stderr, "torirsserver: npc %d already has a parked script\n", slot);
            SSVM_StateRelease(state);
            return 0;
        }
        unpark(srv, state);
        srv->npcs[slot].active_script = state;
        return 1;
    }

    case SSVM_WORLD_SUSPENDED:
    {
        int32_t delay = 0;

        /* world_delay leaves its argument on the stack for the parking code to
         * take, which is what the reference does. Popping it in the command
         * would work equally well, but matching keeps content portable. */
        SSVM_PopInt(state, &delay);
        /* The world queue owns it from here, so whoever was holding a seat for
         * it must let go — see `unpark`. This is the firemaking fix. */
        unpark(srv, state);
        for( int i = 0; i < TORIRSSERVER_WORLD_QUEUE_MAX; i++ )
        {
            if( srv->world_queue[i].active )
                continue;
            srv->world_queue[i].active = 1;
            srv->world_queue[i].state = state;
            srv->world_queue[i].delay = delay + 1;
            return 1;
        }
        fprintf(stderr, "torirsserver: world queue full, dropping a script\n");
        SSVM_StateRelease(state);
        return 0;
    }

    default:
        SSVM_StateRelease(state);
        return 0;
    }
}

/**
 * Start a script by id, with an optional argument, on behalf of the player.
 *
 * `protect` is `Player.executeScript`'s second argument, and it is not decorative:
 * a SOFT timer runs *without* protected access precisely because it is allowed to
 * run while the player is busy, and giving it the pointer would let it do things a
 * busy player must not be doing. Everything else the engine starts is protected.
 */
static int
run_script_id(
    struct ToriRSServer* srv,
    int script_id,
    const int32_t* args,
    int argc,
    int npc_slot,
    int protect,
    const char* expect)
{
    const struct SSVM_Script* script = SSVM_ProviderGet(srv->scripts, script_id);
    struct SSVM_State* state;
    int bind;

    if( !script )
        return 0;
    if( !script_kind_allowed(script, expect) )
        return 0;

    /* Bind what the script asked for rather than what the caller happened to
     * have — the reference is equally forgiving, since its setupNewScript pops
     * exactly int_arg_count and leaves any surplus alone. A script that declares
     * MORE than the caller states is the one real error: those parameters would
     * read uninitialised. */
    if( script->string_arg_count > 0 )
    {
        fprintf(stderr, "torirsserver: %s declares string arguments the engine cannot supply\n",
                script->name);
        return 0;
    }
    if( script->int_arg_count > argc )
    {
        fprintf(stderr, "torirsserver: %s declares %d int argument(s), the caller stated %d\n",
                script->name, (int)script->int_arg_count, argc);
        return 0;
    }
    bind = script->int_arg_count;

    state = SSVM_StateAlloc(srv->script_env, script, bind > 0 ? args : NULL, bind, NULL, 0);
    if( !state )
        return 0;
    state->last_int = argc > 0 ? args[0] : 0;

    SSVM_SetActive(state, SSVM_ENT_PLAYER, SSVM_PRIMARY, srv->active_player);
    if( protect )
        SSVM_PointerAdd(state, SSVM_PTR_PROTECTED_PLAYER);
    if( npc_slot >= 0 && npc_slot < TORIRSSERVER_NPC_MAX && srv->npcs[npc_slot].active )
    {
        SSVM_SetActive(state, SSVM_ENT_NPC, SSVM_PRIMARY, &srv->npcs[npc_slot]);
        state->host_tag = npc_slot + 1;
    }
    return run_or_park(srv, state);
}

/* ------------------------------------------------------------------ */
/* Resuming                                                            */
/* ------------------------------------------------------------------ */

/*
 * Re-arm the VM's ACTIVE_NPC pointer bit from the slot parked in `host_tag`.
 *
 * LostCity keeps `_activeNpc` on the same ScriptState across p_pausebutton /
 * p_delay; resume just re-executes that state. Here the durable identity is the
 * slot in `host_tag` (a raw pointer would dangle if the npc despawned), and the
 * bit the VM's require table checks is separate. Without this, a conversation
 * that parks on ~chatplayer after a ~p_choice and then resumes into ~chatnpc
 * aborts on NPC_TYPE even though the slot is still valid — the bit was set at
 * trigger entry and is not guaranteed to still be set when the state wakes.
 */
static void
rebind_active_npc(
    struct ToriRSServer* srv,
    struct SSVM_State* state)
{
    int slot = (int)state->host_tag - 1;

    if( slot < 0 || slot >= TORIRSSERVER_NPC_MAX )
        return;
    if( !srv->npcs[slot].active )
        return;
    SSVM_SetActive(state, SSVM_ENT_NPC, SSVM_PRIMARY, &srv->npcs[slot]);
}

void
ToriRSServer_ScriptsResumePlayer(struct ToriRSServer* srv)
{
    struct SSVM_State* state = srv->active_player->active_script;

    if( !state || !srv->scripts_ok )
        return;
    /* A resume-button or count-dialog wait is released by client input, not by
     * the clock, so the tick must leave those alone. */
    if( state->execution != SSVM_SUSPENDED )
        return;
    if( srv->tick < srv->active_player->delayed_until )
        return;

    rebind_active_npc(srv, state);
    run_or_park(srv, state);
}

void
ToriRSServer_ScriptsResumeNpc(
    struct ToriRSServer* srv,
    int slot)
{
    struct ToriRSServerNpc* npc;
    int resume_loot_drop;

    if( !srv->scripts_ok || slot < 0 || slot >= TORIRSSERVER_NPC_MAX )
        return;
    npc = &srv->npcs[slot];
    if( !npc->active_script || srv->tick < npc->delayed_until )
        return;

    /* npc_delay can split [ai_queue3] before its obj_add. Kill attribution is
     * intentionally scoped to executing that death state: leaving the global
     * flag armed for the delay would misattribute unrelated ground spawns. */
    resume_loot_drop = npc->death_stage == TORIRSSERVER_DEATH_REAP &&
                       npc->loot_credit_event_id > 0;
    if( resume_loot_drop )
    {
        assert(!srv->loot_credit_armed);
        srv->loot_credit_armed = 1;
        srv->loot_credit_npc_type = npc->loot_credit_npc_type;
        srv->loot_credit_event_id = npc->loot_credit_event_id;
        memcpy(srv->loot_credit_players, npc->death_credit_players,
               sizeof(srv->loot_credit_players));
    }
    run_or_park(srv, npc->active_script);
    if( resume_loot_drop )
    {
        srv->loot_credit_armed = 0;
        memset(srv->loot_credit_players, 0, sizeof(srv->loot_credit_players));
        if( !npc->active_script )
        {
            memset(npc->death_credit_players, 0, sizeof(npc->death_credit_players));
            npc->loot_credit_event_id = 0;
            npc->loot_credit_npc_type = 0;
        }
    }
}

void
ToriRSServer_ScriptsResumeWorld(struct ToriRSServer* srv)
{
    if( !srv->scripts_ok )
        return;

    for( int i = 0; i < TORIRSSERVER_WORLD_QUEUE_MAX; i++ )
    {
        struct SSVM_State* state;

        if( !srv->world_queue[i].active )
            continue;
        if( --srv->world_queue[i].delay > 0 )
            continue;

        /* Unlink before running: the script may world_delay again, and it has
         * to be able to claim a free slot — possibly this one. */
        state = srv->world_queue[i].state;
        srv->world_queue[i].active = 0;
        srv->world_queue[i].state = NULL;
        run_or_park(srv, state);
    }
}

/** One pass of the queue, over the entries of one kind-set. */
static void
drain_queue(
    struct ToriRSServer* srv,
    int weak)
{
    for( int i = 0; i < TORIRSSERVER_QUEUE_MAX; i++ )
    {
        struct ToriRSServerQueued* entry = &srv->active_player->queue[i];
        int script_id;
        int32_t args[TORIRSSERVER_QUEUE_ARG_MAX];
        int argc;

        if( !entry->active )
            continue;
        if( (entry->kind == TORIRSSERVER_QUEUE_WEAK) != (weak != 0) )
            continue;

        /*
         * Decrement unconditionally and gate only the *run*, which is what
         * `Player.processQueue` does: `const delay = request.delay--; if
         * (this.canAccess() && delay <= 0)`. The difference is visible — an
         * entry that came due while the player was busy fires on the first tick
         * after access returns, rather than restarting its wait.
         */
        entry->delay--;
        if( entry->delay > 0 )
            continue;
        if( !player_can_access(srv) )
        {
            if( getenv("TORIRS_ANIM_DEBUG") )
                fprintf(
                    stderr,
                    "queue: script=%d BLOCKED tick=%d delayed_until=%d mainmodal=%d "
                    "chatmodal=%d\n",
                    entry->script_id,
                    srv->tick,
                    srv->active_player->delayed_until,
                    srv->active_player->mainmodal_group,
                    srv->active_player->chatmodal_group);
            continue;
        }

        script_id = entry->script_id;
        memcpy(args, entry->args, sizeof(args));
        argc = entry->argc;
        entry->active = 0;
        if( getenv("TORIRS_ANIM_DEBUG") )
        {
            const struct SSVM_Script* qs = SSVM_ProviderGet(srv->scripts, script_id);
            fprintf(stderr, "queue: script=%d (%s) FIRE tick=%d\n", script_id,
                    qs && qs->name ? qs->name : "?", srv->tick);
        }
        run_script_id(srv, script_id, args, argc, -1, 1, "queue");
    }
}

void
ToriRSServer_ScriptsClearWeakQueue(struct ToriRSServerPlayer* player)
{
    for( int i = 0; i < TORIRSSERVER_QUEUE_MAX; i++ )
    {
        if( player->queue[i].active && player->queue[i].kind == TORIRSSERVER_QUEUE_WEAK )
            player->queue[i].active = 0;
    }
}

void
ToriRSServer_ScriptsProcessQueues(struct ToriRSServer* srv)
{
    if( !srv->scripts_ok )
        return;

    /*
     * A STRONG entry anywhere in the queue closes whatever modal is up *before*
     * the drain, so that its own entry passes the access check on the tick it is
     * due. That is the kind's entire difference (`Player.processQueues`), and it
     * is why the scan runs even when the strong entry is not yet due.
     */
    for( int i = 0; i < TORIRSSERVER_QUEUE_MAX; i++ )
    {
        if( srv->active_player->queue[i].active &&
            srv->active_player->queue[i].kind == TORIRSSERVER_QUEUE_STRONG )
        {
            ToriRSServer_WorldCloseModal(srv);
            break;
        }
    }

    /* Primary queue, then the weak one — `processQueue()` then
     * `processWeakQueue()`, which is the reference's order and matters when a
     * primary entry closes a modal the weak entries were waiting behind. */
    drain_queue(srv, 0);
    drain_queue(srv, 1);
}

void
ToriRSServer_ScriptsProcessTimers(struct ToriRSServer* srv)
{
    /*
     * Two passes, NORMAL then SOFT, as `World.processPlayers` calls it.
     *
     * The two differ in both directions and one `case` used to serve both:
     * a SOFT timer runs while the player is busy and *without* protected access;
     * a NORMAL one needs `canAccess()` and runs *with* it. A test that only asked
     * "did a timer fire" would stay green for either half being wrong.
     */
    static const int k_order[2] = { TORIRSSERVER_TIMER_NORMAL, TORIRSSERVER_TIMER_SOFT };

    if( !srv->scripts_ok )
        return;

    for( int pass = 0; pass < 2; pass++ )
    {
        int type = k_order[pass];

        for( int i = 0; i < TORIRSSERVER_TIMER_MAX; i++ )
        {
            struct ToriRSServerTimer* timer = &srv->active_player->timers[i];

            /*
             * No lower bound on the interval, and that is the reference's shape,
             * not an omission: `Player.processTimers` tests only
             * `World.currentTick >= timer.clock + timer.interval`, so an
             * interval of 0 is a timer that fires on *every* tick rather than a
             * stopped one. `cleartimer` is the only thing that stops a timer.
             *
             * It is reachable from real content, not a theoretical case: the
             * reference arms `settimer(agilityarena_pillar,
             * sub(%agilityarena_next_pillar_time, map_clock))`, which is 0 or
             * negative the moment that deadline has passed. An `interval <= 0`
             * guard here left such a timer permanently inert while still holding
             * its slot and still answering `gettimer` — set, and never firing.
             */
            if( !timer->active || timer->type != type )
                continue;
            /* Absolute, not a countdown: `clock` is the tick it was armed or
             * last fired at, which is also what `gettimer` returns. */
            if( srv->tick < timer->clock + timer->interval )
                continue;
            if( type == TORIRSSERVER_TIMER_NORMAL && !player_can_access(srv) )
                continue;
            timer->clock = srv->tick;
            run_script_id(srv, timer->script_id, NULL, 0, -1, type == TORIRSSERVER_TIMER_NORMAL,
                          "timer,softtimer");
        }
    }
}

void
ToriRSServer_ScriptsProcessWalktrigger(struct ToriRSServer* srv)
{
    struct ToriRSServerPlayer* player;
    int script_id;

    assert(srv);
    player = srv->active_player;
    assert(player);

    /* LostCity Player.processWalktrigger: fire when armed, not delayed, and not
     * mid protected-access script. Clear before run so the script must re-arm. */
    if( !srv->scripts_ok )
        return;
    if( player->walktrigger < 0 )
        return;
    if( srv->tick < player->delayed_until )
        return;
    if( player->active_script )
        return;

    script_id = player->walktrigger;
    player->walktrigger = -1;
    run_script_id(srv, script_id, NULL, 0, -1, 1, "walktrigger");
}

int
ToriRSServer_ScriptsResumeButton(
    struct ToriRSServer* srv,
    int component_uid)
{
    struct ToriRSServerPlayer* player = srv->active_player;
    struct SSVM_State* state = player->active_script;

    if( !srv->scripts_ok || !state )
        return 0;
    if( state->execution != SSVM_PAUSEBUTTON )
        return 0;

    /* Only a button the script registered may release it. Anything else is a
     * click on some other interface and must leave the script parked — which is
     * also why the uid has to survive the wire at full width. */
    for( int i = 0; i < player->resume_button_count; i++ )
    {
        if( player->resume_buttons[i] != component_uid )
            continue;
        player->last_com = component_uid;
        player->resume_button_count = 0;
        rebind_active_npc(srv, state);
        return run_or_park(srv, state);
    }
    return 0;
}

int
ToriRSServer_ScriptsCloseDialogue(struct ToriRSServer* srv)
{
    struct ToriRSServerPlayer* player;
    struct SSVM_State* state;

    assert(srv);
    if( !srv->active_player )
        return 0;
    player = srv->active_player;
    state = player->active_script;
    if( !state )
        return 0;
    if( state->execution != SSVM_PAUSEBUTTON &&
        state->execution != SSVM_COUNTDIALOG &&
        state->execution != SSVM_NAMEDIALOG )
        return 0;

    /* The conversation ends here rather than resuming: the script's next
     * statement is whatever followed the ~chatnpc, and running it after the
     * player has walked off would put the *rest* of the dialogue on screen one
     * page at a time with nobody to talk to. */
    release_parked(srv, state);
    player->resume_button_count = 0;
    return 1;
}

int
ToriRSServer_ScriptsRunScript(
    struct ToriRSServer* srv,
    int script_id)
{
    if( !srv->scripts_ok )
        return 0;
    return run_script_id(srv, script_id, NULL, 0, -1, 1, "proc");
}

/*
 * Run a named content proc immediately, with int arguments.
 *
 * This is the seam that lets policy live in content while the engine still owns
 * the tick. Anything the reference expresses as a `[proc,...]` — the experience
 * table, the swing animation, the hit formulas — should be reachable from C
 * through here rather than reimplemented as a C `switch`, which is how those
 * rules drift from the reference silently.
 *
 * Immediate, not queued: a caller mid-swing needs the effect this tick, and a
 * queue entry would land on the next one.
 *
 * Missing script means do nothing and say so under TORIRSSERVER_VERBOSE — the same
 * fallback rule the rest of this file follows.
 */
int
ToriRSServer_ScriptsRunHookSv(
    struct ToriRSServer* srv,
    const struct SSVM_Script* script,
    const int32_t* args,
    int argc,
    const char* const* strv,
    int strc)
{
    struct SSVM_State* state;

    /* A NULL script is a no-op — used by the by-name helpers when a name is
     * not in the pack, and by internal callers that may pass a resolved pointer
     * that legitimately came back empty. */
    if( !srv->scripts_ok )
        return 0;
    assert(script);

    state = SSVM_StateAlloc(srv->script_env, script, args, argc, strv, strc);
    if( state && srv->active_player )
        state->last_int = srv->active_player->last_int;
    if( !state )
    {
        fprintf(stderr, "torirsserver: %s rejected %d argument(s)\n", script->name, argc);
        return 0;
    }
    SSVM_SetActive(state, SSVM_ENT_PLAYER, SSVM_PRIMARY, srv->active_player);
    SSVM_PointerAdd(state, SSVM_PTR_PROTECTED_PLAYER);
    return run_or_park(srv, state);
}

/*
 * The by-name form, which is for tests.
 *
 * A test naming the script it tests is stating its subject; the engine naming
 * one is authoring content (§8.6). So the lookup lives here rather than at the
 * call sites, and the engine goes through triggers.
 */
int
ToriRSServer_ScriptsRunProcSv(
    struct ToriRSServer* srv,
    const char* name,
    const int32_t* args,
    int argc,
    const char* const* strv,
    int strc)
{
    const struct SSVM_Script* script;

    if( !srv->scripts_ok )
        return 0;
    script = SSVM_ProviderGetByName(srv->scripts, name);
    if( !script )
    {
        if( srv->verbose )
            printf("torirsserver: no %s — engine fallback\n", name);
        return 0;
    }
    return ToriRSServer_ScriptsRunHookSv(srv, script, args, argc, strv, strc);
}

/*
 * The int-only form, which is most callers.
 *
 * String arguments were reachable all along — `SSVM_StateAlloc` has taken a
 * `strv`/`strc` pair since it was written and this seam passed `NULL, 0` — and
 * that gap quietly decided a design question: content could not be handed a
 * name, so any message mentioning one had to be built in C. The prayer level
 * message ("You need a Prayer level of 31 to use Ultimate Strength.") is the
 * case that surfaced it.
 */
int
ToriRSServer_ScriptsRunProc(
    struct ToriRSServer* srv,
    const char* name,
    const int32_t* args,
    int argc)
{
    return ToriRSServer_ScriptsRunProcSv(srv, name, args, argc, NULL, 0);
}


/** The engine's form of the same call: a resolved hook, no name. */
int
ToriRSServer_ScriptsRunHook(
    struct ToriRSServer* srv,
    const struct SSVM_Script* script,
    const int32_t* args,
    int argc)
{
    return ToriRSServer_ScriptsRunHookSv(srv, script, args, argc, NULL, 0);
}

/*
 * Run a named content proc and read one int back.
 *
 * The void form above lets content own an *action*; this lets it own an
 * *answer*. The reference states plenty of rules as procs that return a value —
 * `[proc,combat_defend_anim](obj $weapon, obj $shield)(seq)` is the shape: a
 * priority chain (shield, then weapon, then unarmed) with membership
 * conditions, which is a policy no single param read can express and which has
 * no business being a C `if`.
 *
 * The result is the top of the int stack when the proc finishes. Read it BEFORE
 * run_or_park, which releases the state on FINISHED — hence the open-coded
 * execute here rather than reusing that helper. A proc that suspends cannot
 * answer, so anything but FINISHED is a miss and the caller keeps its default.
 *
 * Returns 1 and writes *out when the proc answered; 0 otherwise.
 */
int
ToriRSServer_ScriptsRunHookIntSv(
    struct ToriRSServer* srv,
    const struct SSVM_Script* script,
    const int32_t* args,
    int argc,
    const char* const* strv,
    int strc,
    int32_t* out)
{
    struct SSVM_State* state;
    enum SSVM_Exec status;

    if( !srv->scripts_ok )
        return 0;
    assert(out);
    assert(script);

    state = SSVM_StateAlloc(srv->script_env, script, args, argc, strv, strc);
    if( state && srv->active_player )
        state->last_int = srv->active_player->last_int;
    if( !state )
    {
        fprintf(stderr, "torirsserver: %s rejected %d argument(s)\n", script->name, argc);
        return 0;
    }
    SSVM_SetActive(state, SSVM_ENT_PLAYER, SSVM_PRIMARY, srv->active_player);
    SSVM_PointerAdd(state, SSVM_PTR_PROTECTED_PLAYER);

    status = SSVM_Execute(state);
    if( status == SSVM_ABORTED )
        fprintf(stderr, "torirsserver: %s", SSVM_Backtrace(state));
    if( status != SSVM_FINISHED || state->isp < 1 )
    {
        SSVM_StateRelease(state);
        return 0;
    }
    *out = state->int_stack[state->isp - 1];
    SSVM_StateRelease(state);
    return 1;
}

/*
 * The interaction CLAIM seam: content gets asked before the engine dispatches.
 *
 * ------------------------------------------------------------------
 * Why this exists, when the tree already has triggers
 * ------------------------------------------------------------------
 *
 * A trigger binds to one subject. `[opnpc1,<npc>]` is exclusive, a category
 * binding loses to a name binding, and `[opnpc1,_]` shadows every specific
 * handler in the game — which is the failure `inverted-script-fallback`
 * records. So there is no way, from content, to say "ask me about EVERY npc
 * before the npc's own handler runs".
 *
 * Treasure Trails needs exactly that. A cryptic or anagram clue can point at
 * any of 218 npcs and 139 of them already own an `[opnpc1,…]` — they are quest
 * NPCs, shopkeepers, slayer masters. In OSRS, talking to one of them while
 * holding a matching clue gives the CLUE response instead of the usual
 * dialogue, so the clue check has to run first and be able to consume the
 * interaction. Writing that as a trigger would mean either editing 139 quest
 * handlers or shadowing all of them.
 *
 * It is deliberately not a Treasure Trails seam. The same shape is what a
 * Slayer-task hook, an achievement-diary task hook and the loc-search half of
 * the clue targets all want: one question, asked of content, before the
 * ordinary dispatch, whose `true` means "I handled it, stop".
 *
 * ------------------------------------------------------------------
 * The contract
 * ------------------------------------------------------------------
 *
 * The proc is OPTIONAL. A tree that does not define it costs one failed
 * name lookup per interaction and behaves exactly as before — which is what
 * makes this safe to add to a dispatch every click goes through.
 *
 * It runs with the same actives a trigger would have: the player, and the npc
 * or loc that was clicked. So it can `~chatnpc`, and does not have to answer a
 * clue with a floating message.
 *
 * It must return a boolean and it must be cheap on the common path — it is
 * asked about every interaction in the game. Content's side of that bargain is
 * to answer `false` immediately when the player holds no clue.
 */
int
ToriRSServer_ScriptsRunClaim(
    struct ToriRSServer* srv,
    const char* name,
    int npc_slot,
    int loc_slot,
    const int32_t* args,
    int argc,
    int32_t* out)
{
    const struct SSVM_Script* script;
    struct SSVM_State* state;
    enum SSVM_Exec status;

    assert(name);
    assert(out);
    *out = 0;
    if( !srv->scripts_ok )
        return 0;
    script = SSVM_ProviderGetByName(srv->scripts, name);
    if( !script )
        return 0;

    state = SSVM_StateAlloc(srv->script_env, script, args, argc, NULL, 0);
    if( !state )
    {
        fprintf(stderr, "torirsserver: %s rejected %d argument(s)\n", script->name, argc);
        return 0;
    }
    if( srv->active_player )
        state->last_int = srv->active_player->last_int;
    SSVM_SetActive(state, SSVM_ENT_PLAYER, SSVM_PRIMARY, srv->active_player);
    SSVM_PointerAdd(state, SSVM_PTR_PROTECTED_PLAYER);
    if( npc_slot >= 0 && npc_slot < TORIRSSERVER_NPC_MAX && srv->npcs[npc_slot].active )
    {
        SSVM_SetActive(state, SSVM_ENT_NPC, SSVM_PRIMARY, &srv->npcs[npc_slot]);
        state->host_tag = npc_slot + 1;
    }
    if( loc_slot >= 0 )
    {
        struct ToriRSServerSceneLoc* loc = ToriRSServer_SceneLoc(loc_slot);

        /* Slot PLUS ONE, and the liveness check, both copied from
         * `run_trigger_script` rather than reinvented: `SSVM_ENT_LOC` stores a
         * one-based slot so that 0 can mean "none", and binding a dead slot is
         * how a loc opcode ends up describing whatever was there before. */
        if( loc && loc->active )
            SSVM_SetActive(state, SSVM_ENT_LOC, SSVM_PRIMARY, (void*)(intptr_t)(loc_slot + 1));
    }

    status = SSVM_Execute(state);
    if( status == SSVM_ABORTED )
        fprintf(stderr, "torirsserver: %s", SSVM_Backtrace(state));
    /*
     * A claim proc that parks (a dialogue, a delay) has NOT answered, and is
     * treated as no claim. Answering "claimed" for a parked script would eat
     * the interaction on the strength of a script that has not decided yet;
     * answering it later is not possible, because the engine has to dispatch
     * now. Content that wants to talk should claim first and open the dialogue
     * from the claim's own follow-up.
     */
    if( status != SSVM_FINISHED || state->isp < 1 )
    {
        SSVM_StateRelease(state);
        return 0;
    }
    *out = state->int_stack[state->isp - 1];
    SSVM_StateRelease(state);
    return 1;
}

/** By name, for tests. See ToriRSServer_ScriptsRunProcSv on why the split. */
int
ToriRSServer_ScriptsRunProcIntSv(
    struct ToriRSServer* srv,
    const char* name,
    const int32_t* args,
    int argc,
    const char* const* strv,
    int strc,
    int32_t* out)
{
    const struct SSVM_Script* script;

    if( !srv->scripts_ok )
        return 0;
    script = SSVM_ProviderGetByName(srv->scripts, name);
    if( !script )
    {
        if( srv->verbose )
            printf("torirsserver: no %s — engine fallback\n", name);
        return 0;
    }
    return ToriRSServer_ScriptsRunHookIntSv(srv, script, args, argc, strv, strc, out);
}

/** The int-only form. See ToriRSServer_ScriptsRunProc for why the string half
 *  exists at all. */
int
ToriRSServer_ScriptsRunProcInt(
    struct ToriRSServer* srv,
    const char* name,
    const int32_t* args,
    int argc,
    int32_t* out)
{
    return ToriRSServer_ScriptsRunProcIntSv(srv, name, args, argc, NULL, 0, out);
}

/** The engine's form: a resolved hook that answers with one int. */
int
ToriRSServer_ScriptsRunHookInt(
    struct ToriRSServer* srv,
    const struct SSVM_Script* script,
    const int32_t* args,
    int argc,
    int32_t* out)
{
    return ToriRSServer_ScriptsRunHookIntSv(srv, script, args, argc, NULL, 0, out);
}

/*
 * Put a queue script on the player's queue from the engine side.
 *
 * `queue(...)` is a ServerScript op, so content can already do this; what this
 * adds is the engine being able to *start* the exchange. Auto-retaliate is the
 * case that needs it: the npc's swing happens in C, and the response is
 * content.
 *
 * A NULL script queues nothing — used by the by-name form when a name is
 * absent, and by internal callers whose pointer may legitimately be empty.
 */
int
ToriRSServer_ScriptsQueueHook(
    struct ToriRSServer* srv,
    const struct SSVM_Script* script,
    int delay,
    int32_t arg)
{
    struct ToriRSServerPlayer* player;

    if( !srv->scripts_ok )
        return 0;
    assert(script);

    player = srv->active_player;
    for( int i = 0; i < TORIRSSERVER_QUEUE_MAX; i++ )
    {
        if( player->queue[i].active )
            continue;
        player->queue[i].active = 1;
        player->queue[i].script_id = script->id;
        /* +1 for the same reason SS_OP_QUEUE does it: the drain decrements
         * before it fires, so delay 0 has to mean "next tick". */
        player->queue[i].delay = delay + 1;
        player->queue[i].args[0] = arg;
        player->queue[i].argc = 1;
        player->queue[i].kind = TORIRSSERVER_QUEUE_NORMAL;
        player->queue[i].logout_action = 0;
        return 1;
    }
    /*
     * Reported, because it became reachable the day the queue learned to wait.
     *
     * Before the `canAccess()` gate every entry drained on its next tick, so 16
     * slots meant 16 entries in flight. An entry now sits there for as long as
     * the player is busy, and a fight with a level-up message box on screen adds
     * one per hit. The reference's queue is a linked list with no cap at all, so
     * this limit is this engine's and nothing content can see coming — and a
     * dropped `[queue,player_death]` looks exactly like a death that never ends.
     */
    fprintf(stderr, "torirsserver: %s dropped — the player's queue is full (%d)\n", script->name,
            TORIRSSERVER_QUEUE_MAX);
    return 0;
}

/** By name, for tests. See ToriRSServer_ScriptsRunProcSv on why the split. */
int
ToriRSServer_ScriptsQueueNamed(
    struct ToriRSServer* srv,
    const char* name,
    int delay,
    int32_t arg)
{
    const struct SSVM_Script* script;

    if( !srv->scripts_ok )
        return 0;
    script = SSVM_ProviderGetByName(srv->scripts, name);
    if( !script )
    {
        if( srv->verbose )
            printf("torirsserver: no %s — nothing queued\n", name);
        return 0;
    }
    return ToriRSServer_ScriptsQueueHook(srv, script, delay, arg);
}

/* ------------------------------------------------------------------ */
/* Trigger dispatch                                                    */
/* ------------------------------------------------------------------ */

/*
 * Which namespace a trigger's subject id is drawn from.
 *
 * Only so a miss can be *named*: "no trigger for [opnpc2,goblin]" is a sentence
 * somebody can act on and "no trigger for 11/3105" is a puzzle. The reference
 * prints the same line off `type.debugname` (Player.defaultOp, OpHeldHandler);
 * the names here come from the same `pack/` files the compiler resolved the
 * script's own subject through, so a name that prints is a name that could have
 * been bound.
 *
 * The ap/op/ai families are laid out in contiguous runs by subject
 * (ss_trigger.h), so this is ranges rather than 168 rows.
 */
static int
trigger_subject_kind(
    int trigger,
    enum ToriRSServerPackKind* out_kind)
{
    if( (trigger >= SS_TRIGGER_APNPC1 && trigger <= SS_TRIGGER_AI_OPNPC5) ||
        trigger == SS_TRIGGER_AI_TIMER || trigger == SS_TRIGGER_AI_SPAWN ||
        trigger == SS_TRIGGER_AI_DESPAWN || trigger == SS_TRIGGER_AI_WALKTRIGGER ||
        (trigger >= SS_TRIGGER_AI_QUEUE1 && trigger <= SS_TRIGGER_AI_QUEUE20) ||
        (trigger >= SS_TRIGGER_AI_APPLAYER1 && trigger <= SS_TRIGGER_AI_OPPLAYER5) )
    {
        /* The ai_* families are subjects *of an npc*, whichever entity the
         * trigger is about — `[ai_opplayer1,goblin]` names the goblin. */
        *out_kind = TORIRSSERVER_PACK_NPC;
        return 1;
    }
    if( (trigger >= SS_TRIGGER_APOBJ1 && trigger <= SS_TRIGGER_AI_OPOBJ5) ||
        (trigger >= SS_TRIGGER_OPHELD1 && trigger <= SS_TRIGGER_OPHELDT) )
    {
        *out_kind = TORIRSSERVER_PACK_OBJ;
        return 1;
    }
    if( (trigger >= SS_TRIGGER_APLOC1 && trigger <= SS_TRIGGER_AI_OPLOC5) ||
        trigger == SS_TRIGGER_LOCSTEP )
    {
        *out_kind = TORIRSSERVER_PACK_LOC;
        return 1;
    }
    if( trigger == SS_TRIGGER_IF_BUTTON ||
        (trigger >= SS_TRIGGER_INV_BUTTON1 && trigger <= SS_TRIGGER_INV_BUTTOND) )
    {
        *out_kind = TORIRSSERVER_PACK_COMPONENT;
        return 1;
    }
    /* IF_CLOSE / IF_OPEN subjects are bare interface ids (see close_modal's
     * `[if_close,bankmain]` comment) — not packed component uids. */
    if( trigger == SS_TRIGGER_IF_CLOSE || trigger == SS_TRIGGER_IF_OPEN )
    {
        *out_kind = TORIRSSERVER_PACK_INTERFACE;
        return 1;
    }
    return 0;
}

/*
 * Did a player ask for this, or did the engine?
 *
 * Only the first kind reports a miss, which is the reference's own division:
 * `Player.defaultOp` and the OpHeld/InvButton/IfButton handlers print
 * "No trigger for [...]" because a click that does nothing is a thing somebody
 * is standing there waiting for. `World.spawnNpc` and `Npc.processTimers` say
 * nothing, because an npc with no `[ai_spawn]` is every npc — 2,197 of them at
 * world init here, which is a wall of text rather than a diagnostic.
 *
 * The player families are contiguous and end exactly where their `ai_` twins
 * begin (ss_trigger.h): npc 3..16, obj 31..44, loc 59..72, player 87..100.
 */
static int
trigger_is_player_initiated(int trigger)
{
    return (trigger >= SS_TRIGGER_APNPC1 && trigger <= SS_TRIGGER_OPNPCT) ||
           (trigger >= SS_TRIGGER_APOBJ1 && trigger <= SS_TRIGGER_OPOBJT) ||
           (trigger >= SS_TRIGGER_APLOC1 && trigger <= SS_TRIGGER_OPLOCT) ||
           (trigger >= SS_TRIGGER_APPLAYER1 && trigger <= SS_TRIGGER_OPPLAYERT) ||
           (trigger >= SS_TRIGGER_OPHELD1 && trigger <= SS_TRIGGER_OPHELDT) ||
           trigger == SS_TRIGGER_IF_BUTTON || trigger == SS_TRIGGER_IF_CLOSE ||
           (trigger >= SS_TRIGGER_INV_BUTTON1 && trigger <= SS_TRIGGER_INV_BUTTOND);
}

/*
 * Triggers whose subject is an npc and whose scripts expect ACTIVE_NPC armed
 * (LostCity ScriptRunner.init(..., targetNpc)). Running without a live slot
 * reaches ~chatnpc and aborts on NPC_TYPE — joe_prequest's first page was the
 * loud case. Refuse at the door instead of starting a doomed script.
 */
static int
trigger_requires_active_npc(int trigger)
{
    return (trigger >= SS_TRIGGER_APNPC1 && trigger <= SS_TRIGGER_OPNPCT) ||
           (trigger >= SS_TRIGGER_AI_APNPC1 && trigger <= SS_TRIGGER_AI_OPNPC5) ||
           trigger == SS_TRIGGER_AI_TIMER || trigger == SS_TRIGGER_AI_SPAWN ||
           trigger == SS_TRIGGER_AI_DESPAWN || trigger == SS_TRIGGER_AI_WALKTRIGGER ||
           (trigger >= SS_TRIGGER_AI_QUEUE1 && trigger <= SS_TRIGGER_AI_QUEUE20) ||
           (trigger >= SS_TRIGGER_AI_APPLAYER1 && trigger <= SS_TRIGGER_AI_OPPLAYER5);
}

/** Engine-driven npc triggers run in their owned player's context when one is
 * bound. This is intentionally narrower than `trigger_requires_active_npc`:
 * an ordinary player click already has the correct active player. */
static int
trigger_is_ai_npc(int trigger)
{
    return (trigger >= SS_TRIGGER_AI_APNPC1 && trigger <= SS_TRIGGER_AI_OPNPC5) ||
           trigger == SS_TRIGGER_AI_TIMER || trigger == SS_TRIGGER_AI_SPAWN ||
           trigger == SS_TRIGGER_AI_DESPAWN || trigger == SS_TRIGGER_AI_WALKTRIGGER ||
           (trigger >= SS_TRIGGER_AI_QUEUE1 && trigger <= SS_TRIGGER_AI_QUEUE20) ||
           (trigger >= SS_TRIGGER_AI_APPLAYER1 && trigger <= SS_TRIGGER_AI_OPPLAYER5);
}

/** `[opnpc2,goblin]`, or `[opnpc2,3105]` when the id has no name. */
static const char*
trigger_label(
    int trigger,
    int type,
    char* buffer,
    size_t capacity)
{
    enum ToriRSServerPackKind kind = TORIRSSERVER_PACK_COUNT;
    const char* subject = NULL;

    if( type >= 0 && trigger_subject_kind(trigger, &kind) )
        subject = ToriRSServer_ContentSymbolName(kind, type);

    if( subject )
        snprintf(buffer, capacity, "[%s,%s]", SSVM_TriggerName(trigger), subject);
    else if( type >= 0 )
        snprintf(buffer, capacity, "[%s,%d]", SSVM_TriggerName(trigger), type);
    else
        snprintf(buffer, capacity, "[%s,_]", SSVM_TriggerName(trigger));
    return buffer;
}

/*
 * Start a script the way a trigger does: on the active player, with the npc the
 * trigger is about, and with no arguments.
 *
 * Shared by the keyed and the name-addressed forms so that the two cannot come
 * to disagree about what a trigger's execution context is — which they did in
 * one respect already, the npc, and `[if_button,...]` never had one.
 */
/*
 * Trigger-dispatch cost, read by the embedded server's tick breakdown
 * (torirs_server_world.c).
 *
 * Counted at the OUTERMOST dispatch only: a script is free to fire another
 * trigger, and adding the nested run in again would report more script time
 * than the tick took. A parked script's later resumption is not counted here at
 * all -- it re-enters through ToriRSServer_ScriptsResume_*, which the breakdown
 * already attributes to phase 5's `scripts` slot.
 */
uint64_t g_ToriRSServer_ScriptUs;
int g_ToriRSServer_ScriptRuns;
/* The tick's worst single dispatch, by name -- a total is enough to say "a
 * script did this" and never enough to say which one. */
uint64_t g_ToriRSServer_ScriptSlowUs;
char g_ToriRSServer_ScriptSlowName[96];
static int g_script_depth;

static uint64_t
script_now_us(void)
{
    struct timespec ts;

    if( clock_gettime(CLOCK_MONOTONIC, &ts) != 0 )
        return 0;
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

static int
run_trigger_script_inner(
    struct ToriRSServer* srv,
    const struct SSVM_Script* script,
    int npc_slot,
    int loc_slot,
    int player_slot);

static int
run_trigger_script(
    struct ToriRSServer* srv,
    const struct SSVM_Script* script,
    int npc_slot,
    int loc_slot,
    int player_slot)
{
    int outer = g_script_depth++ == 0;
    uint64_t t0 = outer ? script_now_us() : 0;
    int result;

    result = run_trigger_script_inner(srv, script, npc_slot, loc_slot, player_slot);

    g_script_depth--;
    if( outer )
    {
        uint64_t us = script_now_us() - t0;

        g_ToriRSServer_ScriptUs += us;
        g_ToriRSServer_ScriptRuns++;
        if( us > g_ToriRSServer_ScriptSlowUs )
        {
            g_ToriRSServer_ScriptSlowUs = us;
            snprintf(g_ToriRSServer_ScriptSlowName, sizeof(g_ToriRSServer_ScriptSlowName), "%s",
                     script && script->name ? script->name : "?");
        }
    }
    return result;
}

static int
run_trigger_script_inner(
    struct ToriRSServer* srv,
    const struct SSVM_Script* script,
    int npc_slot,
    int loc_slot,
    int player_slot)
{
    struct SSVM_State* state = SSVM_StateAlloc(srv->script_env, script, NULL, 0, NULL, 0);
    if( state )
    {
        /* A queued npc script states its own `last_int`; everything else
         * inherits the player's, which is what every player-context reader has
         * always seen. See ToriRSServer_ScriptsRunTriggerLastint. */
        if( srv->pending_last_int_valid )
            state->last_int = srv->pending_last_int;
        else if( srv->active_player )
            state->last_int = srv->active_player->last_int;
    }

    if( !state )
    {
        fprintf(stderr, "torirsserver: %s expects arguments a trigger cannot supply\n",
                script->name);
        return TORIRSSERVER_TRIGGER_FAILED;
    }

    /* Every trigger the server fires is on behalf of whoever's turn it is, and
     * the engine grants protected access because these all arrive as a direct
     * response to player input. */
    SSVM_SetActive(state, SSVM_ENT_PLAYER, SSVM_PRIMARY, srv->active_player);
    SSVM_PointerAdd(state, SSVM_PTR_PROTECTED_PLAYER);

    /* Targeted player casts keep their owner as active_player so a script's
     * common post-operation commit cannot debit the recipient. The selected
     * live player occupies active_player2 instead. The interaction layer
     * already checked its login generation immediately before dispatch; the
     * secondary pointer is deliberately optional for ordinary triggers. */
    if( player_slot >= 0 && player_slot < TORIRSSERVER_PLAYER_MAX &&
        srv->players[player_slot].active && &srv->players[player_slot] != srv->active_player )
        SSVM_SetActive(state, SSVM_ENT_PLAYER, SSVM_SECONDARY, &srv->players[player_slot]);

    if( npc_slot >= 0 && npc_slot < TORIRSSERVER_NPC_MAX && srv->npcs[npc_slot].active )
    {
        /* The pointer satisfies the VM's require-an-active-npc check; the slot
         * in host_tag is what actually resolves it. Stored +1 so zero means
         * "no npc" without a separate flag. */
        SSVM_SetActive(state, SSVM_ENT_NPC, SSVM_PRIMARY, &srv->npcs[npc_slot]);
        state->host_tag = npc_slot + 1;
    }

    /* The active ground obj, for `[opobj<n>]` — `Player.getOpTrigger` sets
     * `state.activeObj` on the obj arm the same way it sets `activeNpc` on the
     * npc one. One-shot: consumed here so a script that itself fires a trigger
     * does not inherit it. The value is `ToriRSServer_WorldObjHandle`'s — a slot
     * *and* the slot's generation, never a pointer, because the ground array is
     * a free list and a suspended script must resume onto its own obj or none. */
    if( srv->pending_active_obj )
    {
        SSVM_SetActive(state, SSVM_ENT_OBJ, SSVM_PRIMARY, (void*)srv->pending_active_obj);
        srv->pending_active_obj = 0;
    }

    /* The npc the subject npc is acting on — `[ai_opnpc<n>]`'s target, reached
     * from the script as `.npc_*`. One-shot for the same reason the obj above
     * is: a script that fires another trigger must not inherit it. */
    if( srv->pending_active_npc2 )
    {
        int npc2 = srv->pending_active_npc2 - 1;

        srv->pending_active_npc2 = 0;
        if( npc2 >= 0 && npc2 < TORIRSSERVER_NPC_MAX && srv->npcs[npc2].active )
            SSVM_SetActive(state, SSVM_ENT_NPC, SSVM_SECONDARY, &srv->npcs[npc2]);
    }

    /*
     * The loc the trigger is about, on the same footing as the npc.
     *
     * `SSVM_ENT_LOC` had exactly three writers tree-wide before 2026-08-02 — the
     * iterator, `LOC_FIND` and `LOC_ADD` — so **every** `[oploc<n>]` and
     * `[aploc<n>]` script ran with no active loc, and the first `loc_coord`,
     * `loc_angle`, `loc_shape`, `loc_param`, `loc_change` or `loc_del` in it
     * aborted with "the active loc is gone". A door script could bind and could
     * not say anything about the door it was bound to; that, and not the `loc_*`
     * family, was what the `oploc` fallback row was actually waiting on.
     *
     * By *slot*, not by pointer, and encoded `slot + 1` — the convention
     * `torirs_server_ops_loc.c` and `LOC_FIND` already share, for the reason stated
     * there: a script can suspend between the dispatch and the read, and a scene
     * rebuild reallocates the array underneath it. The pointer's only job is to
     * satisfy the VM's require-an-active-loc check.
     */
    if( loc_slot >= 0 )
    {
        struct ToriRSServerSceneLoc* loc = ToriRSServer_SceneLoc(loc_slot);

        if( loc && loc->active )
        {
            SSVM_SetActive(state, SSVM_ENT_LOC, SSVM_PRIMARY,
                           (void*)(intptr_t)(loc_slot + 1));
        }
    }

    /* run_or_park answers 1 for ran-or-parked and 0 for every way a bound script
     * can fail to run — aborted, no parking slot, world queue full. All of those
     * are FAILED here: a script existed and the behaviour did not happen. */
    return run_or_park(srv, state) ? TORIRSSERVER_TRIGGER_RAN : TORIRSSERVER_TRIGGER_FAILED;
}

/*
 * Run one rung, and say whether the dispatch should keep walking.
 *
 * Returns the rung's `TORIRSSERVER_TRIGGER_*`, with `*declined` set when the script
 * finished by calling `trigger_decline`. The flag is cleared immediately before
 * the run and read immediately after, so a script that fires another trigger
 * internally cannot leak a decline outward.
 *
 * A PARKED script cannot decline. `run_or_park` answers "ran" for parked as well
 * as finished, and a script that has opened a dialogue and then declines has
 * already taken over the player's screen — falling to the next rung would run a
 * second interaction underneath it. `trigger_decline` ends the script outright so
 * this cannot happen by accident; the check is here because "cannot happen" and
 * "is not checked" are different claims, and this one is cheap.
 */
static int
run_rung(
    struct ToriRSServer* srv,
    const struct SSVM_Script* script,
    int npc_slot,
    int loc_slot,
    int player_slot,
    int* declined)
{
    int result;

    srv->trigger_declined = 0;
    srv->trigger_dispatch_depth++;
    result = run_trigger_script(srv, script, npc_slot, loc_slot, player_slot);
    srv->trigger_dispatch_depth--;

    *declined = srv->trigger_declined && result == TORIRSSERVER_TRIGGER_RAN;
    srv->trigger_declined = 0;

    if( *declined && srv->active_player && srv->active_player->active_script )
    {
        fprintf(stderr, "torirsserver: %s declined after parking; treating it as handled\n",
                script->name ? script->name : "?");
        *declined = 0;
    }
    return result;
}

/*
 * `chain` walks the reference's getByTrigger ladder — type, then category, then
 * `_` — where `chain == 0` looks up the single key the arguments name, the way
 * getByTriggerSpecific does. `report` is off only for the keyed half of the
 * if_button pair, which is not a miss until the name-addressed half has also
 * missed.
 *
 * The ladder is walked here rather than inside the provider because a rung that
 * DECLINES has to fall to the next one, and only the caller of the script knows
 * that it did. That is the whole of `trigger_decline`: the reference stops at the
 * first binding it finds, so a name-bound script that does not recognise its
 * partner consumes the interaction and the category binding that would have
 * answered never runs. See docs/USEON_DISPATCH_ENGINE_PLAN.md E1.
 */
static int
run_trigger_impl(
    struct ToriRSServer* srv,
    int trigger,
    int type,
    int category,
    int npc_slot,
    int loc_slot,
    int player_slot,
    int chain,
    int report)
{
    struct
    {
        int32_t type;
        int32_t category;
    } rungs[3];
    int rung_count = 0;
    int any_declined = 0;

    if( !srv->scripts_ok )
        return TORIRSSERVER_TRIGGER_NONE;

    srv->dispatch_declined = 0;

    /*
     * Each rung is one `getByTriggerSpecific` key, in the order `getByTrigger`
     * would have tried them. A `-1` key is skipped rather than looked up, because
     * `getByTriggerSpecific(-1, -1)` means the `_` wildcard: without the skip, a
     * dispatch with no type and no category would try the wildcard three times
     * and run it three times if it declined.
     */
    if( chain )
    {
        if( type != -1 )
        {
            rungs[rung_count].type = type;
            rungs[rung_count++].category = -1;
        }
        if( category != -1 )
        {
            rungs[rung_count].type = -1;
            rungs[rung_count++].category = category;
        }
        rungs[rung_count].type = -1;
        rungs[rung_count++].category = -1;
    }
    else
    {
        rungs[rung_count].type = type;
        rungs[rung_count++].category = category;
    }

    for( int i = 0; i < rung_count; i++ )
    {
        const struct SSVM_Script* script = SSVM_ProviderGetByTriggerSpecific(
            srv->scripts, trigger, rungs[i].type, rungs[i].category);
        struct ToriRSServerPlayer* saved_player;
        struct ToriRSServerPlayer* context_player;
        int declined = 0;
        int result;

        if( !script )
            continue;

        if( trigger_requires_active_npc(trigger) )
        {
            int live = npc_slot >= 0 && npc_slot < TORIRSSERVER_NPC_MAX &&
                       srv->npcs[npc_slot].active;

            if( !live )
            {
                char label[192];

                fprintf(
                    stderr,
                    "torirsserver: %s refused — no live npc (slot=%d script=%s)\n",
                    trigger_label(trigger, type, label, sizeof(label)),
                    npc_slot,
                    script->name ? script->name : "?");
                return TORIRSSERVER_TRIGGER_FAILED;
            }
        }

        context_player = srv->active_player;
        if( trigger_is_ai_npc(trigger) && npc_slot >= 0 && npc_slot < TORIRSSERVER_NPC_MAX &&
            srv->npcs[npc_slot].owner_gen != 0 )
        {
            /* A dead generation produces no player context. Falling back here
             * would hand the familiar's timer/queue script to a replacement login
             * or to whichever player happened to run the preceding phase. */
            context_player = ToriRSServer_WorldNpcOwner(srv, &srv->npcs[npc_slot]);
            if( !context_player )
                return TORIRSSERVER_TRIGGER_FAILED;
        }
        saved_player = srv->active_player;
        ToriRSServer_WorldSetActive(srv, context_player);
        result = run_rung(srv, script, npc_slot, loc_slot, player_slot, &declined);
        ToriRSServer_WorldSetActive(srv, saved_player);

        if( srv->verbose && rung_count > 1 )
        {
            char label[192];

            fprintf(stderr, "torirsserver:   %s rung %d/%d -> %s %s\n",
                    trigger_label(trigger, type, label, sizeof(label)), i + 1, rung_count,
                    script->name ? script->name : "?",
                    result != TORIRSSERVER_TRIGGER_RAN ? "FAILED"
                                                  : declined ? "declined" : "handled it");
        }

        if( result != TORIRSSERVER_TRIGGER_RAN || !declined )
            return result;
        any_declined = 1;
    }

    /*
     * Nothing bound, or everything that bound declined. The caller is told the
     * same thing either way — from the player's side both are "nothing
     * interesting happens" — and `dispatch_declined` is what keeps them apart for
     * `ToriRSServer_ScriptsFallback`, whose answer must be no.
     */
    if( any_declined )
    {
        srv->dispatch_declined = 1;
        return TORIRSSERVER_TRIGGER_NONE;
    }

    if( report && srv->verbose && trigger_is_player_initiated(trigger) )
    {
        char label[192];

        /* The reference's own wording, from `Player.defaultOp`, and its own
         * condition: a debug build says so, a production one is silent. Nothing
         * else distinguishes a trigger that deliberately does nothing from a
         * packet that never arrived. */
        fprintf(stderr, "torirsserver: no trigger for %s\n",
                trigger_label(trigger, type, label, sizeof(label)));
    }
    return TORIRSSERVER_TRIGGER_NONE;
}

int
ToriRSServer_ScriptsRunTrigger(
    struct ToriRSServer* srv,
    int trigger,
    int type,
    int category,
    int npc_slot)
{
    return run_trigger_impl(srv, trigger, type, category, npc_slot, -1, -1, 1, 1);
}

/*
 * The same dispatch, carrying the value the trigger's own `last_int` must
 * answer. `Npc.ts` does exactly this for a queued npc script:
 *
 *     const state = ScriptRunner.init(script, this, null, request.args);
 *     state.lastInt = request.lastInt;
 *
 * and the npc queue is the only caller that needs it, because it is the only
 * trigger source with a value of its own — `npc_queue(<n>, $arg, $delay)` — and
 * no player whose `last_int` could stand in.
 */
int
ToriRSServer_ScriptsRunTriggerLastint(
    struct ToriRSServer* srv,
    int trigger,
    int type,
    int category,
    int npc_slot,
    int32_t last_int)
{
    int rc;
    int32_t saved = srv->pending_last_int;
    int saved_valid = srv->pending_last_int_valid;

    srv->pending_last_int = last_int;
    srv->pending_last_int_valid = 1;
    rc = run_trigger_impl(srv, trigger, type, category, npc_slot, -1, -1, 1, 1);
    srv->pending_last_int = saved;
    srv->pending_last_int_valid = saved_valid;
    return rc;
}

/*
 * The same dispatch, naming the npc the subject npc is acting on.
 *
 * `[ai_opnpc<n>,<attacker>]` runs with the attacker as the primary active npc
 * and the target as the secondary — the reference's `activeNpc` / `activeNpc2`
 * pair — because a script about a fight between two npcs has to be able to name
 * both of them, and `.npc_*` is how it names the second.
 */
int
ToriRSServer_ScriptsRunTriggerNpc2(
    struct ToriRSServer* srv,
    int trigger,
    int type,
    int category,
    int npc_slot,
    int npc2_slot)
{
    int rc;

    srv->pending_active_npc2 = npc2_slot >= 0 ? npc2_slot + 1 : 0;
    rc = run_trigger_impl(srv, trigger, type, category, npc_slot, -1, -1, 1, 1);
    /* Cleared again here: a lookup that finds no script never reaches
     * `run_trigger_script`, and a stale value would arm the next trigger. */
    srv->pending_active_npc2 = 0;
    return rc;
}

/*
 * Trigger dispatch with string arguments — used by friend login/logout
 * notifications that hand the display name to content (`[friendlogin,_]`).
 */
int
ToriRSServer_ScriptsRunTriggerSv(
    struct ToriRSServer* srv,
    int trigger,
    int type,
    int category,
    int npc_slot,
    const char* const* strv,
    int strc)
{
    const struct SSVM_Script* script;
    struct SSVM_State* state;

    if( !srv->scripts_ok )
        return TORIRSSERVER_TRIGGER_NONE;

    script = SSVM_ProviderGetByTrigger(srv->scripts, trigger, type, category);
    if( !script )
        return TORIRSSERVER_TRIGGER_NONE;

    state = SSVM_StateAlloc(srv->script_env, script, NULL, 0, strv, strc);
    if( state && srv->active_player )
        state->last_int = srv->active_player->last_int;
    if( !state )
    {
        fprintf(stderr, "torirsserver: %s rejected string argument(s)\n",
                script->name ? script->name : "?");
        return TORIRSSERVER_TRIGGER_FAILED;
    }
    SSVM_SetActive(state, SSVM_ENT_PLAYER, SSVM_PRIMARY, srv->active_player);
    SSVM_PointerAdd(state, SSVM_PTR_PROTECTED_PLAYER);
    if( npc_slot >= 0 && npc_slot < TORIRSSERVER_NPC_MAX && srv->npcs[npc_slot].active )
    {
        SSVM_SetActive(state, SSVM_ENT_NPC, SSVM_PRIMARY, &srv->npcs[npc_slot]);
        state->host_tag = npc_slot + 1;
    }
    return run_or_park(srv, state) ? TORIRSSERVER_TRIGGER_RAN : TORIRSSERVER_TRIGGER_FAILED;
}

int
ToriRSServer_ScriptsRunTriggerOnLoc(
    struct ToriRSServer* srv,
    int trigger,
    int type,
    int category,
    int loc_slot)
{
    return run_trigger_impl(srv, trigger, type, category, -1, loc_slot, -1, 1, 1);
}

/*
 * Run one script by id, on the active player, with no trigger lookup.
 *
 * The walktrigger is the only caller and the only shape that needs this:
 * `walktrigger(X)` stores the script *id* rather than arming a subject the
 * provider could find later, so by the time the engine wants to fire it there
 * is no (trigger, type) pair left to look up. Everything else in this file goes
 * through `run_trigger_impl` and should keep doing so — this is not a general
 * "call any script" hook, and giving it one would route around the trigger
 * table that `test-ss-provider` exists to pin.
 */
void
ToriRSServer_ScriptsRunScriptId(struct ToriRSServer* srv, int script_id)
{
    const struct SSVM_Script* script;

    if( !srv->scripts_ok || !srv->scripts )
        return;
    if( script_id < 0 || script_id >= srv->scripts->count )
        return;
    script = &srv->scripts->scripts[script_id];
    if( !script )
        return;
    run_trigger_script(srv, script, -1, -1, -1);
}

int
ToriRSServer_ScriptsRunTriggerSpecific(
    struct ToriRSServer* srv,
    int trigger,
    int type,
    int category,
    int npc_slot)
{
    return run_trigger_impl(srv, trigger, type, category, npc_slot, -1, -1, 0, 1);
}

/*
 * A targeted cast: `[apnpct,magic_spellbook:wind_strike]`,
 * `[opheldt,magic_spellbook:high_alchemy]` and the loc/obj forms.
 *
 * Keyed by the SPELL rather than by what it was aimed at — one script per
 * spell, matching no npc — so the subject is a component uid, and that is what
 * makes this its own dispatcher rather than a plain `ToriRSServer_ScriptsRunTrigger`
 * call. A spell lives in interface 218, so its uid is 14,286,848 and up: past
 * `ssc_compile.c`'s `1 << 21` ceiling, which means EVERY spell trigger in the
 * tree compiled name-addressed and none of them is reachable by key. Dispatching
 * these by key alone finds nothing, and finds nothing *silently* — the caller
 * reads it as "this spell has no script" and says "Nothing interesting happens".
 *
 * The key rung is still tried first, for the same reason run_if_button_trigger
 * tries both: which spelling a script compiled under is arithmetic, not
 * authorship, and a spellbook interface below 32 would fit.
 */
int
ToriRSServer_ScriptsRunSpellTrigger(
    struct ToriRSServer* srv,
    int trigger,
    int spell_component,
    int npc_slot,
    int player_slot,
    int loc_slot)
{
    const struct SSVM_Script* script;
    const char* component;
    char name[192];
    int result;

    if( !srv->scripts_ok || spell_component <= 0 )
        return TORIRSSERVER_TRIGGER_NONE;

    /* Specific and unreported, as run_if_button_trigger's key rung is: an
     * `[apnpct,_]` wildcard would swallow every cast in the game, and a miss
     * here is the *expected* case (see above), not something to report. */
    result = run_trigger_impl(srv, trigger, spell_component, -1, npc_slot, loc_slot, player_slot, 0,
                              0);
    if( result != TORIRSSERVER_TRIGGER_NONE )
        return result;

    component = ToriRSServer_ContentSymbolName(TORIRSSERVER_PACK_COMPONENT, spell_component);
    if( !component )
    {
        if( srv->verbose )
            fprintf(stderr, "torirsserver: no [%s] subject name for component %d|%d\n",
                    SSVM_TriggerName(trigger), (spell_component >> 16) & 0xffff,
                    spell_component & 0xffff);
        return TORIRSSERVER_TRIGGER_NONE;
    }

    snprintf(name, sizeof(name), "[%s,%s]", SSVM_TriggerName(trigger), component);
    script = SSVM_ProviderGetByName(srv->scripts, name);
    if( !script )
    {
        if( srv->verbose )
            fprintf(stderr, "torirsserver: no trigger for %s\n", name);
        return TORIRSSERVER_TRIGGER_NONE;
    }
    return run_trigger_script(srv, script, npc_slot, loc_slot, player_slot);
}

/*
 * One (trigger, component) attempt: by key, then by name.
 *
 * Both spellings have to be tried for every trigger in this family, because
 * which one a script compiled under is decided by arithmetic rather than by
 * the author. `SSVM_LookupKey` puts the subject at bit 10 of an i32, so a
 * component uid `(interface << 16) | child` only fits for interfaces below 32
 * — everything else compiles name-addressed (`ssc_compile.c`'s
 * `symbol->value >= (1 << 21)` branch). `stats:attack` is 20,971,521 and is in
 * the second class; `chatmenu:options` is in the first.
 */
static int
run_if_button_trigger(
    struct ToriRSServer* srv,
    int trigger,
    int uid,
    const char* component)
{
    const struct SSVM_Script* script;
    char name[192];
    int result;

    /* *Specific*: `IfButtonHandler` uses getByTriggerSpecific, so a component
     * with no script of its own does not fall through to some `[if_button,_]`
     * that would then swallow every click in the game. */
    result = run_trigger_impl(srv, trigger, uid, -1, -1, -1, -1, 0, 0);
    if( result != TORIRSSERVER_TRIGGER_NONE )
        return result;

    if( !component )
        return TORIRSSERVER_TRIGGER_NONE;

    snprintf(name, sizeof(name), "[%s,%s]", SSVM_TriggerName(trigger), component);
    script = SSVM_ProviderGetByName(srv->scripts, name);
    if( !script )
        return TORIRSSERVER_TRIGGER_NONE;
    return run_trigger_script(srv, script, -1, -1, -1);
}

int
ToriRSServer_ScriptsRunIfButton(
    struct ToriRSServer* srv,
    int uid,
    int op_num)
{
    const char* component;
    int result;

    if( !srv->scripts_ok )
        return TORIRSSERVER_TRIGGER_NONE;

    component = ToriRSServer_ContentSymbolName(TORIRSSERVER_PACK_COMPONENT, uid);

    /*
     * The numbered trigger first, then the unnumbered one.
     *
     * A rev-230 component carries up to ten ops and the packet says which was
     * clicked; `stats:attack` alone has "Toggle Attack XP" on op 1 and "View
     * Attack guide" on op 2, and before IF_BUTTON1..10 existed a script bound
     * to that component could not tell them apart. So content that cares
     * writes `[if_button2,stats:attack]`, and content that does not keeps
     * `[if_button,...]` and answers every op — which is what every script in
     * the tree written before this did, and why the unnumbered form is a
     * fallthrough rather than a replacement.
     *
     * This does not widen the *engine's* fallback list (`enum ToriRSServerFallback`,
     * osrs230_mockserver.md §3.18): both rungs are content, and the C rung
     * below is the same single one it always was.
     */
    if( op_num >= 1 && op_num <= 10 )
    {
        result = run_if_button_trigger(srv, SS_TRIGGER_IF_BUTTON1 + (op_num - 1), uid, component);
        if( result != TORIRSSERVER_TRIGGER_NONE )
            return result;
    }

    result = run_if_button_trigger(srv, SS_TRIGGER_IF_BUTTON, uid, component);
    if( result != TORIRSSERVER_TRIGGER_NONE )
        return result;

    if( srv->verbose )
    {
        if( component )
            fprintf(stderr, "torirsserver: no trigger for [if_button%d,%s] or [if_button,%s]\n",
                    op_num, component, component);
        else
            fprintf(stderr, "torirsserver: no trigger for [if_button,%d:%d] (no component name)\n",
                    uid >> 16, uid & 0xffff);
    }
    return TORIRSSERVER_TRIGGER_NONE;
}

/*
 * Resolve a coordinate-subject trigger to its script, or NULL.
 *
 * Both public entry points go through this so the *name* has one spelling. The
 * spelling is the whole contract: if it disagrees with `ssc_compile.c`'s by one
 * character, every zone script in the tree stops running and nothing says so.
 */
static const struct SSVM_Script*
zone_trigger_script(
    struct ToriRSServer* srv,
    int trigger,
    int level,
    int x,
    int z)
{
    const char* trigger_name;
    char name[192];

    if( !srv->scripts_ok )
        return NULL;

    trigger_name = SSVM_TriggerName(trigger);
    if( !trigger_name || !trigger_name[0] )
        return NULL;

    switch( trigger )
    {
    case SS_TRIGGER_ZONE:
    case SS_TRIGGER_ZONEEXIT:
        /*
         * `[zone,<level>_<mx>_<mz>_<lx>_<lz>]`, where the last two are **tile
         * offsets inside the map square**, 0/8/…/56 — not zone indices 0..7, and
         * not zero-padded. `Player.ts`'s own formatter:
         *   `${level}_${x >> 6}_${z >> 6}_${(x & 0x3f) >> 3 << 3}_${…}`
         * Getting a character of this wrong costs nothing at build time and is
         * permanently silent at run time, which is why the selftest asserts a
         * compiled header is retrievable by exactly this string.
         */
        snprintf(name, sizeof(name), "[%s,%d_%d_%d_%d_%d]", trigger_name, level, x >> 6, z >> 6,
                 ((x & 0x3f) >> 3) << 3, ((z & 0x3f) >> 3) << 3);
        break;

    case SS_TRIGGER_MAPZONE:
    case SS_TRIGGER_MAPZONEEXIT:
        /*
         * The map square, and the level is a literal 0 — the reference builds
         * this latch with `CoordGrid.packCoord(0, x, z)`, so climbing a ladder
         * inside one square does not re-enter it. `level` is accepted and
         * ignored on purpose: the caller should not have to know that.
         */
        (void)level;
        snprintf(name, sizeof(name), "[%s,0_%d_%d]", trigger_name, x >> 6, z >> 6);
        break;

    default:
        return NULL;
    }

    /*
     * No keyed rung, unlike `run_if_button`. There, the keyed lookup is a
     * correct fast path; here it is actively wrong — `ssc_lex.c` packs a 5-part
     * coord into 28 bits and the compiled key gives its subject 21, so a zone's
     * key either goes negative (and `ssvm_provider.c` keeps negatives out of the
     * index) or wraps onto a subject that no runtime lookup reproduces. Name is
     * the only address these four have. `ssc_compile.c` now writes -1 for a
     * coord subject so that is true by construction rather than by luck.
     *
     * A miss is silent, and a miss is the overwhelmingly common case: there are
     * tens of thousands of nameable zones and a handful of bound ones. What
     * bounds the cost is not the lookup but the *latch* — this runs once per
     * zone crossing, at most one crossing per four ticks at walking pace, never
     * once per tick. `SSVM_ProviderGetByName` is FNV-1a plus a binary search
     * that stops at the first mismatched hash, so a miss is ~log2(n) compares
     * and no strcmp at all. Measured on this tree's pack (see
     * osrs230_mockserver.md §3.21) rather than asserted.
     */
    return SSVM_ProviderGetByName(srv->scripts, name);
}

int
ToriRSServer_ScriptsRunTriggerAt(
    struct ToriRSServer* srv,
    int trigger,
    int level,
    int x,
    int z)
{
    const struct SSVM_Script* script = zone_trigger_script(srv, trigger, level, x, z);

    if( !script )
        return TORIRSSERVER_TRIGGER_NONE;
    return run_trigger_script(srv, script, -1, -1, -1);
}

int
ToriRSServer_ScriptsQueueTriggerAt(
    struct ToriRSServer* srv,
    int trigger,
    int level,
    int x,
    int z)
{
    const struct SSVM_Script* script = zone_trigger_script(srv, trigger, level, x, z);
    struct ToriRSServerPlayer* player;

    if( !script )
        return TORIRSSERVER_TRIGGER_NONE;

    player = srv->active_player;
    for( int i = 0; i < TORIRSSERVER_ENGINE_QUEUE_MAX; i++ )
    {
        if( player->engine_queue[i].active )
            continue;
        player->engine_queue[i].active = 1;
        player->engine_queue[i].script_id = script->id;
        /*
         * Zero, not `delay + 1`. `enqueueScript` overwrites the delay with 0 for
         * ENGINE, and `processEngineQueue` compares the value the counter had
         * *before* its decrement — so the entry is due on the first drain after
         * this one, which is phase 5 of the next tick.
         */
        player->engine_queue[i].delay = 0;
        player->engine_queue[i].args[0] = 0;
        player->engine_queue[i].argc = 0;
        player->engine_queue[i].kind = TORIRSSERVER_QUEUE_ENGINE;
        player->engine_queue[i].logout_action = 0;
        return TORIRSSERVER_TRIGGER_RAN;
    }

    /* Same argument as `queue_hook`'s: the reference's list has no cap, so an
     * overflow is this engine's own and content cannot see it coming. */
    fprintf(stderr, "torirsserver: %s dropped — the engine queue is full (%d)\n", script->name,
            TORIRSSERVER_ENGINE_QUEUE_MAX);
    return TORIRSSERVER_TRIGGER_NONE;
}

void
ToriRSServer_ScriptsProcessEngineQueue(struct ToriRSServer* srv)
{
    struct ToriRSServerPlayer* player;

    if( !srv->scripts_ok )
        return;

    player = srv->active_player;
    for( int i = 0; i < TORIRSSERVER_ENGINE_QUEUE_MAX; i++ )
    {
        struct ToriRSServerQueued* entry = &player->engine_queue[i];
        int script_id;
        int due;

        if( !entry->active )
            continue;

        /*
         * `const delay = request.delay--; if (this.canAccess() && delay <= 0)`.
         * The decrement is unconditional and only the run is gated, exactly as
         * the normal queue's is — so an entry held by a dialogue fires on the
         * tick the dialogue closes rather than restarting a wait it never had.
         */
        due = entry->delay-- <= 0;
        if( !due || !player_can_access(srv) )
            continue;

        script_id = entry->script_id;
        entry->active = 0;
        /* Protected, as `processEngineQueue`'s `executeScript(script, true)`. */
        run_script_id(srv, script_id, NULL, 0, -1, 1, "zone,zoneexit,mapzone,mapzoneexit");
    }
}

/*
 * The two halves of a use-on, as they were latched before dispatch.
 *
 * `obj` is the item that was clicked (the target) and `use_obj` the one that was
 * dragged onto it. The slots travel with them because a script that reads
 * `last_slot` to consume what it just used would otherwise address the wrong
 * backpack cell.
 */
struct OpHeldUPair
{
    int obj;
    int obj_slot;
    int use_obj;
    int use_slot;
};

/*
 * Point `last_item` at whichever half the rung that matched is bound to.
 *
 * ONE INVARIANT, for every rung: **`last_item` is the item the script is bound
 * to, and `last_useitem` is the other one.** A script never has to know which
 * rung reached it, and a reader of either field never has to ask.
 *
 * This is a deliberate divergence from the reference, which toggles a swap
 * instead (`OpHeldUHandler.ts:98-113`): its rung-2 swap sits outside its own null
 * check, so the two *category* rungs run with the pair exchanged and a category
 * script sees its own subject in `last_useitem`. That inversion is observable
 * only when neither item is type-bound, and the content written against it is
 * exactly nobody: every category `[opheldu]` script in the reference tree and in
 * ours is written `switch_obj(last_useitem) { case <the tool> : ... }`, which is
 * the *type*-rung orientation. So the reference's own content assumes the
 * invariant above and its engine does not provide it. Setting the pair from the
 * originals rather than toggling also makes the class of bug structurally
 * impossible: there is no state to leave half-exchanged.
 */
static void
opheldu_orient(
    struct ToriRSServerPlayer* player,
    const struct OpHeldUPair* pair,
    int bound_to_use_obj)
{
    if( bound_to_use_obj )
    {
        player->last_item = pair->use_obj;
        player->last_slot = pair->use_slot;
        player->last_useitem = pair->obj;
        player->last_useslot = pair->obj_slot;
    }
    else
    {
        player->last_item = pair->obj;
        player->last_slot = pair->obj_slot;
        player->last_useitem = pair->use_obj;
        player->last_useslot = pair->use_slot;
    }
}

/*
 * What each `[opheldu]` rung is keyed on, for the trace.
 *
 * The trace is the whole reason a swallowed pair took this long to find: from
 * outside, "the first rung that matched said nothing happened" and "nothing is
 * bound at all" are one message. This says which script answered and whether it
 * declined, which is the difference.
 */
static const char* const k_opheldu_rung_name[4] = {
    "clicked type",
    "dragged type",
    "clicked category",
    "dragged category",
};

int
ToriRSServer_ScriptsRunOpheldu(
    struct ToriRSServer* srv,
    int obj_type,
    int obj_category,
    int use_obj_type,
    int use_obj_category)
{
    struct ToriRSServerPlayer* player = srv->active_player;
    struct OpHeldUPair pair;
    int any_declined = 0;

    /*
     * Four rungs, `getByTriggerSpecific` throughout: `OpHeldUHandler` never asks
     * for `[opheldu,_]`, and the reference tree has none — a wildcard here would
     * swallow every "use A on B" in the game the moment somebody wrote one.
     *
     * Rung ORDER is the reference's: both types before either category, so a tool
     * with a type binding answers whichever way round the player dragged it, and a
     * category binding is reached only when neither item is named.
     *
     * `bound_to_use_obj` is what makes the orientation right per rung rather than
     * accumulated: see `opheldu_orient`.
     */
    const struct
    {
        int32_t type;
        int32_t category;
        int bound_to_use_obj;
    } rungs[4] = {
        { obj_type, -1, 0 },          /* 1 — the item that was clicked */
        { use_obj_type, -1, 1 },      /* 2 — the item that was dragged */
        { -1, obj_category, 0 },      /* 3 — the clicked item's category */
        { -1, use_obj_category, 1 },  /* 4 — the dragged item's category */
    };

    if( !srv->scripts_ok )
        return TORIRSSERVER_TRIGGER_NONE;

    srv->dispatch_declined = 0;

    /* `handle_opheldu` latched the pair in its clicked/dragged orientation; every
     * rung below re-states it from these rather than toggling. */
    pair.obj = player->last_item;
    pair.obj_slot = player->last_slot;
    pair.use_obj = player->last_useitem;
    pair.use_slot = player->last_useslot;

    for( int i = 0; i < 4; i++ )
    {
        const struct SSVM_Script* script;
        int declined = 0;
        int result;

        /* A category of -1 is "this obj has none", not a key to look up — and
         * `getByTriggerSpecific(-1, -1)` would ask for the wildcard this dispatch
         * deliberately does not have. */
        if( rungs[i].type == -1 && rungs[i].category == -1 )
            continue;

        script = SSVM_ProviderGetByTriggerSpecific(srv->scripts, SS_TRIGGER_OPHELDU,
                                                   rungs[i].type, rungs[i].category);
        if( !script )
        {
            if( srv->verbose )
                fprintf(stderr, "torirsserver:   opheldu rung %d (%s) — nothing bound\n", i + 1,
                        k_opheldu_rung_name[i]);
            continue;
        }

        opheldu_orient(player, &pair, rungs[i].bound_to_use_obj);
        result = run_rung(srv, script, -1, -1, -1, &declined);
        if( srv->verbose )
            fprintf(stderr, "torirsserver:   opheldu rung %d (%s) -> %s %s\n", i + 1,
                    k_opheldu_rung_name[i], script->name ? script->name : "?",
                    result != TORIRSSERVER_TRIGGER_RAN ? "FAILED"
                                                  : declined ? "declined" : "handled it");
        if( result != TORIRSSERVER_TRIGGER_RAN || !declined )
            return result;
        any_declined = 1;
    }

    /*
     * The pair goes back the way the packet described it before the caller answers
     * "nothing interesting happens". A declining rung has already moved
     * `last_item`, and leaving it moved would mean a later reader of `last_item`
     * saw whichever rung happened to decline last.
     */
    opheldu_orient(player, &pair, 0);

    if( any_declined )
    {
        srv->dispatch_declined = 1;
        return TORIRSSERVER_TRIGGER_NONE;
    }

    if( srv->verbose )
    {
        char label[192];

        /* The *clicked* item names the miss, which is what the reference prints
         * (`No trigger for [opheldu,${objType.debugname}]`). */
        fprintf(stderr, "torirsserver: no trigger for %s\n",
                trigger_label(SS_TRIGGER_OPHELDU, obj_type, label, sizeof(label)));
    }
    return TORIRSSERVER_TRIGGER_NONE;
}

/* ------------------------------------------------------------------ */
/* Engine fallbacks                                                    */
/* ------------------------------------------------------------------ */

/*
 * An opcode a `blocked_on` string names as the thing that is missing.
 *
 * This exists for the assertion under it, not for the documentation. `ai_queue3`
 * printed "drop tables need npc categories" at every boot for two stages after
 * categories, the category rung and 69 ported drop-table files had all landed.
 * Nothing was wrong with the code — the *reason* had expired, and an expired
 * reason reads exactly like a live one. Prose cannot go stale loudly, so the
 * citation is machine-readable as well as printed: a row that says
 * `OC_WEARPOS` is missing is only true while nothing implements `OC_WEARPOS`,
 * and `fallback_stale_blockers` says so at boot on the day somebody does.
 *
 * `value` is the opcode's number from `ss_opcode.h`, or **-1 for an opcode
 * nothing has even declared yet** — which is a real state here: `LAST_VERB` is
 * a reader two rows are blocked on and there is no `SS_OP_LAST_VERB` at all.
 * `TORIRSSERVER_BLOCKER_*` below turns the declaration itself into the switch, so
 * declaring the opcode is enough to put it under the check. Every other row
 * uses `BLOCKING_OP(sym)`, which stringifies the same token it evaluates: the
 * printed name and the checked value cannot be edited apart.
 *
 * The check is deliberately one-directional. It catches "this row cites
 * something that is now present", which is the failure that hides; it cannot
 * catch a blocker that is not an opcode (an unwritten entity binding, a
 * component that is armed but not bound), and those are cited in the text with
 * the grep that settles them instead.
 */
struct FallbackBlockingOp
{
    /** As `ss_opcode.h` spells it, so the text and the check cannot drift. */
    const char* name;
    /** Its value, or -1 when nothing declares it yet. */
    int value;
};

/* Nothing declares a `last_verb` reader. The engine latches the verb
 * (`torirs_server_world.c`, `player->last_verb = op_num`) and content cannot read it,
 * which is the shared blocker under `inv_button` and `if_button`. Written as an
 * #ifdef rather than a -1 literal so that declaring the opcode automatically
 * hands it to the staleness check — and under a private name so that a stray
 * `case SS_OP_LAST_VERB:` can never pick up a placeholder. */
#ifdef SS_OP_LAST_VERB
#define TORIRSSERVER_BLOCKER_LAST_VERB SS_OP_LAST_VERB
#else
#define TORIRSSERVER_BLOCKER_LAST_VERB (-1)
#endif

/* Stringify the symbol rather than repeating it, so the printed name and the
 * checked value are the same token and cannot be edited apart. */
#define BLOCKING_OP(sym) { #sym, (sym) }

static const struct FallbackBlockingOp k_blocked_opnpc[] = {
    /* None. `opnpc` is not blocked on an opcode — see its text. */
    { NULL, 0 }
};

/* `k_blocked_oploc` was here. It listed P_OPLOC, LOC_CATEGORY and LC_CATEGORY;
 * all three landed 2026-08-02 and the list emptied in the same commit, because
 * `fallback_stale_blockers` is fatal in the selftest and a row cannot outlive
 * its cited opcodes quietly. The row itself outlived them by a day — see
 * `enum ToriRSServerFallback` in torirs_server.h for what the last blocker actually was —
 * and went the same day the content that answers it landed. */

/*
 * `k_blocked_opobj` was here and is gone with its row, in two stages that are
 * worth keeping as the worked example of the order §2.5 asks for.
 *
 * Stage one widened the surface: it cited OBJ_COORD, OBJ_COUNT, OBJ_DEL,
 * OBJ_FIND, OBJ_TAKEITEM and OBJ_TYPE; five landed in torirs_server_ops_obj.c and
 * OBJ_FIND came *off the list* rather than being implemented, because
 * `[opobj3,_]` does not call it — the dispatch supplies the active obj. The row
 * then stood with an empty `blocked_ops` and a text saying so.
 *
 * Stage two moved the behaviour and deleted the row. What made the deletion
 * checkable rather than hopeful: the two selftest legs were re-pointed at
 * OPOBJ3 *while `interaction_engine_obj` was still present* and unbinding
 * `[opobj3,_]` left them **green**, because the fallback answered. That is the
 * measurement — a leg that stays green under the mutation is measuring the C —
 * and it is why the legs could only become evidence once the C was gone.
 */

/*
 * `k_blocked_opheld` is gone with TORIRSSERVER_FALLBACK_OPHELD, 2026-08-02, and the
 * account of what it cited is worth keeping because only five of its seven
 * opcodes were ever true.
 *
 * Landed the stage before the row went: OC_WEARPOS / OC_WEARPOS2 / OC_WEARPOS3
 * (torirs_server_ops_obj.c) and INV_MOVEFROMSLOT / INV_DROPSLOT (torirs_server_ops_inv.c).
 * `inv_moveitem` moved into that file at the same time and grew the generic
 * container-to-container arm every equip and unequip path needs — it had been
 * three bank arms and a printf, and `gen_opcode_coverage.py` reported the
 * opcode *covered*, because a `case` label is all it can see.
 *
 * Came OFF the list without being implemented: BUILDAPPEARANCE (2004), because
 * `put_appearance` reads `player->worn` unconditionally and the opcode's job in
 * the reference is to *select* the container the encoder reads — so accepting
 * the argument and raising the mask would be plausible, wrong and quiet
 * (§3.13d); and P_CLEARPENDINGACTION (2070), misfiled, because
 * `ToriRSServer_WorldClearPendingAction` was never called from `handle_opheld` at
 * all. Both are still true and neither is implemented.
 *
 * And what actually blocked the row was none of those. It was that **the level
 * requirement had no script-readable form**: dispatch is content-first, so
 * binding `[opheld2,_]` stopped `ToriRSServer_EquipmentMayWear` running and took
 * the gate with it. That is a data-relocation job, and the stage that did it
 * found the thing every prose account of the data had wrong — the requirement
 * is a MERGE, `.obj` overlay plus the cache's own skillrequire/levelrequire
 * params, and the overlay is 59% of it. See
 * skill_combat/scripts/levelrequire.rs2.
 */

static const struct FallbackBlockingOp k_blocked_inv_button[] = {
    { NULL, 0 }
};

static const struct FallbackBlockingOp k_blocked_if_button[] = {
    { NULL, 0 }
};

/*
 * The C that still answers a trigger nothing is bound to, named and counted.
 *
 * `blocked_on` is the whole reason each row is allowed to exist. It is not a
 * comment: the boot prints it, so "why is this behaviour not content?" has an
 * answer at the point where somebody is asking, and a row whose blocker has
 * been cleared is visibly a row to delete.
 *
 * Each string is written to be **checkable in one command** rather than
 * plausible — a symbol as its header spells it, a file:line, a line count, a
 * reference path — because the previous set read plausibly and one of them was
 * simply false. Where a blocker is partly cleared the string says which part;
 * "NOT x" openings are load-bearing, not rhetoric. `blocked_ops` is the machine
 * half of the same job.
 *
 * The rule this table encodes is PORTING_GUIDE §2.5's: widen the ServerScript
 * surface until a script can say it, move the behaviour, delete the row. Adding
 * a row goes the other way and is not a choice a content port gets to make.
 */
static const struct
{
    const char* name;
    const char* blocked_on;
    /** The opcodes `blocked_on` names, NULL-terminated. Never NULL itself. */
    const struct FallbackBlockingOp* blocked_ops;
} k_engine_fallbacks[TORIRSSERVER_FALLBACK_COUNT] = {
    [TORIRSSERVER_FALLBACK_OPNPC] =
        { "opnpc",
          "NOT an opcode — this is the one row with an empty blocked_ops, and the only "
          "one whose blocker is a volume of C. torirs_server_combat.c is 1,061 lines "
          "(`wc -l`) and stays engine; the row itself is interaction_engine_npc, "
          "torirs_server_world.c:2333-2364, whose greeting half is ALREADY content "
          "([proc,npc_default_chat], player/messages.rs2:137) — so what is left in C is "
          "a strcmp against the cache's own Attack verb and the FACE_ENTITY latch "
          "before the proc, and nothing else. [opnpc2,_] now owns Attack for op 2 "
          "and p_opnpc re-dispatches through the interaction path for all ops. "
          "Blocked on the remaining combat_engage callers moving to content",
          k_blocked_opnpc },
    [TORIRSSERVER_FALLBACK_INV_BUTTON] =
        { "inv_button",
          "Bank item ops no longer arrive as INV_BUTTON — the client emits "
          "IF_BUTTON1..10 for IF_SETEVENTS-armed component rows, and content binds "
          "[if_button1..8,bankmain:items] / [if_button2..8,bankside:items] "
          "(interface_bank/scripts/bank.rs2, bank_deposit.rs2). This row remains for "
          "any other unbound INV_BUTTON surface (worn tab, shops). The C bank "
          "quantity ladder is deleted; ToriRSServer_BankHandleButton is a no-op stub",
          k_blocked_inv_button },
    [TORIRSSERVER_FALLBACK_IF_BUTTON] =
        { "if_button",
          "Bank settings and item ops are content on the armed components "
          "(bankmain:{swap_insert,note,quantity*,depositinv,depositworn,items}). "
          "ToriRSServer_BankHandleButton is a no-op stub. This row remains for other "
          "unbound IF_BUTTON clicks outside the bank. No last_verb opcode — numbered "
          "[if_buttonN] encodes the op index (skill_guide pattern)",
          k_blocked_if_button },
};

/**
 * How many live rows cite an opcode that is now implemented — 0 is the only
 * acceptable answer, and the selftest pins it there.
 *
 * This is the guard against the way this list actually failed. A row does not
 * usually go wrong by being wrong when it is written; it goes wrong when the
 * thing it is waiting for arrives and nobody comes back to the row, at which
 * point it keeps printing a reason that is no longer a reason. That is not
 * visible by reading — a stale blocker and a live one look identical — so the
 * moment the opcode lands, this says so, at boot, unconditionally, next to the
 * count it invalidates.
 *
 * It is loud rather than fatal on purpose: the opcode landing is *progress*,
 * and a server that refuses to start because somebody implemented OBJ_DEL would
 * teach exactly the wrong lesson. The selftest is where it is an error.
 */
static int
fallback_stale_blockers(void)
{
    int stale = 0;

    for( int i = 0; i < TORIRSSERVER_FALLBACK_COUNT; i++ )
    {
        const struct FallbackBlockingOp* op = k_engine_fallbacks[i].blocked_ops;

        for( ; op && op->name; op++ )
        {
            /* -1 is "nothing declares it yet", which is a blocker in its own
             * right and cannot be looked up. */
            if( op->value < 0 || !opcode_implemented(op->value) )
                continue;
            fprintf(stderr,
                    "torirsserver: STALE BLOCKER — the `%s` engine fallback says it is waiting "
                    "on %s (%d), and %s is implemented now. Either the row can go or its "
                    "reason has to be rewritten to what is still true "
                    "(k_engine_fallbacks[], torirs_server_scripts.c).\n",
                    k_engine_fallbacks[i].name, op->name, op->value, op->name);
            stale++;
        }
    }
    return stale;
}

int
ToriRSServer_ScriptsStaleBlockers(void)
{
    return fallback_stale_blockers();
}

int
ToriRSServer_ScriptsReportFallbacks(struct ToriRSServer* srv)
{
    /* One line by default and the roll under TORIRSSERVER_VERBOSE: the count is what
     * anyone reading a boot log needs (it should be going down), the reasons are
     * what somebody working on one needs. Both matter; only one of them belongs
     * in a log that also prints twenty times during the selftest. */
    fprintf(stderr, "torirsserver: %d engine fallback(s) still answer triggers content does not bind\n",
            TORIRSSERVER_FALLBACK_COUNT);
    /* Not behind `verbose`: this one is an error report, and the reader who
     * needs it is the person who just implemented the opcode — who has no
     * reason to be running with TORIRSSERVER_VERBOSE and every reason to be told. */
    fallback_stale_blockers();
    if( srv->verbose )
    {
        for( int i = 0; i < TORIRSSERVER_FALLBACK_COUNT; i++ )
            fprintf(stderr, "  %-12s blocked on: %s\n", k_engine_fallbacks[i].name,
                    k_engine_fallbacks[i].blocked_on);
    }
    return TORIRSSERVER_FALLBACK_COUNT;
}

/* ------------------------------------------------------------------ */
/* Shadowed engine verbs                                               */
/* ------------------------------------------------------------------ */

/**
 * Which `p_op*` discharges the obligation a shadowing script takes on.
 *
 * Binding `[opnpc2,goblin]` does not *add* to the engine's Attack — it replaces
 * it, because the engine's verb handling only runs when nothing was bound. A
 * script that means to keep the fight has to say so, and the way it says so is
 * to re-issue the op itself.
 */
static int
discharging_opcode(int trigger)
{
    if( trigger >= SS_TRIGGER_OPNPC1 && trigger <= SS_TRIGGER_OPNPC5 )
        return SS_OP_P_OPNPC;
    /* `[oploc<n>] -> SS_OP_P_OPLOC` was here and is unreachable now rather than
     * merely unused: with `interaction_engine_loc` deleted the engine claims no
     * loc verb, so `ToriRSServer_WorldEngineClaimedVerb` never returns one for a
     * loc and the report never asks what would discharge it. Leaving it would be
     * advice to write `[oploc1,_] p_oploc(1)`, which is an infinite recursion —
     * `p_oploc` re-issues the op and the op is this trigger. `P_OPLOC` itself
     * stays implemented (torirs_server_ops_player.c); it is what a script that means
     * to *resume* an op calls, which is all 43 of the reference's callers. */
    if( trigger >= SS_TRIGGER_OPHELD1 && trigger <= SS_TRIGGER_OPHELD5 )
        return SS_OP_P_OPHELD;
    return -1;
}

static int
script_calls(
    const struct SSVM_Script* script,
    int opcode)
{
    for( int i = 0; i < script->op_count; i++ )
        if( (int)script->opcodes[i] == opcode )
            return 1;
    return 0;
}

/**
 * Report every trigger content binds over a verb the engine answers itself.
 *
 * This is triage §7.7, which the fallback inversion did not close and could
 * not: inverting the fallback made a *missing* script loud, and this is the
 * opposite failure — a script that is present, runs fine, and quietly takes a
 * verb the engine was going to handle. Nothing fails. The goblin simply says
 * its line and stands there.
 *
 * That is not hypothetical. `skill_combat/combat.rs2` carries the scar in its
 * own header: a goblin's Attack is op 2, `[opnpc2,goblin]` replaced it, and the
 * fix was to add `p_opnpc(2)` back. The file then states the rule for everyone
 * else — *"any other script that binds an op the cache gives a verb to has the
 * same obligation"* — and until now nothing enforced it. §7.7's warning is that
 * this gets much worse on import: the reference binds 634 `[opnpc1]` and 867
 * `[oploc1]` triggers, and `levelrequire/` alone binds 304 `[opheld2]`, which
 * is the verb the engine equips on.
 *
 * At **load**, not at call time, for the reason `ToriRSServer_ScriptsReportGaps`
 * gives: a script behind a quest step may never be triggered by anyone, and a
 * swallowed verb that nobody clicks this session is still a swallowed verb.
 *
 * Only exact-type bindings are checked. A `[opnpc1,_bandit]` category binding
 * or a bare `_` wildcard names no record, so there is no op list to read and
 * nothing to compare — those are invisible here, and saying so is better than
 * implying the check is total.
 *
 * The discharge test is deliberately generous: *any* `p_op*` of the right
 * family counts, without checking that its argument is the op that was bound.
 * The failure this exists to catch is the script that forgot entirely; one that
 * re-issues the wrong index is a different bug and this cannot see it.
 *
 * **A hit is a review item, not a defect**, and the distinction is not one this
 * can make. The second legitimate way to discharge the obligation is to do the
 * engine's job yourself, and nothing static tells that apart from doing
 * something else. So this prints a list and never fails a load. It is the same
 * posture as `ToriRSServer_Pack`'s foreign-area spawn-prefix warning (triage §10.2):
 * a prompt to go and look, not a verdict.
 *
 * **The list is empty as of 2026-08-02, and getting it there was an eviction
 * rather than a fix.** It read 1, then 20 when the ladders landed, then 97 when
 * the 78 bank booths did — every entry a script taking a verb
 * `interaction_engine_loc` still answered. Deleting that function took all 97
 * out at once: the engine claims no loc verb now, so there is nothing for a
 * door, a ladder or a booth to shadow. What is left claimable is "Attack" on an
 * npc and "Wear"/"Wield"/"Drop" on a held obj. The 97 were the honest reading —
 * every one of those scripts *was* replacing engine behaviour — and the right
 * response to a review list that long was to delete what it was reviewing.
 *
 * Returns the number of scripts that shadow a verb without re-issuing it.
 */
int
ToriRSServer_ScriptsReportShadowedOps(struct ToriRSServer* srv)
{
    int shadowed = 0;

    if( !srv->scripts_ok || !srv->scripts )
        return 0;

    for( int i = 0; i < srv->scripts->count; i++ )
    {
        const struct SSVM_Script* script = &srv->scripts->scripts[i];
        const char* verb;
        int trigger;
        int discharge;

        if( script->op_count <= 0 || !script->opcodes )
            continue;
        /* Name-addressed scripts (proc, label, queue, …) carry -1 and bind no
         * trigger at all. */
        if( script->lookup_key < 0 )
            continue;
        /* Bits 8..9 are the subject mode; only an exact type names a record. */
        if( ((script->lookup_key >> 8) & 0x3) != SS_LOOKUP_TYPE )
            continue;

        trigger = SSVM_LookupKeyTrigger(script->lookup_key);
        verb = ToriRSServer_WorldEngineClaimedVerb(trigger, script->lookup_key >> 10);
        if( !verb )
            continue;

        /*
         * A `_` wildcard on the same trigger has already taken the verb, so the
         * engine's fallback cannot run for ANY record and a per-record script
         * cannot be shadowing it.
         *
         * This is what made the report say 43 while nothing was wrong.
         * `skill_combat/combat.rs2` binds `[opnpc2,_]`, and dispatch resolves
         * type -> category -> `_` (SSVM_ProviderGetByTrigger), so the Attack
         * branch in `interaction_engine_npc` has been unreachable for npc op 2
         * since that binding landed. `k_engine_npc_verbs` still claimed it —
         * the same dead-claim the loc family carried before its eviction, and
         * the hazard that list's own header warns about: two statements of one
         * fact, and only one of them was updated.
         *
         * Asking dispatch rather than re-deriving the answer is the point. This
         * TIGHTENS the report — it now agrees with what the engine will
         * actually do — rather than tolerating a category of hit.
         */
        if( SSVM_ProviderGetByTriggerSpecific(srv->scripts, trigger, -1, -1) )
            continue;

        discharge = discharging_opcode(trigger);
        if( discharge >= 0 && script_calls(script, discharge) )
            continue;

        if( shadowed == 0 )
            fprintf(stderr,
                    "torirsserver: content binds a trigger over a verb the engine answers itself:\n");
        fprintf(stderr, "  %-34s takes \"%s\" without re-issuing it (%s)\n",
                script->name ? script->name : "?", verb,
                discharge >= 0 ? SSVM_OpcodeName(discharge) : "?");
        shadowed++;
    }

    if( shadowed )
        fprintf(stderr,
                "torirsserver: %d script(s) shadow an engine verb — check each does the engine's "
                "job or means not to; this is a review list, not an error\n",
                shadowed);
    return shadowed;
}

/*
 * Every `settimer`/`queue`/`walktrigger` argument in the pack, checked against
 * the script it actually points at.
 *
 * This is the static half of the `script_kind_allowed` guard, and it exists
 * because the runtime half only fires when the timer does. `settimer(poison,
 * 30)` compiled to `settimer(273, 30)` — obj 273 is also named `poison` — and
 * the engine ran `[label,woman_im_looking_for_a_lady]` on a 30-tick timer. That
 * one was loud because poison is common; the same mistake on a quest-completion
 * queue sits unfired until somebody finishes the quest, and there were **nine**
 * of those, all pointing at script id 0.
 *
 * So the question is asked of the pack rather than of a session: for each of the
 * twelve commands whose first argument is a script (the reference's typed
 * signatures in `content/scripts/engine.rs2` are the authority — `[command,
 * settimer](timer $timer, int $interval)`), read the constant that supplied it
 * and check the target's trigger word.
 *
 * **Only fully-constant argument lists are checked, and that is a real limit
 * rather than a formality.** The script id is `int_in` instructions back from
 * the command, which is only true if every argument in between compiled to
 * exactly one push. `settimer(x, calc(...))` or `queue(x, 0, ~pick())` push a
 * variable number, so the window is wrong and those are skipped rather than
 * guessed at. In this tree that covers the overwhelming majority — a script
 * name is a literal by construction — but a silent skip is still a hole, so the
 * count of skipped sites is reported next to the count of checked ones.
 *
 * Returns the number of mismatches.
 */
int
ToriRSServer_ScriptsReportScriptIdArgs(struct ToriRSServer* srv)
{
    static const struct
    {
        int opcode;
        const char* trigger;
    } k_script_args[] = {
        { SS_OP_QUEUE, "queue" },
        { SS_OP_STRONGQUEUE, "queue" },
        { SS_OP_WEAKQUEUE, "queue" },
        { SS_OP_LONGQUEUE, "queue" },
        { SS_OP_CLEARQUEUE, "queue" },
        { SS_OP_GETQUEUE, "queue" },
        { SS_OP_SETTIMER, "timer" },
        { SS_OP_CLEARTIMER, "timer" },
        { SS_OP_GETTIMER, "timer" },
        { SS_OP_SOFTTIMER, "softtimer" },
        { SS_OP_CLEARSOFTTIMER, "softtimer" },
        { SS_OP_WALKTRIGGER, "walktrigger" },
    };
    int bad = 0;
    int checked = 0;
    int skipped = 0;

    if( !srv->scripts_ok || !srv->scripts )
        return 0;

    for( int i = 0; i < srv->scripts->count; i++ )
    {
        const struct SSVM_Script* script = &srv->scripts->scripts[i];

        if( script->op_count <= 0 || !script->opcodes )
            continue;

        for( int op = 0; op < script->op_count; op++ )
        {
            const struct SSVM_OpcodeMeta* meta;
            const struct SSVM_Script* target;
            const char* want = NULL;
            int argc;
            int at;

            for( size_t k = 0; k < sizeof(k_script_args) / sizeof(k_script_args[0]); k++ )
            {
                if( (int)script->opcodes[op] == k_script_args[k].opcode )
                {
                    want = k_script_args[k].trigger;
                    break;
                }
            }
            if( !want )
                continue;

            meta = SSVM_OpcodeMeta((int)script->opcodes[op]);
            argc = meta ? (int)meta->int_in : 0;
            at = op - argc;
            if( argc <= 0 || at < 0 )
            {
                skipped++;
                continue;
            }
            /* The window has to be one push per argument for the offset to
             * mean anything. Anything else and we cannot see the constant. */
            for( int w = at; w < op; w++ )
            {
                if( script->opcodes[w] != SS_OP_PUSH_CONSTANT_INT )
                {
                    at = -1;
                    break;
                }
            }
            if( at < 0 )
            {
                skipped++;
                continue;
            }

            checked++;
            /* `walktrigger(null)` is the reference spelling for disarming the
             * one-shot hook. The runtime stores -1 as its sentinel, so the
             * pack validator must distinguish that intentional clear from a
             * mistyped positive script id. Queue/timer commands do not share
             * this contract and remain strict. */
            if( script->opcodes[op] == SS_OP_WALKTRIGGER &&
                script->int_operands[at] == -1 )
                continue;
            target = SSVM_ProviderGet(srv->scripts, script->int_operands[at]);
            if( !target )
            {
                fprintf(stderr,
                        "torirsserver: %s passes %d to %s — no script has that id\n",
                        script->name ? script->name : "?", script->int_operands[at],
                        SSVM_OpcodeName((int)script->opcodes[op]));
                bad++;
                continue;
            }
            if( script_kind_matches(target, want) )
                continue;
            fprintf(stderr,
                    "torirsserver: %s passes %s to %s — that argument names a %s script\n",
                    script->name ? script->name : "?",
                    target->name ? target->name : "?",
                    SSVM_OpcodeName((int)script->opcodes[op]), want);
            bad++;
        }
    }

    if( bad )
        fprintf(stderr,
                "torirsserver: %d script-id argument(s) point at the wrong kind of script — a "
                "name that resolved to a content id, not a script (docs/serverscript.md, "
                "\"a queue/timer argument names a script\")\n",
                bad);
    if( srv->verbose )
        fprintf(stderr, "torirsserver: %d script-id argument(s) checked, %d not constant\n",
                checked, skipped);
    return bad;
}

int
ToriRSServer_ScriptsFallback(
    struct ToriRSServer* srv,
    enum ToriRSServerFallback which,
    int result)
{
    const char* name;

    if( which < 0 || which >= TORIRSSERVER_FALLBACK_COUNT )
        return 0;
    name = k_engine_fallbacks[which].name;

    if( result != TORIRSSERVER_TRIGGER_NONE )
    {
        /* RAN needs no word — content did its job. FAILED does: the click is
         * about to do nothing at all, and the reason is a script that aborted
         * several lines above this in the log. Saying so is what stops that
         * being read as an engine bug. */
        if( result == TORIRSSERVER_TRIGGER_FAILED )
            fprintf(stderr,
                    "torirsserver: a bound script failed; the `%s` engine fallback is NOT "
                    "running in its place\n",
                    name);
        return 0;
    }

    if( !srv->scripts_ok )
    {
        if( srv->verbose )
            fprintf(stderr, "torirsserver: no script pack — `%s` does nothing\n", name);
        return 0;
    }

    /*
     * Content looked at this and said it was not its business.
     *
     * `trigger_decline` answers the caller with NONE, because the player sees the
     * same "nothing interesting happens" either way — but an engine fallback
     * stands in for content that is MISSING, and this is content that is present
     * and declined. Standing in here is how a door opens because somebody used a
     * bucket on it. Consumed rather than merely read: one dispatch, one answer.
     */
    if( srv->dispatch_declined )
    {
        srv->dispatch_declined = 0;
        if( srv->verbose )
            fprintf(stderr, "torirsserver: content declined; the `%s` engine fallback is NOT "
                            "running in its place\n",
                    name);
        return 0;
    }

    /* The name, not the blocker. This fires once per unbound click and the
     * blockers are now paragraphs — that is the price of making each one
     * checkable in one command, and the place to pay it is the boot roll, which
     * prints them once. Repeating one of them per click buries the packet log
     * it is supposed to sit beside. */
    if( srv->verbose )
        fprintf(stderr, "torirsserver: engine fallback `%s` ran (why it is still C: the boot "
                        "roll, TORIRSSERVER_VERBOSE)\n",
                name);
    return 1;
}

/* ------------------------------------------------------------------ */
/* ::commands                                                          */
/* ------------------------------------------------------------------ */

/*
 * A `::command`, dispatched to `[debugproc,<name>]`.
 *
 * This is how the reference writes a cheat: `ClientCheatHandler` splits the
 * line, looks the debugproc up by name, and fills its declared parameters from
 * the words that follow. Everything in its
 * `content/scripts/_test/scripts/cheats/` is one of these, and none of it is in
 * the engine.
 *
 * That is the point rather than a detail. A cheat is a *content* entry point —
 * "toggle this prayer", "give me that item" — and every one written in C is a
 * second implementation of something content already does, drifting from the
 * shipped path exactly where it matters. `::pray` was one: it reached a prayer
 * through a C module that knew the prayer table, so it tested that module and
 * not the button.
 *
 * The argument types come from the script itself. LostCity resolves obj, npc,
 * loc and component names here as well; this resolves whatever the content tree
 * names, through the same packs the compiler used, and takes anything else as an
 * int. A word that does not resolve is -1, which every reasonable script tests
 * for anyway.
 */
static int
debugproc_arg_type(
    uint8_t type,
    enum ToriRSServerPackKind* out_kind)
{
    /* ScriptVarType.getTypeChar, the same codes ssc_symbols.c compiles with. */
    switch( type )
    {
    case 105: /* int */
    case 49:  /* boolean */
        return 0;
    case 115: /* string */
        return 1;
    case 111: /* obj */
    case 79:  /* namedobj */
        *out_kind = TORIRSSERVER_PACK_OBJ;
        return 2;
    case 110: /* npc */
        *out_kind = TORIRSSERVER_PACK_NPC;
        return 2;
    case 108: /* loc */
        *out_kind = TORIRSSERVER_PACK_LOC;
        return 2;
    case 73: /* component */
        *out_kind = TORIRSSERVER_PACK_COMPONENT;
        return 2;
    case 97: /* interface */
        *out_kind = TORIRSSERVER_PACK_INTERFACE;
        return 2;
    case 118: /* inv */
        *out_kind = TORIRSSERVER_PACK_INV;
        return 2;
    case 65: /* seq */
        *out_kind = TORIRSSERVER_PACK_SEQ;
        return 2;
    case 116: /* spotanim */
        *out_kind = TORIRSSERVER_PACK_SPOTANIM;
        return 2;
    case 83: /* stat */
        *out_kind = TORIRSSERVER_PACK_STAT;
        return 2;
    case 208: /* dbrow */
        /* Not one the reference resolves — its cheats predate the db tables.
         * It is here because a row name is the only printable handle a db row
         * has: `::complete quest_cooksassistant` against a table whose ids are
         * the cache's, which no cheat could be expected to type. */
        *out_kind = TORIRSSERVER_PACK_DBROW;
        return 2;
    case 99: /* coord */
        return 3;
    default:
        return 0;
    }
}

int
ToriRSServer_ScriptsRunDebugproc(
    struct ToriRSServer* srv,
    const char* line)
{
    const struct SSVM_Script* script;
    char name[192];
    char command[64];
    int32_t argv[SS_MAX_PARAM_TYPES];
    const char* strv[SS_MAX_PARAM_TYPES];
    char words[SS_MAX_PARAM_TYPES][64];
    int argc = 0;
    int strc = 0;
    const char* cursor = line;
    int length = 0;

    if( !srv->scripts_ok )
        return 0;

    while( *cursor && *cursor != ' ' && length + 1 < (int)sizeof(command) )
        command[length++] = *cursor++;
    command[length] = '\0';
    if( length == 0 )
        return TORIRSSERVER_TRIGGER_NONE;

    snprintf(name, sizeof(name), "[debugproc,%s]", command);
    script = SSVM_ProviderGetByName(srv->scripts, name);
    if( !script )
        return TORIRSSERVER_TRIGGER_NONE;

    for( int i = 0; i < script->param_type_count && i < SS_MAX_PARAM_TYPES; i++ )
    {
        enum ToriRSServerPackKind kind = TORIRSSERVER_PACK_COUNT;
        int form = debugproc_arg_type(script->param_types[i], &kind);
        char* word = words[i];
        int taken = 0;

        while( *cursor == ' ' )
            cursor++;
        while( *cursor && *cursor != ' ' && taken + 1 < 64 )
            word[taken++] = *cursor++;
        word[taken] = '\0';

        if( form == 1 )
            strv[strc++] = word;
        else if( form == 2 )
            argv[argc++] = taken ? ToriRSServer_ContentSymbol(kind, word) : -1;
        else if( form == 3 )
        {
            /* `level_mx_mz_lx_lz`, the coord literal's own spelling — the
             * reference parses one here too (ClientCheatHandler's
             * ScriptVarType.COORD arm) and it is the argument a test debugproc
             * most wants, because every other way of naming a tile means
             * hardcoding one in the script. */
            int lvl = 0, mx = 0, mz = 0, lx = 0, lz = 0;
            if( taken && sscanf(word, "%d_%d_%d_%d_%d", &lvl, &mx, &mz, &lx, &lz) == 5 )
                argv[argc++] = ToriRSServer_CoordPack(lvl, mx * 64 + lx, mz * 64 + lz);
            else
                argv[argc++] = -1;
        }
        else
            argv[argc++] = taken ? (int32_t)strtol(word, NULL, 10) : 0;
    }

    if( srv->verbose )
        fprintf(stderr, "torirsserver: %s with %d int and %d string args\n", name, argc, strc);
    return ToriRSServer_ScriptsRunHookSv(srv, script, argv, argc, strv, strc)
               ? TORIRSSERVER_TRIGGER_RAN
               : TORIRSSERVER_TRIGGER_FAILED;
}

/* ------------------------------------------------------------------ */
/* Host commands                                                       */
/* ------------------------------------------------------------------ */

/*
 * The active loc, resolved through its scene slot.
 *
 * Returns NULL when the slot no longer holds a live loc — which is a real
 * state, not a bug: a script can suspend between `loc_find` and `loc_change`,
 * and by the time it resumes somebody else may have taken the loc. The callers
 * abort on NULL rather than acting on whatever is in the slot now.
 */
static struct ToriRSServerSceneLoc*
script_active_loc(struct SSVM_State* state)
{
    struct ToriRSServer* srv = (struct ToriRSServer*)state->env->host.user;

    return ToriRSServer_ScriptLocResolve(srv, SSVM_ActiveSlot(state, SSVM_ENT_LOC, SSVM_PRIMARY));
}

/*
 * Zone-backed active-loc handles: the active loc beyond the scene window.
 *
 * A scene slot can only name a loc the window covers, but the reference's
 * `World.getLoc` reaches every zone in the world and content leans on that —
 * puro-puro's crop circle rotates through eight farms and at most one is ever
 * near a player. Out there the ZoneMap record *is* the loc, so the handle has
 * to carry the record's key, `(x, z, level, shape)`. It cannot carry the
 * record's index: `ToriRSServer_ZoneLocChanged` retires records by swap-remove,
 * so an index held across a suspend could come back naming a different loc,
 * where a re-looked-up key either finds the same record or finds none — the
 * same staleness contract the scene-slot convention already keeps.
 *
 * The two kinds share one intptr encoding: positive is `scene slot + 1`
 * (unchanged), negative is `-(ring index + 1)` into the key table below. The
 * ring recycles; a script parked across 256 fresh out-of-scene finds could see
 * its entry reused, which resolves as "the active loc is gone" or a different
 * out-of-scene loc — accepted, like slot reuse, because the resolver always
 * re-validates against the live ZoneMap.
 */
#define SCRIPT_ZONE_LOC_HANDLE_MAX 256

struct ScriptZoneLocKey
{
    int x, z, level;
    int shape;
    int used;
};

static struct ScriptZoneLocKey g_script_zone_loc_keys[SCRIPT_ZONE_LOC_HANDLE_MAX];
static int g_script_zone_loc_next;

void*
ToriRSServer_ScriptZoneLocHandle(
    int x,
    int z,
    int level,
    int shape)
{
    int idx;

    for( idx = 0; idx < SCRIPT_ZONE_LOC_HANDLE_MAX; idx++ )
    {
        struct ScriptZoneLocKey* key = &g_script_zone_loc_keys[idx];

        if( key->used && key->x == x && key->z == z && key->level == level &&
            key->shape == shape )
            return (void*)(intptr_t) - (idx + 1);
    }
    idx = g_script_zone_loc_next;
    g_script_zone_loc_next = (g_script_zone_loc_next + 1) % SCRIPT_ZONE_LOC_HANDLE_MAX;
    g_script_zone_loc_keys[idx].x = x;
    g_script_zone_loc_keys[idx].z = z;
    g_script_zone_loc_keys[idx].level = level;
    g_script_zone_loc_keys[idx].shape = shape;
    g_script_zone_loc_keys[idx].used = 1;
    return (void*)(intptr_t) - (idx + 1);
}

struct ToriRSServerSceneLoc*
ToriRSServer_ScriptLocResolve(
    struct ToriRSServer* srv,
    void* handle_ptr)
{
    intptr_t handle = (intptr_t)handle_ptr;

    if( handle > 0 )
    {
        struct ToriRSServerSceneLoc* loc = ToriRSServer_SceneLoc((int)handle - 1);

        if( !loc || !loc->active )
            return NULL;
        return loc;
    }
    if( handle < 0 )
    {
        /* A borrowed view with the resolver's lifetime: valid until the next
         * resolve, which is enough because every consumer copies what it needs
         * before doing anything that could resolve again. Mutations do not go
         * through this pointer anyway — `loc_del`/`loc_change`/`loc_anim` all
         * re-key on the coordinates. */
        static struct ToriRSServerSceneLoc view;
        struct ScriptZoneLocKey* key;
        struct ToriRSServerZoneLoc* rec;
        int idx = (int)(-handle) - 1;
        int width;
        int length;

        if( idx >= SCRIPT_ZONE_LOC_HANDLE_MAX )
            return NULL;
        key = &g_script_zone_loc_keys[idx];
        if( !key->used || !srv )
            return NULL;
        rec = ToriRSServer_ZoneLocFind(srv, key->x, key->z, key->level, key->shape);
        if( !rec || rec->loc_id < 0 )
            return NULL;
        memset(&view, 0, sizeof(view));
        view.loc_id = rec->loc_id;
        view.shape = key->shape;
        view.angle = rec->angle;
        view.x = key->x;
        view.z = key->z;
        view.level = key->level;
        ToriRSServer_LocFootprint(view.loc_id, &width, &length);
        if( (view.angle & 1) != 0 )
        {
            view.size_x = length;
            view.size_z = width;
        }
        else
        {
            view.size_x = width;
            view.size_z = length;
        }
        view.active = 1;
        return &view;
    }
    return NULL;
}

/*
 * The active npc, resolved through its slot rather than a stored pointer.
 *
 * A parked script outlives the tick that started it, and an npc can despawn or
 * have its slot reused while the script waits. `host_tag` carries the slot so a
 * resumed script either finds the same npc or finds none — never a different
 * one wearing the same address.
 */
/*
 * The active npc's slot, honouring the `.` operand.
 *
 * `host_tag` is the PRIMARY npc's slot and nothing else — it is stored as a
 * slot rather than a pointer so a parked script cannot resume onto a despawned
 * npc — and every npc command here resolved through it, dotted or not. So the
 * secondary npc pointer was *set* by four call sites and read by none: a `.npc_`
 * command silently answered about the primary.
 *
 * That was invisible while the only dotted npc code in the tree was
 * `[proc,.npc_findcount]`, whose iterator sets the secondary itself and whose
 * primary is normally the same npc anyway. It stopped being invisible with
 * `[ai_opnpc2]`, where the two are the attacker and its target: `.npc_uid`
 * returned the attacker's own uid, so an add fired its shot at itself and
 * queued its own damage onto itself.
 *
 * The secondary is held as a pointer into `srv->npcs`, so its slot is its index
 * — the same array the primary's slot indexes.
 */
static int
active_npc_slot(struct SSVM_State* state)
{
    struct ToriRSServer* srv = (struct ToriRSServer*)state->env->host.user;

    if( state->dot )
    {
        const struct ToriRSServerNpc* npc = SSVM_ActiveSlot(state, SSVM_ENT_NPC, SSVM_SECONDARY);

        if( !npc )
            return -1;
        return (int)(npc - srv->npcs);
    }
    return (int)state->host_tag - 1;
}

static struct ToriRSServerNpc*
active_npc(struct SSVM_State* state)
{
    struct ToriRSServer* srv = (struct ToriRSServer*)state->env->host.user;
    int slot = active_npc_slot(state);

    if( slot < 0 || slot >= TORIRSSERVER_NPC_MAX )
        return NULL;
    if( !srv->npcs[slot].active )
        return NULL;
    return &srv->npcs[slot];
}

/*
 * Resolve the script-visible player uid.  Revision-230 player uids are the
 * stable pool pid plus one (zero is the null sentinel); unlike npc uids there
 * is no generation word in the script value.  The active bit is therefore the
 * lifetime check every consumer must repeat after a suspended script resumes.
 */
static struct ToriRSServerPlayer*
player_by_uid(struct ToriRSServer* srv, int32_t uid)
{
    int pid = (int)uid - 1;

    if( pid < 0 || pid >= TORIRSSERVER_PLAYER_MAX )
        return NULL;
    if( !srv->players[pid].active || srv->players[pid].pid != pid )
        return NULL;
    return &srv->players[pid];
}

/** The online player on this world whose canonical base-37 name matches. */
static struct ToriRSServerPlayer*
player_by_display_name(
    struct ToriRSServer* srv,
    const char* display_name)
{
    int64_t wanted = (int64_t)strtobase37(display_name ? display_name : "");

    if( wanted == 0 )
        return NULL;
    for( int i = 0; i < TORIRSSERVER_PLAYER_MAX; i++ )
    {
        struct ToriRSServerPlayer* candidate = &srv->players[i];

        if( candidate->active && candidate->name37 == wanted )
            return candidate;
    }
    return NULL;
}

/*
 * Whether `::god` should refuse this stat write.
 *
 * The damage funnel is not the only way hitpoints go down. `stat_sub`,
 * `stat_drain` and a negative `stat_add` assign `player->hitpoints` straight
 * from the stat table, and content uses them — which is why an early version of
 * this flag looked like it worked in a unit test and still let the player die
 * in the Inferno. Only reductions are blocked: `stat_heal` and a normal boost
 * must still land, or god mode would also freeze the health it is protecting.
 */
static int
godmode_blocks_stat_write(
    struct ToriRSServerPlayer const* player,
    int stat,
    int target)
{
    return player->godmode && stat == TORIRSSERVER_STAT_HITPOINTS &&
           target < player->stat_boosted[stat];
}

/*
 * The level the content block authored for this npc's stat — its *base*, before
 * anything a script has drained.
 *
 * A rev-230 npc record carries no levels at all (§3.12), so the block is the
 * only source and there is nothing to fall back to. Hitpoints are absent on
 * purpose: they live on `npc->base_hitpoints`, which the callers route to first.
 * 0 for a stat this engine does not model, which is the same answer the read
 * gave before it was a function.
 */
static int
npc_base_stat(
    const struct ToriRSServerNpc* npc,
    int stat)
{
    assert(npc && npc->def);

    switch( stat )
    {
    case TORIRSSERVER_STAT_ATTACK:
        return npc->def->attack;
    case TORIRSSERVER_STAT_STRENGTH:
        return npc->def->strength;
    case TORIRSSERVER_STAT_DEFENCE:
        return npc->def->defence;
    case TORIRSSERVER_STAT_RANGED:
        return npc->def->ranged;
    case TORIRSSERVER_STAT_MAGIC:
        return npc->def->magic;
    default:
        return 0;
    }
}

/*
 * Feed one combat drop into the official client loot tracker. Public drops are
 * credited to every player captured on the dead npc; private drops are visible
 * only to their owner, so notifying anybody else would invent loot they cannot
 * pick up.
 */
static void
ToriRSServer_LootTrackerAddGroundObj(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* private_owner,
    int obj,
    int count)
{
    enum
    {
        TORIRSSERVER_SCRIPT_LOOTTRACKER_ADD_LOOT = 7192
    };
    int args[4];

    assert(srv);
    if( !srv->loot_credit_armed )
        return;

    args[0] = srv->loot_credit_npc_type;
    args[1] = srv->loot_credit_event_id;
    args[2] = obj;
    args[3] = count;
    for( int i = 0; i < TORIRSSERVER_PLAYER_MAX; i++ )
    {
        struct ToriRSServerPlayer* player = &srv->players[i];
        if( !srv->loot_credit_players[i] || !player->active )
            continue;
        if( private_owner && player != private_owner )
            continue;
        ToriRSServer_SendRunClientscript(
            player, TORIRSSERVER_SCRIPT_LOOTTRACKER_ADD_LOOT, args, 4);
    }
}

/*
 * `Npc.changeType()` obtains the new NpcType immediately.  The mock resolves
 * the combat pieces of that type into the live npc when it spawns, so merely
 * changing `type` leaves a transformed npc fighting with the old form's
 * definition and animation rig.
 *
 * This is deliberately not a respawn.  Script vars, movement, mode, target,
 * queue/timer state and the collision footprint belong to the instance and
 * survive a change type in the reference.  In particular, `size`,
 * `blockwalk`, and `blocksight` are stamped into the collision map at spawn;
 * changing any of them here would require an unstamp/restamp and would make a
 * normal visual transformation unexpectedly change its occupied tiles.
 *
 * `wander_radius` is the one cached navigation value that is otherwise read
 * from the type on every reference wander turn, so reseed it alongside `def`.
 * The saved damage is applied to the new hitpoint base, which is the
 * reference's `newBase - (oldBase - oldCurrent)` rule.
 */
static void
npc_changetype_rehydrate(
    struct ToriRSServerNpc* npc,
    int type)
{
    const struct ToriRSServerNpcDef* def;
    int damage;
    int base_hitpoints;

    assert(npc);
    def = ToriRSServer_ContentNpc(type);
    if( !def )
        def = ToriRSServer_ContentNpcDefault();

    damage = npc->base_hitpoints - npc->hitpoints;
    base_hitpoints = def->hitpoints > 0 ? def->hitpoints : 1;

    npc->type = type;
    npc->def = def;
    npc->wander_radius = def->nomove ? 0 : def->wanderrange;

    npc->base_hitpoints = base_hitpoints;
    npc->max_hitpoints = base_hitpoints;
    npc->hitpoints = base_hitpoints - damage;
    if( npc->hitpoints < 0 )
        npc->hitpoints = 0;

    npc->attack_seq = def->attack_anim;
    npc->block_seq = def->defend_anim;
    npc->death_seq = def->death_anim;
    npc->attack_sound = def->attack_sound;
    npc->block_sound = def->defend_sound;
    npc->death_sound = def->death_sound;

    /*
     * AND THE FOOTPRINT, which this used to leave on whatever the SPAWN type
     * had.
     *
     * `size` comes off the cache record, not the content def, and it is not
     * cosmetic on the server side: it is the box `npc_range` measures from
     * (Xarpus' "are you standing underneath me" stomp), the box the collision
     * grid reserves, and the box NPC_INFO measures view range against. A
     * transform that changes size and does not move this leaves the server
     * arguing with its own client — which draws the NEW type's size, because
     * that is all the wire carries.
     *
     * Xarpus is the case that found it: `tob_xarpus_feeding` is 3 and
     * `tob_xarpus_combat` is 5, so for the whole fight the server thought his
     * body was a 3x3 in the corner of the 5x5 the player could see. Verzik and
     * the Nylocas boss change size too.
     *
     * The occupancy is released at the old size and retaken at the new one, in
     * that order: `npc_set_occupancy` derives the rectangle from `npc->size`,
     * so writing the field first would release a rectangle that was never
     * taken and leave the old one reserved forever.
     */
    {
        const struct ToriRSServerNpcInfo* info = ToriRSServer_NpcInfoRecord(type);
        int size = (info && info->size > 0) ? info->size : 1;
        if( size != npc->size )
        {
            ToriRSServer_WorldNpcOccupancy(npc, 0);
            npc->size = size;
            ToriRSServer_WorldNpcOccupancy(npc, 1);
        }

        /*
         * AND THE TURN SPEED, for the same reason and with the same failure
         * mode: the CLIENT re-reads it off the new type on CHANGE_TYPE
         * (`npc->facing.turn_speed = npctype->turn_speed`, app.c), so a server
         * still holding the SPAWN type's value is a server that disagrees with
         * the only copy that draws anything.
         *
         * `turnspeed = 0` is not a rate, it is a veto: `ToriRSServer_NpcFacePlayer`,
         * `ToriRSServer_NpcFaceNpc` and `npc_facesquare` all return early on it, so
         * an npc carrying a stale 0 cannot be turned by ANY facing source -
         * silently, because every one of those sites is a no-op rather than an
         * error.
         *
         * Verzik is the case that found it. `verzik_phase1` is `turnspeed=0` in
         * the cache and correctly so - she is bolted to a throne for phase one -
         * and she SPAWNS as that record. `verzik_phase2` and `verzik_phase3`
         * state no turnspeed at all, i.e. the default 32, but the phase-one veto
         * outlived both transforms: her `npc_facesquare` on landing and every
         * facing latch after it were dropped on the floor, and she fought the
         * whole of phase two and three pointing wherever her throne had pointed.
         * From outside that is "Verzik P2 is stuck facing south".
         *
         * Resolved exactly as the spawn does it (torirs_server_world.c): a stated
         * server overlay wins, an unstated one defers to the cache record.
         */
        npc->turnspeed = def->turnspeed >= 0 ? def->turnspeed
                                             : (info ? info->turnspeed : 32);
    }
}

void
ToriRSServer_NpcChangeType(
    struct ToriRSServerNpc* npc,
    int type,
    int duration)
{
    assert(npc);
    npc_changetype_rehydrate(npc, type);
    npc->change_type = type;
    npc->masks |= TORIRSSERVER_NMASK_CHANGE_TYPE;
    /*
     * Arming the revert is the whole of the duration, and the *cancel* half
     * matters as much as the arm half.
     *
     * Mort'ton's Razmire is the case that needs it: he is spawned unafflicted,
     * turns afflicted for 200 ticks when you talk to him without a serum, and
     * a serum used on him mid-timer is `npc_changetype(razmire_keelgan, 200)`.
     * That call means "you are cured", and it is only cured if it takes the
     * pending reversion to afflicted down with it. Leaving the timer running
     * would put the affliction back a few ticks later — the cure would visibly
     * un-apply itself.
     *
     * A form that IS `spawn_type` therefore never arms a timer regardless of
     * the duration passed: there is nothing to go back to. That also keeps the
     * engine from spending a CHANGE_TYPE on the wire for a transformation from
     * a record to itself.
     */
    if( duration > 0 && type != npc->spawn_type )
        npc->changetype_delay = duration;
    else
        npc->changetype_delay = 0;
}

/*
 * The container the *active* player means by `inv_id`.
 *
 * The registry (torirs_server_container.h) is what actually resolves this, and it
 * takes the player as an argument rather than reading `srv->active_player` —
 * because a `scope=shared` container has no player to read. What is left here
 * is the ServerScript adaptor: a `.`-less container op acts on the primary
 * player, which is LostCity's `ScriptState.activePlayer` and is the correct
 * source *for this layer*. The `.` dialect chooses the secondary player, which
 * lets a targeted special inspect or mutate its validated recipient's
 * containers without replacing the owner used by its common resource commit.
 */
static struct ToriRSServerContainer*
container_row(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int32_t inv_id)
{
    return ToriRSServer_ContainerResolve(srv, player, inv_id);
}

/** The inventory currently listening on `component`, before stoptransmit drops
 *  that association. Revision 239's stop packet names the inventory rather
 *  than the component, so the host must retain this piece of server state long
 *  enough to transcribe the command faithfully. */
static struct ToriRSServerContainer*
container_listener_row(
    struct ToriRSServerPlayer* player,
    int32_t component)
{
    assert(player);
    for( int i = 0; i < TORIRSSERVER_CONTAINER_MAX; i++ )
    {
        struct ToriRSServerContainer* row = &player->containers[i];

        if( !row->used )
            continue;
        for( int listener = 0; listener < row->listener_count; listener++ )
            if( row->listeners[listener].component == component )
                return row;
    }
    return NULL;
}

static struct ToriRSServerItem*
container_for(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int32_t inv_id,
    int* out_slots)
{
    struct ToriRSServerContainer* row = container_row(srv, player, inv_id);

    *out_slots = row ? row->slots : 0;
    return row ? row->items : NULL;
}

/** Mark a container for this tick's transmit. Which *kind* of dirty that is —
 *  a per-slot mask or a whole-container flag — is the row's business now, not
 *  a branch here: it is decided from the slot count, because
 *  UPDATE_INV_PARTIAL's mask can only address 32 of them. */
static void
container_dirty(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    int32_t inv_id,
    int slot)
{
    ToriRSServer_ContainerMark(container_row(srv, player, inv_id), slot);
}

/* `push_typed_param` moved to torirs_server_ops_param.c as
 * `ToriRSServer_PushTypedParam` (declared in torirs_server.h). It was `static` here
 * and so unreachable from a per-domain ops file, and the family's whole
 * difficulty is that there must be exactly one of it — see its comment. */

int
ToriRSServer_ScriptCommand(
    struct SSVM_State* state,
    int opcode,
    int dot)
{
    struct ToriRSServer* srv = (struct ToriRSServer*)state->env->host.user;
    struct ToriRSServerPlayer* player =
        (struct ToriRSServerPlayer*)SSVM_Active(state, SSVM_ENT_PLAYER);

    /* Most commands are authored against the owner primary. A dotted player
     * command deliberately addresses active_player2, which is how a targeted
     * player special can affect its selected recipient without changing the
     * owner used by the common Summoning resource commit. */
    if( !player )
        player = srv->active_player;

    /* Per-domain handlers first. Each returns 1 when it owns the opcode; see the
     * note on ToriRSServer_OpsDb in torirs_server.h for why the split grows this way. */
    if( ToriRSServer_OpsDb(state, opcode, dot) )
        return 1;
    if( ToriRSServer_OpsParam(state, opcode, dot) )
        return 1;
    if( ToriRSServer_OpsLoc(state, opcode, dot) )
        return 1;
    if( ToriRSServer_OpsNpc(state, opcode, dot) )
        return 1;
    if( ToriRSServer_OpsObj(state, opcode, dot) )
        return 1;
    if( ToriRSServer_OpsInv(state, opcode, dot) )
        return 1;
    if( ToriRSServer_OpsPlayer(state, opcode, dot) )
        return 1;
    if( ToriRSServer_OpsPoh(state, opcode, dot) )
        return 1;

    switch( opcode )
    {
    /* ---- messaging ------------------------------------------------ */

    case SS_OP_MES:
    {
        const char* text;

        if( !SSVM_PopStr(state, &text) )
            return 1;
        ToriRSServer_SendMessage(player, text);
        return 1;
    }

    /*
     * `[command,set_player_op](string $op, int $slot, boolean $primary)` —
     * `PlayerOps.ts` SET_PLAYER_OP, one `SetPlayerOp` write.
     *
     * The right-click menu on *other players*: "Attack", "Follow", "Trade with".
     * Five slots, and the reference's `login.rs2` sets three of them on every
     * login and then adds or removes "Attack" as you cross the wilderness ditch —
     * which is the caller the wilderness slice deferred for want of this opcode.
     *
     * A null or empty `$op` clears the slot, which is how the ditch takes
     * "Attack" away again. The reference spells the clear `set_player_op(null, 2,
     * ^false)`, and RuneScript `null` for a string arrives here as an empty one,
     * so the two cases need no distinguishing.
     *
     * Popped string-first because the reference pops string-first, and the order
     * has to match the compiler's push order rather than the source order.
     */
    case SS_OP_SET_PLAYER_OP:
    {
        const char* text;
        int32_t values[2];

        if( !SSVM_PopStr(state, &text) )
            return 1;
        for( int i = 1; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        if( values[0] < 1 || values[0] > 5 )
        {
            SSVM_Abort(state, "set_player_op: slot %d is not 1..5", values[0]);
            return 1;
        }
        ToriRSServer_SendSetPlayerOp(srv->active_player, values[0], values[1], text);
        return 1;
    }

    case SS_OP_ERROR:
    {
        const char* text;

        if( !SSVM_PopStr(state, &text) )
            return 1;
        fprintf(stderr, "torirsserver: script error: %s\n", text);
        return 1;
    }

    /* ---- npc ------------------------------------------------------ */

    case SS_OP_NPC_SAY:
    {
        const char* text;
        struct ToriRSServerNpc* npc = active_npc(state);

        if( !SSVM_PopStr(state, &text) )
            return 1;
        if( !npc )
        {
            SSVM_Abort(state, "npc_say with no active npc");
            return 1;
        }
        /*
         * LostCity `Npc.say()` — fire-and-forget overhead SAY on NPC_INFO.
         * Do not route through the chatbox: [ai_timer] flavour scripts
         * (cows/sheep/chickens) call npc_say with no player set, and leftover
         * srv->active_player would spam "Cow: Moo" as game messages. Do not
         * face the player either; talk-op scripts that need facing do it
         * themselves.
         */
        snprintf(npc->say, sizeof(npc->say), "%s", text);
        npc->masks |= TORIRSSERVER_NMASK_SAY;
        return 1;
    }

    case SS_OP_NPC_COORD:
    {
        struct ToriRSServerNpc* npc = active_npc(state);

        if( !npc )
        {
            SSVM_Abort(state, "npc_coord with no active npc");
            return 1;
        }
        SSVM_PushInt(state, coord_pack(npc->level, npc->x, npc->z));
        return 1;
    }

    case SS_OP_NPC_FACING_COORD:
    {
        int32_t target;
        struct ToriRSServerNpc* npc = active_npc(state);
        static const int dx[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
        static const int dz[8] = { 1, 1, 1, 0, 0, -1, -1, -1 };
        int tx;
        int tz;

        if( !SSVM_PopInt(state, &target) )
            return 1;
        if( !npc )
        {
            SSVM_Abort(state, "npc_facing_coord with no active npc");
            return 1;
        }
        tx = coord_x(target) - npc->x;
        tz = coord_z(target) - npc->z;
        SSVM_PushInt(state,
                     coord_level(target) == npc->level && (tx != 0 || tz != 0) &&
                         npc->face_dir >= 0 && npc->face_dir < 8 &&
                         tx * dx[npc->face_dir] + tz * dz[npc->face_dir] > 0);
        return 1;
    }

    case SS_OP_NPC_NAME:
    {
        struct ToriRSServerNpc* npc = active_npc(state);

        SSVM_PushStr(state, npc ? ToriRSServer_NpcInfo(npc->type)->name : "");
        return 1;
    }

    /*
     * `name` — the ACTIVE player's display name, which is not the same player
     * as the one the script started on: inside a `huntall`/`huntnext` loop it
     * is whoever the hunt reached, and that is the only reason content needs
     * it. A message that has to say *whose* it is ("<name> has discovered a
     * large ball of energy coming their way...") cannot be written any other
     * way, because the string is composed on the sender's side and read on
     * everybody's.
     *
     * `display_name` rather than the base-37 key: the key is what social state
     * is filed under, the display name is what a human is called.
     */
    case SS_OP_NAME:
        SSVM_PushStr(state, player->display_name);
        return 1;

    case SS_OP_NPC_TYPE:
    {
        struct ToriRSServerNpc* npc = active_npc(state);

        SSVM_PushInt(state, npc ? npc->type : -1);
        return 1;
    }

    /* ---- player --------------------------------------------------- */

    case SS_OP_COORD:
        SSVM_PushInt(state, coord_pack(player->level, player->x, player->z));
        return 1;

    case SS_OP_WALKSTEP_COORD:
        /* Populated only around the per-tile walktrigger call in
         * advance_player. Zero is the RuneScript null coord everywhere else. */
        SSVM_PushInt(state, player->walkstep_coord);
        return 1;

    case SS_OP_LAST_STEP_COORD:
        SSVM_PushInt(state,
                     coord_pack(player->level, player->last_step_x, player->last_step_z));
        return 1;

    case SS_OP_WALKTRIGGER:
    {
        int32_t script_id;

        if( !SSVM_PopInt(state, &script_id) )
            return 1;
        player->walktrigger = (int)script_id;
        return 1;
    }

    case SS_OP_GETWALKTRIGGER:
        SSVM_PushInt(state, player->walktrigger);
        return 1;

    case SS_OP_P_WALK:
    {
        int32_t coord;
        int x;
        int z;

        if( !SSVM_PopInt(state, &coord) )
            return 1;
        x = coord_x(coord);
        z = coord_z(coord);
        /* Same-tile destination cancels the route (freeze/stun root). */
        if( x == player->x && z == player->z )
        {
            ToriRSServer_WorldStepsClear(player);
            player->dest_x = -1;
            player->dest_z = -1;
            player->clear_map_flag = 1;
        }
        else
            ToriRSServer_WorldWalkTo(srv, x, z);
        return 1;
    }

    /*
     * `p_teleport` and `p_telejump` move the same distance; they differ only in
     * what the client is told to do about it.
     *
     * "p_teleport() — a command that 'forcibly' moves the player and enables
     * walk animations if the distance is short, so it's good for doors like
     * that. | p_telejump() is an alternative command that forcibly moves the
     * player and never plays walk animations." — Ash, quoted in
     * docs/ASH_MOVEMENT_CORPUS.md §14. The reference splits them at
     * `Player.teleJump` (teleport, then `jump = true`) versus
     * `PathingEntity.teleport` (jump only on a plane change).
     *
     * The two shared one body here, which nailed the wire's jump bit to 1 for
     * both. Firemaking is where that shows: `~push_player` steps you off the
     * fire one tile with `p_teleport`, and the player blinked sideways instead
     * of walking.
     */
    case SS_OP_P_TELEPORT:
    case SS_OP_P_TELEJUMP:
    {
        int32_t coord;

        if( !SSVM_PopInt(state, &coord) )
            return 1;
        {
            int was_level = player->level;
            int was_x = player->x;
            int was_z = player->z;

            player->x = coord_x(coord);
            player->z = coord_z(coord);
            player->level = coord_level(coord);
            /* A teleport is how every path leaves a vessel deck (the gunwale
             * blocks walking off), and a gangplank script disembarks with
             * exactly this op — so it must drop the helm the way
             * ToriRSServer_WorldTeleport does, or the ex-rider's ground
             * clicks keep steering a boat they are no longer on. */
            player->navigating_vessel = 0;
            /* The next PLAYER_INFO has to carry an absolute placement rather
             * than a step direction, and the scene may need re-centring around
             * the new position — both of which the tick handles off
             * place_dirty. */
            player->place_dirty = 1;
            /* Two tiles is the reference's own threshold
             * (`validateDistanceWalked`), and a plane change is a jump there
             * whatever the distance, because the client has no way to walk
             * between floors. */
            player->tele_glide =
                opcode == SS_OP_P_TELEPORT && player->level == was_level &&
                abs(player->x - was_x) <= 2 && abs(player->z - was_z) <= 2;
            /*
             * The same glide, decomposed into the steps an *observer's* stream
             * can carry.
             *
             * The local player has move op 3 to itself and a jump bit to lower.
             * The tracked section has neither: its four ops are "nothing", one
             * step, two steps, and remove — so a teleport there used to be a
             * remove plus a re-add, and a re-add is a jump by construction.
             * A second player watching you light a fire saw you blink.
             *
             * Sending the re-add with the bit down is what the reference does
             * (`add(..., other.jump)`), and it does not port: its client keeps
             * `players[pid]` alive across the pair, so the re-add glides from
             * the entity's real previous tile. Ours destroys the entity
             * (`RS_EntitySync_RemovePlayer`) and respawns it on the *observer's*
             * tile, so a re-add with the bit down would slide the other player
             * out of the watcher's own feet.
             *
             * Which leaves the better answer anyway: a move of two tiles or
             * less IS one or two steps, so say so and never remove them at all.
             * The observer's copy keeps its identity, its appearance and its
             * animation, and no re-add block is spent.
             *
             * The first step takes one tile of each axis and the second takes
             * what is left, which is always inside a single step because the
             * glide test above bounds both axes at two.
             */
            player->tele_glide_step_count = 0;
            if( player->tele_glide )
            {
                int dx = player->x - was_x;
                int dz = player->z - was_z;
                int first_x = dx < 0 ? -1 : (dx > 0 ? 1 : 0);
                int first_z = dz < 0 ? -1 : (dz > 0 ? 1 : 0);
                int first = ToriRSServer_StepDirection(first_x, first_z);
                int second = ToriRSServer_StepDirection(dx - first_x, dz - first_z);

                if( first >= 0 )
                {
                    player->tele_glide_steps[player->tele_glide_step_count++] = first;
                    if( second >= 0 )
                        player->tele_glide_steps[player->tele_glide_step_count++] = second;
                }
            }
            player->waypoint_index = -1;
            /*
             * A plane change is the case place_dirty does not cover. The scene
             * *window* has not moved, so `maybe_rebuild` sees nothing, and the
             * client goes on holding every npc, player and zone from the floor
             * below — which is precisely what a ladder does.
             * `PathingEntity.teleport` is the reference's own seam for this
             * (`if (previousLevel != level)`), and this is the engine half of
             * moving the ladders to content.
             */
            if( player->level != was_level )
                ToriRSServer_WorldPlayerLevelChanged(player);
        }
        return 1;
    }

    /*
     * `map_findsquare(coord, minrange, maxrange, mode)` — a random walkable
     * tile in an annulus around `coord`.
     *
     * The reference's own use is what fixes the shape: an imp picks a tile
     * `map_findsquare(npc_coord, 0, 20, ^map_findsquare_none)` and teleports to
     * it, which is why an imp is never where you left it. Rejection sampling
     * over the box, because the alternative — enumerate every legal tile and
     * pick one — is 1,681 collision reads for a radius of 20 and this runs
     * inside a per-npc timer.
     *
     * `mode` is LostCity MapFindSquareType: 0 lineofwalk, 1 lineofsight,
     * 2 none. LINEOFWALK / LINEOFSIGHT require a clear path from the candidate
     * back to the origin (ServerOps.ts); NONE only needs a standable tile.
     *
     * Failure returns the *source* coord, not -1. The reference's callers
     * assign the result straight into `npc_tele`, so a sentinel would teleport
     * the npc to coordinate -1; standing still is what "no square found" has to
     * look like.
     */
    case SS_OP_MAP_FINDSQUARE:
    {
        int32_t values[4];
        int origin_x;
        int origin_z;
        int level;
        int min_range;
        int max_range;
        int mode;

        for( int i = 3; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        level = coord_level(values[0]);
        origin_x = coord_x(values[0]);
        origin_z = coord_z(values[0]);
        min_range = values[1] < 0 ? 0 : values[1];
        max_range = values[2] < min_range ? min_range : values[2];
        mode = values[3];

        {
            int found = values[0];

            for( int attempt = 0; attempt < 64; attempt++ )
            {
                int dx = ToriRSServer_Random(srv, -max_range, max_range);
                int dz = ToriRSServer_Random(srv, -max_range, max_range);
                int x = origin_x + dx;
                int z = origin_z + dz;
                int adx = dx < 0 ? -dx : dx;
                int adz = dz < 0 ? -dz : dz;

                if( (adx > adz ? adx : adz) < min_range )
                    continue;
                if( !ToriRSServer_SceneContains(x, z) )
                    continue;
                if( ToriRSServer_SceneWalkBlocked(level, x, z) )
                    continue;
                /* Reachability last — same order as ServerOps.ts (cheap filters
                 * first). Candidate → origin. */
                if( mode == 0 /* LINEOFWALK */ &&
                    !ToriRSServer_SceneLineOfWalk(level, x, z, origin_x, origin_z, 1, 1, 1,
                                                1, 0) )
                    continue;
                if( mode == 1 /* LINEOFSIGHT */ &&
                    !ToriRSServer_SceneLineOfSight(level, x, z, origin_x, origin_z, 1, 1, 1,
                                                 1, 0) )
                    continue;
                found = coord_pack(level, x, z);
                break;
            }
            SSVM_PushInt(state, found);
        }
        return 1;
    }

    /*
     * `npc_getmode` — the standing mode phase 4 is running for this npc.
     *
     * The setter has been here since npc modes landed; the getter had not, and
     * content branches on it (`npc_getmode = opplayer2` gates the imp's
     * teleport sound). Reading a mode the engine cannot report makes the branch
     * always-false, which for a sound is invisible and for a guard is a
     * behaviour that silently never happens.
     */
    case SS_OP_NPC_GETMODE:
    {
        struct ToriRSServerNpc* npc = active_npc(state);

        if( !npc )
        {
            SSVM_Abort(state, "npc_getmode with no active npc");
            return 1;
        }
        SSVM_PushInt(state, npc->mode);
        return 1;
    }

    /* ---- coordinates ---------------------------------------------- */

    case SS_OP_COORDX:
    case SS_OP_COORDY:
    case SS_OP_COORDZ:
    {
        int32_t coord;

        if( !SSVM_PopInt(state, &coord) )
            return 1;
        /* COORDY is the plane, not the north axis — the same naming trap the
         * world-map port hit. COORDZ is north/south. */
        if( opcode == SS_OP_COORDX )
            SSVM_PushInt(state, coord_x(coord));
        else if( opcode == SS_OP_COORDY )
            SSVM_PushInt(state, coord_level(coord));
        else
            SSVM_PushInt(state, coord_z(coord));
        return 1;
    }

    /*
     * `[command,movecoord](coord $coord, int $x, int $y, int $z)(coord)` —
     * engine.rs2:38. `ServerOps.ts:107` is
     * `packCoord(position.level + y, position.x + x, position.z + z)`: **y is
     * the plane** and z is north/south, the same naming trap COORDY above
     * documents.
     *
     * This added `$z` to the level and `$y` to the north axis until 2026-08-02.
     * Every one of the twelve callers in the tree passes `y = 0` — the shape
     * `movecoord($coord, $dx, 0, $dz)` is idiomatic precisely because the plane
     * rarely moves — so the two wrong terms were `level + $dz` and `z + 0`, i.e.
     * `~move_north($coord, 3)` went up three floors and did not move north.
     * Nothing called those procs yet, which is the only reason it had not been
     * seen. Found by porting the doors, whose `~door_open` is the first caller
     * that ever passes a non-zero z.
     */
    case SS_OP_MOVECOORD:
    {
        int32_t values[4];

        for( int i = 3; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        SSVM_PushInt(state,
                     coord_pack(coord_level(values[0]) + values[2],
                                coord_x(values[0]) + values[1],
                                coord_z(values[0]) + values[3]));
        return 1;
    }

    case SS_OP_DISTANCE:
    {
        int32_t first;
        int32_t second;
        int fx;
        int fz;
        int fl;
        int sx;
        int sz;
        int sl;
        int dx;
        int dz;

        if( !SSVM_PopInt(state, &second) || !SSVM_PopInt(state, &first) )
            return 1;
        /* Both ends into the ROOT frame before measuring: a rider's `coord`
         * is a deck-reservation tile hundreds of squares off the map, and a
         * distance mixing frames is meaningless — it is what made every
         * script flight-time computed from aboard overflow. */
        fx = coord_x(first);
        fz = coord_z(first);
        fl = coord_level(first);
        sx = coord_x(second);
        sz = coord_z(second);
        sl = coord_level(second);
        ToriRSServer_RootTile(srv, &fx, &fz, &fl);
        ToriRSServer_RootTile(srv, &sx, &sz, &sl);
        dx = fx - sx;
        dz = fz - sz;
        if( dx < 0 )
            dx = -dx;
        if( dz < 0 )
            dz = -dz;
        /* Chebyshev, matching the reference: diagonal movement costs one tile. */
        SSVM_PushInt(state, dx > dz ? dx : dz);
        return 1;
    }

    /* ---- inventory ------------------------------------------------ */

    case SS_OP_INV_ADD:
    {
        int32_t values[3];
        struct ToriRSServerContainer* row;

        for( int i = 2; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        row = container_row(srv, player, values[0]);
        if( !row )
        {
            SSVM_Abort(state, "inv_add on unknown container %d", values[0]);
            return 1;
        }
        /*
         * `ToriRSServer_ContainerAdd` is the whole body now — it merges stacks and
         * spreads unstackables, which this arm did neither of, and it marks the
         * slots it writes through the registry (the inline version this
         * replaced read "backpack, else worn", so a write to the bank marked a
         * *worn* slot and a bank slot past 31 shifted a 32-bit mask by its own
         * width). `assure_full` is off, matching the reference's INV_ADD.
         *
         * What is still not the reference: the overflow. `InvOps.ts:73` puts
         * whatever did not fit on the floor at the player's feet; this says so
         * instead, which is a third different answer from the one
         * `interaction_engine_obj` gives ("inv_no_space_message"). Noted rather
         * than fixed — the floor-drop belongs with the `opheld` row's work.
         */
        if( ToriRSServer_ContainerAdd(row, values[1], values[2], 0) < values[2] )
            ToriRSServer_Say(srv, "inv_full_message", NULL);
        return 1;
    }

    case SS_OP_INV_DEL:
    {
        int32_t values[3];
        int slots = 0;
        struct ToriRSServerItem* items;
        int remaining;

        for( int i = 2; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        items = container_for(srv, player, values[0], &slots);
        if( !items )
        {
            SSVM_Abort(state, "inv_del on unknown container %d", values[0]);
            return 1;
        }
        remaining = values[2];
        for( int i = 0; i < slots && remaining > 0; i++ )
        {
            if( items[i].obj_id != values[1] )
                continue;
            if( items[i].count > remaining )
            {
                items[i].count -= remaining;
                remaining = 0;
            }
            else
            {
                remaining -= items[i].count;
                /* Not a raw clear: a shop's baseline slot empties to a count of
                 * 0 and stays (ToriRSServer_ContainerClearSlot). It marks the slot
                 * itself, so the dirty call below is redundant there and
                 * harmless — the registry's mark is idempotent. */
                ToriRSServer_ContainerClearSlot(container_row(srv, player, values[0]), i);
            }
            /*
             * Through the registry. What was here read "backpack, else worn",
             * so `inv_del(bank, …)` marked a *worn* slot — a wrong appearance
             * push, and the bank never re-transmitted — and for a bank slot
             * past 31, `1u << i` shifted a 32-bit mask by more than its width
             * (undefined; `i` reaches 1409). Same defect as the one inv_add's
             * comment already records; this is the second copy of it.
             */
            container_dirty(srv, player, values[0], i);
        }
        return 1;
    }

    /*
     * inv_delslot empties one cell, whatever is in it.
     *
     * Not a convenience over inv_del: they answer different questions. A script
     * that ate the item the player clicked has to remove *that* stack, and
     * inv_del removes the first matching one — which is a different cell as
     * soon as two slots hold the same obj. The reference's consume path uses
     * delslot for exactly this reason and so does the port.
     */
    case SS_OP_INV_DELSLOT:
    {
        int32_t values[2];
        int slots = 0;
        struct ToriRSServerItem* items;

        for( int i = 1; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        items = container_for(srv, player, values[0], &slots);
        if( !items )
        {
            SSVM_Abort(state, "inv_delslot on unknown container %d", values[0]);
            return 1;
        }
        if( values[1] < 0 || values[1] >= slots )
            return 1;
        /* A shop's baseline slot empties to a count of 0 and stays; see
         * ToriRSServer_ContainerClearSlot. */
        ToriRSServer_ContainerClearSlot(container_row(srv, player, values[0]), (int)values[1]);
        /* Through the registry, for the reason inv_del's comment gives. */
        container_dirty(srv, player, values[0], (int)values[1]);
        return 1;
    }

    case SS_OP_INV_TOTAL:
    {
        int32_t values[2];
        int slots = 0;
        struct ToriRSServerItem* items;
        int total = 0;

        for( int i = 1; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        items = container_for(srv, player, values[0], &slots);
        if( !items )
        {
            SSVM_Abort(state, "inv_total on unknown container %d", values[0]);
            return 1;
        }
        for( int i = 0; i < slots; i++ )
        {
            if( items[i].obj_id == values[1] )
                total += items[i].count;
        }
        SSVM_PushInt(state, total);
        return 1;
    }

    case SS_OP_INV_TOTALCAT:
    {
        int32_t values[2];
        int slots = 0;
        struct ToriRSServerItem* items;
        int total = 0;

        for( int i = 1; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        /*
         * Category 0 is the decoder's "no category stated", so it matches every
         * uncategorised obj in the game. pack/category.pack says content must
         * not bind to it; counting it here would be the same mistake with a
         * quieter symptom, so refuse instead.
         */
        if( values[1] <= 0 )
        {
            SSVM_Abort(state, "inv_totalcat with category %d (0 means unset)", values[1]);
            return 1;
        }
        items = container_for(srv, player, values[0], &slots);
        if( !items )
        {
            SSVM_Abort(state, "inv_totalcat on unknown container %d", values[0]);
            return 1;
        }
        for( int i = 0; i < slots; i++ )
        {
            if( items[i].obj_id < 0 )
                continue;
            if( ToriRSServer_ObjInfo(items[i].obj_id)->category == values[1] )
                total += items[i].count;
        }
        SSVM_PushInt(state, total);
        return 1;
    }

    case SS_OP_INV_FREESPACE:
    {
        int32_t inv_id;
        int slots = 0;
        struct ToriRSServerItem* items;
        int free_slots = 0;

        if( !SSVM_PopInt(state, &inv_id) )
            return 1;
        items = container_for(srv, player, inv_id, &slots);
        if( !items )
        {
            SSVM_Abort(state, "inv_freespace on unknown container %d", inv_id);
            return 1;
        }
        for( int i = 0; i < slots; i++ )
        {
            if( items[i].obj_id < 0 )
                free_slots++;
        }
        SSVM_PushInt(state, free_slots);
        return 1;
    }

    /* ---- variables ------------------------------------------------ */

    case SS_OP_PUSH_VARP:
    {
        int varp = state->script->int_operands[state->pc] & 0xffff;

        if( varp < 0 || varp >= TORIRSSERVER_VARP_COUNT )
        {
            SSVM_Abort(state, "varp %d is outside the mock's range", varp);
            return 1;
        }
        SSVM_PushInt(state, player->varps[varp]);
        return 1;
    }

    case SS_OP_POP_VARP:
    {
        int varp = state->script->int_operands[state->pc] & 0xffff;
        int32_t value;

        if( !SSVM_PopInt(state, &value) )
            return 1;
        if( varp < 0 || varp >= TORIRSSERVER_VARP_COUNT )
        {
            SSVM_Abort(state, "varp %d is outside the mock's range", varp);
            return 1;
        }
        /*
         * Assignment always marks the varp for transmission, even when the
         * value is unchanged.
         *
         * That is the reference's semantics and it is load-bearing:
         * LostCity's content contains `%option_nodef = %option_nodef;` with the
         * comment "resync varp", which only means anything if a write to an
         * equal value still reaches the client. It is also what makes an
         * opening state work at all — [login] setting `%com_mode = 0` on a varp
         * that is already 0 has to *tell* the client 0, because the client has
         * never been told anything.
         *
         * `ToriRSServer_WorldMarkVarp` is idempotent within a tick, so a script
         * writing the same varp repeatedly still produces one packet.
         */
        player->varps[varp] = value;
        ToriRSServer_WorldMarkVarp(player, varp);
        /* And whatever engine state hangs off this varp. Writing the array and
         * marking it for transmission is only *reporting* the change; a varp
         * like `option_run` is where a piece of engine state actually lives,
         * and skipping this is how the run orb came to light up while the
         * player kept walking. */
        ToriRSServer_WorldVarpWritten(srv, varp, value);
        return 1;
    }

    /* Per-NPC variables. The operand is the id allocated in pack/varn.alloc;
     * values live on the active runtime NPC and therefore survive changetype
     * while remaining isolated from every other NPC instance. */
    case SS_OP_PUSH_VARN:
    {
        struct ToriRSServerNpc* npc = active_npc(state);
        int varn = state->script->int_operands[state->pc] & 0xffff;

        if( !npc )
        {
            SSVM_Abort(state, "push_varn with no active npc");
            return 1;
        }
        if( varn < 0 || varn >= TORIRSSERVER_NPC_VAR_MAX )
        {
            SSVM_Abort(state, "varn %d is outside the mock's range", varn);
            return 1;
        }
        SSVM_PushInt(state, npc->script_vars[varn]);
        return 1;
    }

    case SS_OP_POP_VARN:
    {
        struct ToriRSServerNpc* npc = active_npc(state);
        int varn = state->script->int_operands[state->pc] & 0xffff;
        int32_t value;

        if( !SSVM_PopInt(state, &value) )
            return 1;
        if( !npc )
        {
            SSVM_Abort(state, "pop_varn with no active npc");
            return 1;
        }
        if( varn < 0 || varn >= TORIRSSERVER_NPC_VAR_MAX )
        {
            SSVM_Abort(state, "varn %d is outside the mock's range", varn);
            return 1;
        }
        npc->script_vars[varn] = value;
        return 1;
    }

    /*
     * World-shared variables.
     *
     * The counterpart to VARP: same `%name` syntax, same operand encoding, but
     * the value lives on the world and not on the player. There is no
     * transmission and no dirty mark — a varp write is *reported* to the client
     * because the client holds a copy, and nothing holds a copy of these.
     *
     * These two opcodes compiled and did not run for as long as `vars` existed:
     * the symbol kind, the opcode numbers and the compiler's emit table were all
     * present, so `%some_shared_thing = 1` produced valid bytecode that fell
     * through to the unhandled-opcode abort. Content therefore could not use
     * world state at all, and the mechanics that needed it — Pyramid Plunder's
     * world-shared entrance door and tomb doors — were written per-player, which
     * is not a smaller version of the mechanic but a different one.
     */
    case SS_OP_PUSH_VARS:
    {
        int vars = state->script->int_operands[state->pc] & 0xffff;

        if( vars < 0 || vars >= TORIRSSERVER_VARS_COUNT )
        {
            SSVM_Abort(state, "vars %d is outside the mock's range", vars);
            return 1;
        }
        SSVM_PushInt(state, srv->vars[vars]);
        return 1;
    }

    case SS_OP_POP_VARS:
    {
        int vars = state->script->int_operands[state->pc] & 0xffff;
        int32_t value;

        if( !SSVM_PopInt(state, &value) )
            return 1;
        if( vars < 0 || vars >= TORIRSSERVER_VARS_COUNT )
        {
            SSVM_Abort(state, "vars %d is outside the mock's range", vars);
            return 1;
        }
        srv->vars[vars] = value;
        return 1;
    }

    /* ---- config --------------------------------------------------- */

    case SS_OP_OC_NAME:
    {
        int32_t obj_id;

        if( !SSVM_PopInt(state, &obj_id) )
            return 1;
        SSVM_PushStr(state, ToriRSServer_ObjInfo(obj_id)->name);
        return 1;
    }

    /*
     * The examine text, and the whole of what a rev-230 "Examine" op is.
     *
     * Op 10 is Examine on nearly every panel the client draws — the backpack,
     * the worn tab, the bank, the Tool Leprechaun's twelve cells — and it had
     * no server-side answer at all, because the obj record's `examine` string
     * was decoded by the cache and dropped by ToriRSServer_ObjInfo. Reading it is
     * mechanism, not policy: the sentence is the cache's, content decides
     * whether and when to print it.
     *
     * The empty string rather than NULL for a record that states none — a
     * placeholder, or an id nothing occupies — so a script that prints the
     * result unconditionally prints a blank line instead of dereferencing one.
     */
    case SS_OP_OC_DESC:
    {
        int32_t obj_id;
        const char* desc;

        if( !SSVM_PopInt(state, &obj_id) )
            return 1;
        desc = ToriRSServer_ObjInfo(obj_id)->desc;
        SSVM_PushStr(state, desc ? desc : "");
        return 1;
    }

    /*
     * Config queries.
     *
     * Every one of these is a read off a table the boot loaders already decoded
     * — `ToriRSServerObjInfo`, `ToriRSServerNpcInfo`, the content packs — which is the
     * reason this batch is safe to add in bulk. `nc_desc` is still absent —
     * a dat2 npc record has no description at all (it is server-driven at this
     * revision). `oc_cost` / `oc_members` / `oc_tradeable` / `oc_desc` land in
     * torirs_server_ops_obj.c / the cases above once their fields are kept at load.
     *
     * An opcode that cannot be answered from real data is better left to the
     * VM's loud stub than implemented with a plausible guess: the stub says so,
     * and a guess does not.
     *
     * `oc_param` used to be in that list — `runtime_typed`, "and no decoder here
     * keeps a general per-record param table to answer that from". Both halves
     * of that now exist (`ToriRSServer_ObjParam`, `ToriRSServer_ContentParamType`), so
     * it is implemented below, and `nc_param` with it over `ToriRSServer_NpcParam`.
     * `lc_param` / `struct_param` are the same shape over the loc and struct
     * tables, and are left out of this change rather than done badly at speed:
     * neither table is decoded at runtime at all, where the npc one already was
     * — `read_combat_params` had been walking these very rows and discarding
     * all but fourteen keys.
     */
    case SS_OP_NPC_SETMODE:
    {
        int32_t mode;
        struct ToriRSServerNpc* npc = active_npc(state);

        if( !SSVM_PopInt(state, &mode) )
            return 1;
        if( !npc )
        {
            SSVM_Abort(state, "npc_setmode with no active npc");
            return 1;
        }
        /*
         * The mode is stored, not acted on: phase 4 runs it every tick, which
         * is what makes it a *standing state* rather than a one-shot. That
         * distinction is the whole reason `npc_setmode` is in front of 122
         * LostCity files and could not be faked with a one-shot face or walk.
         *
         * `none` and `null` both mean stop, and 162 of the tree's 253 calls are
         * one of the two.
         */
        if( npc->owner_gen != 0 && ToriRSServer_FamiliarDebug() )
        {
            fprintf(
                stderr,
                "familiar_dbg setmode type=%d %d -> %d\n",
                npc->type, npc->mode, (int)mode);
        }
        npc->mode = mode;
        /*
         * A targetless mode CLEARS THE TARGET. `NPC_SETMODE` in the reference is
         * `clearInteraction()` for `none`/`wander`/`patrol` and `resetDefaults()`
         * for `null` (NpcOps.ts), and `clearInteraction` sets `target = null`.
         *
         * Setting only the mode field left `combat_target` alive, and the npc
         * phase skips any npc that has one ("combat and death own the npc's
         * movement") — so a script-driven npc that anything hit once stopped
         * walking for the rest of its life. The Inferno's Ancestral Glyph binds
         * `[ai_queue1] npc_setmode(none)` precisely so that being attacked does
         * not stop its sweep, and it froze on the first hit anyway.
         */
        if( mode == TORIRSSERVER_NPCMODE_NONE || mode == TORIRSSERVER_NPCMODE_NULL ||
            mode == TORIRSSERVER_NPCMODE_WANDER || mode == TORIRSSERVER_NPCMODE_PATROL )
        {
            npc->combat_target = -1;
        }
        /*
         * The npc-versus-npc target goes with EVERY mode, not only the
         * targetless ones, because the npc phase gives that fight priority over
         * the mode machine ("combat and death own the npc's movement"). A script
         * that says `npc_setmode(playerfollow)` while `combat_target_npc` is
         * still set is therefore asking for a mode that can never run a step —
         * silently, which is how a familiar told to go back to its owner
         * stayed standing in the fight it had just been released from. Setting
         * a mode IS `clearInteraction()`/`setInteraction()` in the reference,
         * and both replace whatever target was there.
         */
        npc->combat_target_npc = -1;
        npc->combat_target_npc_gen = 0;
        if( mode == TORIRSSERVER_NPCMODE_NONE || mode == TORIRSSERVER_NPCMODE_NULL )
            npc->step_dir = -1;
        /*
         * A targeted mode also NAMES ITS TARGET. `NpcOps.NPC_SETMODE` ends with
         * `setInteraction(Interaction.SCRIPT, state._activePlayer, mode)` for
         * every mode from `playerescape` up, and `resetDefaults()` when there
         * is no active entity to target.
         *
         * Storing only the mode number left phase 4 to guess who the mode was
         * about, and its guess was `srv->active_player` — which in a phase that
         * belongs to no player is whoever was last served. `~chatnpc` sets
         * `playerfaceclose` on the npc you are talking to; with a second player
         * logged in, the npc measured that mode's one-tile leash against the
         * wrong person, decided its partner had walked off, and went back to
         * wandering away mid-sentence.
         */
        if( mode >= TORIRSSERVER_NPCMODE_PLAYERESCAPE )
        {
            if( player && player->active )
                ToriRSServer_NpcSetModeTarget(npc, player);
            else
                ToriRSServer_NpcResetDefaults(npc);
        }
        else
        {
            ToriRSServer_NpcSetModeTarget(npc, NULL);
        }
        /*
         * `null` is not a synonym for `none` — the comment above says the two
         * "both mean stop" and that is the half of it that is true. In
         * `NpcOps.NPC_SETMODE`, `none` is `clearInteraction()` (stop, and stay
         * stopped) while `null` is `resetDefaults()` (stop *this*, and go back
         * to what the record does), and only `none` reaches the mode field.
         *
         * Storing -1 instead left the npc in a mode the machine dispatches
         * nothing for, so an npc handed `npc_setmode(null)` never patrolled or
         * wandered again — the one outcome the reference's spelling of "revert"
         * is chosen to avoid.
         */
        if( mode == TORIRSSERVER_NPCMODE_NULL )
            ToriRSServer_NpcResetDefaults(npc);
        return 1;
    }

    case SS_OP_NPC_QUEUE:
    {
        int32_t queue;
        int32_t arg;
        int32_t delay;
        struct ToriRSServerNpc* npc = active_npc(state);

        /* `npc_queue(2, $damage, $delay)` — queue number, argument, delay. */
        if( !SSVM_PopInt(state, &delay) )
            return 1;
        if( !SSVM_PopInt(state, &arg) )
            return 1;
        if( !SSVM_PopInt(state, &queue) )
            return 1;
        if( !npc )
        {
            SSVM_Abort(state, "npc_queue with no active npc");
            return 1;
        }
        if( queue < 1 || queue > 20 )
        {
            SSVM_Abort(state, "npc_queue %d is outside [ai_queue1..20]", queue);
            return 1;
        }
        for( int i = 0; i < TORIRSSERVER_NPC_QUEUE_MAX; i++ )
        {
            if( npc->queue[i].active )
                continue;
            npc->queue[i].active = 1;
            npc->queue[i].queue = queue;
            /*
             * The raw delay, and the drain compares the value *after* its
             * decrement — `Npc.processQueue`: `request.delay--; if (!delayed &&
             * request.delay <= 0)`. A player's queue compares the value before
             * it, so an npc's delay 0 and delay 1 both land on the next npc
             * phase and a player's do not.
             *
             * This stored `delay + 1` and the selftest asserted that
             * `npc_queue(q, arg, 1)` fires on tick +2. It fires on +1; the
             * assertion encoded the wrong convention and was rewritten.
             */
            npc->queue[i].delay = delay;
            npc->queue[i].arg = arg;
            return 1;
        }
        SSVM_Abort(state, "npc %d's queue is full", npc->type);
        return 1;
    }

    case SS_OP_NPC_SETTIMER:
    {
        int32_t interval;
        struct ToriRSServerNpc* npc = active_npc(state);

        if( !SSVM_PopInt(state, &interval) )
            return 1;
        if( !npc )
        {
            SSVM_Abort(state, "npc_settimer with no active npc");
            return 1;
        }
        /*
         * 0 stops the timer, which content relies on — `npc_settimer(0)` is how
         * a behaviour says "not until the action is complete". The trigger is
         * not stored: `[ai_timer,<npc>]` is resolved when it fires, so an npc
         * whose type changes picks up the new type's timer script.
         */
        npc->timer_interval = interval > 0 ? interval : 0;
        npc->timer_clock = 0;
        return 1;
    }

    /* The sole engine relation familiars need. Content owns when the relation
     * is created and removed; the host owns the pool-slot generation that
     * makes it safe across logout and reuse. */
    case SS_OP_NPC_SETOWNER:
    {
        struct ToriRSServerNpc* npc = active_npc(state);

        if( !npc || !player )
        {
            SSVM_Abort(state, "npc_setowner requires an active npc and player");
            return 1;
        }
        ToriRSServer_WorldNpcSetOwner(npc, player);
        return 1;
    }

    /*
     * npc_setfollower: this npc IS the player's familiar/pet.
     *
     * Distinct from npc_setowner on purpose. Ownership is "private to this
     * player" and many npcs can share it; following is "the one npc that walks
     * behind them" and there is at most one. Conflating them is what made
     * `call familiar` teleport the Queen Black Dragon: she is owned (the arena
     * is private) and she was the lowest-numbered owned npc, so the old
     * slot-scanning `npc_findowned` handed her back as the familiar.
     *
     * Implies ownership, because a follower is by definition private to its
     * owner and every caller wanted both.
     */
    case SS_OP_NPC_SETFOLLOWER:
    {
        struct ToriRSServerNpc* npc = active_npc(state);
        int slot;

        if( !npc || !player )
        {
            SSVM_Abort(state, "npc_setfollower requires an active npc and player");
            return 1;
        }
        slot = (int)(npc - srv->npcs);
        ToriRSServer_WorldNpcSetOwner(npc, player);
        ToriRSServer_WorldNpcSetFollower(player, npc, slot);
        return 1;
    }

    case SS_OP_NPC_OWNER:
    {
        struct ToriRSServerNpc* npc = active_npc(state);
        struct ToriRSServerPlayer* owner;

        if( !npc )
        {
            SSVM_Abort(state, "npc_owner with no active npc");
            return 1;
        }
        owner = ToriRSServer_WorldNpcOwner(srv, npc);
        SSVM_PushInt(state, owner ? owner->pid : -1);
        return 1;
    }

    /*
     * All three resolve the player's FOLLOWER through the explicit link, never
     * by scanning for "an npc this player owns".
     *
     * The scan was the bug: it returned the lowest-numbered owned npc, and in a
     * private minigame instance that is the minigame's own boss rather than the
     * familiar. `npc_findowned` keeps its name because every existing caller
     * means the familiar by it; `npc_findfollower` is the spelling new content
     * should use, and they are deliberately the same operation.
     */
    case SS_OP_NPC_FINDOWNED:
    case SS_OP_NPC_FINDFOLLOWER:
    {
        int slot = -1;
        struct ToriRSServerNpc* npc =
            player ? ToriRSServer_WorldNpcFollower(srv, player, &slot) : NULL;

        if( !npc )
        {
            SSVM_PushInt(state, 0);
            return 1;
        }
        SSVM_SetActive(state, SSVM_ENT_NPC, SSVM_PRIMARY, npc);
        state->host_tag = slot + 1;
        SSVM_PointerAdd(state, SSVM_PTR_ACTIVE_NPC);
        SSVM_PushInt(state, 1);
        return 1;
    }

    case SS_OP_NPC_FINDOWNED2:
    {
        struct ToriRSServerNpc* npc =
            player ? ToriRSServer_WorldNpcFollower(srv, player, NULL) : NULL;

        if( !npc )
        {
            SSVM_PushInt(state, 0);
            return 1;
        }
        SSVM_SetActive(state, SSVM_ENT_NPC, SSVM_SECONDARY, npc);
        SSVM_PushInt(state, 1);
        return 1;
    }

    case SS_OP_NPC_FINDCOMBAT:
    {
        int slot = player ? player->combat_target : -1;
        struct ToriRSServerNpc* npc;

        if( slot < 0 || slot >= TORIRSSERVER_NPC_MAX )
        {
            SSVM_PushInt(state, 0);
            return 1;
        }
        npc = &srv->npcs[slot];
        if( !npc->active || npc->death_tick >= 0 || npc->level != player->level )
        {
            SSVM_PushInt(state, 0);
            return 1;
        }
        SSVM_SetActive(state, SSVM_ENT_NPC, SSVM_PRIMARY, npc);
        state->host_tag = slot + 1;
        SSVM_PointerAdd(state, SSVM_PTR_ACTIVE_NPC);
        SSVM_PushInt(state, 1);
        return 1;
    }

    /*
     * Per-instance NPC integers. The slot is engine-bounded and content names
     * it with a constant; values otherwise use the ServerScript int unchanged.
     * The storage lives on ToriRSServerNpc, so two NPCs of one type are isolated and
     * npc_changetype naturally preserves it.
     */
    case SS_OP_NPC_VAR_GET:
    {
        int32_t slot;
        struct ToriRSServerNpc* npc = active_npc(state);

        if( !SSVM_PopInt(state, &slot) )
            return 1;
        if( !npc )
        {
            SSVM_Abort(state, "npc_var_get with no active npc");
            return 1;
        }
        if( slot < 0 || slot >= TORIRSSERVER_NPC_VAR_MAX )
        {
            SSVM_Abort(state, "npc_var_get slot %d outside 0..%d", (int)slot,
                       TORIRSSERVER_NPC_VAR_MAX - 1);
            return 1;
        }
        SSVM_PushInt(state, npc->script_vars[slot]);
        return 1;
    }

    /*
     * `npc_attacknpc(npc_uid $target)` — the active npc starts an ordinary
     * fight with another npc, `npc_attackplayer()` with the active player, and
     * `npc_hastarget()` asks whether it is already in one of either kind.
     *
     * The target is what `ToriRSServer_CombatNpcTick` reads to decide that combat
     * owns this npc: it faces, closes to its `attackrange`, and swings on the
     * record's `attackrate` through `[ai_opnpc2]` or `[ai_opplayer2]`. Setting
     * it is what "and now fight this, normally" means, and until these existed
     * content could not say it at all — `npc_setmode(applayer2)` runs one AP
     * handler and falls back to `none`, so a script that wanted a standing
     * fight had to re-arm the mode every tick and carry its own attack clock
     * beside the engine's, and no spelling of it could name another npc.
     *
     * `attack_clock = 0` rather than a flinch delay, matching `maybe_aggress`:
     * a fight the npc *starts* swings on the tick it is in range. The halved
     * clock in `ToriRSServer_CombatHitNpc` belongs to retaliation, which is a
     * different event.
     *
     * A uid rather than a slot for the target: slots are recycled, and the
     * generation half is what stops a fight outliving the npc it was with.
     */
    case SS_OP_NPC_ATTACKNPC:
    {
        int32_t uid;
        struct ToriRSServerNpc* npc = active_npc(state);
        int target;
        uint16_t generation;

        if( !SSVM_PopInt(state, &uid) )
            return 1;
        if( !npc )
        {
            SSVM_Abort(state, "npc_attacknpc with no active npc");
            return 1;
        }
        target = (int)((uint32_t)uid & 0xffffu);
        generation = (uint16_t)((uint32_t)uid >> 16);
        if( uid < 0 || target < 0 || target >= TORIRSSERVER_NPC_MAX ||
            !srv->npcs[target].active || srv->npcs[target].death_tick >= 0 ||
            generation == 0 || srv->npcs[target].generation != generation ||
            &srv->npcs[target] == npc )
        {
            /* A target that is gone is not an error — the caller found it a
             * moment ago and things die. Say nothing and leave the npc idle. */
            return 1;
        }
        if( npc->combat_target_npc != target )
        {
            npc->combat_target_npc = target;
            npc->combat_target_npc_gen = generation;
            npc->attack_clock = 0;
        }
        npc->combat_target = -1;
        ToriRSServer_NpcFaceNpc(npc, target);
        return 1;
    }

    case SS_OP_NPC_ATTACKPLAYER:
    {
        struct ToriRSServerNpc* npc = active_npc(state);

        if( !npc || !player )
        {
            SSVM_Abort(state, "npc_attackplayer needs an active npc and player");
            return 1;
        }
        if( npc->combat_target != player->pid )
        {
            npc->combat_target = player->pid;
            npc->attack_clock = 0;
        }
        npc->combat_target_npc = -1;
        npc->combat_target_npc_gen = 0;
        ToriRSServer_NpcFacePlayer(npc, npc->combat_target);
        return 1;
    }

    /*
     * `npc_attackdelay(int $ticks)` — the combat attack clock, which content
     * could not reach.
     *
     * The only word content had for "wait before swinging again" was
     * `npc_delay`, and that means something else: it makes the npc invalid for
     * the whole of its turn (`Npc.isValid()`), so it runs no timers, no modes
     * and no QUEUE. In this tree an npc's queue is where every hit the player
     * lands arrives — `npc_queue(2, $damage, 0)` — so a monster pacing itself on
     * `npc_delay(4)` took one turn in five and the hitsplat for a hit could sit
     * unshown for four ticks, or be dropped outright when a window's worth
     * landed together past the client's four-hitmark ceiling.
     *
     * Two commands because there are two claims: `npc_delay` is "I am running a
     * scripted sequence, leave me alone" and `npc_attackdelay` is "my weapon is
     * on cooldown". Only the first should stop damage landing.
     *
     * Written straight to `attack_clock`, the deadline
     * `ToriRSServer_CombatNpcTick` reads before firing the swing trigger and the
     * same one it arms from `attackrate` — so a handler that states its own
     * cadence overrides the record's for that swing and nothing else changes.
     *
     * `srv->tick + $ticks`, because the field is a deadline and not a count of
     * ticks remaining (see its declaration in torirs_server.h). This is the same
     * arithmetic the reference writes for the same claim,
     * `%npc_action_delay = add(map_clock, <n>)`, and it is what makes
     * `npc_attackdelay(4)` mean four ticks rather than five.
     */
    case SS_OP_NPC_ATTACKDELAY:
    {
        int32_t ticks;
        struct ToriRSServerNpc* npc = active_npc(state);

        if( !SSVM_PopInt(state, &ticks) )
            return 1;
        if( !npc )
        {
            SSVM_Abort(state, "npc_attackdelay with no active npc");
            return 1;
        }
        npc->attack_clock = srv->tick + (ticks > 0 ? ticks : 0);
        return 1;
    }

    case SS_OP_NPC_HASTARGET:
    {
        struct ToriRSServerNpc* npc = active_npc(state);

        if( !npc )
        {
            SSVM_Abort(state, "npc_hastarget with no active npc");
            return 1;
        }
        SSVM_PushInt(state, (npc->combat_target >= 0 || npc->combat_target_npc >= 0) ? 1 : 0);
        return 1;
    }

    /*
     * `npc_combatplayer()(boolean)` — is the ACTIVE npc fighting the ACTIVE
     * player? NPC_FINDCOMBAT is the same question from the other end, and the
     * two are not interchangeable.
     *
     * `npc_findcombat` reads `player->combat_target`: who the player is
     * swinging at. This reads `npc->combat_target`: who the npc is swinging at.
     * In a multi-way area the distinction rarely shows, because everything ends
     * up fighting everything. In a single-way area it is the whole question — a
     * player can be attacking an npc that has turned on somebody else, and a
     * helper that joined on the strength of `npc_findcombat` alone would be a
     * second attacker on a victim that already had one.
     *
     * That is exactly the line OldSchool's thralls hold: a thrall attacks its
     * owner's target and creates no aggression of its own, so it only ever adds
     * damage to a fight its owner is already the other side of. Content could
     * not ask that before this command, so "assist in singles" could not be
     * written safely at all.
     *
     * A pid comparison rather than an identity one: `combat_target` is a pool
     * index, and a player who logged out leaves the slot to whoever takes it
     * next, so the live player's own pid is the only safe thing to test.
     */
    case SS_OP_NPC_COMBATPLAYER:
    {
        struct ToriRSServerNpc* npc = active_npc(state);

        if( !npc || !player )
        {
            SSVM_Abort(state, "npc_combatplayer needs an active npc and player");
            return 1;
        }
        SSVM_PushInt(state, npc->combat_target == player->pid ? 1 : 0);
        return 1;
    }

    /*
     * `combat_assist_singles()(boolean)` — the server's policy on whether a
     * player's summoned helper may swing in a single-way combat area.
     *
     * Engine state and not a content constant because it is an operator's
     * choice between two coherent rules — the pre-EoC one this port reproduces
     * and the later thrall one — and a content constant can only be changed by
     * rebuilding both script packs. See `familiar_singles_assist` in torirs_server.h
     * for the research behind both positions.
     *
     * The engine itself never reads it. Single-way is not enforced here at all:
     * `map_multiway` is content's own gate over content's own zone table, and
     * this command is the second half of that gate rather than a new mechanism.
     */
    case SS_OP_COMBAT_ASSIST_SINGLES:
    {
        SSVM_PushInt(state, srv->familiar_singles_assist ? 1 : 0);
        return 1;
    }

    case SS_OP_NPC_VAR_SET:
    {
        int32_t slot;
        int32_t value;
        struct ToriRSServerNpc* npc = active_npc(state);

        if( !SSVM_PopInt(state, &value) || !SSVM_PopInt(state, &slot) )
            return 1;
        if( !npc )
        {
            SSVM_Abort(state, "npc_var_set with no active npc");
            return 1;
        }
        if( slot < 0 || slot >= TORIRSSERVER_NPC_VAR_MAX )
        {
            SSVM_Abort(state, "npc_var_set slot %d outside 0..%d", (int)slot,
                       TORIRSSERVER_NPC_VAR_MAX - 1);
            return 1;
        }
        npc->script_vars[slot] = value;
        return 1;
    }

    /*
     * `npc_setmovespeed(int $speed)` — 0 walks, 1 runs.
     *
     * No reference twin: `Npc.defaultMoveSpeed()` returns WALK for every npc
     * LostCity has. The Pestilent Bloat's speed is a health band (walk above
     * 60%, run between 40% and 60%, alternate below 40% on every attack made
     * against it), and content had no way to say so — `npc_walk` queues a
     * waypoint and the npc phase drains exactly one tile from it.
     *
     * Only the noMode drain in `advance_npcs` reads this. Anything above 0 is
     * a run rather than "this many tiles": the wire has one two-step op and no
     * third.
     */
    case SS_OP_NPC_SETMOVESPEED:
    {
        int32_t speed;
        struct ToriRSServerNpc* npc = active_npc(state);

        if( !SSVM_PopInt(state, &speed) )
            return 1;
        if( !npc )
        {
            SSVM_Abort(state, "npc_setmovespeed with no active npc");
            return 1;
        }
        npc->move_speed = speed > 0 ? 1 : 0;
        return 1;
    }

    case SS_OP_NPC_SETMAXHP:
    {
        int32_t max;
        struct ToriRSServerNpc* npc = active_npc(state);

        if( !SSVM_PopInt(state, &max) )
            return 1;
        if( !npc )
        {
            SSVM_Abort(state, "npc_setmaxhp with no active npc");
            return 1;
        }
        if( max <= 0 )
        {
            SSVM_Abort(state, "npc_setmaxhp %d is not a hitpoint pool", max);
            return 1;
        }
        /*
         * The POOL, not the current hitpoints -- `npc_statheal`/`npc_statsub`
         * are how those move.
         *
         * All three fields travel together because the spawn sets them equal
         * (torirs_server_world.c) and two of them answer the same question to
         * different callers: `base_hitpoints` is what `npc_basestat(hitpoints)`
         * reports and what `npc_statheal` clamps at, `max_hitpoints` is what
         * the NPC_INFO HEADBAR encoder divides by. Moving one without the other
         * is what this command exists to make impossible -- a scaled raid boss
         * that set only its current hitpoints left both holding the authored
         * 5-man figure, so its overhead bar drew a fixed fraction of the truth
         * (75% for a 3-scale Maiden, at every point of the fight including full
         * health) while the raid HUD, which tracks the scaled maximum in
         * content, was right the whole time.
         *
         * Current hitpoints clamp DOWN into the new pool and are otherwise left
         * alone: shrinking a pool below what the npc is standing on would
         * otherwise leave it above its own maximum, which reads as >100% on
         * every bar that divides by it. Growing the pool does not heal.
         */
        npc->base_hitpoints = max;
        npc->max_hitpoints = max;
        if( npc->hitpoints > max )
            npc->hitpoints = max;
        return 1;
    }

    /* ---- players by uid, logging, gendered text --------------------- */

    case SS_OP_FINDUID:
    case SS_OP_P_FINDUID:
    {
        int32_t uid;
        struct ToriRSServerPlayer* found;

        if( !SSVM_PopInt(state, &uid) )
            return 1;
        /*
         * Content uses this to re-acquire a player it stashed in a varp — the shape is
         * `if (p_finduid(%npc_aggressive_player) = true) { ... }` — so a uid
         * that no longer names anybody has to return false rather than abort.
         * That is the whole point of the call: it is content asking whether the
         * player it remembers is still here.
         *
         * `p_finduid` differs from `finduid` by granting *protected* access, so
         * the ops that follow it may write the player. The reference draws the
         * same distinction and the VM already enforces it through the meta
         * table's require bits.
         */
        found = player_by_uid(srv, uid);
        if( !found )
        {
            SSVM_PushInt(state, 0);
            return 1;
        }
        SSVM_SetActive(state, SSVM_ENT_PLAYER, SSVM_PRIMARY, found);
        SSVM_PointerAdd(state, SSVM_PTR_ACTIVE_PLAYER);
        if( opcode == SS_OP_P_FINDUID )
            SSVM_PointerAdd(state, SSVM_PTR_PROTECTED_PLAYER);
        SSVM_PushInt(state, 1);
        return 1;
    }

    case SS_OP_P_FINDMUTUALFRIEND:
    {
        const char* display_name = NULL;
        struct ToriRSServerPlayer* requester = player;
        struct ToriRSServerPlayer* found;

        if( !SSVM_PopStr(state, &display_name) )
            return 1;
        found = player_by_display_name(srv, display_name);
        if( !requester || !found || requester == found ||
            !ToriRSServer_FriendsIsFriend(requester->name37, found->name37) ||
            !ToriRSServer_FriendsIsFriend(found->name37, requester->name37) )
        {
            SSVM_PushInt(state, 0);
            return 1;
        }
        SSVM_SetActive(state, SSVM_ENT_PLAYER, SSVM_PRIMARY, found);
        SSVM_PointerAdd(state, SSVM_PTR_ACTIVE_PLAYER);
        SSVM_PointerAdd(state, SSVM_PTR_PROTECTED_PLAYER);
        SSVM_PushInt(state, 1);
        return 1;
    }

    case SS_OP_P_FINDVISIBLEPLAYER:
    {
        const char* display_name = NULL;
        struct ToriRSServerPlayer* requester = player;
        struct ToriRSServerPlayer* found;

        if( !SSVM_PopStr(state, &display_name) )
            return 1;
        found = player_by_display_name(srv, display_name);
        if( !requester || !found || requester == found ||
            !ToriRSServer_FriendsVisibleTo(requester->name37, found->name37) )
        {
            SSVM_PushInt(state, 0);
            return 1;
        }
        SSVM_SetActive(state, SSVM_ENT_PLAYER, SSVM_PRIMARY, found);
        SSVM_PointerAdd(state, SSVM_PTR_ACTIVE_PLAYER);
        SSVM_PointerAdd(state, SSVM_PTR_PROTECTED_PLAYER);
        SSVM_PushInt(state, 1);
        return 1;
    }

    case SS_OP_P_ISFRIEND:
    {
        int32_t other_uid;
        struct ToriRSServerPlayer* other;

        if( !SSVM_PopInt(state, &other_uid) )
            return 1;
        other = player_by_uid(srv, other_uid);
        SSVM_PushInt(state,
                     player && other &&
                         ToriRSServer_FriendsIsFriend(player->name37, other->name37));
        return 1;
    }

    case SS_OP_SESSION_LOG:
    {
        const char* text = NULL;
        int32_t level;

        if( !SSVM_PopStr(state, &text) )
            return 1;
        if( !SSVM_PopInt(state, &level) )
            return 1;
        /*
         * The reference writes these to a per-player adventure log the client
         * can read back. There is no such log here, and inventing a file format
         * for one is a lot of machinery for a feature nothing displays — so it
         * goes to stderr under TORIRSSERVER_VERBOSE, which is where every other
         * "what did content just do" line goes.
         *
         * Implemented rather than left to the loud stub because it is in front
         * of 73 LostCity files (docs/LOSTCITY_PORT_TRIAGE.md §10.5) and every
         * one of them is a quest that otherwise runs. A log line that goes
         * nowhere costs nothing; a quest that aborts on its last statement costs
         * the quest.
         */
        if( srv->verbose )
            fprintf(stderr, "torirsserver: session_log(%d) %s\n", level, text ? text : "");
        return 1;
    }

    case SS_OP_GENDER:
        SSVM_PushInt(state, srv->active_player ? srv->active_player->gender : 0);
        return 1;

    case SS_OP_TEXT_GENDER:
    {
        const char* female = NULL;
        const char* male = NULL;

        /* Popped in reverse: `text_gender("sir", "lady")` pushes male first. */
        if( !SSVM_PopStr(state, &female) )
            return 1;
        if( !SSVM_PopStr(state, &male) )
            return 1;
        SSVM_PushStr(state,
                     (srv->active_player && srv->active_player->gender) ? (female ? female : "")
                                                          : (male ? male : ""));
        return 1;
    }

    /* ---- find-all iterators ---------------------------------------- */

    /*
     * `npc_findallany($coord, $distance, $checkvis)` then
     * `while (npc_findnext = true) { ... npc_type ... }` is the shape all three
     * of these have, and the loop body reads the *active* entity — so
     * `*_findnext` has to set it, not merely return an id. Getting that wrong
     * gives a loop that runs the right number of times over the wrong entity.
     */
    case SS_OP_NPC_FINDALL:
    case SS_OP_NPC_FINDALLANY:
    {
        int32_t coord;
        int32_t npc_type = -1;
        int32_t distance;
        int32_t checkvis;

        if( !SSVM_PopInt(state, &checkvis) )
            return 1;
        if( !SSVM_PopInt(state, &distance) )
            return 1;
        if( opcode == SS_OP_NPC_FINDALL && !SSVM_PopInt(state, &npc_type) )
            return 1;
        if( !SSVM_PopInt(state, &coord) )
            return 1;

        srv->iterator.count = 0;
        srv->iterator.cursor = 0;
        srv->iterator.kind = SSVM_ENT_NPC;
        for( int slot = 0; slot < TORIRSSERVER_NPC_MAX; slot++ )
        {
            struct ToriRSServerNpc* npc = &srv->npcs[slot];
            int dx;
            int dz;

            if( !npc->active ||
                !ToriRSServer_WorldNpcVisibleTo(srv, npc, srv->active_player) ||
                npc->level != coord_level(coord) )
                continue;
            if( opcode == SS_OP_NPC_FINDALL && npc->type != npc_type )
                continue;
            dx = npc->x - coord_x(coord);
            dz = npc->z - coord_z(coord);
            if( dx < 0 )
                dx = -dx;
            if( dz < 0 )
                dz = -dz;
            if( (dx > dz ? dx : dz) > distance )
                continue;
            if( !ToriRSServer_SceneCheckvis(checkvis, npc->level, coord_x(coord),
                                       coord_z(coord), npc->x, npc->z) )
                continue;
            if( srv->iterator.count <
                (int)(sizeof(srv->iterator.slots) / sizeof(srv->iterator.slots[0])) )
                srv->iterator.slots[srv->iterator.count++] = slot;
        }
        return 1;
    }

    case SS_OP_NPC_FINDNEXT:
    {
        if( srv->iterator.kind != SSVM_ENT_NPC )
        {
            SSVM_Abort(state, "npc_findnext without a preceding npc_findall");
            return 1;
        }
        while( srv->iterator.cursor < srv->iterator.count )
        {
            int slot = srv->iterator.slots[srv->iterator.cursor++];

            /* Re-checked, because the list was built before the loop body ran
             * and the body may have killed one of them. */
            if( !srv->npcs[slot].active ||
                !ToriRSServer_WorldNpcVisibleTo(
                    srv, &srv->npcs[slot], srv->active_player) )
                continue;
            SSVM_SetActive(state, SSVM_ENT_NPC, SSVM_PRIMARY, &srv->npcs[slot]);
            state->host_tag = slot + 1;
            SSVM_PointerAdd(state, SSVM_PTR_ACTIVE_NPC);
            SSVM_PushInt(state, 1);
            return 1;
        }
        SSVM_PushInt(state, 0);
        return 1;
    }

    case SS_OP_LOC_FINDALLZONE:
    {
        int32_t coord;
        int zone_x;
        int zone_z;

        if( !SSVM_PopInt(state, &coord) )
            return 1;
        /* A zone is 8x8, and the coord names any tile in it. */
        zone_x = coord_x(coord) & ~7;
        zone_z = coord_z(coord) & ~7;

        srv->iterator.count = 0;
        srv->iterator.cursor = 0;
        srv->iterator.kind = SSVM_ENT_LOC;
        for( int slot = 0;; slot++ )
        {
            struct ToriRSServerSceneLoc* loc = ToriRSServer_SceneLoc(slot);

            if( !loc )
                break;
            if( !loc->active || loc->level != coord_level(coord) )
                continue;
            if( loc->x < zone_x || loc->x >= zone_x + 8 || loc->z < zone_z ||
                loc->z >= zone_z + 8 )
                continue;
            if( srv->iterator.count <
                (int)(sizeof(srv->iterator.slots) / sizeof(srv->iterator.slots[0])) )
                srv->iterator.slots[srv->iterator.count++] = slot;
        }
        return 1;
    }

    case SS_OP_LOC_FINDNEXT:
    {
        if( srv->iterator.kind != SSVM_ENT_LOC )
        {
            SSVM_Abort(state, "loc_findnext without a preceding loc_findallzone");
            return 1;
        }
        while( srv->iterator.cursor < srv->iterator.count )
        {
            int slot = srv->iterator.slots[srv->iterator.cursor++];
            struct ToriRSServerSceneLoc* loc = ToriRSServer_SceneLoc(slot);

            /* A `loc_del` in the loop body frees the slot; skip it rather than
             * handing the body a loc that is no longer there. */
            if( !loc || !loc->active )
                continue;
            SSVM_SetActive(state, SSVM_ENT_LOC, SSVM_PRIMARY, (void*)(intptr_t)(slot + 1));
            SSVM_PointerAdd(state, SSVM_PTR_ACTIVE_LOC);
            SSVM_PushInt(state, 1);
            return 1;
        }
        SSVM_PushInt(state, 0);
        return 1;
    }

    case SS_OP_HUNTALL:
    {
        int32_t coord;
        int32_t distance;
        int32_t checkvis;
        int dx;
        int dz;

        if( !SSVM_PopInt(state, &checkvis) )
            return 1;
        if( !SSVM_PopInt(state, &distance) )
            return 1;
        if( !SSVM_PopInt(state, &coord) )
            return 1;

        /*
         * `huntall` collects *players* in range — `[proc,sound_area]` uses it to
         * play a sound to everyone who can hear it. The fixed pool is sparse:
         * logout leaves a hole until that pid is safe to reap, so membership is
         * the player's `active` bit rather than merely being below the high-water
         * `player_count`. This matters to area mechanics such as a POH throne
         * trap: a stale hole must never be returned as a victim.
         *
         * HuntVis for players casts candidate→source (ScriptIterators.ts), the
         * opposite of the npc finds.
         */
        srv->iterator.count = 0;
        srv->iterator.cursor = 0;
        srv->iterator.kind = SSVM_ENT_PLAYER;
        for( int i = 0; i < TORIRSSERVER_PLAYER_MAX; i++ )
        {
            struct ToriRSServerPlayer* other = &srv->players[i];

            if( i >= srv->player_count || !other->active ||
                other->level != coord_level(coord) )
                continue;
            dx = other->x - coord_x(coord);
            dz = other->z - coord_z(coord);
            if( dx < 0 )
                dx = -dx;
            if( dz < 0 )
                dz = -dz;
            if( (dx > dz ? dx : dz) > distance )
                continue;
            if( !ToriRSServer_SceneCheckvis(checkvis, other->level, other->x, other->z,
                                       coord_x(coord), coord_z(coord)) )
                continue;
            srv->iterator.slots[srv->iterator.count++] = i;
        }
        return 1;
    }

    case SS_OP_HUNTNEXT:
    {
        if( srv->iterator.kind != SSVM_ENT_PLAYER )
        {
            SSVM_Abort(state, "huntnext without a preceding huntall");
            return 1;
        }
        while( srv->iterator.cursor < srv->iterator.count )
        {
            int index = srv->iterator.slots[srv->iterator.cursor++];
            struct ToriRSServerPlayer* other = &srv->players[index];

            if( index >= srv->player_count || !other->active )
                continue;
            SSVM_SetActive(state, SSVM_ENT_PLAYER, SSVM_PRIMARY, other);
            SSVM_PointerAdd(state, SSVM_PTR_ACTIVE_PLAYER);
            SSVM_PushInt(state, 1);
            return 1;
        }
        SSVM_PushInt(state, 0);
        return 1;
    }

    /* ---- npcs: addressing, lifecycle and reads --------------------- */

    /*
     * `npc_find` and friends set the active npc, which every `npc_*` opcode
     * with `require = 0x010` then acts on. The slot rides in `host_tag` (+1, so
     * zero means none) for the same reason the loc's does: a script can suspend
     * between finding an npc and acting on it, and a slot either still names
     * the same npc or names none — never a different one wearing the same
     * address.
     */
    case SS_OP_NPC_FIND:
    case SS_OP_NPC_FINDEXACT:
    {
        int32_t coord;
        int32_t npc_type;
        int32_t distance = 0;
        int32_t checkvis = 0;
        int best = -1;
        int best_range = 0;

        if( opcode == SS_OP_NPC_FIND )
        {
            if( !SSVM_PopInt(state, &checkvis) )
                return 1;
            if( !SSVM_PopInt(state, &distance) )
                return 1;
        }
        if( !SSVM_PopInt(state, &npc_type) )
            return 1;
        if( !SSVM_PopInt(state, &coord) )
            return 1;
        /*
         * `checkvis` is LostCity HuntVis (0 off / 1 lineofsight / 2 lineofwalk).
         * Content uses it to avoid addressing an npc through a wall.
         */

        for( int slot = 0; slot < TORIRSSERVER_NPC_MAX; slot++ )
        {
            struct ToriRSServerNpc* npc = &srv->npcs[slot];
            int dx;
            int dz;
            int range;

            if( !npc->active || npc->type != npc_type ||
                !ToriRSServer_WorldNpcVisibleTo(srv, npc, srv->active_player) )
                continue;
            if( npc->level != coord_level(coord) )
                continue;
            dx = npc->x - coord_x(coord);
            dz = npc->z - coord_z(coord);
            if( dx < 0 )
                dx = -dx;
            if( dz < 0 )
                dz = -dz;
            range = dx > dz ? dx : dz;
            if( opcode == SS_OP_NPC_FINDEXACT )
            {
                if( range != 0 )
                    continue;
            }
            else if( range > distance )
            {
                continue;
            }
            if( !ToriRSServer_SceneCheckvis(checkvis, npc->level, coord_x(coord),
                                       coord_z(coord), npc->x, npc->z) )
                continue;
            /* Nearest wins. The reference does the same, and it is what makes
             * `npc_find(coord, guard, 5, 0)` mean "the guard beside me" rather
             * than "whichever guard the slot array happened to reach first". */
            if( best < 0 || range < best_range )
            {
                best = slot;
                best_range = range;
            }
        }

        if( best < 0 )
        {
            SSVM_PushInt(state, 0);
            return 1;
        }
        SSVM_SetActive(state, SSVM_ENT_NPC, SSVM_PRIMARY, &srv->npcs[best]);
        state->host_tag = best + 1;
        SSVM_PointerAdd(state, SSVM_PTR_ACTIVE_NPC);
        SSVM_PushInt(state, 1);
        return 1;
    }

    /* A content-visible uid is generation:slot.  Keeping the slot in the low
     * 16 bits preserves the projectile wire convention while making handles
     * safe across a despawn and pool-slot reuse. */
    case SS_OP_NPC_UID:
    {
        int slot = active_npc_slot(state);

        if( slot < 0 || slot >= TORIRSSERVER_NPC_MAX )
        {
            SSVM_Abort(state, "npc_uid with no active npc");
            return 1;
        }
        SSVM_PushInt(state, (int32_t)(((uint32_t)srv->npcs[slot].generation << 16) |
                                     (uint32_t)(slot & 0xffff)));
        return 1;
    }

    case SS_OP_NPC_FINDUID:
    {
        int32_t uid;

        if( !SSVM_PopInt(state, &uid) )
            return 1;
        int slot = (int)((uint32_t)uid & 0xffffu);
        uint16_t generation = (uint16_t)((uint32_t)uid >> 16);

        if( slot < 0 || slot >= TORIRSSERVER_NPC_MAX || !srv->npcs[slot].active ||
            !ToriRSServer_WorldNpcVisibleTo(srv, &srv->npcs[slot], srv->active_player) ||
            generation == 0 || srv->npcs[slot].generation != generation )
        {
            SSVM_PushInt(state, 0);
            return 1;
        }
        SSVM_SetActive(state, SSVM_ENT_NPC, SSVM_PRIMARY, &srv->npcs[slot]);
        state->host_tag = slot + 1;
        SSVM_PointerAdd(state, SSVM_PTR_ACTIVE_NPC);
        SSVM_PushInt(state, 1);
        return 1;
    }

    case SS_OP_NPC_ADD:
    {
        int32_t coord;
        int32_t npc_type;
        int32_t duration;
        int slot;

        if( !SSVM_PopInt(state, &duration) )
            return 1;
        if( !SSVM_PopInt(state, &npc_type) )
            return 1;
        if( !SSVM_PopInt(state, &coord) )
            return 1;

        slot = ToriRSServer_WorldNpcSpawn(srv, npc_type, coord_x(coord), coord_z(coord),
                                       coord_level(coord));
        if( slot < 0 )
        {
            /* Soft-fail: aborting mid-enter (e.g. Telekinetic maze setup) leaves
             * the player in a half-built instance with a parked script. Content
             * can retry; the loud stderr line from npc_spawn already says why. */
            fprintf(stderr, "torirsserver: npc_add %d at %d,%d found no free slot\n",
                    npc_type, coord_x(coord), coord_z(coord));
            return 1;
        }
        /* 0 is "stays until something removes it", matching the reference and
         * matching every npc the map squares spawn. */
        srv->npcs[slot].despawn_tick = duration > 0 ? srv->tick + duration : -1;
        /* And this npc is the script's, not the world's: killing it is the end
         * of it. `EntityLifeCycle.DESPAWN` is set at exactly this call in the
         * reference too — see the field. */
        srv->npcs[slot].despawns_on_death = 1;
        /* Left active, so the script can act on what it just made. */
        SSVM_SetActive(state, SSVM_ENT_NPC, SSVM_PRIMARY, &srv->npcs[slot]);
        state->host_tag = slot + 1;
        SSVM_PointerAdd(state, SSVM_PTR_ACTIVE_NPC);
        return 1;
    }

    case SS_OP_NPC_DEL:
    {
        struct ToriRSServerNpc* npc = active_npc(state);

        if( !npc )
        {
            SSVM_Abort(state, "npc_del with no active npc");
            return 1;
        }
        /* The ordinary NPC_INFO remove path, exactly as a death does — see
         * docs/torirs_server_npc_slot_reap.md for why this is a queued free rather
         * than a direct `active = 0`. */
        ToriRSServer_WorldNpcFree(srv, active_npc_slot(state));
        return 1;
    }

    case SS_OP_NPC_TELE:
    {
        int32_t coord;
        struct ToriRSServerNpc* npc = active_npc(state);

        if( !SSVM_PopInt(state, &coord) )
            return 1;
        if( !npc )
        {
            SSVM_Abort(state, "npc_tele with no active npc");
            return 1;
        }
        /* The chokepoint, not three assignments: this used to move the npc
         * without moving its collision stamp or telling the clients, so the
         * imp left a blocked tile behind it and every observer kept drawing it
         * at the tile it teleported out of. */
        ToriRSServer_WorldNpcTeleport(npc, coord_x(coord), coord_z(coord), coord_level(coord));
        return 1;
    }

    /*
     * `npc_walk` is one line in the reference — `activeNpc.queueWaypoint(x, z)`
     * (NpcOps.ts) — and deliberately not a route: the npc phase's stepper walks
     * one tile a tick toward the waypoint, so a caller that wants a path
     * re-queues every tick. The level is dropped exactly as the reference drops
     * it ("level doesn't matter here" in PathingEntity.queueWaypoint); an npc
     * changes plane through `npc_tele`, never by walking.
     */
    case SS_OP_NPC_WALK:
    {
        int32_t coord;
        struct ToriRSServerNpc* npc = active_npc(state);

        if( !SSVM_PopInt(state, &coord) )
            return 1;
        if( !npc )
        {
            SSVM_Abort(state, "npc_walk with no active npc");
            return 1;
        }
        ToriRSServer_WorldNpcQueueWaypoint(npc, coord_x(coord), coord_z(coord));
        return 1;
    }

    /*
     * `npc_range` — the gap between the active npc's FOOTPRINT and a coord.
     *
     * The reference is `CoordGrid.distanceTo(npc, {coord, width: 1, length: 1})`
     * (NpcOps.ts), and `distanceTo` clamps each side to its own occupied square
     * before taking the Chebyshev distance. This measured `npc->x/npc->z`
     * instead — the south-west ANCHOR — which is the same number only for the
     * 1x1 npcs that are most of the roster.
     *
     * It is not the same number for anything bigger, and it is wrong in one
     * direction: a coord north or east of the npc reads (size - 1) tiles too
     * far, while one south or west reads correctly. The asymmetry is what makes
     * it hard to see. `[label,player_combat_start_ap]` (combat.rs2) gates the
     * swing on `npc_range(coord) > $attackrange` and calls `p_aprange` when it
     * fails, so a bow east of TzKal-Zuk — 7x7, so six tiles of error — was told
     * "too far" at every tile it could actually shoot from and walked in until
     * his west edge came inside its reach. That reads exactly like an
     * unreachable target dragging the player toward it, and the same six-tile
     * pull is there for every size>1 npc in the game: JalTok-Jad's melee test
     * (`npc_range(coord) <= 1`) never fires from his north or east side either.
     *
     * `npc_player_distance` in torirs_server_world.c is this same measure for the
     * mode machine, which got it right; the script op simply never followed.
     */
    case SS_OP_NPC_RANGE:
    {
        int32_t coord;
        struct ToriRSServerNpc* npc = active_npc(state);
        int size;
        int x;
        int z;
        int dx = 0;
        int dz = 0;

        if( !SSVM_PopInt(state, &coord) )
            return 1;
        if( !npc )
        {
            SSVM_Abort(state, "npc_range with no active npc");
            return 1;
        }
        /* A rider's `coord` is a deck tile at a DECK plane — project it to
         * the root frame first, or the plane test below answers "unreachable"
         * (0x7fffffff) for a shore npc four tiles off the gunwale, and every
         * flight time content multiplies from it overflows. */
        {
            int px = coord_x(coord);
            int pz = coord_z(coord);
            int pl = coord_level(coord);

            ToriRSServer_RootTile(srv, &px, &pz, &pl);
            coord = coord_pack(pl, px, pz);
        }
        if( npc->level != coord_level(coord) )
        {
            /* Different planes are not "far", they are unreachable. The
             * reference pushes -1 here, which every `> n` test in content reads
             * as "in range" — the wrong direction for a plane the npc cannot
             * act across. This is the scene's own diagonal instead: big enough
             * that no real range reaches it, so the same tests answer "far". */
            SSVM_PushInt(state, 0x7fffffff);
            return 1;
        }
        size = npc->size > 0 ? npc->size : 1;
        x = coord_x(coord);
        z = coord_z(coord);
        if( npc->x > x )
            dx = npc->x - x;
        else if( x > npc->x + size - 1 )
            dx = x - (npc->x + size - 1);
        if( npc->z > z )
            dz = npc->z - z;
        else if( z > npc->z + size - 1 )
            dz = z - (npc->z + size - 1);
        SSVM_PushInt(state, dx > dz ? dx : dz);
        return 1;
    }

    case SS_OP_NPC_STAT:
    case SS_OP_NPC_BASESTAT:
    {
        int32_t stat;
        struct ToriRSServerNpc* npc = active_npc(state);

        if( !SSVM_PopInt(state, &stat) )
            return 1;
        if( !npc )
        {
            SSVM_Abort(state, "npc_stat with no active npc");
            return 1;
        }
        /*
         * Hitpoints keep their own pair, because that is where every hit lands.
         *
         * What was here said hitpoints were "the only npc stat that *moves*",
         * and that a rev-230 npc record carries no levels so "the config is the
         * sole source and nothing drains it". The first half is still true of the
         * *config*; the second stopped being true when `npc_statsub` landed. A
         * script can now drain the other five, and `npc->stat_drain[]` records
         * how far — so base is the authored level and current is that minus the
         * drain, which is the same base/current split the player has.
         */
        if( stat == TORIRSSERVER_STAT_HITPOINTS )
        {
            SSVM_PushInt(state, opcode == SS_OP_NPC_STAT ? npc->hitpoints
                                                         : npc->base_hitpoints);
            return 1;
        }
        {
            int level = npc_base_stat(npc, stat);

            if( opcode == SS_OP_NPC_STAT )
            {
                level -= npc->stat_drain[stat];
                if( level < 0 )
                    level = 0;
            }
            SSVM_PushInt(state, level);
        }
        return 1;
    }

    /*
     * npc_statsub(stat, constant, percent) — `NpcOps.ts` NPC_STATSUB. The npc
     * twin of `stat_sub`, and the same formula against the same base:
     *
     *     level = max(current - (constant + base * percent / 100), 0)
     *
     * This is what makes a magic debuff mean anything: `player_magic.rs2` casts
     * confuse as `npc_statsub($npc_stat, abs($constant), abs($percent))`, and
     * `player_ranged.rs2` shaves defence with `npc_statsub(defence, 0, 5)`. With
     * the opcode stubbed those spells landed, played their animation, and left
     * the target exactly as strong as before.
     *
     * Recorded as a drain below the authored level rather than by writing a
     * level, for the reason `ToriRSServerNpc.stat_drain` gives. Hitpoints route to
     * `npc->hitpoints` instead — the reference writes `levels[stat]` for every
     * stat including that one, so `npc_statsub(hitpoints, …)` is damage with no
     * hitsplat, and content that wants the splat calls `npc_damage`.
     *
     * Nothing here marks the npc dead. `npc_statsub(hitpoints, …)` in the
     * reference does not either — death is noticed by the combat path that
     * reads hitpoints, and this deliberately stays a stat write so that draining
     * a target to 0 and killing it are distinguishable.
     */
    case SS_OP_NPC_STATSUB:
    {
        int32_t values[3];
        struct ToriRSServerNpc* npc = active_npc(state);
        int step;
        int i;

        for( i = 2; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        if( !npc )
        {
            SSVM_Abort(state, "npc_statsub with no active npc");
            return 1;
        }
        if( values[0] < 0 || values[0] >= TORIRSSERVER_STAT_COUNT )
        {
            SSVM_Abort(state, "npc_statsub %d is not a skill", values[0]);
            return 1;
        }

        if( values[0] == TORIRSSERVER_STAT_HITPOINTS )
        {
            step = values[1] + (npc->base_hitpoints * values[2]) / 100;
            npc->hitpoints -= step;
            if( npc->hitpoints < 0 )
                npc->hitpoints = 0;
            return 1;
        }
        step = values[1] + (npc_base_stat(npc, values[0]) * values[2]) / 100;
        npc->stat_drain[values[0]] += step;
        /* A negative step is a *restore*, and it must not push the level above
         * the authored one — the reference's `max(subbed, 0)` clamps the level,
         * which over a drain is a clamp at no drain at all. */
        if( npc->stat_drain[values[0]] < 0 )
            npc->stat_drain[values[0]] = 0;
        return 1;
    }

    /*
     * npc_statadd(stat, constant, percent) — `NpcOps.ts:507`, engine.rs2:599.
     * The mirror of `npc_statsub` above and the same formula against the same
     * base, in the other direction:
     *
     *     level = min(current + (constant + base * percent / 100), 255)
     *
     * Two things it is *not*, both of which read as the obvious implementation:
     *
     *  - It is not `npc_statheal`. That one clamps at the authored base, which
     *    is what "heal" means; this one clamps at 255 and is allowed to leave
     *    the npc above the level its content block states. `NpcOps.ts:241` vs
     *    `:507` differ in exactly that clamp, so collapsing the two would be
     *    invisible until something buffed a full-health npc.
     *  - It is not `npc_statsub` with a negated argument. `npc_statsub` clamps
     *    the *drain* at zero on purpose (see its note), so a restore can never
     *    push a level above the authored one. This opcode is the one that may.
     *
     * The one caller is `[proc,slayer_after_player_hit]`
     * (skill_slayer/slayer_specials.rs2:77): a banshee regains one hitpoint per
     * landed hit. Stubbed, it healed nothing and the npc was simply easier.
     *
     * Hitpoints route to `npc->hitpoints`, the other five to `stat_drain[]` —
     * the same split `npc_stat` / `npc_statsub` use, so a boost is a negative
     * drain and `npc_basestat` keeps answering the authored level, which is what
     * the reference's untouched `baseLevels[]` does.
     *
     * The 255 is the reference's own ceiling on a stat level (the wire holds one
     * byte), not a content-shaped number. Nothing here marks the npc alive or
     * dead: the reference does not either, for the reason `npc_statsub` gives.
     */
    case SS_OP_NPC_STATADD:
    {
        int32_t values[3];
        struct ToriRSServerNpc* npc = active_npc(state);
        int step;
        int i;

        /* Call is npc_statadd(stat, constant, percent) — pop into values[0..2]. */
        for( i = 2; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        if( !npc )
        {
            SSVM_Abort(state, "npc_statadd with no active npc");
            return 1;
        }
        if( values[0] < 0 || values[0] >= TORIRSSERVER_STAT_COUNT )
        {
            SSVM_Abort(state, "npc_statadd %d is not a skill", values[0]);
            return 1;
        }
        if( values[0] == TORIRSSERVER_STAT_HITPOINTS )
        {
            step = values[1] + (npc->base_hitpoints * values[2]) / 100;
            npc->hitpoints += step;
            if( npc->hitpoints > TORIRSSERVER_NPC_STAT_MAX )
                npc->hitpoints = TORIRSSERVER_NPC_STAT_MAX;
            if( npc->hitpoints < 0 )
                npc->hitpoints = 0;
            return 1;
        }
        step = values[1] + (npc_base_stat(npc, values[0]) * values[2]) / 100;
        npc->stat_drain[values[0]] -= step;
        /* Clamp the *level* at 255, which over a drain is a floor: level is
         * `base - drain`, so drain may not fall below `base - 255`. */
        if( npc_base_stat(npc, values[0]) - npc->stat_drain[values[0]] > TORIRSSERVER_NPC_STAT_MAX )
            npc->stat_drain[values[0]] = npc_base_stat(npc, values[0]) - TORIRSSERVER_NPC_STAT_MAX;
        return 1;
    }

    /*
     * npc_sethuntmode(hunt) — `NpcOps.ts` NPC_SETHUNTMODE. Turn this npc's
     * hunting on or off, `null` being off.
     *
     * The reference stores a `HuntType` **id**, because it has `.hunt` config
     * files describing who a npc notices, how far, and through what. This tree
     * has no hunt configs: `ToriRSServerNpcDef.huntmode` is a two-value enum, and
     * aggression is `huntrange` plus nearest-player (`torirs_server_combat.c`
     * `maybe_aggress`). So the port collapses to the one bit that survives —
     * `null` stops it hunting, any hunt id starts it — and the *profile* named by
     * that id is dropped.
     *
     * That reduction is exactly what content asks for at both reference call
     * sites: the chompy bird is given `chompybird` to make it notice you and
     * `null` to make it stop, and the gnome baller is only ever passed `null`.
     * Neither depends on which profile. A tree that grows `.hunt` files will
     * find this opcode is where the id has to start being kept.
     */
    case SS_OP_NPC_SETHUNTMODE:
    {
        int32_t hunt;
        struct ToriRSServerNpc* npc = active_npc(state);

        if( !SSVM_PopInt(state, &hunt) )
            return 1;
        if( !npc )
        {
            SSVM_Abort(state, "npc_sethuntmode with no active npc");
            return 1;
        }
        npc->huntmode = hunt < 0 ? TORIRSSERVER_HUNT_NONE : TORIRSSERVER_HUNT_AGGRESSIVE;
        return 1;
    }

    case SS_OP_NPC_STATHEAL:
    {
        int32_t values[3];
        struct ToriRSServerNpc* npc = active_npc(state);
        int base;
        int current;
        int healed;
        int step;
        int i;

        /* Call is npc_statheal(stat, constant, percent) — pop into values[0..2]. */
        for( i = 2; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        if( !npc )
        {
            SSVM_Abort(state, "npc_statheal with no active npc");
            return 1;
        }
        if( values[0] < 0 || values[0] >= TORIRSSERVER_STAT_COUNT )
        {
            SSVM_Abort(state, "npc_statheal %d is not a skill", values[0]);
            return 1;
        }
        /*
         * Formula matches NpcOps.ts NPC_STATHEAL: add the constant plus a
         * percentage of the authored base, then clamp at that base. Hitpoints
         * keep their own current/base pair; the other levels are represented
         * by `stat_drain`, so healing reduces that drain. This second path is
         * required by encounter resets such as God Wars, which restore all
         * five combat levels after the room empties.
         */
        if( values[0] == TORIRSSERVER_STAT_HITPOINTS )
        {
            base = npc->base_hitpoints;
            current = npc->hitpoints;
        }
        else
        {
            base = npc_base_stat(npc, values[0]);
            current = base - npc->stat_drain[values[0]];
            if( current < 0 )
                current = 0;
        }
        step = values[1] + (base * values[2]) / 100;
        healed = current + step;
        if( healed > base )
            healed = base;
        if( healed < 0 )
            healed = 0;
        if( values[0] == TORIRSSERVER_STAT_HITPOINTS )
            npc->hitpoints = healed;
        else
            npc->stat_drain[values[0]] = base - healed;
        return 1;
    }

    /* ---- locs ------------------------------------------------------ */

    /*
     * The active loc is held by *scene slot*, not by pointer.
     *
     * Same reason the active npc is (see `host_tag`): a script can suspend
     * between `loc_find` and `loc_change`, and a scene rebuild reallocates the
     * loc array underneath it. A stored pointer would dangle; a slot either
     * still names the same loc or names one that has changed, and the opcodes
     * below re-read it every time.
     *
     * The slot rides in the VM's active-entity pointer as `slot + 1`, so a
     * non-NULL pointer means "a loc is active" and zero means none — the same
     * +1 convention `host_tag` uses, and it satisfies the VM's own
     * `SSVM_PTR_ACTIVE_LOC` requirement check without a second field.
     */
    /*
     * `[command,loc_find](coord $coord, loc $loc)(boolean)` — LocOps.ts:79,
     * `World.getLoc(x, z, level, locType.id)` → `Zone.getLoc`, corner tile and
     * type both exact.
     *
     * Two lookups this must NOT be: `ToriRSServer_SceneFindLoc`, whose footprint
     * match and any-loc fallback are the *click* resolver — through it,
     * `loc_find(coord, X)` answered true for any loc on the tile, and
     * puro-puro's clear pass `loc_del`ed whatever the map had standing there —
     * and `_exact`, which matches tombstones.
     *
     * Beyond the scene window the ZoneMap record is the loc (the reference's
     * World holds every zone; ours holds one window plus the diff), so an
     * out-of-scene find falls through to the record and hands back a
     * zone-backed handle. Static map locs out there stay invisible — the
     * ZoneMap is the diff, not the map.
     */
    case SS_OP_LOC_FIND:
    {
        int32_t coord;
        int32_t loc_id;
        int slot;

        if( !SSVM_PopInt(state, &loc_id) )
            return 1;
        if( !SSVM_PopInt(state, &coord) )
            return 1;

        slot = ToriRSServer_SceneFindLocId(coord_x(coord), coord_z(coord), coord_level(coord),
                                         loc_id);
        if( slot >= 0 )
        {
            SSVM_SetActive(state, SSVM_ENT_LOC, SSVM_PRIMARY, (void*)(intptr_t)(slot + 1));
            SSVM_PointerAdd(state, SSVM_PTR_ACTIVE_LOC);
            SSVM_PushInt(state, 1);
            return 1;
        }
        if( !ToriRSServer_SceneContains(coord_x(coord), coord_z(coord)) )
        {
            struct ToriRSServerZoneLoc* rec = ToriRSServer_ZoneLocFindId(
                srv, coord_x(coord), coord_z(coord), coord_level(coord), loc_id);

            if( rec )
            {
                SSVM_SetActive(state, SSVM_ENT_LOC, SSVM_PRIMARY,
                               ToriRSServer_ScriptZoneLocHandle(coord_x(coord), coord_z(coord),
                                                              coord_level(coord), rec->shape));
                SSVM_PointerAdd(state, SSVM_PTR_ACTIVE_LOC);
                SSVM_PushInt(state, 1);
                return 1;
            }
        }
        SSVM_PushInt(state, 0);
        return 1;
    }

    case SS_OP_LOC_COORD:
    case SS_OP_LOC_TYPE:
    case SS_OP_LOC_ANGLE:
    case SS_OP_LOC_SHAPE:
    {
        struct ToriRSServerSceneLoc* loc = script_active_loc(state);

        if( !loc )
        {
            SSVM_Abort(state, "the active loc is gone");
            return 1;
        }
        if( opcode == SS_OP_LOC_COORD )
            SSVM_PushInt(state, coord_pack(loc->level, loc->x, loc->z));
        else if( opcode == SS_OP_LOC_TYPE )
            SSVM_PushInt(state, loc->loc_id);
        else if( opcode == SS_OP_LOC_ANGLE )
            SSVM_PushInt(state, loc->angle);
        else
            SSVM_PushInt(state, loc->shape);
        return 1;
    }

    case SS_OP_LOC_CHANGE:
    {
        int32_t loc_id;
        int32_t duration;
        struct ToriRSServerSceneLoc* loc = script_active_loc(state);
        int was_id;
        int shape;
        int angle;
        int x;
        int z;
        int level;

        if( !SSVM_PopInt(state, &duration) )
            return 1;
        if( !SSVM_PopInt(state, &loc_id) )
            return 1;
        if( !loc )
        {
            SSVM_Abort(state, "loc_change with no active loc");
            return 1;
        }
        was_id = loc->loc_id;
        shape = loc->shape;
        angle = loc->angle;
        x = loc->x;
        z = loc->z;
        level = loc->level;

        if( !ToriRSServer_WorldLocSet(srv, x, z, level, shape, loc_id, angle,
                                   TORIRSSERVER_LOC_SET_CHANGE) )
        {
            SSVM_Abort(state, "loc_change to %d, which is not in the cache", loc_id);
            return 1;
        }
        ToriRSServer_WorldLocRevertQueue(srv, duration, was_id, shape, angle, x, z, level);
        return 1;
    }

    case SS_OP_LOC_ANIM:
    {
        int32_t seq_id;
        struct ToriRSServerSceneLoc* loc = script_active_loc(state);

        if( !SSVM_PopInt(state, &seq_id) )
            return 1;
        if( !loc )
        {
            SSVM_Abort(state, "loc_anim with no active loc");
            return 1;
        }
        ToriRSServer_ZoneLocAnim(srv, loc->x, loc->z, loc->level, loc->shape, loc->angle,
                              (int)seq_id);
        return 1;
    }

    case SS_OP_LOC_DEL:
    {
        int32_t duration;
        struct ToriRSServerSceneLoc* loc = script_active_loc(state);
        int was_id;
        int shape;
        int angle;
        int x;
        int z;
        int level;

        if( !SSVM_PopInt(state, &duration) )
            return 1;
        if( !loc )
        {
            SSVM_Abort(state, "loc_del with no active loc");
            return 1;
        }
        was_id = loc->loc_id;
        shape = loc->shape;
        angle = loc->angle;
        x = loc->x;
        z = loc->z;
        level = loc->level;

        if( !ToriRSServer_WorldLocSet(srv, x, z, level, shape, -1, angle,
                                   TORIRSSERVER_LOC_SET_CHANGE) )
        {
            SSVM_Abort(state, "loc_del on a loc that is already gone");
            return 1;
        }
        ToriRSServer_WorldLocRevertQueue(srv, duration, was_id, shape, angle, x, z, level);
        return 1;
    }

    /*
     * `[command,loc_add](coord $coord, loc $loc, int $angle, locshape $shape,
     * int $duration)` — engine.rs2:657, and `LocOps.ts:19` destructures exactly
     * that order: `const [coord, type, angle, shape, duration] = popInts(5)`.
     *
     * **angle before shape.** This popped them the other way round until
     * 2026-08-02, i.e. it implemented `loc_add($coord, $loc, $shape, $angle,
     * $duration)`. `ss_meta.gen.h` carries arity and stack class, not argument
     * order, so nothing could catch it at the call — and the two selftest
     * callers were written against the transposed order, one of them with
     * `10, 0` where 10 is a *shape* (`centrepiece_straight`) in an argument the
     * reference calls the angle. Symmetric values hide this completely, which is
     * the same failure `docs/` records for POP_ARRAY_INT: the door port is what
     * exposed it, because a swinging door is the first caller whose angle and
     * shape genuinely differ.
     */
    case SS_OP_LOC_ADD:
    {
        int32_t coord;
        int32_t loc_id;
        int32_t shape;
        int32_t angle;
        int32_t duration;
        int slot;

        if( !SSVM_PopInt(state, &duration) )
            return 1;
        if( !SSVM_PopInt(state, &shape) )
            return 1;
        if( !SSVM_PopInt(state, &angle) )
            return 1;
        if( !SSVM_PopInt(state, &loc_id) )
            return 1;
        if( !SSVM_PopInt(state, &coord) )
            return 1;

        if( !ToriRSServer_WorldLocSet(srv, coord_x(coord), coord_z(coord), coord_level(coord),
                                   shape, loc_id, angle, TORIRSSERVER_LOC_SET_ADD) )
        {
            SSVM_Abort(state, "loc_add %d at %d,%d failed — unknown loc or outside the scene",
                       loc_id, coord_x(coord), coord_z(coord));
            return 1;
        }
        slot = ToriRSServer_SceneFindLocExact(coord_x(coord), coord_z(coord),
                                            coord_level(coord), shape);
        /* -1 says "remove it again" rather than "put something back". */
        ToriRSServer_WorldLocRevertQueue(srv, duration, -1, shape, angle, coord_x(coord),
                                       coord_z(coord), coord_level(coord));
        /* The reference leaves the added loc active, so the next `loc_change`
         * or `loc_del` in the same script addresses it without a `loc_find`.
         * Outside the scene window there is no slot — the loc lives only in
         * the ZoneMap — so the handle names the record instead. */
        if( slot >= 0 )
            SSVM_SetActive(state, SSVM_ENT_LOC, SSVM_PRIMARY, (void*)(intptr_t)(slot + 1));
        else
            SSVM_SetActive(state, SSVM_ENT_LOC, SSVM_PRIMARY,
                           ToriRSServer_ScriptZoneLocHandle(coord_x(coord), coord_z(coord),
                                                          coord_level(coord), shape));
        SSVM_PointerAdd(state, SSVM_PTR_ACTIVE_LOC);
        return 1;
    }

    /*
     * loc_add_op(coord, loc, angle, shape, duration, opslot, optext)
     *
     * `loc_add`, plus this placement's own right-click menu: exactly one
     * option, `optext` on slot `opslot`, with every other slot hidden. The two
     * fields LOC_ADD_CHANGE_V2 carries and LOC_ADD_CHANGE did not.
     *
     * Deliberately a *separate* case from SS_OP_LOC_ADD rather than a shared
     * body with a menu argument, because the two say different things about the
     * same tile and the difference is worth reading at the call site: `loc_add`
     * places a loc that means what its type means, and this one places a loc
     * whose menu belongs to where it is standing.
     */
    case SS_OP_LOC_ADD_OP:
    {
        int32_t coord;
        int32_t loc_id;
        int32_t shape;
        int32_t angle;
        int32_t duration;
        int32_t op_slot;
        const char* op_text = NULL;
        struct ToriRSServerLocOps ops;
        int slot;

        if( !SSVM_PopStr(state, &op_text) )
            return 1;
        if( !SSVM_PopInt(state, &op_slot) )
            return 1;
        if( !SSVM_PopInt(state, &duration) )
            return 1;
        if( !SSVM_PopInt(state, &shape) )
            return 1;
        if( !SSVM_PopInt(state, &angle) )
            return 1;
        if( !SSVM_PopInt(state, &loc_id) )
            return 1;
        if( !SSVM_PopInt(state, &coord) )
            return 1;

        if( op_slot < 1 || op_slot > 5 )
        {
            SSVM_Abort(state, "loc_add_op: op slot %d is not 1..5", op_slot);
            return 1;
        }
        assert(op_text);
        if( op_text[0] == '\0' )
        {
            /* An empty label is not "no override" here — it is a menu with one
             * slot shown and nothing in it, which the client draws as a loc
             * that cannot be clicked. A caller that wants the loctype's own
             * menu has `loc_add`. */
            SSVM_Abort(state, "loc_add_op: empty op text for loc %d — use loc_add", loc_id);
            return 1;
        }
        if( strlen(op_text) >= TORIRSSERVER_LOC_OP_TEXT_MAX )
        {
            SSVM_Abort(state, "loc_add_op: op text '%s' is longer than %d", op_text,
                       TORIRSSERVER_LOC_OP_TEXT_MAX - 1);
            return 1;
        }

        memset(&ops, 0, sizeof(ops));
        ops.flags = 1 << (op_slot - 1);
        snprintf(ops.name[op_slot - 1], sizeof(ops.name[op_slot - 1]), "%s", op_text);

        if( !ToriRSServer_WorldLocSetOps(srv, coord_x(coord), coord_z(coord),
                                       coord_level(coord), shape, loc_id, angle,
                                       TORIRSSERVER_LOC_SET_ADD, &ops) )
        {
            SSVM_Abort(state,
                       "loc_add_op %d at %d,%d failed — unknown loc or outside the scene",
                       loc_id, coord_x(coord), coord_z(coord));
            return 1;
        }
        slot = ToriRSServer_SceneFindLocExact(coord_x(coord), coord_z(coord),
                                            coord_level(coord), shape);
        ToriRSServer_WorldLocRevertQueue(srv, duration, -1, shape, angle, coord_x(coord),
                                       coord_z(coord), coord_level(coord));
        if( slot >= 0 )
            SSVM_SetActive(state, SSVM_ENT_LOC, SSVM_PRIMARY, (void*)(intptr_t)(slot + 1));
        else
            SSVM_SetActive(state, SSVM_ENT_LOC, SSVM_PRIMARY,
                           ToriRSServer_ScriptZoneLocHandle(coord_x(coord), coord_z(coord),
                                                          coord_level(coord), shape));
        SSVM_PointerAdd(state, SSVM_PTR_ACTIVE_LOC);
        return 1;
    }

    case SS_OP_OC_PARAM:
    {
        int32_t obj_id;
        int32_t param_id;
        const struct ToriRSServerObjParam* row;

        if( !SSVM_PopInt(state, &param_id) )
            return 1;
        if( !SSVM_PopInt(state, &obj_id) )
            return 1;

        row = ToriRSServer_ObjParam(obj_id, param_id);
        ToriRSServer_PushTypedParam(state, param_id, row ? row->sval : NULL,
                                 row ? row->ival : 0, row != NULL, "obj", obj_id);
        return 1;
    }

    case SS_OP_NC_PARAM:
    {
        int32_t npc_id;
        int32_t param_id;
        const struct ToriRSServerNpcParam* row;

        if( !SSVM_PopInt(state, &param_id) )
            return 1;
        if( !SSVM_PopInt(state, &npc_id) )
            return 1;

        row = ToriRSServer_NpcParam(npc_id, param_id);
        ToriRSServer_PushTypedParam(state, param_id, row ? row->sval : NULL,
                                 row ? row->ival : 0, row != NULL, "npc", npc_id);
        return 1;
    }

    case SS_OP_OC_DEBUGNAME:
    {
        int32_t obj_id;
        const char* symbol;

        if( !SSVM_PopInt(state, &obj_id) )
            return 1;
        /* The *content* name (`bronze_scimitar`), not the display name
         * ("Bronze scimitar"). That is what makes it a debug name: it is the
         * symbol a script would have written. */
        symbol = ToriRSServer_ContentSymbolName(TORIRSSERVER_PACK_OBJ, obj_id);
        SSVM_PushStr(state, symbol ? symbol : "null");
        return 1;
    }

    case SS_OP_INV_DEBUGNAME:
    {
        int32_t inv_id;
        const char* symbol;

        if( !SSVM_PopInt(state, &inv_id) )
            return 1;
        symbol = ToriRSServer_ContentSymbolName(TORIRSSERVER_PACK_INV, inv_id);
        SSVM_PushStr(state, symbol ? symbol : "null");
        return 1;
    }

    case SS_OP_NC_NAME:
    {
        int32_t npc_type;

        if( !SSVM_PopInt(state, &npc_type) )
            return 1;
        SSVM_PushStr(state, ToriRSServer_NpcInfo(npc_type)->name);
        return 1;
    }

    case SS_OP_NC_DEBUGNAME:
    {
        int32_t npc_type;
        const char* symbol;

        if( !SSVM_PopInt(state, &npc_type) )
            return 1;
        symbol = ToriRSServer_ContentSymbolName(TORIRSSERVER_PACK_NPC, npc_type);
        SSVM_PushStr(state, symbol ? symbol : "null");
        return 1;
    }

    case SS_OP_NC_OP:
    {
        int32_t npc_type;
        int32_t op_num;
        const struct ToriRSServerNpcInfo* info;

        if( !SSVM_PopInt(state, &op_num) || !SSVM_PopInt(state, &npc_type) )
            return 1;
        info = ToriRSServer_NpcInfo(npc_type);
        /* 1-based, as every other op index on the wire and in content is. An
         * absent op is the empty string rather than an abort: asking whether an
         * npc offers op 4 is a normal thing for content to do. */
        if( op_num < 1 || op_num > 5 || !info->ops[op_num - 1] )
            SSVM_PushStr(state, "");
        else
            SSVM_PushStr(state, info->ops[op_num - 1]);
        return 1;
    }

    case SS_OP_NC_SIZE:
    {
        int32_t npc_type;

        if( !SSVM_PopInt(state, &npc_type) )
            return 1;
        SSVM_PushInt(state, ToriRSServer_NpcInfo(npc_type)->size);
        return 1;
    }

    case SS_OP_NC_VISLEVEL:
    {
        int32_t npc_type;

        if( !SSVM_PopInt(state, &npc_type) )
            return 1;
        /* The level the client prints beside the name, which is the record's
         * own `combat_level` — not anything derived from the npc's stats. */
        SSVM_PushInt(state, ToriRSServer_NpcInfo(npc_type)->combat_level);
        return 1;
    }

    case SS_OP_OC_STACKABLE:
    {
        int32_t obj_id;

        if( !SSVM_PopInt(state, &obj_id) )
            return 1;
        SSVM_PushInt(state, ToriRSServer_ObjInfo(obj_id)->stackable);
        return 1;
    }

    /* The obj record's own category (opcode 94) — the same number the
     * `[opheld<n>,_<category>]` trigger keys on, so content can test it
     * directly for the cases a trigger cannot express. */
    case SS_OP_OC_CATEGORY:
    {
        int32_t obj_id;

        if( !SSVM_PopInt(state, &obj_id) )
            return 1;
        SSVM_PushInt(state, ToriRSServer_ObjInfo(obj_id)->category);
        return 1;
    }

    /* ---- animation and effects ------------------------------------ */

    case SS_OP_ANIM:
    {
        int32_t values[2];

        for( int i = 1; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        /* Through the gate, exactly like the engine's own animations — the
         * reference routes `anim` through `playAnimation` too, so a script that
         * plays a low-priority emote cannot cut off a swing already queued this
         * tick.
         *
         * `anim(null, …)` is the exception, and it is not the gate's business:
         * -1 CANCELS, and it reaches the client as 65535 ("stop what you are
         * playing"). `ToriRSServer_AnimPlayPlayer` refuses a negative id on
         * purpose — from C, -1 only ever means "the content named nothing" —
         * but from a script it is the one way to end an animation, and the
         * reference's own `death.rs2` ends with it. Without this, a death
         * animation whose last frame holds for 20,000 cycles (which is what
         * `human_death` states) never ends: the player respawns, walks, fights
         * and banks lying on the ground. */
        if( values[0] < 0 )
        {
            player->anim_id = -1;
            player->anim_delay = (int)values[1];
            player->masks |= TORIRSSERVER_PMASK_SEQUENCE;
            return 1;
        }
        ToriRSServer_AnimPlayPlayer(player, values[0], values[1]);
        return 1;
    }

    /* Appearance stance seqs — LostCity PlayerOps READYANIM…RUNANIM. Each
     * write dirties APPEARANCE so a BAS change without a worn-container write
     * (agility ~bas_set restore, equip already dirties via worn) still ships. */
    case SS_OP_READYANIM:
    {
        int32_t seq;

        if( !SSVM_PopInt(state, &seq) )
            return 1;
        if( player->world->verbose )
            fprintf(stderr, "torirsserver: readyanim seq=%d\n", seq);
        player->readyanim = seq;
        player->masks |= TORIRSSERVER_PMASK_APPEARANCE;
        return 1;
    }
    case SS_OP_TURNANIM:
    {
        int32_t seq;

        if( !SSVM_PopInt(state, &seq) )
            return 1;
        if( player->world->verbose )
            fprintf(stderr, "torirsserver: turnanim seq=%d\n", seq);
        player->turnanim = seq;
        player->masks |= TORIRSSERVER_PMASK_APPEARANCE;
        return 1;
    }
    case SS_OP_WALKANIM:
    {
        int32_t seq;

        if( !SSVM_PopInt(state, &seq) )
            return 1;
        if( player->world->verbose )
            fprintf(stderr, "torirsserver: walkanim seq=%d\n", seq);
        player->walkanim = seq;
        player->masks |= TORIRSSERVER_PMASK_APPEARANCE;
        return 1;
    }
    case SS_OP_WALKANIM_B:
    {
        int32_t seq;

        if( !SSVM_PopInt(state, &seq) )
            return 1;
        if( player->world->verbose )
            fprintf(stderr, "torirsserver: walkanim_b seq=%d\n", seq);
        player->walkanim_b = seq;
        player->masks |= TORIRSSERVER_PMASK_APPEARANCE;
        return 1;
    }
    case SS_OP_WALKANIM_L:
    {
        int32_t seq;

        if( !SSVM_PopInt(state, &seq) )
            return 1;
        if( player->world->verbose )
            fprintf(stderr, "torirsserver: walkanim_l seq=%d\n", seq);
        player->walkanim_l = seq;
        player->masks |= TORIRSSERVER_PMASK_APPEARANCE;
        return 1;
    }
    case SS_OP_WALKANIM_R:
    {
        int32_t seq;

        if( !SSVM_PopInt(state, &seq) )
            return 1;
        if( player->world->verbose )
            fprintf(stderr, "torirsserver: walkanim_r seq=%d\n", seq);
        player->walkanim_r = seq;
        player->masks |= TORIRSSERVER_PMASK_APPEARANCE;
        return 1;
    }
    case SS_OP_RUNANIM:
    {
        int32_t seq;

        if( !SSVM_PopInt(state, &seq) )
            return 1;
        if( player->world->verbose )
            fprintf(stderr, "torirsserver: runanim seq=%d\n", seq);
        /* LostCity allows -1 (null) to clear runanim. */
        player->runanim = seq;
        player->masks |= TORIRSSERVER_PMASK_APPEARANCE;
        return 1;
    }

    case SS_OP_NPC_ANIM:
    {
        int32_t values[2];
        struct ToriRSServerNpc* npc = active_npc(state);

        for( int i = 1; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        if( !npc )
        {
            SSVM_Abort(state, "npc_anim with no active npc");
            return 1;
        }
        if( getenv("TORIRS_ANIM_DEBUG") )
            fprintf(
                stderr,
                "srv: npc_anim tick=%d slot=%d seq=%d delay=%d\n",
                srv->tick,
                (int)(npc - srv->npcs),
                values[0],
                values[1]);
        ToriRSServer_AnimPlayNpc(npc, values[0], values[1]);
        return 1;
    }

    case SS_OP_SPOTANIM_PL:
    {
        int32_t values[3];

        for( int i = 2; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        player->spotanim_id = values[0];
        /* Height and delay share one int on the wire: height in the high half,
         * delay in the low. */
        player->spotanim_height_delay = (values[1] << 16) | (values[2] & 0xffff);
        player->masks |= TORIRSSERVER_PMASK_SPOTANIM;
        return 1;
    }

    /*
     * spotanim_map(spotanim, coord, height, delay) — engine.rs2. LostCity
     * World.animMap → MAP_ANIM zone sub-packet. The client already decodes it
     * (App_WorldSpotanimSpawn); this is the host that was missing.
     */
    case SS_OP_SPOTANIM_MAP:
    {
        int32_t values[4];

        for( int i = 3; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        ToriRSServer_ZoneMapanim(srv, ToriRSServer_CoordX(values[1]), ToriRSServer_CoordZ(values[1]),
                             ToriRSServer_CoordLevel(values[1]), (int)values[0], (int)values[2],
                             (int)values[3]);
        return 1;
    }

    case SS_OP_SPOTANIM_NPC:
    {
        int32_t values[3];
        struct ToriRSServerNpc* npc = active_npc(state);

        for( int i = 2; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        if( !npc )
        {
            SSVM_Abort(state, "spotanim_npc with no active npc");
            return 1;
        }
        npc->spotanim_id = values[0];
        npc->spotanim_height_delay = (values[1] << 16) | (values[2] & 0xffff);
        npc->masks |= TORIRSSERVER_NMASK_SPOTANIM;
        return 1;
    }

    case SS_OP_FACESQUARE:
    {
        int32_t coord;

        if( !SSVM_PopInt(state, &coord) )
            return 1;
        /* Absolute half-tiles (LostCity faceSquare → fine(x,1)); the client
         * treats faceSquareX/Z as (tile<<1)+1 and 0,0 is the "none" sentinel. */
        player->face_x = ToriRSServer_CoordFine(coord_x(coord), 1);
        player->face_z = ToriRSServer_CoordFine(coord_z(coord), 1);
        player->masks |= TORIRSSERVER_PMASK_FACE_COORD;
        return 1;
    }

    case SS_OP_NPC_FACESQUARE:
    {
        int32_t coord;
        struct ToriRSServerNpc* npc = active_npc(state);

        if( !SSVM_PopInt(state, &coord) )
            return 1;
        if( !npc )
        {
            SSVM_Abort(state, "npc_facesquare with no active npc");
            return 1;
        }
        /* Same rule as the FACE_ENTITY seam: a record with turnspeed 0 never
         * turns, whatever the facing source. Gated here as well because this
         * op writes face_x/face_z directly and does not go through
         * `ToriRSServer_NpcFacePlayer`. */
        if( npc->turnspeed == 0 )
            return 1;
        npc->face_x = ToriRSServer_CoordFine(coord_x(coord), 1);
        npc->face_z = ToriRSServer_CoordFine(coord_z(coord), 1);
        npc->masks |= TORIRSSERVER_NMASK_FACE_COORD;
        /*
         * A coord facing SUPERSEDES the entity latch, and the server's own copy
         * has to say so or the two ends desync permanently.
         *
         * The rev-239 client clears `facing.entity_id` the moment a FACE_COORD
         * arrives (`World_BeginModernFacing`, reached from the FACE_COORD op in
         * task_exec_entity_info.c), and a V5 Face block cannot carry a loc and
         * an entity at once — `v5_face_from_classic` lets the coord win. But
         * `ToriRSServer_NpcFaceNpc` only sets the mask when the latch VALUE
         * changes, so a server that still believes it is facing slot N never
         * re-sends it: the client faces nobody, forever, while the server sees
         * a perfectly good latch.
         *
         * That is what made a familiar stop tracking a moving victim. Every
         * special move calls `npc_facesquare`, and one call killed the
         * FACE_ENTITY that `npc_attacknpc` had established for the whole fight
         * (measured: 331 cycles tracking the target before the call, 0 after).
         * Dropping our copy here lets the next `ToriRSServer_NpcFaceNpc` — which
         * combat and the mode machine both run every tick — see a change and
         * re-latch, so the facing self-heals on the following tick.
         */
        npc->face_entity = -1;
        return 1;
    }

    case SS_OP_SAY:
    {
        const char* text;

        if( !SSVM_PopStr(state, &text) )
            return 1;
        snprintf(player->say, sizeof(player->say), "%s", text);
        player->masks |= TORIRSSERVER_PMASK_SAY;
        return 1;
    }

    case SS_OP_NPC_CHANGETYPE_KEEPALL:
    case SS_OP_NPC_CHANGETYPE:
    {
        int32_t type;
        int32_t duration;
        struct ToriRSServerNpc* npc = active_npc(state);

        /* engine.rs2 declares (npc type, int duration), and the type is the
         * first value, not the top-most duration. `duration` ticks the new form
         * down to `spawn_type` in the npc phase — see
         * `ToriRSServerNpc.changetype_delay`. */
        if( !SSVM_PopInt(state, &duration) || !SSVM_PopInt(state, &type) )
            return 1;
        if( !npc )
        {
            SSVM_Abort(state, "npc_changetype with no active npc");
            return 1;
        }
        ToriRSServer_NpcChangeType(npc, type, duration);
        return 1;
    }

    /* ---- interfaces ----------------------------------------------- */

    case SS_OP_IF_SETTEXT:
    {
        const char* text;
        int32_t uid;

        if( !SSVM_PopStr(state, &text) )
            return 1;
        if( !SSVM_PopInt(state, &uid) )
            return 1;
        ToriRSServer_SendIfSettext(srv->active_player, uid, text);
        return 1;
    }

    case SS_OP_IF_SETNPCHEAD:
    {
        int32_t values[2];

        for( int i = 1; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        ToriRSServer_SendIfSetnpchead(srv->active_player, values[0], values[1]);
        return 1;
    }

    case SS_OP_IF_SETPLAYERHEAD:
    {
        int32_t uid;

        if( !SSVM_PopInt(state, &uid) )
            return 1;
        ToriRSServer_SendIfSetplayerhead(srv->active_player, uid);
        return 1;
    }

    case SS_OP_IF_SETANIM:
    {
        int32_t values[2];

        for( int i = 1; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        ToriRSServer_SendIfSetanim(srv->active_player, values[0], values[1]);
        return 1;
    }

    case SS_OP_IF_SETCOLOUR:
    {
        int32_t values[2];
        int colour;

        for( int i = 1; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        /*
         * CONTENT WRITES 24-BIT, THE WIRE CARRIES 15. `IF_SETCOLOUR` is two
         * bytes on every revision this server speaks and the client expands
         * them as RGB555 (`rs15_to_rgb`, rs_gameproto_exec.c), so a script
         * colour has to be packed here — LostCity's `ifSetColour` does the same
         * conversion at the same seam.
         *
         * Without it the low sixteen bits were sent raw, which is a silent
         * black for every colour whose bottom two bytes are zero: 0xa00000 and
         * 0x300000, the Theatre's title card and its wash, both arrived as 0 —
         * and a rect that is asked for black over a dark room is a rect nobody
         * can see. Content writes `0x8f0000` here for the same reason it writes
         * `<col=8f0000>` in a message: one spelling of a colour in one tree.
         */
        colour = (((values[1] >> 16) & 0xff) >> 3) << 10;
        colour |= (((values[1] >> 8) & 0xff) >> 3) << 5;
        colour |= ((values[1] & 0xff) >> 3);
        ToriRSServer_SendIfSetcolour(srv->active_player, values[0], colour);
        return 1;
    }

    case SS_OP_IF_SETHIDE:
    {
        int32_t values[2];

        for( int i = 1; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        ToriRSServer_SendIfSethide(srv->active_player, values[0], values[1]);
        return 1;
    }

    case SS_OP_IF_SETMODEL:
    {
        int32_t values[2];

        for( int i = 1; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        ToriRSServer_SendIfSetmodel(srv->active_player, values[0], values[1]);
        return 1;
    }

    case SS_OP_IF_SETOBJECT:
    {
        int32_t values[3];

        for( int i = 2; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        ToriRSServer_SendIfSetobject(
            srv->active_player, values[0], values[1], values[2]);
        return 1;
    }

    case SS_OP_IF_SETPOSITION:
    {
        int32_t values[3];

        for( int i = 2; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        ToriRSServer_SendIfSetposition(
            srv->active_player, values[0], values[1], values[2]);
        return 1;
    }

    case SS_OP_IF_SETSCROLLPOS:
    {
        int32_t values[2];

        for( int i = 1; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        ToriRSServer_SendIfSetscroll(srv->active_player, values[0], values[1]);
        return 1;
    }

    case SS_OP_IF_OPENCHAT:
    {
        int32_t group;
        int slot = ToriRSServer_Ids()->com_chatbox_modal;

        if( !SSVM_PopInt(state, &group) )
            return 1;
        /*
         * One packet. `chatbox:chatmodal` ships hidden=1, but unhiding it is
         * the *client's* job, not the server's: mounting a sub-interface into
         * it fires the gameframe's on_sub_change hook, and script908 both
         * unhides the modal and hides `chatbox:chatdisplay` behind it. See
         * ToriRSServerIds.com_chatbox_modal.
         */
        ToriRSServer_SendIfOpensub(
            srv->active_player, TORIRSSERVER_COM_GROUP(slot), TORIRSSERVER_COM_CHILD(slot), group, 0);
        return 1;
    }

    /*
     * if_setevents(component, from, to, events) — the rev-230 command with no
     * LostCity equivalent (src/serverscript/gen_opcode_meta.py EXTRA_OPCODES).
     *
     * At rev 230 a component is inert until the server arms it: the cache says
     * a widget *has* an op, the events mask says whether picking that op is
     * transmitted. Every "clicking it does nothing" in the gameframe is this
     * packet not having been sent — the client resolves the verb, runs the
     * component's own CS2 onop, and then has nothing to tell the server,
     * because bit N of the mask was clear.
     *
     * `from`/`to` are the sub-id range for a grid; a plain component uses 0,0.
     */
    case SS_OP_IF_SETEVENTS:
    {
        int32_t com;
        int32_t from;
        int32_t to;
        int32_t events;

        /* Popped last-argument-first, like every other host command here. */
        if( !SSVM_PopInt(state, &events) || !SSVM_PopInt(state, &to) ||
            !SSVM_PopInt(state, &from) || !SSVM_PopInt(state, &com) )
            return 1;
        ToriRSServer_SendIfSetevents(srv->active_player, (int)com, (int)from, (int)to, (int)events);
        return 1;
    }

    /*
     * if_opensub(component, interface, type) — the general form of the
     * reference's if_openmain / if_openside / if_openoverlay.
     *
     * Those name one fixed slot each, which is the whole 2004 vocabulary. At
     * rev 230 a panel mounts into an arbitrary component of whatever is already
     * open, and panels nest — the side journal's five tabs all mount into
     * `side_journal:tab_container`. So the slot is an argument.
     */
    case SS_OP_IF_OPENSUB:
    {
        int32_t com;
        int32_t group;
        int32_t type;

        if( !SSVM_PopInt(state, &type) || !SSVM_PopInt(state, &group) ||
            !SSVM_PopInt(state, &com) )
            return 1;
        ToriRSServer_SendIfOpensub(
            srv->active_player, TORIRSSERVER_COM_GROUP(com), TORIRSSERVER_COM_CHILD(com), (int)group, (int)type);
        return 1;
    }

    /*
     * if_closesub(component) — the inverse of if_opensub, naming the slot.
     *
     * Distinct from SS_OP_IF_CLOSE below, which is the reference's `if_close`
     * and is specialised here to the chatbox modal because that is what every
     * `[if_close]` caller in the tree means by it. A panel that mounted itself
     * into `toplevel_osrs_stretch:mainmodal` has to name that component again
     * to come out, and the note-the-mount bookkeeping the X and Escape read is
     * done inside the encoder, so a close through here keeps CLOSE_MODAL right
     * for free.
     */
    case SS_OP_IF_CLOSESUB:
    {
        int32_t com;

        if( !SSVM_PopInt(state, &com) )
            return 1;
        ToriRSServer_SendIfClosesub(srv->active_player, (int)com);
        return 1;
    }

    case SS_OP_IF_OPENTOP:
    {
        int32_t group;

        if( !SSVM_PopInt(state, &group) )
            return 1;
        ToriRSServer_GameframeOpentop(srv->active_player, (int)group);
        return 1;
    }

    case SS_OP_IF_MOVESUB:
    {
        int32_t dest;
        int32_t source;

        /* Declaration order is source, dest; stack pops reverse. */
        if( !SSVM_PopInt(state, &dest) || !SSVM_PopInt(state, &source) )
            return 1;
        ToriRSServer_SendIfMovesub(srv->active_player, (int)source, (int)dest);
        return 1;
    }

    case SS_OP_IF_CLOSE:
        /* Unmounting is the whole message: the same on_sub_change hook that
         * hid `chatbox:chatdisplay` on the way in brings it back when the
         * modal has no sub again (script908's else branch). */
        ToriRSServer_SendIfClosesub(srv->active_player, ToriRSServer_Ids()->com_chatbox_modal);
        player->resume_button_count = 0;
        return 1;

    /*
     * if_getmain()(interface) — which interface occupies the gameframe
     * mainmodal slot. See EXTRA_OPCODES in gen_opcode_meta.py.
     *
     * The encoder stores 0 when the slot is empty; content compares against
     * `null` (−1), so map empty to null here rather than inventing a second
     * sentinel content has to learn.
     */
    case SS_OP_IF_GETMAIN:
    {
        int group = player->mainmodal_group;

        SSVM_PushInt(state, group > 0 ? group : -1);
        return 1;
    }

    /*
     * `runclientscript_ss(clientscript, string, string)` — see the opcode's
     * entry in gen_opcode_meta.py for why it exists.
     *
     * Arguments are popped in reverse, as every RuneScript command does, and
     * handed to the encoder in declaration order.
     */
    case SS_OP_RUNCLIENTSCRIPT_SS:
    {
        const char* argv[2];
        int32_t script_id;

        if( !SSVM_PopStr(state, &argv[1]) || !SSVM_PopStr(state, &argv[0]) ||
            !SSVM_PopInt(state, &script_id) )
            return 1;
        ToriRSServer_SendRunClientscriptMixed(srv->active_player, (int)script_id, "ss", NULL, argv, 2);
        return 1;
    }

    /*
     * `runclientscript*(clientscript)(args...)` — the general form.
     *
     * The compiler lays a vararg call out as: the declared arguments, then the
     * vararg values, then a type string describing them (`ssc_compile.c`'s
     * vararg block; the same layout the reference's popScriptArgs reads). So
     * this pops the type string first and walks it *backwards*, because the
     * last value pushed is the first one off — which is exactly the order the
     * RUNCLIENTSCRIPT packet writes its arguments in anyway.
     *
     * The type string is copied rather than held: it is a pool pointer, and
     * the loop below pops other pool pointers on top of it.
     */
    case SS_OP_RUNCLIENTSCRIPTVARARG:
    {
        char types[TORIRSSERVER_RUNCLIENTSCRIPT_ARG_MAX + 1];
        int intv[TORIRSSERVER_RUNCLIENTSCRIPT_ARG_MAX];
        const char* strv[TORIRSSERVER_RUNCLIENTSCRIPT_ARG_MAX];
        const char* type_string;
        int32_t script_id;
        int argc;
        int i;

        if( !SSVM_PopStr(state, &type_string) )
            return 1;
        argc = (int)strlen(type_string);
        if( argc > TORIRSSERVER_RUNCLIENTSCRIPT_ARG_MAX )
        {
            /* Louder than a truncation: a short packet would run the
             * clientscript with the wrong arguments and look like a content
             * bug in the panel it drew. */
            SSVM_Abort(state, "runclientscript* takes at most %d arguments, given %d",
                       TORIRSSERVER_RUNCLIENTSCRIPT_ARG_MAX, argc);
            return 1;
        }
        memcpy(types, type_string, (size_t)argc);
        types[argc] = '\0';

        for( i = argc - 1; i >= 0; i-- )
        {
            intv[i] = 0;
            strv[i] = "";
            if( types[i] == 's' )
            {
                if( !SSVM_PopStr(state, &strv[i]) )
                    return 1;
            }
            else
            {
                int32_t value;

                if( !SSVM_PopInt(state, &value) )
                    return 1;
                intv[i] = (int)value;
            }
        }
        if( !SSVM_PopInt(state, &script_id) )
            return 1;
        ToriRSServer_SendRunClientscriptMixed(srv->active_player, (int)script_id, types, intv,
                                            strv, argc);
        return 1;
    }

    /*
     * The split buffer belongs to SSVM_State, matching LostCity's ScriptState:
     * a proc can split, call helper procs, and read the same pages, while two
     * players' suspended scripts cannot overwrite one another.  Glyph widths
     * come from the fontmetrics archive content passed by name through the
     * font pack (torirs_server_split.c).
     */
    case SS_OP_SPLIT_INIT:
    {
        int32_t font_id;
        int32_t lines_per_page;
        int32_t max_width;
        const char* text;

        if( !SSVM_PopInt(state, &font_id) || !SSVM_PopInt(state, &lines_per_page) ||
            !SSVM_PopInt(state, &max_width) || !SSVM_PopStr(state, &text) )
            return 1;
        ToriRSServer_SplitInit(
            state, text, (int)max_width, (int)lines_per_page, (int)font_id);
        return 1;
    }

    case SS_OP_SPLIT_GET:
    {
        int32_t line;
        int32_t page;

        if( !SSVM_PopInt(state, &line) || !SSVM_PopInt(state, &page) )
            return 1;
        SSVM_PushStr(state, ToriRSServer_SplitGet(state, (int)page, (int)line));
        return 1;
    }

    case SS_OP_SPLIT_PAGECOUNT:
        SSVM_PushInt(state, ToriRSServer_SplitPagecount(state));
        return 1;

    case SS_OP_SPLIT_LINECOUNT:
    {
        int32_t page;

        if( !SSVM_PopInt(state, &page) )
            return 1;
        SSVM_PushInt(state, ToriRSServer_SplitLinecount(state, (int)page));
        return 1;
    }

    case SS_OP_SPLIT_GETANIM:
    {
        int32_t page;

        if( !SSVM_PopInt(state, &page) )
            return 1;
        (void)page;
        SSVM_PushInt(state, state->split_mesanim);
        return 1;
    }

    case SS_OP_IF_ADDRESUMEBUTTON:
    {
        int32_t uid;

        if( !SSVM_PopInt(state, &uid) )
            return 1;
        /*
         * A full table is a content bug, and dropping the overflow is the one
         * response that cannot be noticed: the row still draws, still lights up
         * under the cursor, and answers nothing. `~skill_multi` arms eighteen
         * of these in a row, so the ceiling is now reachable by ordinary
         * content rather than only by a runaway loop.
         */
        assert(player->resume_button_count < TORIRSSERVER_RESUME_BUTTON_MAX);
        player->resume_buttons[player->resume_button_count++] = uid;
        /*
         * Registering the button server-side is only half of it: at rev 230
         * nothing is clickable until the server says so, so the component's
         * events have to be enabled too or the player looks at a live-looking
         * prompt that swallows every click.
         *
         * The slot range covers dynamic children, not just 0. A resume button
         * on a *container* is the multi-choice dialogue: `chatmenu:options` has
         * no rows of its own, and the five the clientscript `cc_create`s carry
         * sub-ids 1..5. Arming 0..0 arms the empty container and none of the
         * rows, which is the same looks-right-does-nothing failure this call
         * exists to prevent. A plain component has no sub-ids, so the wider
         * range costs it nothing.
         *
         * Clear last_slot so a bare RESUME_PAUSEBUTTON / IF_BUTTON cannot
         * inherit memset 0 or a stale inv slot and make ~p_choice* take its
         * last option.
         */
        player->last_slot = -1;
        ToriRSServer_SendIfSetevents(srv->active_player, uid, 0, TORIRSSERVER_RESUME_SUB_MAX, TORIRSSERVER_EVENT_CLICK);
        return 1;
    }

    case SS_OP_P_PAUSEBUTTON:
        /* Waits for client input, not for the clock — so nothing in the tick
         * resumes it. ToriRSServer_ScriptsResumeButton does, on a matching click.
         * Same last_slot clear as if_addresumebutton: the click that unparks
         * must be what sets the row, not whatever was latched earlier. */
        player->last_slot = -1;
        SSVM_Suspend(state, SSVM_PAUSEBUTTON);
        return 1;

    case SS_OP_LAST_COM:
        SSVM_PushInt(state, player->last_com);
        return 1;

    /*
     * `[command,busy]()(boolean)` — `PlayerOps.ts` BUSY:
     * `activePlayer.busy() || activePlayer.loggingOut`, where `Player.busy()` is
     * `this.delayed || this.containsModalInterface()`.
     *
     * `player_can_access` above is already that predicate, negated: same
     * `delayed_until` test and the same main-or-chat modal test (the reference's
     * `containsModalInterface` checks MAIN | CHAT and not the side slot, which is
     * why a script can run with the backpack tab replaced). Sharing it is the
     * point — this opcode is content asking the question the engine asks itself
     * before it hands over a script, and two spellings of it would drift.
     *
     * `loggingOut` has no term here and does not need one: the reference has a
     * window between "asked to log out" and "gone", and this server does not —
     * `p_logout` kills the session and `logout_action` is stored but never read
     * (torirs_server.h). So there is no tick on which a script could observe it.
     *
     * What content uses it for is refusing to start something on top of a
     * dialogue: `if (busy() = true) return;` at the top of a trigger, which is
     * how the reference guards an npc's `[ai_opplayer]` self-heal against firing
     * mid-conversation.
     */
    case SS_OP_BUSY:
        SSVM_PushInt(state, !player_can_access(srv));
        return 1;

    /*
     * `[command,busy2]()(boolean)` — `PlayerOps.ts` BUSY2:
     * `activePlayer.hasInteraction() || activePlayer.hasWaypoints()`.
     *
     * A *different* question from `busy` above, and the difference is the whole
     * point: `busy` is "can a script be handed to this player" (delayed, or a
     * modal is up), while this one is "is this player already committed to
     * something" — a latched target, or a route still being walked.
     *
     * The only caller is auto-retaliation, and it is the reason the command
     * exists at all: `p_opnpc(2)` clears the interaction and the step queue
     * before it latches, so an ungated retaliation queue *takes* a player who
     * is mid-fight with another monster, or half way to the one they clicked,
     * and points them at whatever hit them last. Reading the two fields is the
     * reference's own test, and both terms are needed — a player walking to a
     * target they have already committed to has waypoints and an interaction,
     * one running for a bank door has waypoints and none.
     *
     * `interaction.kind` is `hasInteraction()`: the field is cleared to
     * TORIRSSERVER_INTERACT_NONE by `interaction_clear`, which is what
     * `clearInteraction` is here. `waypoint_index >= 0` is `hasWaypoints()`:
     * -1 is the idle sentinel (see the queue's comment in torirs_server.h).
     */
    case SS_OP_BUSY2:
        SSVM_PushInt(state,
                     player->interaction.kind != TORIRSSERVER_INTERACT_NONE ||
                             player->waypoint_index >= 0
                         ? 1
                         : 0);
        return 1;

    /*
     * QBD-style time stop. This is intentionally not busy()/canAccess(): the
     * latter is the queue gate, while delayed damage and encounter queues must
     * keep running during the stop. World input dispatch and phase-5 pathing
     * enforce the lock; scripts own when it ends.
     */
    case SS_OP_PLAYER_LOCK:
        ToriRSServer_WorldPlayerLock(srv);
        return 1;

    case SS_OP_PLAYER_UNLOCK:
        /* Unlike PLAYER_LOCK, the inverse is deliberately pointer-free. A
         * player-bound softtimer has no protected SSVM entity pointer, but is
         * precisely where an activity must clear a stale time-stop lock after
         * an unrelated teleport. The timer dispatcher has already selected
         * srv->active_player, which is the only object this command touches. */
        ToriRSServer_WorldPlayerUnlock(srv);
        return 1;

    /* ---- combat ---------------------------------------------------- */

    case SS_OP_P_OPNPC:
    {
        int32_t op_num;
        int slot = (int)state->host_tag - 1;
        struct ToriRSServerNpc* npc;
        const struct ToriRSServerNpcInfo* info;
        struct ToriRSServerPlayer* player = srv->active_player;

        if( !SSVM_PopInt(state, &op_num) )
            return 1;
        if( op_num < 1 || op_num > 5 )
        {
            SSVM_Abort(state, "p_opnpc: op %d is not 1..5", (int)op_num);
            return 1;
        }
        if( slot < 0 || slot >= TORIRSSERVER_NPC_MAX || !srv->npcs[slot].active )
        {
            SSVM_Abort(state, "p_opnpc with no active npc");
            return 1;
        }
        npc = &srv->npcs[slot];
        info = ToriRSServer_NpcInfo(npc->type);
        /*
         * LostCity PlayerOps.P_OPNPC: stopAction (clear interaction + walk) then
         * setInteraction — not clearPendingAction's combat_stop. Clearing
         * combat_target here aborted every melee loop the moment content called
         * p_opnpc(2) after a swing.
         */
        if( !info->ops[op_num - 1] )
            return 1;
        /*
         * The 20-minute anti-AFK rule (TORIRSSERVER_AFK_COMBAT_TICKS).
         *
         * Here as well as in `ToriRSServer_CombatEngage` because the two are not
         * one path: a click arrives as OPNPC and engages, while every *script*
         * re-issue — the retaliation queue, the melee label, the ranged loop —
         * arrives here and latches `combat_target` itself. Gating only engage
         * left the queue free to keep a silent player swinging forever.
         *
         * Attack only: a player who has stopped fighting has not stopped
         * talking, and `p_opnpc(1)` on a shopkeeper is not combat.
         */
        if( strcmp(info->ops[op_num - 1], "Attack") == 0 &&
            ToriRSServer_CombatPlayerAfk(player) )
        {
            ToriRSServer_CombatStopPlayer(srv);
            return 1;
        }
        ToriRSServer_WorldInteractionClear(srv);
        ToriRSServer_WorldStepsClear(player);
        ToriRSServer_WorldInteractionSet(srv, TORIRSSERVER_INTERACT_NPC, (int)op_num, slot,
                                      npc->type, npc->x, npc->z, npc->level,
                                      info->size, info->size);
        {
            struct CollisionApproach approach;
            ToriRSServer_SceneNpcApproach(info->size, &approach);
            ToriRSServer_WorldWalkToApproach(srv, npc->x, npc->z, &approach);
        }
        /* Attack keeps the engine face/approach latch; other ops do not. */
        if( strcmp(info->ops[op_num - 1], "Attack") == 0 )
            player->combat_target = slot;
        return 1;
    }

    /*
     * `[command,p_opnpct](component $spell)` — engine.rs2:183. `PlayerOps.ts`
     * P_OPNPCT: `stopAction()` then `setInteraction(SCRIPT, activeNpc, APNPCT,
     * spellId)`.
     *
     * The spell half of `p_opnpc` above, and the same three steps: clear what the
     * player was doing, latch the npc, walk to it. What differs is only which
     * trigger the arrival resolves to — `[apnpct,magic:<spell>]` keyed by the
     * spell rather than `[opnpc<n>]` keyed by the npc — which is why the spell
     * goes on the interaction (see `ToriRSServerInteraction.spell`) instead of being
     * turned into an op number here.
     *
     * No `info->ops` check, unlike `p_opnpc`. A spell is not one of the npc's
     * five right-click options and an npc does not have to advertise anything to
     * be castable at; the reference checks `NumberNotNull` on the spell and
     * nothing about the npc.
     *
     * `combat_target` is deliberately not set. It is the melee approach latch,
     * and a cast is resolved by the ap rung at spell range — latching it would
     * make the caster walk in to touch a target it can already hit.
     */
    case SS_OP_P_OPNPCT:
    {
        int32_t spell;
        int slot = (int)state->host_tag - 1;
        struct ToriRSServerNpc* npc;
        const struct ToriRSServerNpcInfo* info;

        if( !SSVM_PopInt(state, &spell) )
            return 1;
        if( slot < 0 || slot >= TORIRSSERVER_NPC_MAX || !srv->npcs[slot].active )
        {
            SSVM_Abort(state, "p_opnpct with no active npc");
            return 1;
        }
        if( spell <= 0 )
        {
            SSVM_Abort(state, "p_opnpct: %d is not a spell component", (int)spell);
            return 1;
        }
        npc = &srv->npcs[slot];
        info = ToriRSServer_NpcInfo(npc->type);
        ToriRSServer_WorldInteractionClear(srv);
        ToriRSServer_WorldStepsClear(player);
        ToriRSServer_WorldInteractionSet(srv, TORIRSSERVER_INTERACT_NPC, 1, slot, npc->type, npc->x,
                                      npc->z, npc->level, info->size, info->size);
        player->interaction.spell = (int)spell;
        {
            struct CollisionApproach approach;
            ToriRSServer_SceneNpcApproach(info->size, &approach);
            ToriRSServer_WorldWalkToApproach(srv, npc->x, npc->z, &approach);
        }
        return 1;
    }

    /*
     * projanim_pl(coord $from, player_uid $to, spotanim, fromHeight, toHeight,
     * delay, duration, peak, arc) — engine.rs2:70; `projanim_npc` (:72) and
     * `projanim_map` (:74) carry the same nine values with an npc uid or a
     * destination coord in slot two. All three reduce to one
     * `World.mapProjAnim` call; the coordinate form uses target 0.
     *
     * This is the arrow, the spell and the dragon's breath — every projectile in
     * the game. `skill_combat/scripts/projectile.rs2` wraps both, and every
     * ranged and magic script in the reference goes through that wrapper, which
     * is why the two land together: hosting one would leave half the combat
     * scripts casting invisible spells.
     *
     * The destination is the target's tile **now**, read here rather than sent as
     * a coord, because that is what the reference does and what the wire can
     * carry: MAP_PROJANIM has one signed byte per axis, as an offset from the
     * source. The client then re-aims at the live entity every cycle (`target`),
     * so the tile written here only decides the opening arc — which is exactly
     * how a shot at a walking target bends to follow it.
     *
     * `target` is the wire's own encoding of "whom": `slot + 1` for an npc,
     * `-pid - 1` for a player. Two spaces in one signed field, which is why an
     * npc can never be slot -1 and a pid is never negative.
     *
     * The player uid is resolved against the live player pool.  This matters
     * for destructive targeted specials: the selected recipient, projectile
     * homing entity and damage recipient must remain the same player after a
     * delay, not whichever player's phase happens to be active when it resumes.
     */
    case SS_OP_PROJANIM_MAP:
    case SS_OP_PROJANIM_PL:
    case SS_OP_PROJANIM_NPC:
    {
        int32_t values[9];
        int dst_x;
        int dst_z;
        int target;

        for( int i = 8; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }

        if( opcode == SS_OP_PROJANIM_MAP )
        {
            /* A coordinate target does not home on an entity. As in the
             * reference, the destination level is ignored: the projectile is
             * filed and rendered on the source coordinate's plane. */
            dst_x = ToriRSServer_CoordX(values[1]);
            dst_z = ToriRSServer_CoordZ(values[1]);
            target = 0;
        }
        else if( opcode == SS_OP_PROJANIM_NPC )
        {
            int slot = (int)((uint32_t)values[1] & 0xffffu);
            uint16_t generation = (uint16_t)((uint32_t)values[1] >> 16);

            if( slot < 0 || slot >= TORIRSSERVER_NPC_MAX || !srv->npcs[slot].active ||
                generation == 0 || srv->npcs[slot].generation != generation )
            {
                SSVM_Abort(state, "projanim_npc: npc uid %d is not a live npc",
                           (int)values[1]);
                return 1;
            }
            dst_x = srv->npcs[slot].x;
            dst_z = srv->npcs[slot].z;
            target = slot + 1;
        }
        else
        {
            struct ToriRSServerPlayer* target_player = player_by_uid(srv, values[1]);

            if( !target_player )
            {
                SSVM_Abort(state, "projanim_pl: player uid %d is not a live player",
                           (int)values[1]);
                return 1;
            }
            dst_x = target_player->x;
            dst_z = target_player->z;
            target = -target_player->pid - 1;
        }

        /* TORIRSSERVER_PROJ_DEBUG=1: one line per send. A projectile that never
         * leaves and a projectile the client drops look identical from the
         * outside, and this is the line that separates them — it is how the
         * commented-out `projanim_pl` in `skill_combat/scripts/projectile.rs2`
         * was found (0 sends across a whole TzKal-Zuk fight). */
        if( getenv("TORIRSSERVER_PROJ_DEBUG") )
            fprintf(stderr, "projanim: src=%d,%d dst=%d,%d target=%d spot=%d delay=%d dur=%d\n",
                    ToriRSServer_CoordX(values[0]), ToriRSServer_CoordZ(values[0]), dst_x, dst_z,
                    target, (int)values[2], (int)values[5], (int)values[6]);
        ToriRSServer_ZoneProjanim(srv, ToriRSServer_CoordX(values[0]), ToriRSServer_CoordZ(values[0]),
                              ToriRSServer_CoordLevel(values[0]), dst_x, dst_z, target,
                              (int)values[2], (int)values[3], (int)values[4], (int)values[5],
                              (int)values[6], (int)values[7], (int)values[8]);
        return 1;
    }

    /*
     * The hint arrow family -- `hint_npc`, `hint_coord`, `hint_pl`, `hint_stop`.
     *
     * These were in `k_opcode_gap_allowed` with the note "blocked on the
     * client's hint-arrow render lane, and it deletes itself the day that lands".
     * That lane has landed (`app_overlay_build_hint_arrow`), so the rows are
     * gone and these are the implementations they were waiting for.
     *
     * They are also the only mechanism this revision has for two All Settings
     * rows -- 272 "Clue scroll helper - Worldmap marker" and 273 "... World
     * arrows". Neither has a reader in the cache or in the NXT engine; the arrow
     * is the payload and the server's choice of whether to send it is the
     * setting. Content decides both, which is why these are script opcodes
     * rather than an engine feature keyed on a varbit.
     */
    case SS_OP_HINT_NPC:
    {
        int slot = (int)state->host_tag - 1;

        if( slot < 0 )
        {
            SSVM_Abort(state, "hint_npc with no active npc");
            return 1;
        }
        if( srv->active_player )
            ToriRSServer_SendHintArrow(srv->active_player, TORIRSSERVER_HINT_ARROW_NPC, slot, 0, 0);
        return 1;
    }

    case SS_OP_HINT_PL:
    {
        /* The ACTIVE player is both the subject and the recipient here, which
         * is what `hint_pl` means in a single-player-facing script: point me at
         * myself. A script that wants to point one player at another has no
         * spelling for it in this opcode's metadata (zero arguments), so this
         * does not invent one. */
        if( srv->active_player )
            ToriRSServer_SendHintArrow(srv->active_player, TORIRSSERVER_HINT_ARROW_PLAYER,
                                   srv->active_player->pid, 0, 0);
        return 1;
    }

    case SS_OP_HINT_COORD:
    {
        int32_t values[3];

        for( int i = 2; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        /* (coord, height, unused). The coord is a packed server coord; the wire
         * wants absolute x and z, which is what the client converts back. */
        if( srv->active_player )
            ToriRSServer_SendHintArrow(srv->active_player, TORIRSSERVER_HINT_ARROW_COORD,
                                   ToriRSServer_CoordX(values[0]),
                                   ToriRSServer_CoordZ(values[0]), values[1]);
        return 1;
    }

    case SS_OP_HINT_STOP:
    {
        /* 255 is the wire's clear, which `rs_gameproto_exec.c` normalises to
         * type 0 on the way in. */
        if( srv->active_player )
            ToriRSServer_SendHintArrow(srv->active_player, TORIRSSERVER_HINT_ARROW_CLEAR, 0, 0, 0);
        return 1;
    }

    case SS_OP_NPC_DAMAGE:
    {
        int32_t values[2];
        int slot = (int)state->host_tag - 1;

        for( int i = 1; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        if( slot < 0 )
        {
            SSVM_Abort(state, "npc_damage with no active npc");
            return 1;
        }
        ToriRSServer_CombatHitNpc(srv, slot, values[0], values[1]);
        return 1;
    }

    /*
     * npc_hitmark(hitsplat, amount) — the active npc's cosmetic splat.
     *
     * Deliberately NOT routed through `ToriRSServer_CombatHitNpc`: that one
     * subtracts hitpoints and starts a fight. This is for a splat whose health
     * effect the caller applies itself, and for the overhead health bar the
     * hitmark mask is the only carrier of.
     */
    case SS_OP_NPC_HITMARK:
    {
        int32_t values[2];
        int slot = (int)state->host_tag - 1;

        for( int i = 1; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        if( slot < 0 )
        {
            SSVM_Abort(state, "npc_hitmark with no active npc");
            return 1;
        }
        ToriRSServer_CombatHitmarkNpc(srv, slot, values[0], values[1]);
        return 1;
    }

    case SS_OP_NPC_POISON:
    {
        int32_t severity;
        int slot = (int)state->host_tag - 1;

        if( !SSVM_PopInt(state, &severity) )
            return 1;
        if( slot < 0 )
        {
            SSVM_Abort(state, "npc_poison with no active npc");
            return 1;
        }
        ToriRSServer_CombatPoisonNpc(srv, slot, srv->active_player, (int)severity);
        return 1;
    }

    case SS_OP_DAMAGE:
    {
        int32_t values[3];
        struct ToriRSServerPlayer* target_player;
        struct ToriRSServerPlayer* saved_player;

        for( int i = 2; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        target_player = player_by_uid(srv, values[0]);
        if( !target_player )
        {
            SSVM_Abort(state, "damage: player uid %d is not a live player",
                       (int)values[0]);
            return 1;
        }
        saved_player = srv->active_player;
        /* A script damaging its own player is the shape every self-inflicted
         * hit in the game takes — an overload's five ticks of 10, a dwarven
         * rock cake, a poison karambwan. The Nightmare Zone's absorption pool
         * must not soak those, and the damage funnel cannot tell them from a
         * swing on its own. See `hit_self_inflicted`. */
        if( target_player == saved_player )
            target_player->hit_self_inflicted = 1;
        ToriRSServer_WorldSetActive(srv, target_player);
        /*
         * `saved_player` is the ATTACKER, and it is the only place the attacker
         * is still on the stack: the call below runs with the VICTIM active, by
         * design, so the damage funnel cannot name a dealer on its own.
         *
         * Naming it here is the whole of setting 5 for player-versus-player --
         * `hitsplat_damage_other` (the tinted wrapper) instead of
         * `hitsplat_damage_me`. A script damaging its own player passes itself,
         * which is also right: an overload's self-hit is damage you dealt.
         */
        ToriRSServer_CombatHitPlayerFrom(
            srv, values[1], values[2],
            saved_player ? (int)(saved_player - &srv->players[0]) : -1);
        ToriRSServer_WorldSetActive(srv, saved_player);
        return 1;
    }

    /*
     * p_overhit(uid, amount, type, lethal) — a hit whose verdict was settled
     * when it was rolled.
     *
     * Ordinary `damage` subtracts from whatever the target has NOW, which is
     * what makes tick-eating work: heal between the launch and the landing and
     * the same number no longer kills. A few attacks in this game are not
     * supposed to be survivable that way — the Theatre's Maiden and the
     * Inferno's Zuk both "over-hit ... beyond their current health when damage
     * is calculated" — and this is that.
     *
     * The engine does not decide `lethal`. It cannot: the question is about
     * the target's hitpoints at a tick that has already passed, and only the
     * caller was there. Content answers it at launch and this honours it, the
     * same mechanism/policy split `npc_freeze` is written around.
     */
    case SS_OP_P_OVERHIT:
    {
        int32_t values[4];
        struct ToriRSServerPlayer* target_player;
        struct ToriRSServerPlayer* saved_player;

        for( int i = 3; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        target_player = player_by_uid(srv, values[0]);
        if( !target_player )
        {
            SSVM_Abort(state, "p_overhit: player uid %d is not a live player",
                       (int)values[0]);
            return 1;
        }
        saved_player = srv->active_player;
        ToriRSServer_WorldSetActive(srv, target_player);
        if( values[3] )
        {
            /*
             * Lethal when rolled. Send the hit the target's remaining health
             * so the splat reads as the killing blow rather than as a number
             * larger than the bar, then let the ordinary path run: it is the
             * one place hitpoints reach zero and the only one that starts the
             * death sequence.
             */
            ToriRSServer_CombatHitPlayerFrom(
                srv, values[2], target_player->hitpoints,
                saved_player ? (int)(saved_player - &srv->players[0]) : -1);
        }
        else
        {
            /* The attacker, for the same reason as `damage` above: this runs
             * with the victim active and `saved_player` is the last frame that
             * still knows who swung. */
            ToriRSServer_CombatHitPlayerFrom(
                srv, values[2], values[1],
                saved_player ? (int)(saved_player - &srv->players[0]) : -1);
        }
        ToriRSServer_WorldSetActive(srv, saved_player);
        return 1;
    }

    case SS_OP_HITMARK:
    {
        int32_t values[3];
        struct ToriRSServerPlayer* target_player;
        struct ToriRSServerPlayer* saved_player;

        for( int i = 2; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        target_player = player_by_uid(srv, values[0]);
        if( !target_player )
        {
            SSVM_Abort(state, "hitmark: player uid %d is not a live player",
                       (int)values[0]);
            return 1;
        }
        saved_player = srv->active_player;
        ToriRSServer_WorldSetActive(srv, target_player);
        ToriRSServer_CombatHitmarkPlayer(srv, values[1], values[2]);
        ToriRSServer_WorldSetActive(srv, saved_player);
        return 1;
    }

    /*
     * healenergy(amount) — run energy, in the hundredths-of-a-percent unit the
     * whole energy system is written in, so the reference's `healenergy(10000)`
     * is a full bar.
     *
     * It restored *hitpoints* here, which is a different resource and, worse, a
     * silent one: `[queue,player_death]` calls `healenergy` right after
     * `stat_heal(hitpoints, 99, 100)`, so the wrong op was covered by the right
     * one on the only path that runs it. Content asking for energy got health,
     * and nothing anywhere said so.
     */
    case SS_OP_HEALENERGY:
    {
        int32_t amount;

        if( !SSVM_PopInt(state, &amount) )
            return 1;
        player->run_energy += amount;
        if( player->run_energy > TORIRSSERVER_RUN_ENERGY_MAX )
            player->run_energy = TORIRSSERVER_RUN_ENERGY_MAX;
        if( player->run_energy < 0 )
            player->run_energy = 0;
        /* Force the next run_energy_flush to re-send — content that restores a
         * full bar mid-session must not wait for a coincidental drift. */
        player->run_energy_sent = -1;
        return 1;
    }

    /*
     * runenergy() is the script-visible percentage (0..100), not the mock's
     * hundredths-of-a-percent storage.  It is needed by effects such as Bull
     * Ant's Unburden to reject a full bar before committing their scroll.
     */
    case SS_OP_RUNENERGY:
        SSVM_PushInt(state, player->run_energy / 100);
        return 1;

    /*
     * Camera family — LostCity PlayerOps CAM_* (coord → scene-local via the
     * current rebuild base). Wire opcodes measured from RSProt osrs-230
     * GameServerProtId: CAM_LOOKAT=30, CAM_RESET=65, CAM_MOVETO=67, CAM_SHAKE=107.
     */
    case SS_OP_CAM_LOOKAT:
    case SS_OP_CAM_MOVETO:
    {
        int32_t values[4];
        int local_x;
        int local_z;

        for( int i = 3; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        /* The world coordinate, unconverted: which of the two the wire wants
         * is the encoder's question now, because the revisions disagree. */
        local_x = coord_x(values[0]);
        local_z = coord_z(values[0]);
        if( opcode == SS_OP_CAM_MOVETO )
            ToriRSServer_SendCamMoveto(player, local_x, local_z, (int)values[1],
                                    (int)values[2], (int)values[3]);
        else
            ToriRSServer_SendCamLookat(player, local_x, local_z, (int)values[1],
                                    (int)values[2], (int)values[3]);
        return 1;
    }

    case SS_OP_CAM_RESET:
        ToriRSServer_SendCamReset(player);
        return 1;

    case SS_OP_CAM_SHAKE:
    {
        int32_t values[4];

        for( int i = 3; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        ToriRSServer_SendCamShake(player, (int)values[0], (int)values[1],
                               (int)values[2], (int)values[3]);
        return 1;
    }

    case SS_OP_UID:
        SSVM_PushInt(state, player ? player->pid + 1 : 0);
        return 1;

    case SS_OP_NPC_FINDHERO:
        /* Combat currently retains the hitter as the active world player. */
        SSVM_PushInt(state, srv->active_player ? srv->active_player->pid + 1 : 0);
        return 1;

    case SS_OP_NPC_ATTACKRANGE:
    {
        /* The active npc's own reach, off its record — `param=attackrange,N` in
         * a .npc block, defaulting to the melee 1. It answered a C constant
         * before, so a ranged npc's script asked how far it could shoot and was
         * told "one tile" no matter what its config said. */
        struct ToriRSServerNpc* npc = active_npc(state);

        if( !npc )
        {
            SSVM_Abort(state, "npc_attackrange with no active npc");
            return 1;
        }
        SSVM_PushInt(state, npc->def ? npc->def->attackrange
                                     : ToriRSServer_ContentNpcDefault()->attackrange);
        return 1;
    }

    /* ---- waiting -------------------------------------------------- */

    case SS_OP_P_DELAY:
    {
        int32_t ticks;

        if( !SSVM_PopInt(state, &ticks) )
            return 1;
        /* tick + 1 + n, matching the reference: p_delay(0) still costs the rest
         * of this tick, so a script cannot delay by nothing and keep running. */
        player->delayed_until = srv->tick + 1 + ticks;
        SSVM_Suspend(state, SSVM_SUSPENDED);
        return 1;
    }

    /*
     * `p_aprange(int)` — PlayerOps.ts:352, `apRange = n; apRangeCalled = true`.
     *
     * The script is saying "I am not finished; call me again when I am within
     * n". Both halves matter and they fail differently:
     *
     *  - the range is what the *next* tick's at-range gate tests, so a ranged
     *    attack resolves at the weapon's reach instead of walking into melee;
     *  - `ap_range_called` is what stops `interaction_try` from treating this
     *    run as a completed interaction. Without it the first `p_aprange`
     *    clears the target and the attack never happens — which is the shape
     *    the whole of `skill_combat` is built on, so "combat does nothing" is
     *    the symptom rather than an error.
     *
     * `ap_tried` is re-armed for the same reason: it exists to stop the ap
     * lookup running on every tick of a long walk, and a script that asked to
     * be called again is the one case where it must.
     */
    case SS_OP_P_APRANGE:
    {
        int32_t range;

        if( !SSVM_PopInt(state, &range) )
            return 1;
        player->interaction.ap_range = (int)range;
        player->interaction.ap_range_called = 1;
        player->interaction.ap_tried = 0;
        return 1;
    }

    /*
     * `p_stun(ticks)` — OldSchool's stun, as a first-class player state.
     *
     * There was no primitive for this, and the workaround content reached for
     * (`%action_delay`) is not one: it is an ordinary varp that only gates the
     * scripts which choose to read it, so a "stunned" player kept walking and
     * kept attacking. Zulrah's tail is the case that made that visible — the
     * swing is supposed to root you for five ticks, and rooting is most of
     * what makes the crimson phase dangerous.
     *
     * What a stun stops is movement and world interaction. It deliberately
     * does NOT stop the inventory, the equipment or the prayer book: eating
     * and flicking a prayer through a stun is how the mechanic is survived in
     * OldSchool, and a stun that blocked those would be strictly harsher than
     * the thing being modelled. See `stun_ticks` in torirs_server.h for how that
     * splits from `delayed_until` and `action_locked`.
     *
     * The longer stun wins, matching `npc_freeze` — a second swing landing on
     * an already-stunned player must not shorten the first one. Which leaves
     * no way to end one early, so `ticks <= 0` is the cure: it clears outright
     * rather than being a no-op that loses to whatever is already running.
     * Death and a Freedom-style effect both need that, and a second opcode for
     * it would be a worse answer than a documented argument.
     */
    case SS_OP_P_STUN:
    {
        int32_t ticks;

        if( !SSVM_PopInt(state, &ticks) )
            return 1;
        if( ticks <= 0 )
            player->stun_ticks = 0;
        else if( ticks > player->stun_ticks )
            player->stun_ticks = (int)ticks;
        /*
         * Applied here rather than left to the phase gate, because a stun that
         * only took effect next tick would let the route this tick already
         * planned carry the player out of the swing that stunned them.
         */
        if( player->stun_ticks > 0 )
            ToriRSServer_WorldStunInterrupt(player);
        return 1;
    }

    case SS_OP_P_STUNNED:
        SSVM_PushInt(state, player->stun_ticks);
        return 1;

    /*
     * `map_canstep(coord, dx, dz)` — can a size-1 actor standing on `coord`
     * take one step by (dx, dz)?
     *
     * `map_blocked` answers "is that tile blocked", which is not the same
     * question and is the wrong one for anything that walks: it cannot see a
     * wall between two open tiles, and it cannot see the corner rule that
     * stops a diagonal from cutting past one. Content that needed to know
     * where a shove or a slide would come to rest had to approximate with
     * `map_blocked` and got pushed through walls.
     *
     * TERRAIN ONLY — npcs and players are not collision here. That is
     * deliberate and is what a knockback wants: being shoved into somebody
     * does not stop the shove. Entity occupancy is a separate question with
     * separate opcodes.
     */
    case SS_OP_MAP_CANSTEP:
    {
        int32_t coord;
        int32_t dx;
        int32_t dz;

        if( !SSVM_PopInt(state, &dz) )
            return 1;
        if( !SSVM_PopInt(state, &dx) )
            return 1;
        if( !SSVM_PopInt(state, &coord) )
            return 1;
        SSVM_PushInt(state,
                     ToriRSServer_SceneCanTravel(coord_level(coord), coord_x(coord),
                                              coord_z(coord), (int)dx, (int)dz, 1, 0)
                         ? 1
                         : 0);
        return 1;
    }

    case SS_OP_P_ARRIVEDELAY:
        /* Only waits when the player actually moved this tick; a stationary
         * player runs straight through. */
        if( player->move_count > 0 )
        {
            player->delayed_until = srv->tick + 1;
            SSVM_Suspend(state, SSVM_SUSPENDED);
        }
        return 1;

    case SS_OP_NPC_FREEZE:
    {
        int32_t ticks;
        struct ToriRSServerNpc* npc = active_npc(state);

        if( !SSVM_PopInt(state, &ticks) )
            return 1;
        if( !npc )
        {
            SSVM_Abort(state, "npc_freeze with no active npc");
            return 1;
        }
        /*
         * The longer freeze wins rather than the newer one. Re-freezing a
         * target that is already frozen for longer must not shorten it, which
         * is what a plain assignment would do — and the case is not exotic: it
         * is a Barrage landing on an npc a Blitz already froze.
         */
        if( ticks > npc->frozen_ticks )
            npc->frozen_ticks = (int)ticks;
        return 1;
    }

    case SS_OP_NPC_FROZEN:
    {
        struct ToriRSServerNpc* npc = active_npc(state);

        if( !npc )
        {
            SSVM_Abort(state, "npc_frozen with no active npc");
            return 1;
        }
        SSVM_PushInt(state, npc->frozen_ticks);
        return 1;
    }

    /*
     * WARNING — `npc_delay` is not "make that npc wait". It SUSPENDS THE
     * CALLING SCRIPT.
     *
     * The name reads like a setter on the active npc, and the body below is
     * two lines of which only the first is about the npc. The second one ends
     * the current script's turn: everything after the `npc_delay(...)` call
     * resumes `ticks` later, in a fresh dispatch, or not at all.
     *
     * That is correct and harmless in an npc's own context — `[ai_applayer2]`,
     * `[ai_timer]`, `[ai_queue*]` — where the script IS the npc's turn and
     * suspending it is the whole point.
     *
     * It is a bug in the PLAYER's context. `~player_hit_npc_prepare` and every
     * encounter rung under it run on the player's frame with an active npc set
     * (that is how they read `npc_var_get`/`npc_stat` at all), so `npc_delay`
     * there compiles, finds an npc, and suspends the player's hit mid-way:
     * the damage is never returned, the caller never queues the splat, and the
     * hit silently evaporates. Nothing reports it — the script simply stops.
     *
     * The rule: a script may only call `npc_delay` if being suspended for
     * `ticks` is what that script wants. To stall an npc from somewhere else,
     * write a clock into an `npc_var_*` slot and let the npc turn it into a
     * delay in its own context. `bosses/boss_tormented_demons` does exactly
     * that with `^td_var_stun_until` for the prayer-swap stall, which is
     * written from the player's hit script and consumed in `~td_attack`.
     *
     * Same shape, same warning: `p_delay` (SS_OP_P_DELAY) suspends too.
     */
    case SS_OP_NPC_DELAY:
    {
        int32_t ticks;
        struct ToriRSServerNpc* npc = active_npc(state);

        if( !SSVM_PopInt(state, &ticks) )
            return 1;
        if( !npc )
        {
            SSVM_Abort(state, "npc_delay with no active npc");
            return 1;
        }
        npc->delayed_until = srv->tick + 1 + ticks;
        /* Suspends the CALLER, not just the npc. See the warning above. */
        SSVM_Suspend(state, SSVM_NPC_SUSPENDED);
        return 1;
    }

    /*
     * `npc_arrivedelay` — LostCity `NpcOps.ts:557`, ported verbatim:
     *
     *     if (activeNpc.lastMovement < World.currentTick - 1) return;
     *     delayed = true;
     *     delayedUntil = lastMovement === currentTick - 1
     *         ? currentTick + 1 : currentTick + 2;
     *     execution = ScriptState.NPC_SUSPENDED;
     *
     * "Let the step I am mid-way through finish before I act." `last_movement`
     * is the moving tick **plus one** (see its comment on `ToriRSServerNpc`), so
     * `< tick - 1` means "has not moved for two ticks" and the script runs on
     * without waiting at all — an npc standing still must not be delayed, or
     * `[ai_queue3]` death sequences would stall a tick every time.
     *
     * Suspension is `npc_delay`'s machinery unchanged: phase 4 offers the
     * parked script a resume once `srv->tick >= delayed_until`, and the npc is
     * invalid — no timers, queues, hunts or modes — until then.
     */
    case SS_OP_NPC_ARRIVEDELAY:
    {
        struct ToriRSServerNpc* npc = active_npc(state);

        if( !npc )
        {
            SSVM_Abort(state, "npc_arrivedelay with no active npc");
            return 1;
        }
        if( npc->last_movement < srv->tick - 1 )
            return 1;
        npc->delayed_until =
            npc->last_movement == srv->tick - 1 ? srv->tick + 1 : srv->tick + 2;
        SSVM_Suspend(state, SSVM_NPC_SUSPENDED);
        return 1;
    }

    case SS_OP_WORLD_DELAY:
        /* The argument stays on the stack; the parking code takes it. */
        SSVM_Suspend(state, SSVM_WORLD_SUSPENDED);
        return 1;

    /* ---- queues and timers ---------------------------------------- */

    /*
     * The four queue commands are one body: they differ only in the *kind* they
     * store, and the kind is read by the drain and by `close_modal`.
     *
     *   queue       (script, delay, arg)                   NORMAL
     *   strongqueue (script, delay, arg)                   STRONG — closes modals
     *   weakqueue   (script, delay, arg)                   WEAK   — dies with them
     *   longqueue   (script, delay, arg, logout_action)    LONG   — survives logout
     *
     * The vararg forms (`queue*` and friends) are a separate declaration the
     * compiler refuses outright (`ssc_compile.c`: "it packs a type string the
     * compiler does not build"), so both halves agree that they do not exist.
     */
    case SS_OP_QUEUE:
    case SS_OP_STRONGQUEUE:
    case SS_OP_WEAKQUEUE:
    case SS_OP_LONGQUEUE:
    {
        int32_t values[4] = { 0, 0, 0, 0 };
        int argc = opcode == SS_OP_LONGQUEUE ? 4 : 3;
        int kind = opcode == SS_OP_STRONGQUEUE ? TORIRSSERVER_QUEUE_STRONG
                   : opcode == SS_OP_WEAKQUEUE ? TORIRSSERVER_QUEUE_WEAK
                   : opcode == SS_OP_LONGQUEUE ? TORIRSSERVER_QUEUE_LONG
                                               : TORIRSSERVER_QUEUE_NORMAL;

        for( int i = argc - 1; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        for( int i = 0; i < TORIRSSERVER_QUEUE_MAX; i++ )
        {
            if( player->queue[i].active )
                continue;
            player->queue[i].active = 1;
            player->queue[i].script_id = values[0];
            /* +1 so delay 0 means "next tick", not "this one". The drain
             * pre-decrements, so this is the reference's raw store read the
             * other way round — same answer for every delay. */
            player->queue[i].delay = values[1] + 1;
            player->queue[i].args[0] = values[2];
            player->queue[i].argc = 1;
            player->queue[i].kind = kind;
            player->queue[i].logout_action = values[3];
            return 1;
        }
        SSVM_Abort(state, "the player's queue is full");
        return 1;
    }

    /*
     * `queue*(script, delay)(args…)` — QUEUEVARARG and its three siblings.
     *
     * A separate opcode from `queue`, not a modifier on it: `queue` states
     * exactly one argument (`[command,queue](queue $queue, int $delay, int $arg)`
     * in the reference's engine.rs2) and the vararg form states a list.
     *
     * It was unhosted, and the content had been written as though `queue` took
     * two — `queue(combat_damage_player, $delay, npc_uid, $damage)` at ten sites
     * across the Inferno, the Gauntlet, Elvarg and Melzar. Four values pushed
     * into a three-value pop does not fail; it shifts, so the SCRIPT ID came out
     * of the delay slot and every one of those hits queued a garbage id. That is
     * why TzKal-Zuk's projectile arrived and did nothing.
     *
     * The vararg tail arrives as a type-string plus that many values, the same
     * shape `runclientscript*` uses.
     */
    case SS_OP_QUEUEVARARG:
    case SS_OP_STRONGQUEUEVARARG:
    case SS_OP_WEAKQUEUEVARARG:
    case SS_OP_LONGQUEUEVARARG:
    {
        int32_t vals[TORIRSSERVER_QUEUE_ARG_MAX];
        int32_t script_id = 0;
        int32_t delay = 0;
        int32_t logout_action = 0;
        const char* types = NULL;
        int n = 0;
        int kind = opcode == SS_OP_STRONGQUEUEVARARG ? TORIRSSERVER_QUEUE_STRONG
                   : opcode == SS_OP_WEAKQUEUEVARARG ? TORIRSSERVER_QUEUE_WEAK
                   : opcode == SS_OP_LONGQUEUEVARARG ? TORIRSSERVER_QUEUE_LONG
                                                     : TORIRSSERVER_QUEUE_NORMAL;

        if( !SSVM_PopStr(state, &types) )
            return 1;
        n = types ? (int)strlen(types) : 0;
        if( n > TORIRSSERVER_QUEUE_ARG_MAX )
        {
            SSVM_Abort(state, "queue* states %d argument(s); the queue carries %d", n,
                       TORIRSSERVER_QUEUE_ARG_MAX);
            return 1;
        }
        for( int i = n - 1; i >= 0; i-- )
        {
            if( types[i] != 'i' )
            {
                SSVM_Abort(state, "queue* argument %d is '%c'; only int is carried", i,
                           types[i]);
                return 1;
            }
            if( !SSVM_PopInt(state, &vals[i]) )
                return 1;
        }
        if( opcode == SS_OP_LONGQUEUEVARARG && !SSVM_PopInt(state, &logout_action) )
            return 1;
        if( !SSVM_PopInt(state, &delay) )
            return 1;
        if( !SSVM_PopInt(state, &script_id) )
            return 1;

        for( int i = 0; i < TORIRSSERVER_QUEUE_MAX; i++ )
        {
            if( player->queue[i].active )
                continue;
            player->queue[i].active = 1;
            player->queue[i].script_id = script_id;
            player->queue[i].delay = delay + 1;
            for( int a = 0; a < n; a++ )
                player->queue[i].args[a] = vals[a];
            player->queue[i].argc = n;
            player->queue[i].kind = kind;
            player->queue[i].logout_action = logout_action;
            return 1;
        }
        SSVM_Abort(state, "the player's queue is full");
        return 1;
    }

    /*
     * `clearqueue` unlinks every copy of one script from the primary *and* the
     * weak queue — `unlinkQueuedScript`'s default branch walks both.
     */
    case SS_OP_CLEARQUEUE:
    {
        int32_t script_id;

        if( !SSVM_PopInt(state, &script_id) )
            return 1;
        for( int i = 0; i < TORIRSSERVER_QUEUE_MAX; i++ )
        {
            if( player->queue[i].active && player->queue[i].script_id == script_id )
                player->queue[i].active = 0;
        }
        return 1;
    }

    /* `getqueue` counts and does not clear. Two copies of one script are two
     * entries, which is the whole reason content asks. */
    case SS_OP_GETQUEUE:
    {
        int32_t script_id;
        int count = 0;

        if( !SSVM_PopInt(state, &script_id) )
            return 1;
        for( int i = 0; i < TORIRSSERVER_QUEUE_MAX; i++ )
        {
            if( player->queue[i].active && player->queue[i].script_id == script_id )
                count++;
        }
        SSVM_PushInt(state, count);
        return 1;
    }

    case SS_OP_SETTIMER:
    case SS_OP_SOFTTIMER:
    {
        int32_t values[2];

        for( int i = 1; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        /* A timer is keyed by its script: setting the same one twice re-arms it
         * rather than stacking a second copy. So look for it first, and only
         * take a free slot if it is not already running. */
        {
            struct ToriRSServerTimer* slot = NULL;

            for( int i = 0; i < TORIRSSERVER_TIMER_MAX; i++ )
            {
                if( player->timers[i].active && player->timers[i].script_id == values[0] )
                {
                    slot = &player->timers[i];
                    break;
                }
            }
            for( int i = 0; !slot && i < TORIRSSERVER_TIMER_MAX; i++ )
            {
                if( !player->timers[i].active )
                    slot = &player->timers[i];
            }
            if( !slot )
            {
                /* Name the eight occupants, not just the refusal.
                 *
                 * "no free timer slot" alone says a pool is full and nothing
                 * about which timers filled it, and the abort lands mid-way
                 * through whatever content was doing — Ancient Curses' first
                 * activation set its mask, aborted here, and never reached its
                 * own message or animation, which reads as "the button did
                 * nothing" rather than as a full table. The occupant list is
                 * what turns that into a one-line diagnosis. */
                char full[512];
                int at = 0;

                for( int i = 0; i < TORIRSSERVER_TIMER_MAX; i++ )
                {
                    const struct SSVM_Script* held =
                        SSVM_ProviderGet(srv->scripts, player->timers[i].script_id);

                    at += snprintf(
                        full + at, sizeof(full) - (size_t)at, "%s%s(every %d, armed %d)",
                        i ? ", " : "", held && held->name ? held->name : "?",
                        player->timers[i].interval, player->timers[i].clock);
                    if( at >= (int)sizeof(full) )
                        break;
                }
                fprintf(stderr, "torirsserver: timer table full: %s\n", full);
                SSVM_Abort(state, "no free timer slot");
                return 1;
            }
            slot->active = 1;
            slot->script_id = values[0];
            slot->interval = values[1];
            /* The world tick, not zero. `Player.setTimer` stores
             * `World.currentTick` and fires on `currentTick >= clock + interval`
             * — and `gettimer` returns this number, so a countdown here would
             * make that opcode unimplementable rather than merely different. */
            slot->clock = srv->tick;
            slot->type = opcode == SS_OP_SOFTTIMER ? TORIRSSERVER_TIMER_SOFT : TORIRSSERVER_TIMER_NORMAL;
        }
        return 1;
    }

    /*
     * `cleartimer` and `clearsofttimer` are the same operation on the same
     * table: the reference's `clearTimer(timerId)` is `timers.delete(timerId)`
     * with no type in it at all, and a timer is keyed by its script, so the two
     * commands can never name the same one.
     *
     * The `active` test is new. Without it the loop matched zeroed slots, whose
     * `script_id` is 0 — a no-op in practice, but it made the code say something
     * it did not mean.
     */
    case SS_OP_CLEARTIMER:
    case SS_OP_CLEARSOFTTIMER:
    {
        int32_t script_id;

        if( !SSVM_PopInt(state, &script_id) )
            return 1;
        for( int i = 0; i < TORIRSSERVER_TIMER_MAX; i++ )
        {
            if( player->timers[i].active && player->timers[i].script_id == script_id )
                player->timers[i].active = 0;
        }
        return 1;
    }

    /* The absolute tick the timer was last armed or fired at, or -1. */
    case SS_OP_GETTIMER:
    {
        int32_t script_id;
        int32_t answer = -1;

        if( !SSVM_PopInt(state, &script_id) )
            return 1;
        for( int i = 0; i < TORIRSSERVER_TIMER_MAX; i++ )
        {
            if( player->timers[i].active && player->timers[i].script_id == script_id )
                answer = player->timers[i].clock;
        }
        SSVM_PushInt(state, answer);
        return 1;
    }

    case SS_OP_MAP_CLOCK:
        SSVM_PushInt(state, srv->tick);
        return 1;

    /*
     * Wall-clock minutes since the Unix epoch. LostCity NumberOps.ts:
     * `Math.floor(currentMs / 60000)`. Farming crop growth (and Miscellania)
     * persist deadlines across logout; softtimer alone is only while online.
     */
    case SS_OP_DATE_MINUTES:
    {
        struct timespec ts;
        long long ms;

        if( clock_gettime(CLOCK_REALTIME, &ts) != 0 )
            ts.tv_sec = 0, ts.tv_nsec = 0;
        ms = (long long)ts.tv_sec * 1000LL + (long long)ts.tv_nsec / 1000000LL;
        SSVM_PushInt(state, (int)(ms / 60000LL));
        return 1;
    }

    /*
     * Wall-clock days since the Unix epoch — `date_minutes`'s own comment
     * gives the reference formula for minutes (`Math.floor(currentMs /
     * 60000)`); this is the same shape one unit coarser
     * (`Math.floor(currentMs / 86400000)`), not a verified read of
     * NumberOps.ts's actual `runeday` epoch, which is not vendored in this
     * tree — stated rather than assumed, the same way abyssal_tentacle's gap
     * is stated in docs/ITEM_CHARGES_PLAN.md rather than guessed at.
     * `%current_runeday` (varbit 9535, docs/collection_log_server_reqs.md)
     * is the client's own day counter and is a *different* thing this does
     * not write — a display value, not this opcode's source of truth.
     *
     * What matters for a daily-reset charge count (docs/ITEM_CHARGES_PLAN.md
     * §2c) is only that the value is monotonic and advances exactly once
     * every 24 hours in one consistent zone, which `floor(unix_seconds /
     * 86400)` in UTC already is — content compares `%last_reset_day !=
     * date_runeday()`, not the number's absolute magnitude.
     */
    case SS_OP_DATE_RUNEDAY:
    {
        struct timespec ts;

        if( clock_gettime(CLOCK_REALTIME, &ts) != 0 )
            ts.tv_sec = 0, ts.tv_nsec = 0;
        SSVM_PushInt(state, (int)(ts.tv_sec / 86400LL));
        return 1;
    }

    /*
     * `map_members` gates the members-only branches in LostCity's drop tables
     * and, at this era, the whole of Fletching (every one of its `[label,…]`
     * entry points opens on this check). `srv->members_world` defaults to 1
     * (members) at construction — content ported from the reference expects
     * a members world — with `TORIRSSERVER_MEMBERS_WORLD=0` available to force a
     * free world for testing.
     */
    case SS_OP_MAP_MEMBERS:
        SSVM_PushInt(state, srv->members_world != 0);
        return 1;

    /* ---- map instances -------------------------------------------- */

    /*
     * The six core map-instance commands plus their owner/shared-flag metadata.
     * What each one is for is argued in gen_opcode_meta.py beside its declaration
     * and in torirs_server_mapinstance.h; this is only the host side.
     *
     * They are thin on purpose. Every one of them is "pop the arguments, call the
     * registry, push what it said" — the registry owns the pool, the rotation
     * arithmetic and the descriptor window, and the scene owns the copy. What is
     * *not* here is any policy: no opcode decides which zones a house has, where
     * the player lands, or when the instance ends. That was the condition for
     * these existing at all (PORTING_GUIDE §2.4): the engine gets the mechanism,
     * content keeps the decisions.
     */
    case SS_OP_MAP_INSTANCE_ALLOC:
    {
        int32_t zone_w;
        int32_t zone_h;
        int handle;

        if( !SSVM_PopInt(state, &zone_h) || !SSVM_PopInt(state, &zone_w) )
            return 1;
        handle = ToriRSServer_MapInstanceAlloc(ToriRSServer_WorldCacheDir(), zone_w, zone_h);
        if( handle && srv->active_player )
            ToriRSServer_MapInstanceSetOwner(handle, srv->active_player->pid + 1);
        SSVM_PushInt(state, handle);
        return 1;
    }

    case SS_OP_MAP_INSTANCE_OWNER:
    {
        int32_t handle;

        if( !SSVM_PopInt(state, &handle) )
            return 1;
        SSVM_PushInt(state, ToriRSServer_MapInstanceOwner(handle));
        return 1;
    }

    case SS_OP_MAP_INSTANCE_FIND_OWNER:
    {
        int32_t owner_uid;
        int32_t required_flags;

        if( !SSVM_PopInt(state, &required_flags) ||
            !SSVM_PopInt(state, &owner_uid) )
            return 1;
        SSVM_PushInt(
            state, ToriRSServer_MapInstanceFindOwner(owner_uid, required_flags));
        return 1;
    }

    /*
     * `map_instance_findflag(int $required_flags)(int)` — the join side.
     *
     * `map_instance_find_owner` beside it needs the owner's uid, which is
     * precisely what a player trying to enter somebody else's instance does
     * not have. Without this every Theatre of Blood entrant allocated a
     * private copy of the room, so a five-man party was five solo raids and
     * every mechanic about a second player — Bloat's fly spread, its near-side
     * line of sight past a pillar — was unreachable code.
     */
    case SS_OP_MAP_INSTANCE_FINDFLAG:
    {
        int32_t required_flags;

        if( !SSVM_PopInt(state, &required_flags) )
            return 1;
        SSVM_PushInt(state, ToriRSServer_MapInstanceFindFlagged(required_flags));
        return 1;
    }

    case SS_OP_MAP_INSTANCE_SETLINGER:
    {
        int32_t handle;
        int32_t ticks;

        if( !SSVM_PopInt(state, &ticks) || !SSVM_PopInt(state, &handle) )
            return 1;
        ToriRSServer_MapInstanceSetLinger(handle, ticks);
        return 1;
    }

    case SS_OP_MAP_INSTANCE_LINGER:
    {
        int32_t handle;

        if( !SSVM_PopInt(state, &handle) )
            return 1;
        SSVM_PushInt(state, ToriRSServer_MapInstanceLinger(handle));
        return 1;
    }

    case SS_OP_MAP_INSTANCE_SETLINGERGROUP:
    {
        int32_t handle;
        int32_t group;

        if( !SSVM_PopInt(state, &group) || !SSVM_PopInt(state, &handle) )
            return 1;
        ToriRSServer_MapInstanceSetLingerGroup(handle, group);
        return 1;
    }

    case SS_OP_MAP_INSTANCE_PLAYERCOUNT:
    {
        int32_t handle;
        int count = 0;

        if( !SSVM_PopInt(state, &handle) )
            return 1;
        for( int i = 0; i < srv->player_count; i++ )
        {
            const struct ToriRSServerPlayer* occupant = &srv->players[i];

            if( occupant->active &&
                ToriRSServer_MapInstanceFind(occupant->x, occupant->z) == handle )
                count++;
        }
        SSVM_PushInt(state, count);
        return 1;
    }

    case SS_OP_MAP_INSTANCE_FLAG_GET:
    {
        int32_t handle;
        int32_t mask;

        if( !SSVM_PopInt(state, &mask) || !SSVM_PopInt(state, &handle) )
            return 1;
        SSVM_PushInt(state, ToriRSServer_MapInstanceFlagGet(handle, mask));
        return 1;
    }

    case SS_OP_MAP_INSTANCE_FLAG_SET:
    {
        int32_t handle;
        int32_t mask;
        int32_t enabled;

        if( !SSVM_PopInt(state, &enabled) || !SSVM_PopInt(state, &mask) ||
            !SSVM_PopInt(state, &handle) )
            return 1;
        SSVM_PushInt(state, ToriRSServer_MapInstanceFlagSet(handle, mask, enabled));
        return 1;
    }

    case SS_OP_MAP_INSTANCE_VAR_GET:
    {
        int32_t handle;
        int32_t slot;

        if( !SSVM_PopInt(state, &slot) || !SSVM_PopInt(state, &handle) )
            return 1;
        SSVM_PushInt(state, ToriRSServer_MapInstanceVarGet(handle, slot));
        return 1;
    }

    case SS_OP_MAP_INSTANCE_VAR_SET:
    {
        int32_t handle;
        int32_t slot;
        int32_t value;

        if( !SSVM_PopInt(state, &value) || !SSVM_PopInt(state, &slot) ||
            !SSVM_PopInt(state, &handle) )
            return 1;
        SSVM_PushInt(state, ToriRSServer_MapInstanceVarSet(handle, slot, value));
        return 1;
    }

    /*
     * `map_instance_setchunk(handle, level, zone_x, zone_z, src, rotation)`.
     *
     * `src` is a coord rather than three ints so content can name a source zone
     * off a landmark — `movecoord(0_50_50_32_16, ...)` — instead of shifting
     * tiles into zones by hand. Any tile inside the zone names it; the registry
     * floors it.
     */
    case SS_OP_MAP_INSTANCE_SETCHUNK:
    {
        int32_t values[6];

        for( int i = 5; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        ToriRSServer_MapInstanceSetchunk(values[0], values[1], values[2], values[3],
                                     coord_x(values[4]), coord_z(values[4]),
                                     coord_level(values[4]), values[5]);
        return 1;
    }

    case SS_OP_MAP_INSTANCE_BUILD:
    {
        int32_t handle;

        if( !SSVM_PopInt(state, &handle) )
            return 1;
        if( ToriRSServer_MapInstanceBuild(handle) )
            ToriRSServer_WorldMapInstanceBuilt(srv, handle);
        return 1;
    }

    /*
     * The instance-relative offset as an absolute coord — the only way content
     * can address a tile inside something the allocator placed. A dead handle
     * gives back coord 0, which is the same "nowhere" the reference's null
     * Location is, and is what an unchecked `map_instance_alloc` failure turns
     * into — a teleport to 0,0 rather than a teleport into someone else's house.
     */
    case SS_OP_MAP_INSTANCE_COORD:
    {
        int32_t values[4];
        int base_x;
        int base_z;

        for( int i = 3; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        if( !ToriRSServer_MapInstanceBase(values[0], &base_x, &base_z) )
        {
            SSVM_PushInt(state, 0);
            return 1;
        }
        SSVM_PushInt(state, coord_pack(values[3], base_x + values[1], base_z + values[2]));
        return 1;
    }

    case SS_OP_MAP_INSTANCE_FREE:
    {
        int32_t handle;
        int left = 0;

        if( !SSVM_PopInt(state, &handle) )
            return 1;
        /*
         * Count what is still standing in it, and say so.
         *
         * The engine does not delete them — whose npcs those are is content's
         * question, and a rule about when a minigame is over does not belong
         * here (`map_instance_procs.rs2`'s header makes the same point). But an
         * abandoned spawn is the one consequence of a sloppy teardown that
         * nothing else reports: the pool re-issues a released square
         * immediately, `npc_add`'s duration is measured in thousands of ticks,
         * and the symptom is the *next* session finding somebody else's boss
         * already in the arena. One line at the moment of the release is what
         * turns that into something a run can be read for.
         */
        for( int i = 0; i < TORIRSSERVER_NPC_MAX; i++ )
        {
            if( srv->npcs[i].active &&
                ToriRSServer_MapInstanceFind(srv->npcs[i].x, srv->npcs[i].z) == handle )
                left++;
        }
        if( left > 0 && getenv("TORIRSSERVER_VERBOSE") )
            fprintf(stderr,
                    "torirsserver: map instance %d freed with %d npc(s) still inside — "
                    "the next session on this square inherits them\n",
                    (int)handle, left);
        ToriRSServer_WorldMapInstanceFree(srv, handle);
        return 1;
    }

    case SS_OP_MAP_INSTANCE_FIND:
    {
        int32_t coord;

        if( !SSVM_PopInt(state, &coord) )
            return 1;
        SSVM_PushInt(state, ToriRSServer_MapInstanceFind(coord_x(coord), coord_z(coord)));
        return 1;
    }

    /* ---- vessels --------------------------------------------------- */

    /*
     * The sailing hulls (docs/SAILING_PLAN.md S1), thin like the map-instance
     * band above: pop the arguments, call the registry, push what it said.
     * Script arguments are DATA here — an out-of-range footprint or heading is
     * refused the way the registries refuse a dead handle, never handed to the
     * C contract asserts that guard host-code callers.
     */
    case SS_OP_VESSEL_SPAWN:
    {
        /* config, size_x, size_z, coord, angle */
        int32_t values[5];
        int handle = 0;

        for( int i = 4; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        if( values[0] >= 0 && values[1] > 0 && values[2] > 0 )
            handle = ToriRSServer_VesselSpawn(
                srv, values[0], values[1], values[2], coord_level(values[3]),
                coord_x(values[3]), coord_z(values[3]),
                values[4] & TORIRSSERVER_VESSEL_ANGLE_MASK);
        if( handle && srv->active_player )
            ToriRSServer_VesselGet(srv, handle)->owner_uid = srv->active_player->pid + 1;
        SSVM_PushInt(state, handle);
        return 1;
    }

    case SS_OP_VESSEL_SETTARGET:
    {
        int32_t handle;
        int32_t coord;
        struct ToriRSServerVessel* vessel;

        if( !SSVM_PopInt(state, &coord) || !SSVM_PopInt(state, &handle) )
            return 1;
        vessel = ToriRSServer_VesselGet(srv, handle);
        if( vessel )
            ToriRSServer_VesselSetTarget(vessel, coord_x(coord), coord_z(coord));
        return 1;
    }

    case SS_OP_VESSEL_SETHEADING:
    {
        int32_t handle;
        int32_t heading;
        struct ToriRSServerVessel* vessel;

        if( !SSVM_PopInt(state, &heading) || !SSVM_PopInt(state, &handle) )
            return 1;
        vessel = ToriRSServer_VesselGet(srv, handle);
        if( vessel && heading >= 0 && heading < 16 )
        {
            ToriRSServer_VesselSetHeading(vessel, heading);
            /* A scripted heading means "sail there", not "point there": the
             * launch-model sail gate (vessel.sails_set) is a helm control,
             * and content driving a hull by opcode expects it to move.
             * Forward, specifically — a navigator who was reversing when the
             * script fired must not creep backward again the moment the sails
             * come down (the ::sails toggle only clears reversing on set). */
            vessel->sails_set = 1;
            vessel->reversing = 0;
        }
        return 1;
    }

    case SS_OP_VESSEL_SETSPEED:
    {
        int32_t handle;
        int32_t tier;
        struct ToriRSServerVessel* vessel;

        if( !SSVM_PopInt(state, &tier) || !SSVM_PopInt(state, &handle) )
            return 1;
        vessel = ToriRSServer_VesselGet(srv, handle);
        if( vessel && tier >= TORIRSSERVER_VESSEL_SPEED_TIER_MIN &&
            tier <= TORIRSSERVER_VESSEL_SPEED_TIER_MAX )
            ToriRSServer_VesselSetSpeed(vessel, tier);
        return 1;
    }

    /* The tile the hull centers on, or coord 0 for a dead handle — the same
     * "nowhere" map_instance_coord answers with. */
    case SS_OP_VESSEL_POS:
    {
        int32_t handle;
        struct ToriRSServerVessel* vessel;

        if( !SSVM_PopInt(state, &handle) )
            return 1;
        vessel = ToriRSServer_VesselGet(srv, handle);
        if( !vessel )
        {
            SSVM_PushInt(state, 0);
            return 1;
        }
        SSVM_PushInt(
            state, coord_pack(vessel->level, vessel->fine_x >> 7, vessel->fine_z >> 7));
        return 1;
    }

    case SS_OP_VESSEL_FREE:
    {
        int32_t handle;

        if( !SSVM_PopInt(state, &handle) )
            return 1;
        ToriRSServer_VesselFree(srv, handle);
        return 1;
    }

    case SS_OP_VESSEL_HERE:
    {
        struct ToriRSServerPlayer* player = srv->active_player;
        struct ToriRSServerVessel* vessel =
            player ? ToriRSServer_VesselAtTile(srv, player->x, player->z) : NULL;

        SSVM_PushInt(state, vessel ? vessel->index : 0);
        return 1;
    }

    case SS_OP_VESSEL_SAILS:
    {
        int32_t handle;
        int32_t set;
        struct ToriRSServerVessel* vessel;

        if( !SSVM_PopInt(state, &set) || !SSVM_PopInt(state, &handle) )
            return 1;
        vessel = ToriRSServer_VesselGet(srv, handle);
        if( vessel )
        {
            /* -1 toggles: the mast's one op is "work the sails", and content
             * has no read-back to decide which way. */
            vessel->sails_set = set < 0 ? !vessel->sails_set : set != 0;
            /* Setting sail and holding position are exclusive states — the
             * same rule the helm's own toggle keeps. */
            if( vessel->sails_set )
                vessel->reversing = 0;
        }
        SSVM_PushInt(state, vessel ? vessel->sails_set : 0);
        return 1;
    }

    case SS_OP_VESSEL_HELM:
    {
        int32_t handle;
        struct ToriRSServerVessel* vessel;
        struct ToriRSServerPlayer* player = srv->active_player;

        if( !SSVM_PopInt(state, &handle) )
            return 1;
        if( !player )
        {
            SSVM_PushInt(state, 0);
            return 1;
        }
        if( handle == 0 )
        {
            player->navigating_vessel = 0;
            SSVM_PushInt(state, 0);
            return 1;
        }
        vessel = ToriRSServer_VesselGet(srv, handle);
        if( !vessel )
        {
            SSVM_PushInt(state, 0);
            return 1;
        }
        if( player->navigating_vessel == vessel->index )
        {
            /* The helm's one op toggles: steering already, so step away. */
            player->navigating_vessel = 0;
            SSVM_PushInt(state, 0);
            return 1;
        }
        player->navigating_vessel = vessel->index;
        player->navigating_vessel_serial = vessel->serial;
        /* Hold the current heading so turning and reversing act immediately —
         * taking the helm is not a movement order in itself (the ::helm
         * cheat's rule, kept exactly). */
        ToriRSServer_VesselSetHeading(
            vessel,
            ((vessel->angle + TORIRSSERVER_VESSEL_HEADING_STEP / 2) /
             TORIRSSERVER_VESSEL_HEADING_STEP) &
                15);
        SSVM_PushInt(state, 1);
        return 1;
    }

    case SS_OP_NPC_SETRESPAWN:
    {
        int32_t delay;
        struct ToriRSServerNpc* npc = active_npc(state);

        if( !SSVM_PopInt(state, &delay) )
            return 1;
        if( !npc )
        {
            SSVM_Abort(state, "npc_setrespawn with no active npc");
            return 1;
        }
        /* Negative → next respawn pass (2009scape setRespawnTick(-1)). */
        if( delay < 0 )
            npc->respawn_tick = srv->tick;
        else
            npc->respawn_tick = srv->tick + delay;
        return 1;
    }

    case SS_OP_NPC_RESPAWN_REMAINING:
    {
        int32_t values[3];
        int best_slot = -1;
        int best_distance = 0;
        int origin_x;
        int origin_z;
        int origin_level;

        /* npc_respawn_remaining(coord, npc, range) */
        for( int i = 2; i >= 0; i-- )
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        origin_x = coord_x(values[0]);
        origin_z = coord_z(values[0]);
        origin_level = coord_level(values[0]);
        for( int slot = 0; slot < TORIRSSERVER_NPC_MAX; slot++ )
        {
            const struct ToriRSServerNpc* candidate = &srv->npcs[slot];
            int dx;
            int dz;
            int distance;

            /* This query exists for the state npc_find cannot represent: a
             * dead world actor waiting on an absolute respawn clock. */
            if( candidate->active || !candidate->def ||
                candidate->type != values[1] ||
                candidate->respawn_tick <= srv->tick ||
                candidate->spawn_level != origin_level )
                continue;
            dx = abs(candidate->spawn_x - origin_x);
            dz = abs(candidate->spawn_z - origin_z);
            distance = dx > dz ? dx : dz;
            if( distance > values[2] )
                continue;
            if( best_slot < 0 || distance < best_distance )
            {
                best_slot = slot;
                best_distance = distance;
            }
        }
        SSVM_PushInt(
            state, best_slot < 0
                       ? -1
                       : srv->npcs[best_slot].respawn_tick - srv->tick);
        return 1;
    }

    /*
     * inv_setvar / inv_getvar — per-slot ints keyed by obj (LC engine.rs2
     * comments; never wired there). Crystal / ethereum charges need this
     * rather than a player-scoped varp.
     */
    case SS_OP_INV_SETVAR:
    {
        int32_t values[4];
        struct ToriRSServerContainer* row;

        for( int i = 3; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        row = container_row(srv, player, values[0]);
        if( !row )
        {
            SSVM_Abort(state, "inv_setvar on unknown container %d", values[0]);
            return 1;
        }
        if( values[1] < 0 || values[1] >= row->slots )
        {
            SSVM_Abort(state, "inv_setvar slot %d outside container %d (%d slots)",
                       values[1], values[0], row->slots);
            return 1;
        }
        ToriRSServer_ItemSetVar(&row->items[values[1]], (int)values[2], (int)values[3]);
        ToriRSServer_ContainerMark(row, (int)values[1]);
        return 1;
    }

    case SS_OP_INV_GETVAR:
    {
        int32_t inv_id;
        int32_t slot;
        int32_t key_obj;
        struct ToriRSServerContainer* row;

        if( !SSVM_PopInt(state, &key_obj) || !SSVM_PopInt(state, &slot) ||
            !SSVM_PopInt(state, &inv_id) )
            return 1;
        row = container_row(srv, player, inv_id);
        if( !row )
        {
            SSVM_Abort(state, "inv_getvar on unknown container %d", inv_id);
            return 1;
        }
        if( slot < 0 || slot >= row->slots )
        {
            SSVM_PushInt(state, 0);
            return 1;
        }
        SSVM_PushInt(state, ToriRSServer_ItemGetVar(&row->items[slot], (int)key_obj));
        return 1;
    }

    /*
     * A rectangle test, and the argument order is the trap.
     *
     * `inzone(from, to, pos)` pushes three coords, so they pop in reverse: pos
     * first. Getting that backwards makes the test read "is `from` inside the
     * rectangle (to, pos)", which is true often enough to look like it works.
     *
     * The reference tests all three axes including level, and its own comparison
     * assumes `from` is the south-west corner and `to` the north-east. A caller
     * that passes them the other way round gets an empty rectangle rather than a
     * diagnostic, in the reference too.
     */
    case SS_OP_INZONE:
    {
        int32_t corner_sw;
        int32_t corner_ne;
        int32_t pos;
        int inside;

        if( !SSVM_PopInt(state, &pos) || !SSVM_PopInt(state, &corner_ne) ||
            !SSVM_PopInt(state, &corner_sw) )
            return 1;
        inside = coord_x(pos) >= coord_x(corner_sw) && coord_x(pos) <= coord_x(corner_ne) &&
                 coord_z(pos) >= coord_z(corner_sw) && coord_z(pos) <= coord_z(corner_ne) &&
                 coord_level(pos) >= coord_level(corner_sw) &&
                 coord_level(pos) <= coord_level(corner_ne);
        SSVM_PushInt(state, inside);
        return 1;
    }

    /*
     * Can you see / walk a straight line between two tiles?
     *
     * LostCity `ServerOps.ts` LINEOFSIGHT / LINEOFWALK → rsmod
     * `hasLineOfSight` / `hasLineOfWalk(..., 1,1,1,1, 0)`. Different plane →
     * false. Outside the built scene → false (same "unknown means don't" rule
     * as map_blocked). The F2P-zone gate the reference applies is not here —
     * this tree has no free-to-play map mask yet.
     */
    case SS_OP_LINEOFSIGHT:
    case SS_OP_LINEOFWALK:
    {
        int32_t from;
        int32_t to;
        int level;
        int x1;
        int z1;
        int x2;
        int z2;
        int clear;

        if( !SSVM_PopInt(state, &to) || !SSVM_PopInt(state, &from) )
            return 1;
        level = coord_level(from);
        if( level != coord_level(to) )
        {
            SSVM_PushInt(state, 0);
            return 1;
        }
        x1 = coord_x(from);
        z1 = coord_z(from);
        x2 = coord_x(to);
        z2 = coord_z(to);
        clear = opcode == SS_OP_LINEOFSIGHT
                    ? ToriRSServer_SceneLineOfSight(level, x1, z1, x2, z2, 1, 1, 1, 1, 0)
                    : ToriRSServer_SceneLineOfWalk(level, x1, z1, x2, z2, 1, 1, 1, 1, 0);
        SSVM_PushInt(state, clear ? 1 : 0);
        return 1;
    }

    /*
     * Does this tile block walking?
     *
     * The reference is `isFlagged(x, z, level, CollisionFlag.WALK_BLOCKED)`, and
     * `COLL_FLAG_WALK_BLOCKED` in collision_map.h is that same composite
     * (LOC | FLOOR | ANTIMACRO). Reading it off the CollisionMap the scene
     * already built means the server and the client answer this from one model
     * rather than two that can drift.
     *
     * A tile outside the built scene reports *blocked*. That is the safe
     * direction: content asks this before dropping a fire or picking a wander
     * target, and "unknown" has to mean "don't" or the loop spawns things in
     * unloaded map.
     */
    case SS_OP_MAP_BLOCKED:
    {
        int32_t coord;

        if( !SSVM_PopInt(state, &coord) )
            return 1;
        SSVM_PushInt(state, ToriRSServer_SceneWalkBlocked(coord_level(coord),
                                                       coord_x(coord),
                                                       coord_z(coord)));
        return 1;
    }

    /* `map_loc(coord)` — whether an active scenery footprint covers this
     * tile. Spirit Spider uses the same source guard before placing eggs, so
     * decorative but non-blocking scenery cannot receive a ground drop. */
    case SS_OP_MAP_LOC:
    {
        int32_t coord;

        if( !SSVM_PopInt(state, &coord) )
            return 1;
        SSVM_PushInt(state, ToriRSServer_SceneFindLoc(coord_x(coord), coord_z(coord),
                                                    coord_level(coord), -1) >= 0);
        return 1;
    }

    /*
     * How many players stand inside a rectangle.
     *
     * One player for now — but the count, not a boolean, because the content
     * that asks (`~playercount_coord_pair_table`) compares it against a
     * threshold. The reference walks the zones the rectangle covers; this walks
     * the player pool, which is the same answer while the pool is small and is
     * what the zone map (§6.1 step 3) will replace.
     */
    case SS_OP_MAP_PLAYERCOUNT:
    {
        int32_t corner_sw;
        int32_t corner_ne;
        int count = 0;

        if( !SSVM_PopInt(state, &corner_ne) || !SSVM_PopInt(state, &corner_sw) )
            return 1;
        for( int i = 0; i < srv->player_count; i++ )
        {
            const struct ToriRSServerPlayer* other = &srv->players[i];

            if( other->level < coord_level(corner_sw) ||
                other->level > coord_level(corner_ne) )
                continue;
            if( other->x < coord_x(corner_sw) || other->x > coord_x(corner_ne) )
                continue;
            if( other->z < coord_z(corner_sw) || other->z > coord_z(corner_ne) )
                continue;
            count++;
        }
        SSVM_PushInt(state, count);
        return 1;
    }

    /*
     * The server-wide player count, no rectangle — LostCity's
     * `~scale_by_playercount` (skill_mining/general/scripts/player_count.rs2)
     * scales respawn ticks by world population and needs this rather than
     * MAP_PLAYERCOUNT's zone-local answer.
     */
    case SS_OP_PLAYERCOUNT:
        SSVM_PushInt(state, srv->player_count);
        return 1;

    /* ---- enums ----------------------------------------------------- */

    /*
     * `enum(inputtype, outputtype, enum, key)`.
     *
     * The declared output type decides **which stack** the result goes on, so
     * this is one of the few host commands where getting a type wrong does not
     * produce a wrong number — it produces a wrong *stack depth*, and every
     * value the script reads afterwards is somebody else's. That is why the
     * reference validates the declared types against the enum's own and why this
     * aborts rather than guessing.
     *
     * A key with no entry yields the enum's `default=`, not an error: the
     * reference's content relies on that for sparse tables.
     */
    case SS_OP_ENUM:
    {
        int32_t values[4];
        const struct ToriRSServerEnumDef* def;

        for( int i = 3; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        def = ToriRSServer_ContentEnumById(values[2]);
        if( !def )
        {
            SSVM_Abort(state, "enum %d is not defined by any .enum config", values[2]);
            return 1;
        }
        for( int i = 0; i < def->count; i++ )
        {
            if( def->values[i].key != values[3] )
                continue;
            if( def->output_is_string )
                SSVM_PushStr(state, def->values[i].text ? def->values[i].text : "");
            else
                SSVM_PushInt(state, def->values[i].value);
            return 1;
        }
        if( def->output_is_string )
            SSVM_PushStr(state, def->default_text ? def->default_text : "null");
        else
            SSVM_PushInt(state, def->default_int);
        return 1;
    }

    case SS_OP_ENUM_GETOUTPUTCOUNT:
    {
        int32_t enum_id;
        const struct ToriRSServerEnumDef* def;

        if( !SSVM_PopInt(state, &enum_id) )
            return 1;
        def = ToriRSServer_ContentEnumById(enum_id);
        if( !def )
        {
            SSVM_Abort(state, "enum_getoutputcount on undefined enum %d", enum_id);
            return 1;
        }
        SSVM_PushInt(state, def->count);
        return 1;
    }

    case SS_OP_RANDOM:
    {
        int32_t bound;

        if( !SSVM_PopInt(state, &bound) )
            return 1;
        /* random(n) is 0..n-1, matching the reference. A zero or negative
         * bound yields 0 rather than aborting: content computing a bound from
         * a table size should not take the server down when the table is
         * empty. */
        SSVM_PushInt(state, bound > 0 ? ToriRSServer_Random(srv, 0, bound - 1) : 0);
        return 1;
    }

    /* ---- ground objs ---------------------------------------------- */

    /*
     * `obj_add(coord, obj, count, duration)` — the command every drop table in
     * LostCity's content is written against. Duration is in ticks
     * (`^lootdrop_duration` is 200); a non-positive one means a permanent
     * spawn, which is how the map squares' own objs are placed.
     */
    case SS_OP_OBJ_ADD:
    {
        int32_t values[4];

        for( int i = 3; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        ToriRSServer_WorldObjAdd(srv, values[1], values[2], coord_x(values[0]),
                              coord_z(values[0]), coord_level(values[0]),
                              values[3] > 0 ? values[3] : -1);
        /*
         * Official client loot tracker: RUNCLIENTSCRIPT script_7192
         * (LOOTTRACKER_ADD_LOOT) with (npcId, eventId, itemId, qty). Only while
         * combat death credit is armed — not every ground spawn.
         */
        ToriRSServer_LootTrackerAddGroundObj(
            srv, NULL, (int)values[1], (int)values[2]);
        return 1;
    }

    /*
     * `obj_add_private(coord, obj, count, duration, private_ticks)` — the
     * owner-filtered counterpart to OBJ_ADD. Familiar foragers use the source
     * game's one-hundred-tick owner window; the world owns visibility and
     * promotion, so a forged ground-target packet cannot bypass it.
     */
    case SS_OP_OBJ_ADD_PRIVATE:
    {
        int32_t values[5];

        for( int i = 4; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        if( !player )
        {
            SSVM_Abort(state, "obj_add_private: no active player");
            return 1;
        }
        ToriRSServer_WorldObjAddPrivate(srv, player, values[1], values[2], coord_x(values[0]),
                                      coord_z(values[0]), coord_level(values[0]),
                                      values[3] > 0 ? values[3] : -1, values[4]);
        /* Private drops used to stop here, bypassing the only notification the
         * client loot tracker consumes. That made misses depend on which drop
         * table opcode happened to roll the item. */
        ToriRSServer_LootTrackerAddGroundObj(
            srv, player, (int)values[1], (int)values[2]);
        return 1;
    }

    /* ---- stats ----------------------------------------------------- */

    /*
     * Every stat command aborts on an id outside the table rather than
     * returning zero or doing nothing.
     *
     * A stat is a bare name in RuneScript, and this cache uses three of the 23
     * names for something else as well — `hitpoints` is also a param, `attack`
     * a varp, `fishing` a loc. The compiler now resolves the stat family with a
     * kind hint so those cannot arrive here, and this is the second half of
     * that fix: if one ever does, the number will be in the thousands, and a
     * silent no-op means a script that heals nothing and says nothing. That is
     * how the first version of the food content shipped looking correct.
     */
    case SS_OP_STAT:
    {
        int32_t stat;

        if( !SSVM_PopInt(state, &stat) )
            return 1;
        if( stat < 0 || stat >= TORIRSSERVER_STAT_COUNT )
        {
            SSVM_Abort(state, "stat %d is not a skill", stat);
            return 1;
        }
        /* `stat` is the *boosted* level in the reference — what a level check
         * in content wants to know is what you can do right now. */
        SSVM_PushInt(state, player->stat_boosted[stat]);
        return 1;
    }

    /* The *base* level — what the stat would be with no boost or drain on it.
     * Content asks for this when a cap is involved: an altar restores prayer to
     * its base, not to whatever a potion left it at. */
    case SS_OP_STAT_BASE:
    {
        int32_t stat;

        if( !SSVM_PopInt(state, &stat) )
            return 1;
        if( stat < 0 || stat >= TORIRSSERVER_STAT_COUNT )
        {
            SSVM_Abort(state, "stat_base %d is not a skill", stat);
            return 1;
        }
        SSVM_PushInt(state, player->stat_level[stat]);
        return 1;
    }

    /* Exact accumulated experience in the same tenths-of-XP unit accepted by
     * stat_advance. Unlike stat/stat_base this is not derived from a level, so
     * level-99 accounts at the 200m cap remain distinguishable. */
    case SS_OP_STAT_XP:
    {
        int32_t stat;

        if( !SSVM_PopInt(state, &stat) )
            return 1;
        if( stat < 0 || stat >= TORIRSSERVER_STAT_COUNT )
        {
            SSVM_Abort(state, "stat_xp %d is not a skill", stat);
            return 1;
        }
        SSVM_PushInt(state, player->stat_xp_tenths[stat]);
        return 1;
    }

    /*
     * stat_heal(stat, constant, percent) — the reference's formula, verbatim.
     *
     *   healed = current + (constant + base * percent / 100)
     *   level  = max(min(healed, base), current)
     *
     * The two clamps are what make it a *heal*: it never exceeds the base level
     * and never takes a stat down, so calling it on a stat already at full is a
     * no-op rather than a reset. Food is (n, 0); an altar is (base - current,
     * 0); a percentage restore is (0, n).
     */
    /*
     * stat_boost / stat_drain: the two directions of a temporary level change.
     *
     * Same `(stat, constant, percent)` shape as stat_heal below, and the same
     * `constant + base * percent / 100` arithmetic — that is the reference's
     * formula and the reason a super attack potion is written `(5, 15)` rather
     * than as a number of levels.
     *
     * The difference from stat_heal is which direction the clamp faces. A boost
     * may take the boosted level *above* base and must not be undone by a second
     * boost that computes a smaller target; a drain may take it below and must
     * not go under zero. stat_heal restores toward base and clamps at it.
     *
     * What was here said `stat_add` and `stat_sub` were deliberately absent
     * because "whether they move the base level or the boosted one is not
     * something this repo pins down". `PlayerOps.ts` pins it down for all four:
     * every one of them reads `baseLevels[stat]` only to compute the step and
     * writes `levels[stat]`, so *none* of them touches the base level. The four
     * differ solely in which way the result is clamped, which is the table in
     * SS_OP_STAT_ADD below.
     */
    case SS_OP_STAT_BOOST:
    /* `stat_sub` and `stat_drain` are the same operation: subtract a flat
     * amount plus a percentage of the base level from the boosted level. The
     * reference has both names because content reads better one way for a
     * poison hit and the other for prayer drain; there is nothing to
     * distinguish in the handler, and giving `stat_sub` its own would be two
     * copies of one rule. */
    case SS_OP_STAT_SUB:
    case SS_OP_STAT_DRAIN:
    {
        int32_t values[3];
        int base;
        int current;
        int target;
        int boosting = opcode == SS_OP_STAT_BOOST;

        for( int i = 2; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        if( values[0] < 0 || values[0] >= TORIRSSERVER_STAT_COUNT )
        {
            SSVM_Abort(state, "%s %d is not a skill", SSVM_OpcodeName(opcode), values[0]);
            return 1;
        }

        base = player->stat_level[values[0]];
        current = player->stat_boosted[values[0]];
        target = values[1] + base * values[2] / 100;
        /* The inner `min(current + d, base + d)` is what stops a second dose of
         * the same potion stacking: one boost's worth above base is the ceiling,
         * however many times it is drunk. Without it every consume script in
         * player/scripts/consumption compounds — four sips of an attack potion
         * read as +12 instead of +3, and an overload's 25-tick re-apply climbs
         * for its whole five minutes. stat_add is the unclamped member of the
         * family for the callers that genuinely want to keep adding. */
        target = boosting ? (current + target < base + target ? current + target : base + target)
                          : current - target;
        if( target < 0 )
            target = 0;
        if( boosting && target < current )
            target = current;
        if( !boosting && target > current )
            target = current;
        if( target == current )
            return 1;
        if( godmode_blocks_stat_write(player, values[0], target) )
            return 1;

        player->stat_boosted[values[0]] = target;
        /* Hitpoints are two views of one number — the stat the skills tab
         * prints and the health orb's `hitpoints`. */
        if( values[0] == TORIRSSERVER_STAT_HITPOINTS )
            player->hitpoints = target;
        ToriRSServer_CombatStatMark(player, values[0]);
        return 1;
    }

    /*
     * stat_add(stat, constant, percent) — engine.rs2, `PlayerOps.ts` STAT_ADD.
     *
     * The unclamped member of the family. All four compute the same step
     * `d = constant + base * percent / 100` against the boosted level; they part
     * company on the ceiling:
     *
     *   stat_add    current + d,                       capped at 255
     *   stat_sub    current - d,                       floored at 0
     *   stat_boost  max(min(current + d, base + d), current), capped at 255
     *   stat_heal   max(min(current + d, base), current)
     *
     * So `stat_add` is not a spelling of `stat_boost`: boost's inner `min` caps
     * the result at one boost's worth above base, which is what stops a second
     * dose of the same potion stacking, while add just adds. That is why the
     * reference uses add for the places a level is being *set up* rather than
     * temporarily improved — `appearance.rs2` builds a max-stat dummy with
     * `stat_add(attack, 255 - stat(attack), 0)`, which boost would refuse to
     * take past `base + d`.
     *
     * 255 is the ceiling rather than some multiple of the base because the wire
     * field is a byte: `changeStat` writes the boosted level as u8, so a level
     * of 256 arrives as 0 and the skills tab reads it as a drained stat.
     */
    case SS_OP_STAT_ADD:
    {
        int32_t values[3];
        int target;

        for( int i = 2; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        if( values[0] < 0 || values[0] >= TORIRSSERVER_STAT_COUNT )
        {
            SSVM_Abort(state, "stat_add %d is not a skill", values[0]);
            return 1;
        }

        target = player->stat_boosted[values[0]] +
                 (values[1] + player->stat_level[values[0]] * values[2] / 100);
        if( target > 255 )
            target = 255;
        if( target < 0 )
            target = 0;
        if( target == player->stat_boosted[values[0]] )
            return 1;
        if( godmode_blocks_stat_write(player, values[0], target) )
            return 1;

        player->stat_boosted[values[0]] = target;
        if( values[0] == TORIRSSERVER_STAT_HITPOINTS )
            player->hitpoints = target;
        ToriRSServer_CombatStatMark(player, values[0]);
        return 1;
    }

    /*
     * `[command,map_multiway](coord $coord)(boolean)` — `ServerOps.ts`
     * MAP_MULTIWAY, one `World.gameMap.isMulti(coord)`.
     *
     * Multi-combat: whether more than one thing may attack you on that tile, and
     * whether an AoE spreads. `player_combat.rs2` and `npc_combat.rs2` both open
     * with `if (map_multiway(npc_coord) = false & nc_param(npc_type,
     * npc_forcemulti) = ^false)` before refusing a second attacker, so this is
     * the gate on every fight in the game rather than a wilderness detail.
     *
     * The data is content's (`maps/multiway.csv`, ported from the reference's own
     * file) and the lookup is a binary search over a zone set —
     * `ToriRSServer_ContentMultiway`. The Kronos queue recorded this as "opcode
     * exists, no multi map": the opcode was *declared* and never hosted, so a
     * script calling it reached the VM's stub and got 0. Which is the same answer
     * an empty zone set gives, and that is exactly why it was worth hosting
     * rather than leaving — the two are indistinguishable from content, so
     * nothing could tell whether the map was missing or the opcode was.
     */
    case SS_OP_MAP_MULTIWAY:
    {
        int32_t coord;

        if( !SSVM_PopInt(state, &coord) )
            return 1;
        SSVM_PushInt(state, ToriRSServer_ContentMultiway(ToriRSServer_CoordX(coord),
                                                     ToriRSServer_CoordZ(coord),
                                                     ToriRSServer_CoordLevel(coord)));
        return 1;
    }

    case SS_OP_STAT_TOTAL:
    {
        int total = 0;

        /* Base levels, not boosted: the total-level number is what the skills
         * tab prints, and a potion does not change it. */
        for( int stat = 0; stat < TORIRSSERVER_STAT_COUNT; stat++ )
            total += player->stat_level[stat];
        SSVM_PushInt(state, total);
        return 1;
    }

    /*
     * p_logout: end the session.
     *
     * Killing the session is the whole of it — the socket loop and the embedded
     * pump both exit on a dead session, and the teardown that follows is what
     * saves the player. Doing anything more here would duplicate that path.
     */
    case SS_OP_P_LOGOUT:
        if( srv->active_player && srv->active_player->session )
            ToriRSServer_SessionKill(srv->active_player->session);
        return 1;

    case SS_OP_STAT_HEAL:
    {
        int32_t values[3];
        int base;
        int current;
        int healed;

        for( int i = 2; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        if( values[0] < 0 || values[0] >= TORIRSSERVER_STAT_COUNT )
        {
            SSVM_Abort(state, "stat_heal %d is not a skill", values[0]);
            return 1;
        }
        base = player->stat_level[values[0]];
        current = player->stat_boosted[values[0]];
        healed = current + values[1] + base * values[2] / 100;
        if( healed > base )
            healed = base;
        if( healed < current )
            healed = current;
        if( healed == current )
            return 1;
        player->stat_boosted[values[0]] = healed;
        /* Hitpoints are two views of one number — the stat the skills tab
         * prints and the health orb's `hitpoints`. Writing only the stat would
         * heal a player whose orb never moved. */
        if( values[0] == TORIRSSERVER_STAT_HITPOINTS )
            player->hitpoints = healed;
        ToriRSServer_CombatStatMark(player, values[0]);
        return 1;
    }

    /*
     * stat_random(stat, low, high) — the level-interpolated success roll.
     *
     *   value  = low * (99 - level) / 98 + high * (level - 1) / 98 + 1
     *   return value > random(256)
     *
     * `low` is the chance out of 256 at level 1 and `high` the chance at level
     * 99, with everything between them on a straight line. Every skill in
     * OldSchool that can fail rolls this, which is why it is a command and not
     * four lines of `calc()` in content: it is the formula, and the rule this
     * tree keeps is that arithmetic stays in C.
     */
    case SS_OP_STAT_RANDOM:
    {
        int32_t values[3];
        int level;
        int value;

        for( int i = 2; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        if( values[0] < 0 || values[0] >= TORIRSSERVER_STAT_COUNT )
        {
            SSVM_Abort(state, "stat_random %d is not a skill", values[0]);
            return 1;
        }
        level = player->stat_boosted[values[0]];
        value = values[1] * (99 - level) / 98 + values[2] * (level - 1) / 98 + 1;
        SSVM_PushInt(state, value > ToriRSServer_Random(srv, 0, 255) ? 1 : 0);
        return 1;
    }

    case SS_OP_STAT_ADVANCE:
    {
        int32_t stat;
        int32_t experience;

        if( !SSVM_PopInt(state, &experience) )
            return 1;
        if( !SSVM_PopInt(state, &stat) )
            return 1;
        if( stat < 0 || stat >= TORIRSSERVER_STAT_COUNT )
        {
            SSVM_Abort(state, "stat_advance %d is not a skill", stat);
            return 1;
        }
        /* The reference's xp argument is already in tenths. */
        ToriRSServer_CombatAddXp(srv, stat, experience);
        return 1;
    }

    /* `npc_param` was here. It is `torirs_server_ops_npc.c`'s now — and had been
     * unreachable since that domain's hook went in above, because the per-domain
     * handlers are offered the opcode first. Deleted rather than marked dead: it
     * answered one param by spelling `death_drop` in C, which is the hard rule
     * in PORTING_GUIDE §2.4 item 3, and a dead copy of a wrong answer is the
     * thing somebody eventually reads and believes. */

    /* ---- audio ---------------------------------------------------- */

    case SS_OP_SOUND_SYNTH:
    {
        int32_t values[3];

        for( int i = 2; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        /* `[command,sound_synth](synth $sound, int $loops, int $delay)`
         * (LostCity_Server/content/scripts/engine.rs2:205) — values[0] sound,
         * values[1] loops, values[2] delay, the same order SYNTH_SOUND wants
         * on the wire (WEAPON_FX.md §6). The encoder now exists
         * (ToriRSServer_SendSynthSound, torirs_server_encode.c) and the packet is
         * routed (packetin.h:102 -> PKT_NAME_SYNTH_SOUND); this used to be a
         * stub that only printed under TORIRSSERVER_VERBOSE. */
        if( srv->verbose )
            fprintf(stderr, "torirsserver: sound_synth(%d, %d, %d)\n", values[0], values[1],
                    values[2]);
        /*
         * A negative id is "no sound", not sound -1. LostCity's engine rejects
         * it outright (`check(synth, NumberNotNull)` in PlayerOps.ts) because
         * its sound params default to `null`; this tree's default to -1 for the
         * same reason, and a caller that forgets to guard should cost a dropped
         * packet rather than a wrong noise. Zero is deliberately NOT filtered:
         * sound effect 0 exists and something may legitimately want it.
         */
        if( values[0] < 0 )
            return 1;
        if( player != NULL )
            ToriRSServer_SendSynthSound(player, values[0], values[1], values[2]);
        return 1;
    }

    case SS_OP_MIDI_SONG:
    {
        int32_t id;

        if( !SSVM_PopInt(state, &id) )
            return 1;
        if( srv->verbose )
            fprintf(stderr, "torirsserver: midi_song(%d)\n", id);
        if( player != NULL )
        {
            /* Keep scripted overrides in the same state slot as region music.
             * The next mapped region can then restore its normal track when an
             * instanced encounter teleports the player back out. */
            player->music_track = id;
            /*
             * A negative id is "no music", not track -1 — the same rule
             * `SS_OP_SOUND_SYNTH` states a few cases above, and for the same
             * reason: these ids default to -1, so a caller that means silence
             * has one spelling and it should not be a second command.
             *
             * It has to be MIDI_SONG_STOP and not MIDI_SONG carrying the id.
             * MIDI_SONG's id is two bytes with 65535 as the sentinel, and the
             * 239 client turns that into -1 and then starts nothing — it never
             * stops what is already playing. So `midi_song(-1)` on a track that
             * is running would leave the track running, which is the failure a
             * cutscene asking for silence cannot see: it would look exactly
             * like the command not being called at all.
             *
             * 0 delay, 30 speed: the same 600 ms ramp `ToriRSServer_MusicEnterRegion`
             * uses in both directions, so a stop and the region's own fade-in
             * are the same length rather than two different-feeling ramps.
             */
            if( id < 0 )
                ToriRSServer_SendMidiSongStop(player, 0, 30);
            else
                ToriRSServer_SendMidiSong(player, id);
        }
        return 1;
    }

    case SS_OP_MIDI_LENGTH:
    {
        int32_t id;

        if( !SSVM_PopInt(state, &id) )
            return 1;
        if( id < 0 || id >= TORIRSSERVER_JINGLE_LENGTH_COUNT )
        {
            SSVM_PushInt(state, 0);
            return 1;
        }
        SSVM_PushInt(state, k_ToriRSServer_JingleLengthMs[id]);
        return 1;
    }

    case SS_OP_MIDI_JINGLE:
    {
        int32_t id;
        int32_t length_ms;

        if( !SSVM_PopInt(state, &id) )
            return 1;
        /* `[command,midi_jingle](midi $jingle)` (LostCity_Server/content/
         * scripts/engine.rs2:331) takes one arg; the reference computes the
         * wire's length field itself (`Player.playJingle` ->
         * `new MidiJingle(id, Midi.getLength(id))`), which is why this looks
         * up the length rather than taking a second argument. */
        if( srv->verbose )
            fprintf(stderr, "torirsserver: midi_jingle(%d)\n", id);
        /* Negative is "no jingle", the same rule sound_synth and midi_song
         * both state a few cases above -- these ids default to -1 too. */
        if( id < 0 )
            return 1;
        length_ms = (id < TORIRSSERVER_JINGLE_LENGTH_COUNT) ? k_ToriRSServer_JingleLengthMs[id] : 0;
        if( player != NULL )
            ToriRSServer_SendMidiJingle(player, id, length_ms);
        return 1;
    }

    /*
     * `ambientsound(int $soundscape)` — the region's background bed.
     *
     * The id names a config group-15 soundscape (a set of continuous loops
     * plus independently timed random sets), not a sound effect, so this is
     * not a spelling of `sound_synth`; and negative means stop, the same rule
     * `midi_song` and `sound_synth` state.
     *
     * Calling it claims the bed for the map square the caller is standing on,
     * which is how the claim survives `ToriRSServer_AmbientEnterRegion` running
     * later in the same tick as the teleport that got the player here. The
     * claim is released by leaving that square, so a script that silences a
     * place does not also have to remember to restore the world's bed on every
     * exit path it has — including the ones it does not control, like a death
     * or a logout.
     */
    case SS_OP_AMBIENTSOUND:
    {
        int32_t id;

        if( !SSVM_PopInt(state, &id) )
            return 1;
        if( srv->verbose )
            fprintf(stderr, "torirsserver: ambientsound(%d)\n", id);
        if( player != NULL )
        {
            int map_x;
            int map_z;

            ToriRSServer_RegionSquareFor(player, &map_x, &map_z);
            player->ambient_script_map_x = map_x;
            player->ambient_script_map_z = map_z;

            if( player->ambient_scape != id )
            {
                player->ambient_scape = id < 0 ? -1 : id;
                if( id < 0 )
                    ToriRSServer_SendAmbientsoundStop(player, 1);
                else
                    ToriRSServer_SendAmbientsoundStart(player, id, 1);
            }
        }
        return 1;
    }

    /* ---- containers ------------------------------------------------ */

    /*
     * The container commands the bank content needs.
     *
     * These are LostCity's own signatures, so `content/scripts/interface_bank`
     * ports across as text. What differs is the *implementation*: LostCity's
     * inventories are dynamic objects with a transmit list per client, and the
     * mock has three fixed containers and one client, so "transmit" is a flag
     * and "stop transmitting" is clearing it.
     */

    case SS_OP_INV_SIZE:
    {
        int32_t inv_id;
        int size;

        if( !SSVM_PopInt(state, &inv_id) )
            return 1;
        /*
         * Straight off the cache, not through the registry. `inv_size` is a
         * question about the *type*, which is what LostCity asks
         * (`InvType.get(inv).size`) — a container nobody has touched yet still
         * has a size. Routing it through `container_for` is what made this
         * return 0 for 1,023 of the cache's 1,026 invs while
         * `ToriRSServer_BankInvSize` sat beside it answering every one.
         */
        size = ToriRSServer_BankInvSize((int)inv_id);
        /*
         * The cache is not the only place a type size can live, and answering
         * only the first source is what made this op lie.
         *
         * A `pack/inv.alloc` id has no config group 5 record at all, so the
         * lookup above answers 0 for it -- while the container the same inv
         * hands out is sized from the `size=` its `.inv` declared. That is the
         * two-source chain `ToriRSServer_ContainerResolve` already walks, and
         * walking only half of it put `inv_size` and `inv_freespace` in
         * disagreement about one container. Content cannot see that: it reads
         * as an inv with no slots, and every loop bounded by it silently does
         * nothing.
         *
         * `~gauntlet_login` is what that cost. Its "is anything being held"
         * guard is `inv_freespace(x) = inv_size(x)`; on
         * `gauntlet_holding_worn` that was `14 = 0`, false for every player on
         * every login, so the proc fell through to `p_teleport` and put every
         * account in the Gauntlet lobby. `~gauntlet_restore_gear`'s
         * `while ($slot < inv_size(gauntlet_holding_worn))` was
         * `while ($slot < 0)` in the same breath, so the gear it was meant to
         * hand back never moved.
         */
        if( size <= 0 )
            size = ToriRSServer_ShopContentSize(inv_id);
        /*
         * Neither source knows this inv: content named an id that is in no
         * cache record and no `.inv`. There is no size to answer with, and
         * pushing 0 -- a perfectly ordinary-looking number that reads as "no
         * slots" -- is precisely the silence that hid the above for two weeks.
         */
        assert(size > 0);
        SSVM_PushInt(state, size);
        return 1;
    }

    /*
     * inv_setslot: write one cell, whatever was there.
     *
     * `[command,inv_setslot](inv $inv, int $slot, namedobj $obj, int $count)` in
     * the reference's engine.rs2. The registry's own mutator owns the dirty
     * flag, which is the only reason this is three lines: a per-slot mask for a
     * 28-slot backpack and a whole-container flag for a 500-slot one are the
     * same call here.
     *
     * A count of zero is not this opcode's to interpret: writing an obj at zero
     * is how content creates a bank placeholder (`~bank_leave_placeholder`) and
     * is meaningless anywhere else, and `ToriRSServer_ContainerSet` is where that
     * distinction lives — see `zero_count_is_meaningful` for the cell it used to
     * build in a backpack, which counted as full while holding nothing.
     *
     * A *negative* count has no reading at all, so it aborts where it was
     * written rather than at the frame that later finds the container one slot
     * short.
     */
    case SS_OP_INV_SETSLOT:
    {
        int32_t values[4];
        struct ToriRSServerContainer* row;

        for( int i = 3; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        row = container_row(srv, player, values[0]);
        if( !row )
        {
            SSVM_Abort(state, "inv_setslot on unknown container %d", values[0]);
            return 1;
        }
        if( values[1] < 0 || values[1] >= row->slots )
        {
            SSVM_Abort(state, "inv_setslot slot %d outside container %d (%d slots)",
                       values[1], values[0], row->slots);
            return 1;
        }
        if( values[3] < 0 )
        {
            SSVM_Abort(state, "inv_setslot count %d into container %d slot %d", values[3],
                       values[0], values[1]);
            return 1;
        }
        ToriRSServer_ContainerSet(row, (int)values[1], (int)values[2], (int)values[3]);
        return 1;
    }

    case SS_OP_INV_GETOBJ:
    case SS_OP_INV_GETNUM:
    {
        int32_t inv_id;
        int32_t slot;
        int slots = 0;
        struct ToriRSServerItem* items;

        if( !SSVM_PopInt(state, &slot) || !SSVM_PopInt(state, &inv_id) )
            return 1;
        items = container_for(srv, player, inv_id, &slots);
        if( !items )
        {
            SSVM_Abort(state, "inv_getobj/getnum on unknown container %d", inv_id);
            return 1;
        }
        if( slot < 0 || slot >= slots || items[slot].obj_id < 0 )
        {
            /* `null` is -1 for an obj and 0 for a count, which is what every
             * caller branches on. */
            SSVM_PushInt(state, opcode == SS_OP_INV_GETOBJ ? -1 : 0);
            return 1;
        }
        SSVM_PushInt(state, opcode == SS_OP_INV_GETOBJ ? items[slot].obj_id
                                                       : items[slot].count);
        return 1;
    }

    /*
     * inv_itemspace / inv_itemspace2: "will this fit" and "how much will not".
     *
     * The reference splits them because the *messages* differ — a stack that
     * will not fit and a pile that will not all fit are different sentences —
     * and both need the overflow count, not a boolean.
     */
    case SS_OP_INV_ITEMSPACE:
    case SS_OP_INV_ITEMSPACE2:
    {
        int32_t inv_id;
        int32_t obj_id;
        int32_t count;
        int32_t limit;
        int slots = 0;
        struct ToriRSServerItem* items;
        int space = 0;

        if( !SSVM_PopInt(state, &limit) || !SSVM_PopInt(state, &count) ||
            !SSVM_PopInt(state, &obj_id) || !SSVM_PopInt(state, &inv_id) )
            return 1;
        items = container_for(srv, player, inv_id, &slots);
        if( !items )
        {
            SSVM_Abort(state, "inv_itemspace on unknown container %d", inv_id);
            return 1;
        }
        if( limit > 0 && limit < slots )
            slots = (int)limit;
        {
            /* The CONTAINER's stack policy, not the obj record's alone: a shop
             * stacks everything (`stackall=yes`), so asking for one free slot
             * per pot refused a sale into a nearly-full store that in fact had
             * room in the pot cell it already held. `ToriRSServer_ContainerAdd` has
             * always used the container's rule; this now agrees with it. */
            if( ToriRSServer_ContainerStacksObj(
                    container_row(srv, player, inv_id), (int)obj_id) )
            {
                int has_stack = 0;

                for( int i = 0; i < slots; i++ )
                    if( items[i].obj_id == obj_id )
                        has_stack = 1;
                    else if( items[i].obj_id < 0 )
                        space++;
                space = (has_stack || space > 0) ? (int)count : 0;
            }
            else
            {
                for( int i = 0; i < slots; i++ )
                    if( items[i].obj_id < 0 )
                        space++;
                if( space > count )
                    space = (int)count;
            }
        }
        if( opcode == SS_OP_INV_ITEMSPACE )
            SSVM_PushInt(state, space >= count ? 1 : 0);
        else
            SSVM_PushInt(state, (int)count - space);
        return 1;
    }

    case SS_OP_INV_MOVETOSLOT:
    {
        int32_t from_inv;
        int32_t to_inv;
        int32_t from_slot;
        int32_t to_slot;
        int from_slots = 0;
        int to_slots = 0;
        struct ToriRSServerItem* from_items;
        struct ToriRSServerItem* to_items;

        if( !SSVM_PopInt(state, &to_slot) || !SSVM_PopInt(state, &from_slot) ||
            !SSVM_PopInt(state, &to_inv) || !SSVM_PopInt(state, &from_inv) )
            return 1;
        from_items = container_for(srv, player, from_inv, &from_slots);
        to_items = container_for(srv, player, to_inv, &to_slots);
        if( !from_items || !to_items )
        {
            SSVM_Abort(state, "inv_movetoslot between containers %d and %d", from_inv, to_inv);
            return 1;
        }
        if( from_slot < 0 || from_slot >= from_slots || to_slot < 0 || to_slot >= to_slots )
            return 1;
        {
            struct ToriRSServerItem swap = from_items[from_slot];

            from_items[from_slot] = to_items[to_slot];
            to_items[to_slot] = swap;
        }
        container_dirty(srv, player, from_inv, (int)from_slot);
        container_dirty(srv, player, to_inv, (int)to_slot);
        return 1;
    }

    /* `inv_moveitem` and its `_cert` / `_uncert` siblings were here. They are
     * torirs_server_ops_inv.c's now, with their three bank arms unchanged and a
     * fourth, generic container-to-container arm appended after them — the arm
     * every reference equip and unequip path needs and which used to print
     * "is not modelled" and do nothing. That gap was invisible to
     * gen_opcode_coverage.py because the opcode had a `case` label. */

    case SS_OP_INV_CLEAR:
    {
        int32_t inv_id;
        int slots = 0;
        struct ToriRSServerItem* items = NULL;

        if( !SSVM_PopInt(state, &inv_id) )
            return 1;
        items = container_for(srv, player, inv_id, &slots);
        if( !items )
        {
            SSVM_Abort(state, "inv_clear on unknown container %d", inv_id);
            return 1;
        }
        for( int i = 0; i < slots; i++ )
        {
            items[i].obj_id = -1;
            items[i].count = 0;
            container_dirty(srv, player, inv_id, i);
        }
        return 1;
    }

    /*
     * inv_transmit / inv_stoptransmit: bind a container to a component.
     *
     * The reference keeps a per-client list of (inv, component) bindings and
     * sends a full update when one is added. There is one client here and the
     * bindings are fixed, so the whole of it is "send the container now" —
     * which is the part that matters, because the interface it paints was built
     * before the container existed and its paint hook only runs on a transmit.
     */
    case SS_OP_INV_TRANSMIT:
    {
        int32_t inv_id;
        int32_t component;

        if( !SSVM_PopInt(state, &component) || !SSVM_PopInt(state, &inv_id) )
            return 1;
        /*
         * The bank is deliberately not bound through the registry.
         *
         * Its transmit is gated on the interface being open and re-sends tab
         * bookkeeping with it (ToriRSServer_BankFlush / bank_push_settings), which
         * the generic binding does not model — binding it here would put two
         * senders on one container, which is exactly the failure mode the
         * registry exists to remove. Its *row* is in the registry all the same,
         * and that is what stopped `inv_del(bank,…)` dirtying a worn slot.
         * Folding the transmit in means moving `bank.open` into the binding
         * table; that is a real simplification and it is not this stage's.
         */
        if( inv_id == ToriRSServer_Ids()->inv_bank )
        {
            struct ToriRSServerContainer* row = container_row(srv, player, inv_id);
            int used = 0;

            if( !row )
                return 1;
            /* Only the used prefix: UPDATE_INV_FULL clears everything past the
             * capacity it carries, so 1,398 empty slots cost nothing. */
            for( int i = 0; i < row->slots; i++ )
                if( row->items[i].obj_id >= 0 )
                    used = i + 1;
            srv->active_player->bank.open = 1;
            ToriRSServer_ContainerClean(row);
            ToriRSServer_SendInvFull(srv->active_player, (int)component, (int)inv_id, row->items,
                                  used);
            return 1;
        }
        if( !ToriRSServer_ContainerBind(srv, srv->active_player, inv_id, component) )
        {
            SSVM_Abort(state, "inv_transmit on unknown container %d", inv_id);
            return 1;
        }
        return 1;
    }

    case SS_OP_INV_TRANSMIT_FROM:
    {
        int32_t owner_uid;
        int32_t inv_id;
        int32_t component;
        struct ToriRSServerPlayer* owner;

        if( !SSVM_PopInt(state, &component) || !SSVM_PopInt(state, &inv_id) ||
            !SSVM_PopInt(state, &owner_uid) )
            return 1;
        owner = player_by_uid(srv, owner_uid);
        if( !owner )
        {
            SSVM_PushInt(state, 0);
            return 1;
        }
        SSVM_PushInt(state,
                     ToriRSServer_ContainerBindFrom(srv, owner, srv->active_player, inv_id,
                                                 component));
        return 1;
    }

    case SS_OP_INV_STOPTRANSMIT:
    {
        int32_t component;
        struct ToriRSServerContainer* row;
        const struct ToriRSServerWire* wire;
        int bank_scrollbar;
        int stop_inv = -1;

        if( !SSVM_PopInt(state, &component) )
            return 1;

        /* Remember the inventory before unbind erases the only component ->
         * inv association available to this command. If another component is
         * still listening, keep the rev-239 client's global inventory alive:
         * UPDATE_INV_STOPTRANSMIT removes it by inv id, not by component.
         * Revision 230 addresses the component and therefore always receives
         * the stop for the listener that was removed. */
        row = container_listener_row(srv->active_player, component);
        ToriRSServer_ContainerUnbind(srv, srv->active_player, component);
        wire = srv->wire ? srv->wire : ToriRSServer_WireDefault();
        if( row && (wire->revision < 239 || row->listener_count == 0) )
            stop_inv = row->inv_id;

        /* The bank owns its transmit outside the generic listener registry.
         * Its close script names bankmain:scrollbar even though the payload
         * that was transmitted paints bankmain:items, matching the content's
         * authoritative stoptransmit convention. bankside:items is the normal
         * backpack and must not clear that still-live global inventory. */
        bank_scrollbar =
            ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "bankmain:scrollbar");
        if( component == bank_scrollbar )
        {
            srv->active_player->bank.open = 0;
            stop_inv = ToriRSServer_Ids()->inv_bank;
        }

        if( stop_inv >= 0 )
            ToriRSServer_SendInvStopTransmit(
                srv->active_player, (int)component, stop_inv);
        return 1;
    }

    /* ---- objs ------------------------------------------------------ */

    /*
     * oc_cert / oc_uncert: the note form of an obj and back.
     *
     * The cache states only one direction — a note record names the item it
     * stands for — so the forward link is a reverse index ToriRSServer_ObjInfo
     * builds. Both return the input unchanged when there is no other form,
     * which is what the reference does and what every caller tests for.
     */
    case SS_OP_OC_CERT:
    case SS_OP_OC_UNCERT:
    {
        int32_t obj_id;
        const struct ToriRSServerObjInfo* info;

        if( !SSVM_PopInt(state, &obj_id) )
            return 1;
        info = ToriRSServer_ObjInfo((int)obj_id);
        if( opcode == SS_OP_OC_CERT )
            SSVM_PushInt(state, info->cert_id >= 0 ? info->cert_id : obj_id);
        else
            SSVM_PushInt(state, (info->noted_template >= 0 && info->noted_id >= 0)
                                    ? info->noted_id
                                    : obj_id);
        return 1;
    }

    /*
     * oc_placeholder / oc_unplaceholder: the bank-placeholder form of an obj
     * and back. The note pair above in every respect (see ToriRSServerObjInfo) —
     * opcodes 148/149 instead of 97/98 — and deliberately the same shape, so
     * "does this slot hold a placeholder" reads identically here and in
     * `bankmain_drawitem`: `oc_unplaceholder($obj) ! $obj`.
     *
     * Forward: an item states `placeholder_id` and no template.
     * Back: a placeholder states both, and `placeholder_id` is the item.
     */
    case SS_OP_OC_PLACEHOLDER:
    case SS_OP_OC_UNPLACEHOLDER:
    {
        int32_t obj_id;
        const struct ToriRSServerObjInfo* info;

        if( !SSVM_PopInt(state, &obj_id) )
            return 1;
        info = ToriRSServer_ObjInfo((int)obj_id);
        if( opcode == SS_OP_OC_PLACEHOLDER )
            SSVM_PushInt(state, (info->placeholder_template < 0 && info->placeholder_id >= 0)
                                    ? info->placeholder_id
                                    : obj_id);
        else
            SSVM_PushInt(state, (info->placeholder_template >= 0 && info->placeholder_id >= 0)
                                    ? info->placeholder_id
                                    : obj_id);
        return 1;
    }

    /* ---- varbits --------------------------------------------------- */

    /*
     * A varbit is a bit range inside a varplayer, and which range is a cache
     * fact — see torirs_server_bank.c. Writing one as a whole varp would destroy
     * whatever else shares it, which for the bank is always something: the
     * withdraw-as-note flag and the current tab are both in varp 115.
     */
    /*
     * The varbit id is the *operand*, exactly as it is for PUSH_VARP/POP_VARP —
     * `%name` compiles to one instruction carrying the id, with `1 << 16` set
     * for the `.%name` secondary-player form (ssc_compile.c `resolve_variable`).
     *
     * Both of these used to pop the id off the int stack instead. That is not a
     * near miss: PUSH_VARBIT pops nothing per ss_meta.gen.h, so it underflowed;
     * POP_VARBIT popped the *value* and read it as the id, then underflowed
     * looking for a value that was never there. Every `%varbit` in the tree
     * aborted its script at the first mention, and because [login,_] is where
     * opening state is set, one such line took the whole login script with it.
     */
    case SS_OP_PUSH_VARBIT:
    {
        int varbit_id = state->script->int_operands[state->pc] & 0xffff;

        SSVM_PushInt(state, ToriRSServer_VarbitGet(player, varbit_id));
        return 1;
    }

    case SS_OP_POP_VARBIT:
    {
        int varbit_id = state->script->int_operands[state->pc] & 0xffff;
        int32_t value;

        if( !SSVM_PopInt(state, &value) )
            return 1;
        /* A varbit the cache does not place has no varp to write, so the write
         * would vanish. Loud, like every other unresolvable id here. */
        /* On the SCRIPT's active player, which `SS_OP_PUSH_VARBIT` above has
         * always read from. Writing through `srv->active_player` instead meant
         * a script that hunted a set and wrote a varbit to each read one player
         * and wrote another - see `ToriRSServer_VarbitSetOn`. */
        if( ToriRSServer_VarbitSetOn(srv, player, varbit_id, (int)value) < 0 )
            SSVM_Abort(state, "varbit %d is not in the cache", varbit_id);
        return 1;
    }

    /* ---- interfaces ------------------------------------------------ */

    /*
     * if_openmain_side: the two-panel open a bank (or a shop, or a trade) is.
     *
     * The main interface goes into toplevel's `mainmodal` and the side one
     * replaces the whole sidebar through `sidemodal`, which is what puts the
     * bank's inventory panel where the tab strip was. Doing only the first
     * leaves the player's real inventory tab beside a bank that cannot see it.
     */
    case SS_OP_IF_OPENMAIN_SIDE:
    {
        int32_t main_group;
        int32_t side_group;

        if( !SSVM_PopInt(state, &side_group) || !SSVM_PopInt(state, &main_group) )
            return 1;
        if( main_group == ToriRSServer_Ids()->iface_bankmain )
        {
            /* The bank knows how to open itself — settings, events and both
             * containers — and doing it here rather than leaving the script to
             * push fifteen varbits is what keeps the ported content readable. */
            ToriRSServer_BankOpen(srv);
            return 1;
        }
        ToriRSServer_SendIfOpensub(
            srv->active_player,
            ToriRSServer_Ids()->iface_gameframe,
            TORIRSSERVER_COM_CHILD(ToriRSServer_Ids()->com_gameframe_mainmodal),
            (int)main_group,
            0);
        ToriRSServer_SendIfOpensub(
            srv->active_player,
            ToriRSServer_Ids()->iface_gameframe,
            TORIRSSERVER_COM_CHILD(ToriRSServer_Ids()->com_gameframe_sidemodal),
            (int)side_group,
            3);
        return 1;
    }

    case SS_OP_IF_OPENMAIN:
    {
        int32_t group;

        if( !SSVM_PopInt(state, &group) )
            return 1;
        ToriRSServer_SendIfOpensub(
            srv->active_player,
            ToriRSServer_Ids()->iface_gameframe,
            TORIRSSERVER_COM_CHILD(ToriRSServer_Ids()->com_gameframe_mainmodal),
            (int)group,
            0);
        return 1;
    }

    case SS_OP_IF_OPENOVERLAY:
    {
        int32_t group;
        int floater;

        if( !SSVM_PopInt(state, &group) )
            return 1;
        floater = ToriRSServer_PlayerFloater(srv->active_player);
        ToriRSServer_SendIfOpensub(
            srv->active_player,
            TORIRSSERVER_COM_GROUP(floater),
            TORIRSSERVER_COM_CHILD(floater),
            (int)group,
            1);
        return 1;
    }

    /*
     * p_countdialog: ask for a number and wait.
     *
     * The client opens its own "Enter amount" prompt and answers with
     * RESUME_P_COUNTDIALOG. Nothing in the tick releases this — the same shape
     * as p_pausebutton, and for the same reason.
     */
    case SS_OP_P_COUNTDIALOG:
        player->last_int = 0;
        ToriRSServer_SendIfOpencountdialog(srv->active_player);
        SSVM_Suspend(state, SSVM_COUNTDIALOG);
        return 1;

    /*
     * p_countdialog_noprompt: wait for a number WITHOUT opening the prompt.
     *
     * The wait half of the op above, and nothing else. `resume_countdialog` is
     * an ordinary CS2 opcode at this revision, so an interface already on
     * screen can answer a parked script itself — the cache's bank PIN keypad
     * (213) is exactly that: it collects four clicked digits and sends the
     * assembled number.
     *
     * Waiting for it with `p_countdialog` would work and would still be wrong:
     * the prompt it opens echoes the digits as they are typed, which is the
     * one thing a PIN keypad exists to prevent. There is no reference for the
     * split because there is no rev-230 client in the reference.
     */
    case SS_OP_P_COUNTDIALOG_NOPROMPT:
        player->last_int = 0;
        SSVM_Suspend(state, SSVM_COUNTDIALOG);
        return 1;

    /* Revision 239's generic name-entry meslayer. The client returns the
     * value through RESUME_P_NAMEDIALOG; LAST_STRING reads the copied result
     * after this instruction resumes. */
    case SS_OP_P_NAMEDIALOG:
    {
        const char* prompt = NULL;
        const char* strings[1];

        if( !SSVM_PopStr(state, &prompt) )
            return 1;
        strings[0] = prompt ? prompt : "Enter a player name:";
        state->last_string = "";
        ToriRSServer_SendRunClientscriptMixed(
            player, 109, "s", NULL, strings, 1);
        SSVM_Suspend(state, SSVM_NAMEDIALOG);
        return 1;
    }

    /*
     * `last_int` is a property of the SCRIPT STATE, not of the player — the
     * reference pushes `state.lastInt` (PlayerOps.ts) and the npc queue seeds it
     * per request (`Npc.ts`: `state.lastInt = request.lastInt`).
     *
     * Reading `player->last_int` here worked for every player-context reader and
     * silently returned 0 for the one context that has no player value to read:
     * an `[ai_queue<n>]` on an npc. That is where `npc_queue(2, $damage, $delay)`
     * delivers its damage, so EVERY npc-to-npc hit in the tree landed for zero —
     * the Inferno's adds chewed on the Ancestral Glyph for 0s and its health line
     * reprinted 600/600 forever. `state->last_int` was already declared for this
     * and was read by nothing.
     */
    case SS_OP_LAST_INT:
        SSVM_PushInt(state, state->last_int);
        return 1;
    case SS_OP_LAST_STRING:
        SSVM_PushStr(state, state->last_string ? state->last_string : "");
        return 1;
    case SS_OP_LAST_SLOT:
        SSVM_PushInt(state, player->last_slot);
        return 1;
    /*
     * Which sub-option of an interface op fired — the `subaction=<op>,<n>,
     * <name>` rows a rev-230 obj record can hang off one op (Xeric's talisman
     * "Rub" has five, the Slayer ring four, and Giantsoul's Rub has its
     * Bryophyta/Obor/Branda and Eldric destinations).
     *
     * The value has been decoded off IF_SUBOP into `player->last_subop` since
     * that packet was wired (`handle_if_buttonx_packet`), and reset to -1
     * between dispatches; this is only the read side, which content had no way
     * to reach. This is a rev-230 addition (see gen_opcode_meta.py's
     * EXTRA_OPCODES entry); -1 means the op carried no submenu, which is every
     * ordinary `opheldN`.
     */
    /*
     * "Not mine — try the next rung."
     *
     * Two effects, and both are the point. It raises the flag the resolver reads
     * to keep walking, and it ENDS the script: a decline is the last thing a
     * script does, and making that structural rather than a documented promise is
     * what keeps a half-finished interaction from being handed to the next rung.
     * `~displaymessage(^dm_default)` is the shape this replaces, and that one is
     * commonly reached after work has already happened (`bows.rs2` strings a bow
     * and then falls through to it), which is exactly why declining is a separate
     * command a script opts into rather than a change of meaning for that proc.
     *
     * With no chained resolver above — a `[proc]`, a queue entry, a
     * `[debugproc]` — there is no rung to fall to, so it degrades to what the
     * default message did: the engine says nothing interesting happened.
     */
    case SS_OP_TRIGGER_DECLINE:
        srv->trigger_declined = 1;
        state->execution = SSVM_FINISHED;
        if( srv->trigger_dispatch_depth == 0 )
            ToriRSServer_Say(srv, "nothing_interesting_message", NULL);
        return 1;

    case SS_OP_LAST_SUBOP:
        SSVM_PushInt(state, player->last_subop);
        return 1;
    case SS_OP_LAST_TARGETSLOT:
        SSVM_PushInt(state, player->last_targetslot);
        return 1;
    case SS_OP_LAST_ITEM:
        SSVM_PushInt(state, player->last_item);
        return 1;

    /*
     * The other half of a use-on. `last_item` is the trigger's subject and these
     * are the item it was used *with* — for `[oplocu]`/`[opnpcu]`/`[opobju]` the
     * subject is not an item at all, so this is the only way in.
     */
    case SS_OP_LAST_USEITEM:
        SSVM_PushInt(state, player->last_useitem);
        return 1;
    case SS_OP_LAST_USESLOT:
        SSVM_PushInt(state, player->last_useslot);
        return 1;

    case SS_OP_DISPLAYNAME:
        SSVM_PushStr(state, player->display_name[0] ? player->display_name : "Player");
        return 1;

    /*
     * `p_stopaction` ends whatever the player was doing before the script's own
     * effect lands. Combat is the only standing action the mock models — the
     * walk queue is not one, because a click that starts a script has already
     * replaced it — so that is the whole implementation rather than a partial
     * one.
     */
    case SS_OP_P_STOPACTION:
        ToriRSServer_CombatStopPlayer(srv);
        return 1;

    case SS_OP_P_LOCMERGE:
    {
        /* LostCity PlayerOps: pop start, end, se, nw; mergeLoc(loc, player,
         * start, end, se.z, se.x, nw.z, nw.x) → wire offsets from loc tile. */
        int32_t start_cycle;
        int32_t end_cycle;
        int32_t se;
        int32_t nw;
        struct ToriRSServerSceneLoc* loc = script_active_loc(state);
        int east;
        int south;
        int west;
        int north;

        if( !SSVM_PopInt(state, &nw) )
            return 1;
        if( !SSVM_PopInt(state, &se) )
            return 1;
        if( !SSVM_PopInt(state, &end_cycle) )
            return 1;
        if( !SSVM_PopInt(state, &start_cycle) )
            return 1;
        if( !loc )
        {
            SSVM_Abort(state, "p_locmerge with no active loc");
            return 1;
        }
        east = coord_x(se) - loc->x;
        south = coord_z(se) - loc->z;
        west = coord_x(nw) - loc->x;
        north = coord_z(nw) - loc->z;
        ToriRSServer_ZoneLocMerge(
            srv, loc->x, loc->z, loc->level, loc->shape, loc->angle, loc->loc_id,
            (int)start_cycle, (int)end_cycle, player->pid, east, south, west, north);
        return 1;
    }

    /*
     * `p_exactmove(start, end, start_cycle, end_cycle, direction)` —
     * LostCity `PlayerOps.ts:939`:
     *
     *     state.activePlayer.unsetMapFlag();
     *     state.activePlayer.exactMove(startPos.x, startPos.z, endPos.x,
     *                                  endPos.z, startCycle, endCycle, direction);
     *
     * and `Player.exactMove` (`Player.ts:2109`) *teleports the player to the
     * END tile first*, then records the window and raises the EXACT_MOVE mask.
     * That order is the whole opcode: the player is already standing at the
     * destination as far as the server, collision and every other client are
     * concerned, and the glide the observer sees is purely the extended-info
     * block replaying the two tiles over the cycle window. A version that
     * moved the player when the animation ended would desync anything that
     * asked where they were mid-swing.
     *
     * `this.teleport(endX, endZ, this.level)` — the reference passes its own
     * level, not the coord's, so an exactmove never changes plane and none of
     * `p_teleport`'s plane bookkeeping applies here.
     *
     * The other half of the pair is `p_locmerge` above; content calls both
     * from `[proc,agility_exactmove]`, which is every agility obstacle.
     */
    case SS_OP_P_EXACTMOVE:
    {
        int32_t start;
        int32_t end;
        int32_t start_cycle;
        int32_t end_cycle;
        int32_t direction;

        if( !SSVM_PopInt(state, &direction) )
            return 1;
        if( !SSVM_PopInt(state, &end_cycle) )
            return 1;
        if( !SSVM_PopInt(state, &start_cycle) )
            return 1;
        if( !SSVM_PopInt(state, &end) )
            return 1;
        if( !SSVM_PopInt(state, &start) )
            return 1;

        /* `unsetMapFlag()` = clearWaypoints + the UnsetMapFlag packet. Same
         * three lines the same-tile arm of `p_walk` above uses. */
        ToriRSServer_WorldStepsClear(player);
        player->dest_x = -1;
        player->dest_z = -1;
        player->clear_map_flag = 1;

        player->exact_start_x = coord_x(start);
        player->exact_start_z = coord_z(start);
        player->exact_end_x = coord_x(end);
        player->exact_end_z = coord_z(end);
        player->exact_start_cycle = (int)start_cycle;
        player->exact_end_cycle = (int)end_cycle;
        player->exact_direction = (int)direction;

        player->x = player->exact_end_x;
        player->z = player->exact_end_z;
        player->place_dirty = 1;
        player->waypoint_index = -1;

        player->masks |= TORIRSSERVER_PMASK_EXACT_MOVE;
        return 1;
    }

    /*
     * The overhead icons, as an int content owns outright.
     *
     * This is the reference's whole prayer surface in its engine: `Player.ts`
     * has a `headicons: number` field, `PlayerOps.ts` has a get and a set, and
     * nothing anywhere in it knows what a prayer is. Which icon a prayer draws,
     * and when, is `~headicon_add`/`~headicon_del` in content.
     *
     * The write marks the appearance block, because that is where the byte
     * rides — turning on Protect from Melee is an appearance change like
     * putting on a helmet, and every client that can see the player learns
     * about it through the PLAYER_INFO they were getting anyway.
     */
    case SS_OP_HEADICONS_GET:
        SSVM_PushInt(state, player->headicons);
        return 1;

    case SS_OP_HEADICONS_SET:
    {
        int32_t value;

        if( !SSVM_PopInt(state, &value) )
            return 1;
        if( player->headicons != (int)value )
        {
            player->headicons = (int)value;
            player->masks |= TORIRSSERVER_PMASK_APPEARANCE;
        }
        return 1;
    }

    case SS_OP_P_TRANSMOGRIFY:
    {
        int32_t npc;

        if( !SSVM_PopInt(state, &npc) )
            return 1;
        if( player->transmog_npc != (int)npc )
        {
            player->transmog_npc = (int)npc;
            player->masks |= TORIRSSERVER_PMASK_APPEARANCE;
        }
        return 1;
    }

    default:
        /* Not ours. The VM reports it through the loud stub, which pops and
         * pushes what the signature declares so the script survives. */
        return 0;
    }
}

/**
 * Release a p_countdialog wait with the number the client sent.
 *
 * Separate from ToriRSServer_ScriptsResumeButton because the two waits are
 * released by different packets and neither may release the other — a click
 * arriving while a count dialog is up must leave the script parked.
 */
int
ToriRSServer_ScriptsResumeCountdialog(
    struct ToriRSServer* srv,
    int32_t value)
{
    struct SSVM_State* state = srv->active_player->active_script;

    if( !srv->scripts_ok || !state )
        return 0;
    if( state->execution != SSVM_COUNTDIALOG )
        return 0;
    srv->active_player->last_int = value;
    state->last_int = value;
    rebind_active_npc(srv, state);
    return run_or_park(srv, state);
}

/** Release a p_namedialog wait with a bounded copy of the client's reply. */
int
ToriRSServer_ScriptsResumeNamedialog(
    struct ToriRSServer* srv,
    const uint8_t* text,
    int len)
{
    struct SSVM_State* state = srv->active_player->active_script;
    char* copy;

    if( !srv->scripts_ok || !state || state->execution != SSVM_NAMEDIALOG ||
        !text || len < 0 )
        return 0;
    copy = SSVM_StrPoolDupLen(&state->pool, (const char*)text, (size_t)len);
    if( !copy )
    {
        SSVM_Abort(state, "namedialog reply exhausted the string pool");
        return run_or_park(srv, state);
    }
    state->last_string = copy;
    rebind_active_npc(srv, state);
    return run_or_park(srv, state);
}

/*
 * Say a content-owned message.
 *
 * A thin wrapper over run_proc/run_proc_sv, because the alternative at twenty
 * call sites is twenty four-line blocks declaring an argument array. `name` is
 * the bare proc name — "unequip_message", not "[proc,unequip_message]" — since
 * every caller of this is naming a message and the brackets are noise.
 *
 * Silent when the script is missing: a server with no content tree says nothing
 * rather than falling back to a second copy of the text in C, which is the
 * arrangement that let the two disagree in the first place.
 */
void
ToriRSServer_Say(
    struct ToriRSServer* srv,
    const char* name,
    const char* arg)
{
    char qualified[128];

    snprintf(qualified, sizeof(qualified), "[proc,%s]", name);
    if( arg )
        ToriRSServer_ScriptsRunProcSv(srv, qualified, NULL, 0, &arg, 1);
    else
        ToriRSServer_ScriptsRunProc(srv, qualified, NULL, 0);
}

/*
 * Run a named proc with an npc made active.
 *
 * The combat swing needs it: `[proc,player_melee_swing]` calls `npc_stat`,
 * `npc_param` and `npc_damage`, all of which resolve against the active npc, so
 * the engine names the target once here rather than passing a slot number
 * content would then have to carry through four procs.
 *
 * `host_tag` is the slot, stored +1 so zero means "no npc" without a second
 * flag; the entity pointer only satisfies the VM's require-an-active-npc check.
 * Both are needed — see ToriRSServer_ScriptsRunTrigger, which does the same pair.
 */
int
ToriRSServer_ScriptsRunHookOnNpc(
    struct ToriRSServer* srv,
    const struct SSVM_Script* script,
    int npc_slot)
{
    struct SSVM_State* state;

    if( !srv->scripts_ok )
        return 0;
    assert(script);
    state = SSVM_StateAlloc(srv->script_env, script, NULL, 0, NULL, 0);
    if( state && srv->active_player )
        state->last_int = srv->active_player->last_int;
    if( !state )
        return 0;
    SSVM_SetActive(state, SSVM_ENT_PLAYER, SSVM_PRIMARY, srv->active_player);
    SSVM_PointerAdd(state, SSVM_PTR_PROTECTED_PLAYER);
    if( npc_slot >= 0 && npc_slot < TORIRSSERVER_NPC_MAX && srv->npcs[npc_slot].active )
    {
        SSVM_SetActive(state, SSVM_ENT_NPC, SSVM_PRIMARY, &srv->npcs[npc_slot]);
        state->host_tag = npc_slot + 1;
    }
    return run_or_park(srv, state);
}

/** By name, for tests. See ToriRSServer_ScriptsRunProcSv on why the split. */
int
ToriRSServer_ScriptsRunProcOnNpc(
    struct ToriRSServer* srv,
    const char* name,
    int npc_slot)
{
    const struct SSVM_Script* script;

    if( !srv->scripts_ok )
        return 0;
    script = SSVM_ProviderGetByName(srv->scripts, name);
    if( !script )
    {
        if( srv->verbose )
            printf("torirsserver: no %s — engine fallback\n", name);
        return 0;
    }
    return ToriRSServer_ScriptsRunHookOnNpc(srv, script, npc_slot);
}

int
ToriRSServer_ScriptsRunProcArgsOnNpc(
    struct ToriRSServer* srv,
    const char* name,
    int npc_slot,
    const int32_t* args,
    int argc)
{
    const struct SSVM_Script* script;
    struct SSVM_State* state;

    if( !srv->scripts_ok )
        return 0;
    script = SSVM_ProviderGetByName(srv->scripts, name);
    if( !script )
        return 0;
    state = SSVM_StateAlloc(srv->script_env, script, args, argc, NULL, 0);
    if( !state )
        return 0;
    SSVM_SetActive(state, SSVM_ENT_PLAYER, SSVM_PRIMARY, srv->active_player);
    SSVM_PointerAdd(state, SSVM_PTR_PROTECTED_PLAYER);
    if( npc_slot < 0 || npc_slot >= TORIRSSERVER_NPC_MAX || !srv->npcs[npc_slot].active )
    {
        SSVM_StateRelease(state);
        return 0;
    }
    SSVM_SetActive(state, SSVM_ENT_NPC, SSVM_PRIMARY, &srv->npcs[npc_slot]);
    state->host_tag = npc_slot + 1;
    return run_or_park(srv, state);
}

int
ToriRSServer_ScriptsRunProcIntOnNpc(
    struct ToriRSServer* srv,
    const char* name,
    int npc_slot,
    const int32_t* args,
    int argc,
    int32_t* out)
{
    const struct SSVM_Script* script;
    struct SSVM_State* state;
    enum SSVM_Exec status;

    if( !srv->scripts_ok || !out )
        return 0;
    script = SSVM_ProviderGetByName(srv->scripts, name);
    if( !script )
        return 0;
    state = SSVM_StateAlloc(srv->script_env, script, args, argc, NULL, 0);
    if( !state )
        return 0;
    SSVM_SetActive(state, SSVM_ENT_PLAYER, SSVM_PRIMARY, srv->active_player);
    SSVM_PointerAdd(state, SSVM_PTR_PROTECTED_PLAYER);
    if( npc_slot < 0 || npc_slot >= TORIRSSERVER_NPC_MAX || !srv->npcs[npc_slot].active )
    {
        SSVM_StateRelease(state);
        return 0;
    }
    SSVM_SetActive(state, SSVM_ENT_NPC, SSVM_PRIMARY, &srv->npcs[npc_slot]);
    state->host_tag = npc_slot + 1;
    status = SSVM_Execute(state);
    if( status == SSVM_ABORTED )
        fprintf(stderr, "torirsserver: %s", SSVM_Backtrace(state));
    if( status != SSVM_FINISHED || state->isp < 1 )
    {
        SSVM_StateRelease(state);
        return 0;
    }
    *out = state->int_stack[state->isp - 1];
    SSVM_StateRelease(state);
    return 1;
}
