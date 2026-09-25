package com.tobutilities.verzik;

import com.tobutilities.common.RoomHandler;
import com.tobutilities.common.enums.Region;
import com.tobutilities.common.util.CommonUtils;

import com.tobutilities.common.player.TobPlayerOrb;
import com.tobutilities.TobUtilitiesPlugin;
import com.tobutilities.TobUtilitiesConfig;

import static com.tobutilities.verzik.VerzikConstants.VERZIK_NAME;
import static com.tobutilities.verzik.VerzikConstants.VERZIK_P1_IDS;
import java.awt.event.KeyEvent;
import java.util.HashMap;
import java.util.Map;
import javax.annotation.Nullable;
import javax.inject.Inject;

import javax.inject.Singleton;
import lombok.Getter;
import lombok.extern.slf4j.Slf4j;
import net.runelite.api.Client;
import net.runelite.api.GameState;
import net.runelite.api.WorldView;
import net.runelite.api.gameval.InventoryID;
import net.runelite.api.ItemContainer;
import net.runelite.api.gameval.ItemID;
import net.runelite.api.NPC;
import net.runelite.api.Player;
import net.runelite.api.Renderable;
import net.runelite.api.events.GameTick;
import net.runelite.api.events.GameStateChanged;
import net.runelite.api.events.ItemContainerChanged;
import static net.runelite.api.kit.KitType.WEAPON;

import net.runelite.client.callback.ClientThread;
import net.runelite.client.eventbus.Subscribe;
import net.runelite.client.party.PartyService;
import org.apache.commons.lang3.StringUtils;


@Slf4j
@Singleton
public class VerzikHandler extends RoomHandler

{
	private static final int VERZIK_CAMERA_RESTORE_TICKS = 3;

	@Getter
	private TobPlayerOrb tobPlayerOrb = TobPlayerOrb.UNKNOWN;
	@Getter
	private DawnbringerStatus dawnbringerStatus = DawnbringerStatus.UNKNOWN;
	private boolean isVerzikHidden = false;
	@Getter
	private boolean isLightbearerOverlayDisplayed = false;
	private int savedCameraYawTarget = -1;
	private int savedCameraPitchTarget = -1;
	private int cameraRestoreTicksRemaining = 0;

	@Inject
	private PartyService partyService;
	@Inject
	private ClientThread clientThread;


	// Tracks Dawnbringer status of all party members
	private final Map<String, DawnbringerStatus> memberDawnbringerStatus = new HashMap<>();


	@Inject
	protected VerzikHandler(TobUtilitiesPlugin plugin, TobUtilitiesConfig config, Client client)
	{
		super(plugin, config, client);
	}


	@Subscribe
	public void onGameTick(GameTick tick)
	{
		restoreSavedCameraTargetsIfNeeded();
		isLightbearerOverlayDisplayed = shouldDisplayLightbearerReminder();
		if (!config.enableDawnbringerOverlay())
		{
			return;
		}

		if (config.enableDawnbringerParty() && partyService.isInParty())
		{
			handlePartyDawnbringerOverlay();
		}
		else
		{
			handleNonPartyDawnbringerOverlay();
		}
	}

	private void handleNonPartyDawnbringerOverlay()
	{
		String dawnbringerHolderName = removeNonAlphanumeric(getDawnbringerHolderName());
		if (dawnbringerHolderName != null)
		{
			for (TobPlayerOrb tobPlayer : TobPlayerOrb.values())
			{
				String playerName = removeNonAlphanumeric(client.getVarcStrValue(tobPlayer.getNameVarc()));
				if (dawnbringerHolderName.equals(playerName))
				{
					tobPlayerOrb = tobPlayer;
					dawnbringerStatus = DawnbringerStatus.EQUIPPED;
					return;
				}
			}
		}
		tobPlayerOrb = TobPlayerOrb.UNKNOWN;
	}

	private void handlePartyDawnbringerOverlay()
	{
		Map.Entry<String, DawnbringerStatus> playerStatusEntry = getPlayerWithDawnbringerInParty();
		if (playerStatusEntry == null)
		{
			tobPlayerOrb = TobPlayerOrb.UNKNOWN;
			dawnbringerStatus = DawnbringerStatus.UNKNOWN;
			return;
		}
		String dawnbringerHolderName = removeNonAlphanumeric(playerStatusEntry.getKey());
		if (dawnbringerHolderName != null)
		{
			for (TobPlayerOrb tobPlayer : TobPlayerOrb.values())
			{
				String playerName = removeNonAlphanumeric(client.getVarcStrValue(tobPlayer.getNameVarc()));
				if (dawnbringerHolderName.equals(playerName))
				{
					tobPlayerOrb = tobPlayer;
					dawnbringerStatus = playerStatusEntry.getValue();
					return;
				}
			}
		}

		handleNonPartyDawnbringerOverlay();
	}


	private String getDawnbringerHolderName()
	{

		for (Player player : client.getWorldView(WorldView.TOPLEVEL).players())
		{
			if (player == null || player.getName() == null)
			{
				continue; // Skip null or invalid players
			}
			int weaponId = player.getPlayerComposition().getEquipmentId(WEAPON);
			if (weaponId == ItemID.VERZIK_SPECIAL_WEAPON)
			{
				return player.getName(); // Stop once we find a match
			}
		}
		return null;
	}

	private static String removeNonAlphanumeric(String str)
	{
		if (str == null)
		{
			return null;
		}
		str = str.replaceAll(
			"[^a-zA-Z0-9]", "");
		return str;
	}

	public void keyPressed(KeyEvent e)
	{
		if (config.hideVerzikHotkey().matches(e) && config.enableHideVerzik())
		{
			isVerzikHidden = !isVerzikHidden;
			e.consume();
		}

	}

