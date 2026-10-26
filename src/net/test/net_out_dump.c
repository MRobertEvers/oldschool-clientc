/*
 * Every client packet writer (net/net_out.h) at every revision, with seeded
 * arguments and a fixed ISAAC seed, printed as bytes -- one line per
 * (revision, writer, seed).
 *
 *     make -C src net-out-dump > before.txt     (at one commit)
 *     make -C src net-out-dump > after.txt      (at another)
 *     diff before.txt after.txt
 *
 * The writers' output is the protocol's half the server reads, and the only
 * honest test of a change to how they are built is the bytes: this is what a
 * refactor of net_out.c is diffed with. Arguments stay inside what each writer
 * accepts (an op number 1..5, a heading 0..15), so every line is a packet a
 * caller could have sent.
 */
#include "net/net_out.h"
#include "net/rev/gameproto_revisions.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t g_lcg;

static uint32_t
rnd(void)
{
    g_lcg = g_lcg * 1103515245u + 12345u;
    return g_lcg >> 8;
}

static int
r16(void)
{
    return (int)(rnd() & 0x3fff);
}

static int
rcom(void)
{
    return (int)(((rnd() % 900) << 16) | (rnd() % 60));
}

static struct Isaac* g_isaac;

static void
reseed(void)
{
    int seed[4] = { 11, 22, 33, 44 };

    if( g_isaac )
        isaac_free(g_isaac);
    g_isaac = isaac_new(seed, 4);
}

static uint8_t g_buf[8192];

static void
emit(char const* rev, char const* fn, int seed, int n)
{
    printf("%s %s %d: %d", rev, fn, seed, n);
    for( int i = 0; i < n && i < 160; i++ )
        printf(" %02x", g_buf[i]);
    printf("\n");
}

#define RUN(NAME, CALL)                                                                            \
    do                                                                                             \
    {                                                                                              \
        int n_;                                                                                    \
        reseed();                                                                                  \
        n_ = (CALL);                                                                               \
        emit(rev_name, NAME, seed, n_);                                                            \
    } while( 0 )

