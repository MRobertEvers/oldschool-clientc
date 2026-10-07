/*
 * The bot runner: a party of sessionless players driven by an agent process,
 * on the real server, at CPU speed.
 *
 * WHY THIS EXISTS
 * ---------------
 * The owner, 2026-10-07: "we just need to iterate faster ... the real
 * automation runner doesn't need to click, it should just send commands."
 * A raid test was three full clients in lockstep at about real speed: a
 * Verzik fight is ~1,100 ticks, ten minutes a name, and a tactic took a
 * survey of three names to judge. Nothing in that loop needs a client. The
 * fight is the server's scripts; the player is a stream of packets.
 *
 * So here the players have no session and no client. Each tick the agent is
 * handed what the tick did -- the server's own ticklog rows, the same rows the
 * quest driver reads from the embedded server -- plus each bot's inventory and
 * gear, and answers with commands. A command becomes the packet a client would
 * have sent and goes through ToriRSServer_WorldHandle, the one door every
 * client packet takes, so content cannot tell a bot from a player: the scene
 * barrier, the action lock, the stun, the op triggers all apply.
 *
 * THE PROTOCOL (text, tab separated, one line per record)
 * -------------------------------------------------------
 * runner -> agent, once per tick, after the world ticked:
 *     tick <n>
 *     row <serial> <tick> <kind> <a> <b> <c> <d> <e> <f> <label> <g>
 *                                     (ticklog.tsv's columns, every new row)
 *     npcsize <slot> <size>           (after each npc_spawn / npc_retype row)
 *     msg <pid> <text>                (every game message to a bot)
 *     self <pid> <x> <z> <level> <inv slot:obj:count,...> <worn slot:obj,...>
 *          <mainmodal group> <chatmodal group>    (0: none)
 *     end
 * agent -> runner, before the next tick:
 *     <pid> walk <x> <z> [ctrl]       (ctrl 1: the ctrl-click that turns run on)
 *     <pid> opnpc <op> <world npc slot>
 *     <pid> opheld <op> <obj> <inv slot>
 *     <pid> opobj <op> <x> <z> <obj>   (a ground item: op 3 is Take)
 *     <pid> button <component uid>
 *     <pid> resume <component uid>
 *     <pid> cheat <text without ::>
 *     <pid> collision <x0> <z0> <w> <h>   a query, not a packet: the next tick's
 *                                      stream carries "coll <x0> <z0> <w> <h>
 *                                      <w*h of 0/1, z-major>" (1: walk-blocked,
 *                                      the scene's static collision) -- the floor
 *                                      as the server walks it, not as a policy
 *                                      guessed it (a Verzik pool landed at x 44
 *                                      of a floor the policy had ending at 41)
 *     <pid> close                     (the client's CLOSE_MODAL: a modal left open
 *                                      blocks every normal queue -- the Verzik
 *                                      P1 bolt lands through one)
 *     done                            (or `quit` to stop the run)
 * A command answered after tick n is handled on tick n + 1, the tick a click
 * made while watching tick n reaches the server.
 *
 * USAGE
 *   torirsserver --botrun --bots 3 --agent "lua tools/raid_agent/run.lua" \
 *       [--name <run name>] [--ticks N] [--ticklog path] [--realtime]
 * --name seeds the npc stream as TORIRSSERVER_RUN_NAME does for a quest run
 * and names the bots <name>1..N. --realtime paces ticks at 600 ms so a client
 * logged in beside them can watch.
 *
 * REPLAY.  The world is seeded by the run name and the bots' only input is
 * the agent's lines, so a run is those lines.  --record <file> writes each
 * one as "<tick>\t<line>"; --replay <file> feeds them back on their ticks
 * with no agent at all.  Same --name, same --bots, same tree: the same run,
 * which a ticklog diff against the original proves (any difference is a
 * nondeterminism to find, not noise).  Seeking is re-running: ~2 ms a tick.
 */

