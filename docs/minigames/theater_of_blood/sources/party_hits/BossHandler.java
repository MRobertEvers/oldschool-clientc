package com.partyhits.bosses;

import com.partyhits.PartyHitsConfig;
import com.partyhits.XpToDamage;
import com.partyhits.util.FontTypes;
import com.partyhits.util.Hit;
import lombok.Getter;
import net.runelite.api.*;
import net.runelite.api.events.GameTick;
import net.runelite.client.callback.ClientThread;
import net.runelite.client.eventbus.Subscribe;
import net.runelite.client.ui.overlay.OverlayManager;
import org.apache.commons.lang3.tuple.Pair;

import javax.inject.Inject;
import java.awt.*;
import java.util.ArrayList;
import java.util.List;


public abstract class BossHandler
{
    @Inject
    PartyHitsConfig config;
    @Inject
    private ClientThread clientThread;
    private BossOverlay bossOverlay;
    @Inject
    private OverlayManager overlayManager;
    @Inject
    protected XpToDamage xpToDamage;
    @Getter
    private NPC bossNpc;
    private int maxHp;
    @Getter
    private double predictedHpPercent;
    private double realHpPercent;
    @Getter
    private boolean bossActive;
    private List<Pair<Integer, Integer>> queuedDamage = new ArrayList<>();

    public void init(NPC boss)
    {
        if (bossOverlay == null)
            bossOverlay = new BossOverlay(this);

        overlayManager.add(bossOverlay);
        queuedDamage.clear();
        bossActive = true;
        bossNpc = boss;
        predictedHpPercent = 100.0;
        realHpPercent = 100.0;

        maxHp = getMaxHp();
    }

    public void deactivate()
    {
        overlayManager.remove(bossOverlay);
        bossActive = false;
        queuedDamage.clear();
        bossNpc = null;
    }

    @Subscribe
    protected void onGameTick(GameTick event)
    {
        if (bossActive)
        {
            updateHpPercentage();
            updatePredictedHp(1);
            reduceQueuedDamage();
        }
    }

    private void reduceQueuedDamage()
    {
        List<Pair<Integer, Integer>> newQueuedDamage = new ArrayList<>();

        for (Pair<Integer, Integer> entry : queuedDamage)
        {
            int dmg = entry.getLeft();
            int tickDelay = entry.getRight() - 1;

            if (tickDelay >= 0)
            {
                newQueuedDamage.add(Pair.of(dmg, tickDelay));
            }
        }
        queuedDamage = newQueuedDamage;
    }

    public void updatePredictedHp(int tick)
    {
        if (bossNpc == null || bossNpc.isDead())
        {
            return;
        }

        int queuedDmgTotal = 0;
        for (Pair<Integer, Integer> entry : queuedDamage)
        {
            if (entry.getRight() >= tick)
                queuedDmgTotal += entry.getLeft();
        }

        double queuedDamagePercentage = (queuedDmgTotal / (double) maxHp) * 100;
        predictedHpPercent = realHpPercent - queuedDamagePercentage;
    }

    private void updateHpPercentage()
    {
        clientThread.invoke(() ->
        {
            if (bossNpc.getHealthRatio() / bossNpc.getHealthScale() != 1)
                realHpPercent = ((double) bossNpc.getHealthRatio() / (double) bossNpc.getHealthScale() * 100);
        });
    }

    public void queueDamage(Hit hit, boolean ownHit)
    {
        if (hit.getTickDelay() > 0)
        {
            if (ownHit)
            {
                queuedDamage.add(Pair.of(hit.getDamage(), hit.getTickDelay()));
                if (!onTickOnly())
                    updatePredictedHp(1);
            }
            else
            {
                queuedDamage.add(Pair.of(hit.getDamage(), hit.getTickDelay() - 1));
                if (!onTickOnly())
                    updatePredictedHp(0);
            }
        }
    }

    protected abstract int getMaxHp();
    protected abstract int vertOffset();
    protected abstract int horOffset();
    protected abstract FontTypes hpFont();
    protected abstract int fontSize();
    protected abstract Color textColor();
    protected abstract double updateThresh();
    protected abstract boolean onTickOnly();
}
