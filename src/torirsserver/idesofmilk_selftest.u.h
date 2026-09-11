/* The Ides of Milk Gate D C-walk.
 * Worker-branch only. Do not merge this .u.h onto parent v3.
 *
 * Proves: offer parks on chatmenu:options; refuse row 2 leaves %cowquest==0;
 * accept writes 3; ::iomrun reaches 22. Godmode on.
 */
{
    int loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    int cassius_type;
    int chatmenu;
    int cowquest;
    int slot = -1;
    int offer_parked = 0;
    int refuse_zero = 0;
    int accept_three = 0;
    int iomrun_ok = 0;
    static struct ToriRSServerCapture iom_capture;

    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());

    fprintf(stderr, "ToriRSServer selftest: The Ides of Milk (IOM_ONLY)\n");
    SELFTEST_CHECK(loaded, "IOM_ONLY loads a compiled script pack");
    if( !loaded )
    {
        fprintf(stderr, "  SKIP  no compiled script pack\n");
    }
    else
    {
        player->godmode = 1;

        cassius_type = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "cowboss_farmer");
        chatmenu = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatmenu:options");
        cowquest = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "cowquest");
        SELFTEST_CHECK(cassius_type > 0, "npc cowboss_farmer (Cassius) should resolve");
        SELFTEST_CHECK(chatmenu > 0, "chatmenu:options should resolve");
        SELFTEST_CHECK(cowquest >= 0, "varbit cowquest should resolve");

        selftest_reset_world(srv, player, 402, 402);
        player->godmode = 1;
        ToriRSServer_VarbitSet(srv, cowquest, 0);

        slot = ToriRSServer_WorldNpcSpawn(srv, cassius_type, player->x + 1, player->z,
                                          player->level);
        SELFTEST_CHECK(slot >= 0, "Cassius should be spawnable");
        SELFTEST_CHECK(
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, cassius_type, -1, slot) ==
                TORIRSSERVER_TRIGGER_RAN,
            "[opnpc1,cowboss_farmer] should run");
        SELFTEST_CHECK(player->active_script != NULL,
                       "Cassius intro should park on dialogue");

        {
            int pages = 0;

            while( pages < 40 && player->active_script )
            {
                if( player->resume_button_count > 0 && player->resume_buttons[0] == chatmenu )
                    break;
                if( player->resume_button_count <= 0 )
                    break;
                selftest_click_through(srv, 1);
                pages++;
            }
        }
        offer_parked = player->active_script != NULL && player->resume_button_count == 1 &&
                       player->resume_buttons[0] == chatmenu;
        SELFTEST_CHECK(offer_parked,
                       "offer should park on chatmenu:options (Yes./No.)");

        /* Refuse: row 2. Must not write %cowquest. */
        if( offer_parked )
        {
            uint8_t button[6];

            button[0] = (uint8_t)(chatmenu >> 24);
            button[1] = (uint8_t)(chatmenu >> 16);
            button[2] = (uint8_t)(chatmenu >> 8);
            button[3] = (uint8_t)chatmenu;
            button[4] = 0;
            button[5] = 2;
            selftest_handle(player, PKTOUT_NAME_IF_BUTTON1, button, sizeof(button));
            selftest_click_through(srv, 8);
        }
        refuse_zero = ToriRSServer_VarbitGet(player, cowquest) == 0;
        SELFTEST_CHECK(refuse_zero, "refuse row 2 must leave %%cowquest==0, got %d",
                       ToriRSServer_VarbitGet(player, cowquest));

        player->active_script = NULL;
        ToriRSServer_VarbitSet(srv, cowquest, 0);

        /* Accept: talk again, pick Yes. (row 1). Writes 3. */
        SELFTEST_CHECK(
            ToriRSServer_ScriptsRunTrigger(srv, SS_TRIGGER_OPNPC1, cassius_type, -1, slot) ==
                TORIRSSERVER_TRIGGER_RAN,
            "[opnpc1,cowboss_farmer] accept talk should run");
        {
            int pages = 0;

            while( pages < 40 && player->active_script )
            {
                if( player->resume_button_count > 0 && player->resume_buttons[0] == chatmenu )
                    break;
                if( player->resume_button_count <= 0 )
                    break;
                selftest_click_through(srv, 1);
                pages++;
            }
        }
        SELFTEST_CHECK(
            player->active_script != NULL && player->resume_button_count == 1 &&
                player->resume_buttons[0] == chatmenu,
            "accept offer should park on chatmenu:options");
        if( player->active_script && player->resume_button_count > 0 &&
            player->resume_buttons[0] == chatmenu )
        {
            uint8_t button[6];

            button[0] = (uint8_t)(chatmenu >> 24);
            button[1] = (uint8_t)(chatmenu >> 16);
            button[2] = (uint8_t)(chatmenu >> 8);
            button[3] = (uint8_t)chatmenu;
            button[4] = 0;
            button[5] = 1;
            selftest_handle(player, PKTOUT_NAME_IF_BUTTON1, button, sizeof(button));
            selftest_click_through(srv, 40);
        }
        accept_three = ToriRSServer_VarbitGet(player, cowquest) == 3;
        SELFTEST_CHECK(accept_three, "accept must write %%cowquest==3, got %d",
                       ToriRSServer_VarbitGet(player, cowquest));

        player->active_script = NULL;
        ToriRSServer_CaptureBegin(srv, &iom_capture);
        ToriRSServer_ScriptsRunDebugproc(srv, "iomrun");
        ToriRSServer_CaptureEnd(srv);
        for( int i = ToriRSServer_CaptureFindNamed(&iom_capture, PKT_NAME_MESSAGE_GAME, 0); i >= 0;
             i = ToriRSServer_CaptureFindNamed(&iom_capture, PKT_NAME_MESSAGE_GAME, i + 1) )
        {
            const struct ToriRSServerCapturedPacket* packet = &iom_capture.packets[i];
            const char* text;

            text = selftest_message_text(srv, packet);
            if( !text )
                continue;
            if( strstr(text, "iomrun") == NULL )
                continue;
            fprintf(stderr, "  %s\n", text);
            if( strstr(text, "iomrun OK") != NULL )
                iomrun_ok = 1;
        }
        SELFTEST_CHECK(iomrun_ok, "::iomrun should reach 22 (iomrun OK)");
        SELFTEST_CHECK(ToriRSServer_VarbitGet(player, cowquest) == 22,
                       "::iomrun should leave %%cowquest==22, got %d",
                       ToriRSServer_VarbitGet(player, cowquest));

        player->active_script = NULL;
        player->godmode = 1;
        if( slot >= 0 )
            ToriRSServer_WorldNpcFree(srv, slot);
        ToriRSServer_ScriptsFree(srv);
    }
}