#include "torirs_server.h"
#include "torirs_server_boot.h"
#include "net/rev/pktnames.h"

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define BOTRUN_MAX 8
#define BOTRUN_ROWS 512
#define BOTRUN_LINE 1024
/* The world's tick, as torirs_server_host.c paces it. */
#define BOTRUN_TICK_MS 600

struct BotRun
{
    struct ToriRSServer* srv;
    int count;
    struct ToriRSServerPlayer* bots[BOTRUN_MAX];
    FILE* to_agent;
    FILE* from_agent;
    pid_t agent_pid;
    uint32_t cursor;
    /* The tick's file-only rows (raider, input, consume), from the side sink. */
    struct ToriRSServerTicklogRow* side;
    int side_count;
    int side_capacity;
    /* The tick's game messages to bots, as "msg\t<pid>\t<text>\n" lines. */
    char* msgs;
    size_t msgs_len;
    size_t msgs_capacity;
    int commands;
    int refused;
    /* collision queries answered on the next tick's stream */
    struct
    {
        int level, x0, z0, w, h;
    } queries[8];
    int query_count;
    FILE* record;
    FILE* replay;
    /* The replay's next line, read ahead: its tick says when it is due. */
    char replay_line[BOTRUN_LINE];
    int replay_tick;
};

static int
botrun_spawn(
    struct BotRun* run,
    const char* command)
{
    int down[2];
    int up[2];

    assert(run);
    assert(command);
    if( pipe(down) != 0 || pipe(up) != 0 )
    {
        perror("botrun: pipe");
        return -1;
    }
    run->agent_pid = fork();
    if( run->agent_pid < 0 )
    {
        perror("botrun: fork");
        return -1;
    }
    if( run->agent_pid == 0 )
    {
        dup2(down[0], 0);
        dup2(up[1], 1);
        close(down[1]);
        close(up[0]);
        execl("/bin/sh", "sh", "-c", command, (char*)NULL);
        _exit(127);
    }
    close(down[0]);
    close(up[1]);
    run->to_agent = fdopen(down[1], "w");
    run->from_agent = fdopen(up[0], "r");
    assert(run->to_agent);
    assert(run->from_agent);
    return 0;
}

static void
put2(uint8_t* out, int v)
{
    out[0] = (uint8_t)((v >> 8) & 0xff);
    out[1] = (uint8_t)(v & 0xff);
}

static void
put4(uint8_t* out, int v)
{
    out[0] = (uint8_t)((v >> 24) & 0xff);
    out[1] = (uint8_t)((v >> 16) & 0xff);
    out[2] = (uint8_t)((v >> 8) & 0xff);
    out[3] = (uint8_t)(v & 0xff);
}

/* A bot has no client to answer the scene barrier, so it answers at once: the
 * world it would load is the world it is already in. Without this every
 * packet after a login or a rebuild is dropped behind the barrier. */
static void
botrun_scene_ack(struct ToriRSServerPlayer* player)
{
    assert(player);
    if( player->active && (player->login_scene_pending || player->rebuild_scene_pending) )
        ToriRSServer_WorldHandle(player, PKTOUT_NAME_MAP_BUILD_COMPLETE, NULL, 0);
}

static struct ToriRSServerPlayer*
botrun_bot(
    struct BotRun* run,
    int pid)
{
    for( int i = 0; i < run->count; i++ )
    {
        if( run->bots[i] && run->bots[i]->pid == pid )
            return run->bots[i];
    }
    return NULL;
}

/* One agent line -> one packet through the client's door. Returns 1 for
 * `done`, 2 for `quit`, 0 otherwise. */
