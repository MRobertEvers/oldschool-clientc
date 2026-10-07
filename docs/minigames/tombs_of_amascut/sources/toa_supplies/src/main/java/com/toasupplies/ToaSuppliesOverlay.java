package com.toasupplies;


import net.runelite.api.*;
import net.runelite.api.widgets.WidgetItem;
import net.runelite.client.game.ItemManager;
import net.runelite.client.ui.overlay.WidgetItemOverlay;
import net.runelite.client.ui.overlay.tooltip.TooltipManager;

import javax.inject.Inject;
import java.awt.*;
import java.math.BigDecimal;


class ToaSuppliesOverlay extends WidgetItemOverlay
{
    private final ToaSuppliesConfig config;
    private final ToaSuppliesPlugin plugin;

    private static final int[] potions = {27315, 27327, 27347};
    private static final int[] potionMax = {4,4,2};
    private static final Color[] potionColors = {Color.yellow,Color.magenta,Color.white};

    @Inject
    ToaSuppliesOverlay(ToaSuppliesConfig config, ToaSuppliesPlugin plugin)
    {
        this.config = config;
        this.plugin = plugin;
        showOnInventory();
    }

    @Override
    public void renderItemOverlay(Graphics2D graphics, int itemId, WidgetItem widgetItem)
    {
        if (itemId != ItemID.SUPPLIES)
        {
            return;
        }

        for (int i = 0; i < potions.length; i++)
        {
            BigDecimal amount = BigDecimal.valueOf(plugin.getAmount(potions[i]));

            if (config.showSips())
            {
                String text = amount + "";
                drawString(graphics, text, widgetItem,potionColors[i], i);
            }
            else
            {
                String text = amount.divide(BigDecimal.valueOf(potionMax[i])) + "";
                drawString(graphics, text, widgetItem,potionColors[i], i);

            }
        }
    }

    private void drawString(Graphics2D graphics, String text,WidgetItem widgetItem,Color color,int i)
    {
        graphics.setColor(color);
        graphics.drawString(text, widgetItem.getCanvasBounds().x, widgetItem.getCanvasBounds().y + 10 + (i*10));
    }
}