	public boolean shouldDraw(Renderable renderable, boolean drawingUI)
	{
		if (renderable instanceof NPC)
		{
			NPC npc = (NPC) renderable;
			return !VERZIK_NAME.equals(npc.getName()) || !isVerzikHidden || !config.enableHideVerzik();
		}
		return true;
	}

	/**
	 * Gets the name of the player holding the Dawnbringer
	 *
	 * @return The name of the player with Dawnbringer, or null if no one has it
	 */
	@Nullable
	public Map.Entry<String, DawnbringerStatus> getPlayerWithDawnbringerInParty()
	{
		return memberDawnbringerStatus.entrySet().stream()
			.filter(entry -> entry.getValue() != DawnbringerStatus.UNKNOWN)
			.findFirst()
			.orElse(null);
	}

	@Subscribe
	public void onItemContainerChanged(ItemContainerChanged event)
	{
		// Only check inventory changes
		if (event.getContainerId() == InventoryID.INV || event.getContainerId() == InventoryID.WORN)
		{
			clientThread.invoke(this::checkLocalPlayerForDawnbringer);
		}
	}

	private void checkLocalPlayerForDawnbringer()
	{
		// Make sure we're in a party
		if (!partyService.isInParty())
		{
			return;
		}

		// Check if we have Dawnbringer in inventory
		DawnbringerStatus status = DawnbringerStatus.UNKNOWN;
		String localPlayerName = client.getLocalPlayer().getName();
		ItemContainer inventory = client.getItemContainer(InventoryID.INV);
		ItemContainer equippedItems = client.getItemContainer(InventoryID.WORN);
		if (inventory != null && inventory.contains(ItemID.VERZIK_SPECIAL_WEAPON))
		{
			status = DawnbringerStatus.IN_INVENTORY;
		}
		else if (equippedItems != null && equippedItems.contains(ItemID.VERZIK_SPECIAL_WEAPON))
		{
			status = DawnbringerStatus.EQUIPPED;
		}
		// Store our status
		memberDawnbringerStatus.put(localPlayerName, status);

		// Send status to party
		partyService.send(new DawnbringerStatusMessage(localPlayerName, status));

	}

	/**
	 * Handles incoming Dawnbringer status messages from party members
	 */
	@Subscribe
	public void onDawnbringerStatusMessage(DawnbringerStatusMessage message)
	{
		log.debug("Message received: {}", message);
		if (client.getLocalPlayer() != null && StringUtils.isNotBlank(client.getLocalPlayer().getName()) && client.getLocalPlayer().getName().equals(message.getPlayerName()))
		{
			//ignore messages sent by local player
			return;
		}
		// Update status map
		memberDawnbringerStatus.put(message.getPlayerName(), message.getDawnbringerStatus());
	}

	private boolean shouldDisplayLightbearerReminder()
	{
		return config.enableLightbearerOverlay() && isP1VerzikAlive() && isLightbearerInInventory();
	}

	private boolean isP1VerzikAlive()
	{
		return client.getWorldView(WorldView.TOPLEVEL).npcs()
			.stream()
			.anyMatch(npc -> VERZIK_P1_IDS.contains(npc.getId()) && !npc.isDead());
	}

	private boolean isLightbearerInInventory()
	{
		ItemContainer inventory = client.getItemContainer(InventoryID.INV);
		return inventory != null && inventory.contains(ItemID.LIGHTBEARER);
	}

	public void captureEntryCameraTargets()
	{
		if (!config.preserveVerzikEntryCamera() || client.getGameState() != GameState.LOGGED_IN)
		{
			return;
		}

		savedCameraYawTarget = client.getCameraYawTarget();
		savedCameraPitchTarget = client.getCameraPitchTarget();
	}

	public void onRoomEntry()
	{
		if (!config.preserveVerzikEntryCamera())
		{
			cameraRestoreTicksRemaining = 0;
			return;
		}

		beginCameraRestore();
		restoreSavedCameraTargetsIfNeeded();
	}

	public void onRoomExit()
	{
		cameraRestoreTicksRemaining = 0;
	}

	public void onGameStateChanged(GameStateChanged event)
	{
		if (event.getGameState() == GameState.LOGIN_SCREEN)
		{
			clearCameraSnapshot();
			return;
		}

		if (!config.preserveVerzikEntryCamera())
		{
			cameraRestoreTicksRemaining = 0;
			return;
		}

		switch (event.getGameState())
		{
			case LOGGED_IN:
				if (CommonUtils.getRegionByRegionId(CommonUtils.getRegionID(client)) == Region.VERZIK)
				{
					beginCameraRestore();
					restoreSavedCameraTargetsIfNeeded();
				}
				break;
		}
	}

	private void beginCameraRestore()
	{
		if (savedCameraYawTarget < 0 || savedCameraPitchTarget < 0)
		{
			return;
		}

		cameraRestoreTicksRemaining = VERZIK_CAMERA_RESTORE_TICKS;
	}

	private void restoreSavedCameraTargetsIfNeeded()
	{
		if (cameraRestoreTicksRemaining <= 0 || savedCameraYawTarget < 0 || savedCameraPitchTarget < 0)
		{
			return;
		}

		client.setCameraYawTarget(savedCameraYawTarget);
		client.setCameraPitchTarget(savedCameraPitchTarget);
		cameraRestoreTicksRemaining--;
	}

	private void clearCameraSnapshot()
	{
		savedCameraYawTarget = -1;
		savedCameraPitchTarget = -1;
		cameraRestoreTicksRemaining = 0;
	}

	public void startUp()
	{
		memberDawnbringerStatus.clear();
		clearCameraSnapshot();
	}


	public void shutDown()
	{
		memberDawnbringerStatus.clear();
		clearCameraSnapshot();
	}

}