static int
botrun_command(
    struct BotRun* run,
    char* line)
{
    char* fields[8];
    int n = 0;
    char* save = NULL;
    struct ToriRSServerPlayer* player;
    uint8_t payload[160];
    const char* verb;

    line[strcspn(line, "\r\n")] = '\0';
    if( strcmp(line, "done") == 0 )
        return 1;
    if( strcmp(line, "quit") == 0 )
        return 2;
    if( line[0] == '\0' || line[0] == '#' )
        return 0;
    for( char* tok = strtok_r(line, "\t", &save); tok && n < 8; tok = strtok_r(NULL, "\t", &save) )
        fields[n++] = tok;
    if( n < 2 )
    {
        fprintf(stderr, "botrun: agent line has no verb: %s\n", line);
        run->refused++;
        return 0;
    }
    player = botrun_bot(run, atoi(fields[0]));
    verb = fields[1];
    if( !player || !player->active )
    {
        fprintf(stderr, "botrun: agent named pid %s, not a live bot\n", fields[0]);
        run->refused++;
        return 0;
    }
    botrun_scene_ack(player);
    run->commands++;
    if( strcmp(verb, "walk") == 0 && n >= 4 )
    {
        payload[0] = (uint8_t)(n >= 5 ? atoi(fields[4]) : 0);
        put2(payload + 1, atoi(fields[2]));
        put2(payload + 3, atoi(fields[3]));
        ToriRSServer_WorldHandle(player, PKTOUT_NAME_MOVE_GAMECLICK, payload, 5);
    }
    else if( strcmp(verb, "opnpc") == 0 && n >= 4 )
    {
        int op = atoi(fields[2]);
        int client_slot = ToriRSServer_SlotMapAcquire(player, atoi(fields[3]));

        if( op < 1 || op > 5 || client_slot < 0 )
        {
            run->refused++;
            return 0;
        }
        put2(payload, client_slot);
        payload[2] = 0;
        ToriRSServer_WorldHandle(player, PKTOUT_NAME_OPNPC1 + (op - 1), payload, 3);
    }
    else if( strcmp(verb, "opheld") == 0 && n >= 5 )
    {
        int op = atoi(fields[2]);

        if( op < 1 || op > 5 )
        {
            run->refused++;
            return 0;
        }
        put2(payload, atoi(fields[3]));
        put2(payload + 2, atoi(fields[4]));
        put4(payload + 4, (149 << 16) | 0);
        ToriRSServer_WorldHandle(player, PKTOUT_NAME_OPHELD1 + (op - 1), payload, 8);
    }
    else if( strcmp(verb, "opobj") == 0 && n >= 6 )
    {
        int op = atoi(fields[2]);

        if( op < 1 || op > 5 )
        {
            run->refused++;
            return 0;
        }
        put2(payload, atoi(fields[3]));
        put2(payload + 2, atoi(fields[4]));
        put2(payload + 4, atoi(fields[5]));
        payload[6] = 0;
        ToriRSServer_WorldHandle(player, PKTOUT_NAME_OPOBJ1 + (op - 1), payload, 7);
    }
    else if( strcmp(verb, "button") == 0 && n >= 3 )
    {
        put4(payload, (int)strtol(fields[2], NULL, 0));
        ToriRSServer_WorldHandle(player, PKTOUT_NAME_IF_BUTTON, payload, 4);
    }
    else if( strcmp(verb, "resume") == 0 && n >= 3 )
    {
        put4(payload, (int)strtol(fields[2], NULL, 0));
        put2(payload + 4, -1);
        ToriRSServer_WorldHandle(player, PKTOUT_NAME_RESUME_PAUSEBUTTON, payload, 6);
    }
    else if( strcmp(verb, "collision") == 0 && n >= 6 )
    {
        run->commands--;
        if( run->query_count < 8 )
        {
            run->queries[run->query_count].level = player->level;
            run->queries[run->query_count].x0 = atoi(fields[2]);
            run->queries[run->query_count].z0 = atoi(fields[3]);
            run->queries[run->query_count].w = atoi(fields[4]);
            run->queries[run->query_count].h = atoi(fields[5]);
            run->query_count++;
        }
    }
    else if( strcmp(verb, "close") == 0 )
    {
        ToriRSServer_WorldHandle(player, PKTOUT_NAME_CLOSE_MODAL, NULL, 0);
    }
    else if( strcmp(verb, "cheat") == 0 && n >= 3 )
    {
        int len = snprintf((char*)payload, sizeof(payload), "%s\n", fields[2]);

        if( len >= (int)sizeof(payload) )
        {
            run->refused++;
            return 0;
        }
        ToriRSServer_WorldHandle(player, PKTOUT_NAME_CLIENT_CHEAT, payload, len);
    }
    else
    {
        fprintf(stderr, "botrun: unknown agent command '%s' (%d fields)\n", verb, n);
        run->refused++;
        run->commands--;
    }
    return 0;
}

