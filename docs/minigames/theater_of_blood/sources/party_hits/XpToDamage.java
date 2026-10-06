package com.partyhits;


import com.partyhits.npcs.ToBNPCs;
import net.runelite.api.Client;
import net.runelite.client.util.Text;

import javax.inject.Inject;

public class XpToDamage
{
    private final Client client;

    @Inject
    protected XpToDamage(Client client)
    {
        this.client = client;
    }

    public int calculateHit(int id, int hpXpDiff)
    {
        if (ToBNPCs.isTOBNPC(id))
        {
            int partySize = getToBPartySize();
            double modifier = ToBNPCs.getModifier(id, partySize);
            return (int) Math.round((hpXpDiff * (3.0d / 4.0d)) / modifier);
        }
        else
        {
            return (int) Math.round((hpXpDiff * (3.0d / 4.0d)));
        }
    }

    public int getToBPartySize()
    {
        int count = 0;
        for (int i = 330; i < 335; i++)
        {
            String jagexName = client.getVarcStrValue(i);
            if (jagexName != null)
            {
                String name = Text.removeTags(jagexName).replace('\u00A0', ' ').trim();
                if (!"".equals(name))
                {
                    count++;
                }
            }
        }
        return count;
    }
}
