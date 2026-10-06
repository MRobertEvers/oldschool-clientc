package com.partyhits.bosses;

import com.partyhits.util.FontTypes;
import net.runelite.api.NpcID;

import java.awt.*;

public class VerzikHandler extends BossHandler
{
    @Override
    protected int getMaxHp()
    {
        int partySize = xpToDamage.getToBPartySize();
        boolean verzikEMP2 = getBossNpc().getId() == NpcID.VERZIK_VITUR_10833;
        boolean verzikEMP3 = getBossNpc().getId() == NpcID.VERZIK_VITUR_10835;

        if (verzikEMP2)
        {
            switch (partySize)
            {
                case 1: return 400;
                case 2: return 760;
                case 3: return 1080;
                case 4: return 1360;
                default: return 1600;
            }
        }
        else if (verzikEMP3)
        {
            switch (partySize)
            {
                case 1: return 600;
                case 2: return 1140;
                case 3: return 1620;
                case 4: return 2040;
                default: return 2400;
            }
        }
        else
        {
            switch (partySize)
            {
                case 4: return 3062;
                case 5: return 3500;
                default: return 2625;
            }
        }
    }

    protected int vertOffset()
    {
        return config.verzikOffset();
    }
    protected int horOffset()
    {
        return config.verzikHorOffset();
    }
    protected FontTypes hpFont()
    {
        return config.verzikFont();
    }
    protected int fontSize()
    {
        return config.verzikSize();
    }
    protected Color textColor()
    {
        return config.verzikColor();
    }
    protected double updateThresh()
    {
        return config.updateVerzikThreshold();
    }
    protected boolean onTickOnly()
    {
        return config.onTickOnlyVerzik();
    }
}