static void
botrun_side_row(
    const struct ToriRSServerTicklogRow* row,
    void* ctx)
{
    struct BotRun* run = ctx;

    assert(row);
    assert(run);
    if( run->side_count == run->side_capacity )
    {
        run->side_capacity = run->side_capacity ? run->side_capacity * 2 : 64;
        run->side = realloc(run->side, (size_t)run->side_capacity * sizeof(*run->side));
        assert(run->side);
    }
    run->side[run->side_count++] = *row;
}

static void
botrun_message(
    const struct ToriRSServerPlayer* player,
    const char* text,
    void* ctx)
{
    struct BotRun* run = ctx;
    char line[320];
    int n;

    assert(player);
    assert(text);
    assert(run);
    n = snprintf(line, sizeof(line), "msg\t%d\t", player->pid);
    for( const char* s = text; *s && n < (int)sizeof(line) - 2; s++ )
        line[n++] = (*s == '\t' || *s == '\n' || *s == '\r') ? ' ' : *s;
    line[n++] = '\n';
    if( run->msgs_len + (size_t)n > run->msgs_capacity )
    {
        run->msgs_capacity = (run->msgs_capacity + (size_t)n) * 2;
        run->msgs = realloc(run->msgs, run->msgs_capacity);
        assert(run->msgs);
    }
    memcpy(run->msgs + run->msgs_len, line, (size_t)n);
    run->msgs_len += (size_t)n;
}

static void
botrun_put_row(
    FILE* out,
    const struct ToriRSServerTicklogRow* r)
{
    fprintf(out, "row\t%u\t%d\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%s\t%d\n", r->serial, r->tick,
            ToriRSServer_TicklogKindName(r->kind), r->a, r->b, r->c, r->d, r->e, r->f, r->label,
            r->g);
}

