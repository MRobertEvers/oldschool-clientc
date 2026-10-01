/*
 * Tribal Totem selftest (::totemrun and the combination lock's real
 * [if_button] dispatch). Moved out of torirs_server_world_selftest.c so it can
 * run alone: TORIRSSERVER_SELFTEST_TOTEM_ONLY=1 torirsserver --selftest
 */
static void
selftest_quest_totem(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    fprintf(stderr, "ToriRSServer selftest: ::totemrun\n");
    {
        /*
         * Tribal Totem (2026-08-20 audit). The mansion body was entirely
         * missing -- kangai_mau.rs2's own header said "Totem body
         * (crate/house) deferred", handelmort_traps.varp's said "trap body
         * deferred", and alphabet.enum (landed for the door's letter-wheel
         * interface) had zero consumers anywhere. `[opnpc1,kangai_mau]`/
         * `[opnpc1,ardounge_wizard]` block on dialogue, so `totemrun`
         * mirrors those state transitions (established precedent), but
         * calls three real, `~`-callable procs directly:
         * `~totem_take_label`, `~totem_chest_grant`, and the
         * completion's own item/xp/qp grants. The genuinely novel piece
         * this iteration -- a raw IF3 combination-lock interface, the
         * first of its kind wired in this pass -- is proven for real here,
         * C-side: a real `[if_button,tribal_door:tribalenter]` dispatch
         * via `ToriRSServer_ScriptsRunTrigger`, both wrong and correct
         * combinations, since neither reads the debugproc's missing
         * npc/loc context at all.
         */
        int loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());

        if( !loaded )
            loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());

        if( !loaded )
        {
            fprintf(stderr, "  SKIP  no compiled script pack\n");
        }
        else
        {
            static struct ToriRSServerCapture totemrun_capture;
            int said_ok = 0;
            int said_fail = 0;

            ToriRSServer_CaptureBegin(srv, &totemrun_capture);
            ToriRSServer_ScriptsRunDebugproc(srv, "totemrun");
            ToriRSServer_CaptureEnd(srv);

            for( int i = ToriRSServer_CaptureFindNamed(&totemrun_capture, PKT_NAME_MESSAGE_GAME, 0);
                 i >= 0;
                 i = ToriRSServer_CaptureFindNamed(&totemrun_capture, PKT_NAME_MESSAGE_GAME, i + 1) )
            {
                const struct ToriRSServerCapturedPacket* packet = &totemrun_capture.packets[i];
                const char* text;

                text = selftest_message_text(srv, packet);
                if( !text )
                    continue;
                fprintf(stderr, "  DBG %s\n", text);
                if( strstr(text, "TOTEMRUN OK") != NULL )
                    said_ok = 1;
                if( strstr(text, "TOTEMRUN FAIL") != NULL )
                {
                    said_fail = 1;
                    fprintf(stderr, "  %s\n", text);
                }
            }

            SELFTEST_CHECK(!said_fail, "::totemrun should report no failures");
            SELFTEST_CHECK(said_ok, "::totemrun should reach its OK line");

            /*
             * C-side: the combination-lock door's real [if_button] dispatch.
             * The wheels are the lock's own varbits (totemquest_combodoor_code1..4,
             * quest_totem/configs/totem_combodoor.varp), stepped by the real
             * arrow buttons -- never %if1..%if4, which carry the Slayer
             * assignment (seam21 tribal_totem_lock_clobbers_slayer). ::totemrun
             * leaves the lock open with every wheel at A.
             */
            {
                static char const* const arrow_names[4] = {
                    "tribal_door:tribala_right", /* A -> K: right x10 */
                    "tribal_door:tribalb_left",  /* A -> U: left x6 */
                    "tribal_door:tribalc_left",  /* A -> R: left x9 */
                    "tribal_door:tribald_left",  /* A -> T: left x7 */
                };
                static int const arrow_presses[4] = { 10, 6, 9, 7 };
                int arrows[4];
                int varp_traps = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARP, "varp6195_handelmort_traps_disabled");
                int com_enter = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "tribal_door:tribalenter");
                int resolved = varp_traps >= 0 && com_enter >= 0;

                for( int w = 0; w < 4; w++ )
                {
                    arrows[w] = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, arrow_names[w]);
                    if( arrows[w] < 0 )
                        resolved = 0;
                }
                SELFTEST_CHECK(resolved,
                               "the ::totemrun C-side names should all resolve: traps=%d enter=%d "
                               "arrows=%d/%d/%d/%d",
                               varp_traps, com_enter, arrows[0], arrows[1], arrows[2], arrows[3]);
                if( resolved )
                {
                    /*
                     * `~mesbox`'s text does not surface as a plain
                     * MESSAGE_GAME (opcode 90) packet the way `mes()` does,
                     * so state, not chat text, is the observable signal.
                     */
                    player->varps[varp_traps] = 0;

                    ToriRSServer_ScriptsRunIfButton(srv, com_enter, 1);

                    SELFTEST_CHECK((player->varps[varp_traps] & (1 << 0)) == 0,
                                   "A/A/A/A should not set the door-solved bit, got %d",
                                   player->varps[varp_traps]);

                    /* Enter closes the lock on every press, so open it afresh
                     * (wheels back to A) before dialling KURT. */
                    ToriRSServer_ScriptsRunDebugproc(srv, "totemopenlock");

                    for( int w = 0; w < 4; w++ )
                        for( int k = 0; k < arrow_presses[w]; k++ )
                            ToriRSServer_ScriptsRunIfButton(srv, arrows[w], 1);

                    ToriRSServer_ScriptsRunIfButton(srv, com_enter, 1);

                    SELFTEST_CHECK((player->varps[varp_traps] & (1 << 0)) != 0,
                                   "K/U/R/T dialled on the arrows should set the door-solved bit, got %d",
                                   player->varps[varp_traps]);
                }
            }
            ToriRSServer_ScriptsFree(srv);
        }
    }
}
