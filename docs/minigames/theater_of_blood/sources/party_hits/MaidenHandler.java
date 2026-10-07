package com.partyhits.bosses;

import com.partyhits.util.FontTypes;
import net.runelite.api.NpcID;

import java.awt.*;

public class MaidenHandler extends BossHandler
{
    @Override
    protected int getMaxHp()
    {
        int partySize = xpToDamage.getToBPartySize();
        boolean maidenEM = getBossNpc().getId() == NpcID.THE_MAIDEN_OF_SUGADINTI_10814;

        if (maidenEM)
        {
            switch (partySize)
            {
                case 1: return 500;
                case 2: return 950;
                case 3: return 1350;
                case 4: return 1700;
                default: return 2000;
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
        return config.maidenOffset();
    }

    protected int horOffset()
    {
        return config.maidenHorOffset();
    }

    protected FontTypes hpFont()
    {
        return config.maidenFont();
    }

    protected int fontSize()
    {
        return config.maidenSize();
    }

    protected Color textColor()
    {
        return config.maidenColor();
    }

    protected double updateThresh()
    {
        return config.updateThreshold();
    }
    protected boolean onTickOnly()
    {
        return config.onTickOnly();
    }
}
