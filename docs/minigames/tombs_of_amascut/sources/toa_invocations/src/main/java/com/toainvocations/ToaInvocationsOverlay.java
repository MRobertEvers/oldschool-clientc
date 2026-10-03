package com.toainvocations;

import java.awt.Dimension;
import java.awt.Graphics2D;
import java.util.List;
import javax.inject.Inject;
import net.runelite.client.ui.overlay.OverlayPanel;
import net.runelite.client.ui.overlay.OverlayPosition;
import net.runelite.client.ui.overlay.components.LineComponent;
import net.runelite.client.ui.overlay.components.TitleComponent;

public class ToaInvocationsOverlay extends OverlayPanel
{
	private final ToaInvocationsPlugin plugin;
	private final ToaInvocationsConfig config;

	@Inject
	ToaInvocationsOverlay(ToaInvocationsPlugin plugin, ToaInvocationsConfig config)
	{
		this.plugin = plugin;
		this.config = config;
		setPosition(OverlayPosition.TOP_LEFT);
	}

	@Override
	public Dimension render(Graphics2D graphics)
	{
		ToaRoom room = plugin.getCurrentRoom();
		if (room == null)
		{
			return null;
		}

		List<ToaInvocation> invocations = plugin.getActiveInvocations();
		int count = invocations.size();

		panelComponent.getChildren().clear();

		if (config.displayMode() == ToaInvocationsConfig.DisplayMode.COUNT_ONLY)
		{
			panelComponent.getChildren().add(TitleComponent.builder()
				.text(room.getDisplayName() + " (" + count + ")")
				.build());
		}
		else
		{
			panelComponent.getChildren().add(TitleComponent.builder()
				.text("Invocations (" + count + "):")
				.build());

			for (ToaInvocation inv : invocations)
			{
				panelComponent.getChildren().add(LineComponent.builder()
					.left(inv.getDisplayName())
					.build());
			}
		}

		return super.render(graphics);
	}
}