static void
botrun_send_tick(struct BotRun* run)
{
    struct ToriRSServerTicklogRow rows[BOTRUN_ROWS];
    FILE* out = run->to_agent;
    int got;

    fprintf(out, "tick\t%d\n", (int)run->srv->tick);
    do
    {
        got = ToriRSServer_TicklogRead(run->cursor, rows, BOTRUN_ROWS);
        for( int i = 0; i < got; i++ )
        {
            botrun_put_row(out, &rows[i]);
            run->cursor = rows[i].serial;
            /* A spawn or a retype says what the npc is, never how big: an
             * npc that never walks has no NPC_TILE row to carry its size. */
            if( (rows[i].kind == TORIRSSERVER_TICKLOG_NPC_SPAWN ||
                 rows[i].kind == TORIRSSERVER_TICKLOG_NPC_RETYPE) &&
                rows[i].a >= 0 && rows[i].a < TORIRSSERVER_NPC_MAX )
                fprintf(out, "npcsize\t%d\t%d\n", rows[i].a, run->srv->npcs[rows[i].a].size);
        }
    } while( got == BOTRUN_ROWS );
    for( int q = 0; q < run->query_count; q++ )
    {
        int w = run->queries[q].w;
        int h = run->queries[q].h;

        if( w < 1 || h < 1 || w > 128 || h > 128 )
            continue;
        fprintf(out, "coll\t%d\t%d\t%d\t%d\t", run->queries[q].x0, run->queries[q].z0, w, h);
        for( int dz = 0; dz < h; dz++ )
        {
            for( int dx = 0; dx < w; dx++ )
                fputc(ToriRSServer_SceneWalkBlocked(run->queries[q].level, run->queries[q].x0 + dx,
                                                    run->queries[q].z0 + dz)
                          ? '1'
                          : '0',
                      out);
        }
        fputc('\n', out);
    }
    run->query_count = 0;
    for( int i = 0; i < run->side_count; i++ )
        botrun_put_row(out, &run->side[i]);
    run->side_count = 0;
    if( run->msgs_len > 0 )
        fwrite(run->msgs, 1, run->msgs_len, out);
    run->msgs_len = 0;
    for( int i = 0; i < run->count; i++ )
    {
        const struct ToriRSServerPlayer* p = run->bots[i];

        if( !p || !p->active )
            continue;
        fprintf(out, "self\t%d\t%d\t%d\t%d\t", p->pid, p->x, p->z, p->level);
        for( int s = 0; s < TORIRSSERVER_INV_SLOTS; s++ )
        {
            if( p->inv[s].obj_id >= 0 && p->inv[s].count > 0 )
                fprintf(out, "%d:%d:%d,", s, p->inv[s].obj_id, p->inv[s].count);
        }
        fputc('\t', out);
        for( int s = 0; s < TORIRSSERVER_WORN_SLOTS; s++ )
        {
            if( p->worn[s].obj_id >= 0 && p->worn[s].count > 0 )
                fprintf(out, "%d:%d,", s, p->worn[s].obj_id);
        }
        fprintf(out, "\t%d\t%d\n", p->mainmodal_group, p->chatmodal_group);
    }
    fputs("end\n", out);
    fflush(out);
}

/* A replay tick: every recorded line for `tick`, then `done` (or `quit`).
 * Returns what botrun_command returned for the last line. */
static int
botrun_replay_tick(
    struct BotRun* run,
    int tick)
{
    for( ;; )
    {
        char* tab;

        if( run->replay_line[0] == '\0' )
        {
            if( !fgets(run->replay_line, sizeof(run->replay_line), run->replay) )
                return 2;
            run->replay_tick = atoi(run->replay_line);
        }
        if( run->replay_tick > tick )
            return 1;
        tab = strchr(run->replay_line, '\t');
        if( tab && run->replay_tick == tick )
        {
            char line[BOTRUN_LINE];
            int verdict;

            snprintf(line, sizeof(line), "%s", tab + 1);
            run->replay_line[0] = '\0';
            verdict = botrun_command(run, line);
            if( verdict != 0 )
                return verdict;
            continue;
        }
        run->replay_line[0] = '\0';
    }
}