static void
dump(struct GameProtoRevTable const* rev, char const* rev_name)
{
    static char const* const k_words[] = { "hello", "wield bronze sword", "a b c", "trade me" };
    int const rx[4] = { 3, 2, 1, 0 };
    int const rz[4] = { 0, 1, 1, 2 };

    for( int seed = 1; seed <= 4; seed++ )
    {
        int values[3];
        char const* strings[3] = { "", "s", "" };
        char const* word = k_words[seed - 1];
        int op5;
        int op8;
        int op10;
        int x;
        int z;

        g_lcg = (uint32_t)seed * 2654435761u;
        op5 = 1 + (int)(rnd() % 5);
        op8 = 1 + (int)(rnd() % 8);
        op10 = 1 + (int)(rnd() % 10);
        x = 3000 + r16() % 400;
        z = 3000 + r16() % 400;
        values[0] = (int)rnd() - 0x40000000;
        values[1] = 0;
        values[2] = (int)(rnd() % 1000);

        RUN("no_timeout", net_out_no_timeout(rev, g_isaac, g_buf, sizeof(g_buf)));
        RUN("idle_timer", net_out_idle_timer(rev, g_isaac, g_buf, sizeof(g_buf)));
        RUN("map_build_complete", net_out_map_build_complete(rev, g_isaac, g_buf, sizeof(g_buf)));
        RUN("window_status", net_out_window_status(rev, g_isaac, g_buf, sizeof(g_buf), seed % 3, 765, 503));
        RUN("if_button", net_out_if_button(rev, g_isaac, g_buf, sizeof(g_buf), rcom()));
        RUN("if_script_trigger", net_out_if_script_trigger(rev, g_isaac, g_buf, sizeof(g_buf), (int)rnd(),
                                                           rcom(), r16(), r16(), "isi", values, strings));
        RUN("if_button_op", net_out_if_button_op(rev, g_isaac, g_buf, sizeof(g_buf), op10, rcom(), r16()));
        RUN("if_button_obj_op",
            net_out_if_button_obj_op(rev, g_isaac, g_buf, sizeof(g_buf), op10, rcom(), r16(), r16()));
        RUN("click_world_map", net_out_click_world_map(rev, g_isaac, g_buf, sizeof(g_buf), seed % 4, x, z));
        RUN("resume_pausebutton",
            net_out_resume_pausebutton(rev, g_isaac, g_buf, sizeof(g_buf), rcom(), seed == 2 ? -1 : r16()));
        RUN("close_modal", net_out_close_modal(rev, g_isaac, g_buf, sizeof(g_buf)));
        RUN("resume_countdialog", net_out_resume_countdialog(rev, g_isaac, g_buf, sizeof(g_buf), (int)rnd()));
        RUN("resume_namedialog", net_out_resume_namedialog(rev, g_isaac, g_buf, sizeof(g_buf), word));
        RUN("resume_stringdialog", net_out_resume_stringdialog(rev, g_isaac, g_buf, sizeof(g_buf), word));
        RUN("resume_countdialog_long",
            net_out_resume_countdialog_long(rev, g_isaac, g_buf, sizeof(g_buf), ((int64_t)rnd() << 20) + rnd()));
        RUN("resume_objdialog", net_out_resume_objdialog(rev, g_isaac, g_buf, sizeof(g_buf), r16()));
        RUN("bug_report", net_out_bug_report(rev, g_isaac, g_buf, sizeof(g_buf), word, "steps", seed));
        RUN("send_snapshot", net_out_send_snapshot(rev, g_isaac, g_buf, sizeof(g_buf), word, seed, seed & 1));
        RUN("friendchat_join_leave",
            net_out_friendchat_join_leave(rev, g_isaac, g_buf, sizeof(g_buf), seed == 3 ? NULL : word));
        RUN("friendchat_kick", net_out_friendchat_kick(rev, g_isaac, g_buf, sizeof(g_buf), word));
        RUN("friendchat_setrank", net_out_friendchat_setrank(rev, g_isaac, g_buf, sizeof(g_buf), word, seed));
        RUN("clanchannel_full_request", net_out_clanchannel_full_request(rev, g_isaac, g_buf, sizeof(g_buf), seed));
        RUN("clansettings_full_request",
            net_out_clansettings_full_request(rev, g_isaac, g_buf, sizeof(g_buf), seed));
        RUN("clanchannel_kickuser", net_out_clanchannel_kickuser(rev, g_isaac, g_buf, sizeof(g_buf), seed, r16(), word));
        RUN("affinedclansettings_addbanned_fromchannel",
            net_out_affinedclansettings_addbanned_fromchannel(rev, g_isaac, g_buf, sizeof(g_buf), seed, r16(), word));
        RUN("affinedclansettings_setmuted_fromchannel",
            net_out_affinedclansettings_setmuted_fromchannel(rev, g_isaac, g_buf, sizeof(g_buf), seed, r16(),
                                                             seed & 1, word));
        RUN("tut_clickside", net_out_tut_clickside(rev, g_isaac, g_buf, sizeof(g_buf), seed));
        RUN("set_heading", net_out_set_heading(rev, g_isaac, g_buf, sizeof(g_buf), (int)(rnd() % 16)));
        RUN("move_gameclick",
            net_out_move_gameclick(rev, g_isaac, g_buf, sizeof(g_buf), x, z, rx, rz, 4, seed & 1));
        RUN("move_opclick", net_out_move_opclick(rev, g_isaac, g_buf, sizeof(g_buf), x, z, rx, rz, 4, seed & 1));
        RUN("move_minimapclick",
            net_out_move_minimapclick(rev, g_isaac, g_buf, sizeof(g_buf), x, z, rx, rz, 4, seed & 1, 40, 50,
                                      (int)(rnd() % 2048), 7, 9, r16(), r16(), seed));
        RUN("opheld", net_out_opheld(rev, g_isaac, g_buf, sizeof(g_buf), op5, r16(), seed, rcom()));
        RUN("opheld_subop",
            net_out_opheld_subop(rev, g_isaac, g_buf, sizeof(g_buf), op5, seed, r16(), seed, rcom()));
        RUN("opheldt", net_out_opheldt(rev, g_isaac, g_buf, sizeof(g_buf), r16(), seed, rcom(), rcom()));
        RUN("opheldu",
            net_out_opheldu(rev, g_isaac, g_buf, sizeof(g_buf), r16(), seed, rcom(), r16(), seed + 1, rcom()));
        RUN("inv_button", net_out_inv_button(rev, g_isaac, g_buf, sizeof(g_buf), op5, r16(), seed, rcom()));
        RUN("inv_buttond", net_out_inv_buttond(rev, g_isaac, g_buf, sizeof(g_buf), rcom(), r16(), seed, rcom(),
                                               r16(), seed + 3, seed & 1));
        RUN("oploc", net_out_oploc(rev, g_isaac, g_buf, sizeof(g_buf), op5, x, z, r16()));
        RUN("oploct", net_out_oploct(rev, g_isaac, g_buf, sizeof(g_buf), x, z, r16(), rcom()));
        RUN("oplocu", net_out_oplocu(rev, g_isaac, g_buf, sizeof(g_buf), x, z, r16(), r16(), seed, rcom()));
        RUN("opnpc", net_out_opnpc(rev, g_isaac, g_buf, sizeof(g_buf), op5, r16() % 2047));
        RUN("opnpct", net_out_opnpct(rev, g_isaac, g_buf, sizeof(g_buf), r16() % 2047, rcom()));
        RUN("opnpcu", net_out_opnpcu(rev, g_isaac, g_buf, sizeof(g_buf), r16() % 2047, r16(), seed, rcom()));
        RUN("opobj", net_out_opobj(rev, g_isaac, g_buf, sizeof(g_buf), op5, x, z, r16()));
        RUN("opobjt", net_out_opobjt(rev, g_isaac, g_buf, sizeof(g_buf), x, z, r16(), rcom()));
        RUN("opobju", net_out_opobju(rev, g_isaac, g_buf, sizeof(g_buf), x, z, r16(), r16(), seed, rcom()));
        RUN("opplayer", net_out_opplayer(rev, g_isaac, g_buf, sizeof(g_buf), op8, r16() % 2047));
        RUN("opplayert", net_out_opplayert(rev, g_isaac, g_buf, sizeof(g_buf), r16() % 2047, rcom()));
        RUN("opplayeru", net_out_opplayeru(rev, g_isaac, g_buf, sizeof(g_buf), r16() % 2047, r16(), seed, rcom()));
        RUN("message_public", net_out_message_public(rev, g_isaac, g_buf, sizeof(g_buf), word, seed * 0x101));
        RUN("message_private",
            net_out_message_private(rev, g_isaac, g_buf, sizeof(g_buf), 0x2a5f4be9 + seed, word));
        RUN("client_cheat", net_out_client_cheat(rev, g_isaac, g_buf, sizeof(g_buf), word));
        RUN("chat_setmode", net_out_chat_setmode(rev, g_isaac, g_buf, sizeof(g_buf), seed % 3, 1, 2));
        RUN("friendlist_add", net_out_friendlist_add(rev, g_isaac, g_buf, sizeof(g_buf), 0x2a5f4be9 + seed));
        RUN("friendlist_del", net_out_friendlist_del(rev, g_isaac, g_buf, sizeof(g_buf), 0x2a5f4be9 + seed));
        RUN("ignorelist_add", net_out_ignorelist_add(rev, g_isaac, g_buf, sizeof(g_buf), 0x2a5f4be9 + seed));
        RUN("ignorelist_del", net_out_ignorelist_del(rev, g_isaac, g_buf, sizeof(g_buf), 0x2a5f4be9 + seed));
    }
}

int
main(void)
{
    static char const* const k_revs[] = { "osrs239", "osrs241", "lc254", "lc245_2", "lc289" };
    char name[16];

    for( size_t i = 0; i < sizeof(k_revs) / sizeof(k_revs[0]); i++ )
        dump(GameProtoRev_ByName(k_revs[i]), k_revs[i]);
    for( int n = 227; n <= 241; n++ )
    {
        snprintf(name, sizeof(name), "rsprox%d", n);
        dump(GameProtoRev_ByName(name), name);
    }
    return 0;
}