static int64_t
botrun_now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int
ToriRSServer_BotRun(
    struct ToriRSServer* srv,
    const struct ToriRSServerBootConfig* config,
    int argc,
    char** argv)
{
    struct BotRun run;
    const char* agent = NULL;
    const char* name = "bot";
    const char* ticklog = NULL;
    const char* record = NULL;
    const char* replay = NULL;
    int bots = 1;
    int ticks_max = 3000;
    int realtime = 0;
    int status = 0;
    int64_t next = 0;
    char line[BOTRUN_LINE];

    assert(srv);
    assert(config);
    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--bots") == 0 && i + 1 < argc )
            bots = atoi(argv[++i]);
        else if( strcmp(argv[i], "--agent") == 0 && i + 1 < argc )
            agent = argv[++i];
        else if( strcmp(argv[i], "--name") == 0 && i + 1 < argc )
            name = argv[++i];
        else if( strcmp(argv[i], "--ticks") == 0 && i + 1 < argc )
            ticks_max = atoi(argv[++i]);
        else if( strcmp(argv[i], "--ticklog") == 0 && i + 1 < argc )
            ticklog = argv[++i];
        else if( strcmp(argv[i], "--realtime") == 0 )
            realtime = 1;
        else if( strcmp(argv[i], "--record") == 0 && i + 1 < argc )
            record = argv[++i];
        else if( strcmp(argv[i], "--replay") == 0 && i + 1 < argc )
            replay = argv[++i];
    }
    if( (!agent && !replay) || (agent && replay) || bots < 1 || bots > BOTRUN_MAX )
    {
        fprintf(stderr, "torirsserver --botrun: needs one of --agent <command> or --replay <file>,"
                        " and --bots 1..%d\n",
                BOTRUN_MAX);
        return 2;
    }

    memset(&run, 0, sizeof(run));
    run.srv = srv;
    run.count = bots;
    setenv("TORIRSSERVER_RUN_NAME", name, 1);
    ToriRSServer_WorldInit(srv, ToriRSServer_BootZone(config->home_x),
                           ToriRSServer_BootZone(config->home_z));
    ToriRSServer_TicklogEnable(srv, ticklog);
    ToriRSServer_TicklogSideSink(botrun_side_row, &run);
    ToriRSServer_MessageSink(botrun_message, &run);
    for( int i = 0; i < bots; i++ )
    {
        char bot_name[32];
        struct ToriRSServerPlayer* player = ToriRSServer_WorldAddPlayer(srv, NULL);

        assert(player);
        snprintf(bot_name, sizeof(bot_name), "%s%d", name, i + 1);
        ToriRSServer_WorldPlayerInit(player);
        ToriRSServer_WorldSetDisplayName(player, bot_name);
        ToriRSServer_WorldLogin(player);
        botrun_scene_ack(player);
        run.bots[i] = player;
        fprintf(stderr, "botrun: %s is pid %d at %d,%d\n", player->display_name, player->pid,
                player->x, player->z);
    }
    if( replay )
    {
        run.replay = fopen(replay, "r");
        if( !run.replay )
        {
            fprintf(stderr, "botrun: cannot read --replay %s\n", replay);
            return 1;
        }
    }
    else if( botrun_spawn(&run, agent) != 0 )
        return 1;
    if( record )
    {
        run.record = fopen(record, "w");
        if( !run.record )
        {
            fprintf(stderr, "botrun: cannot write --record %s\n", record);
            return 1;
        }
    }

    next = botrun_now_ms();
    for( int t = 0; t < ticks_max; t++ )
    {
        int verdict = 0;

        for( int i = 0; i < run.count; i++ )
            botrun_scene_ack(run.bots[i]);
        if( run.replay )
        {
            /* the stream still runs: its side rows and messages must not pile up */
            run.side_count = 0;
            run.msgs_len = 0;
            verdict = botrun_replay_tick(&run, (int)srv->tick);
        }
        else
        {
            botrun_send_tick(&run);
        }
        while( verdict == 0 )
        {
            if( !fgets(line, sizeof(line), run.from_agent) )
            {
                fprintf(stderr, "botrun: the agent closed its output at tick %d\n", (int)srv->tick);
                verdict = 2;
                status = 1;
                break;
            }
            if( run.record )
                fprintf(run.record, "%d\t%s", (int)srv->tick, line);
            verdict = botrun_command(&run, line);
        }
        if( verdict == 2 )
            break;
        if( realtime )
        {
            next += BOTRUN_TICK_MS;
            while( botrun_now_ms() < next )
                usleep(2000);
        }
        ToriRSServer_WorldTick(srv);
    }

    if( run.replay )
    {
        fclose(run.replay);
    }
    else
    {
        fclose(run.to_agent);
        fclose(run.from_agent);
        waitpid(run.agent_pid, NULL, 0);
    }
    if( run.record )
        fclose(run.record);
    fprintf(stderr, "botrun: %d ticks, %d commands, %d refused, ended at tick %d\n",
            (int)srv->tick, run.commands, run.refused, (int)srv->tick);
    ToriRSServer_TicklogSideSink(NULL, NULL);
    ToriRSServer_MessageSink(NULL, NULL);
    free(run.msgs);
    ToriRSServer_TicklogDisable();
    free(run.side);
    return status;
}
