package com.tektonreset;

import com.google.common.collect.ImmutableSet;
import com.google.inject.Provides;
import java.time.temporal.ChronoUnit;
import java.util.Set;
import javax.inject.Inject;
import javax.swing.SwingUtilities;
import lombok.extern.slf4j.Slf4j;
import net.runelite.api.ChatMessageType;
import net.runelite.api.Client;
import net.runelite.api.GameState;
import net.runelite.api.NPC;
import net.runelite.api.Player;
import net.runelite.api.coords.WorldPoint;
import net.runelite.api.events.GameStateChanged;
import net.runelite.api.events.GameTick;
import net.runelite.api.events.NpcDespawned;
import net.runelite.api.events.NpcSpawned;
import net.runelite.api.gameval.NpcID;
import net.runelite.api.gameval.VarbitID;
import net.runelite.client.config.ConfigManager;
import net.runelite.client.eventbus.Subscribe;
import net.runelite.client.events.ConfigChanged;
import net.runelite.client.plugins.Plugin;
import net.runelite.client.plugins.PluginDescriptor;
import net.runelite.client.task.Schedule;
import net.runelite.client.ui.ClientToolbar;
import net.runelite.client.ui.NavigationButton;
import net.runelite.client.util.ImageUtil;

@Slf4j
@PluginDescriptor(
	name = "Tekton Reset Tracker",
	description = "Counts how many Chambers of Xeric Challenge Mode raids you reset at Tekton, and the time wasted.",
	tags = {"tekton", "cox", "cm", "challenge", "chambers", "xeric", "raid", "reset", "tracker", "counter", "skip"}
)
public class TektonResetTrackerPlugin extends Plugin
{
	// Every NPC state Tekton can be in. Seeing any of these means we reached the Tekton room.
	// The same NPC ids are used in normal CoX and Challenge Mode.
	static final Set<Integer> TEKTON_NPC_IDS = ImmutableSet.of(
		NpcID.RAIDS_TEKTON_WAITING,            // 7540 (sitting at the anvil)
		NpcID.RAIDS_TEKTON_WALKING_STANDARD,   // 7541
		NpcID.RAIDS_TEKTON_FIGHTING_STANDARD,  // 7542
		NpcID.RAIDS_TEKTON_WALKING_ENRAGED,    // 7543
		NpcID.RAIDS_TEKTON_FIGHTING_ENRAGED,   // 7544
		NpcID.RAIDS_TEKTON_HAMMERING           // 7545 (repairing his armour at the anvil)
	);

	// How far (tiles) the player must get from where Tekton died before we treat it as having
	// moved on to the next room. Looting happens within a few tiles of the corpse; the next room
	// (combat or puzzle, e.g. the crabs room) is a full chunk-plus away, so anything past this is
	// "progressed past Tekton" and must never count as a reset. Heuristic - tune if needed.
	private static final int PROGRESS_DISTANCE = 24;

	@Inject
	private Client client;

	@Inject
	private TektonResetTrackerConfig config;

	@Inject
	private ClientToolbar clientToolbar;

	private TektonResetTrackerPanel panel;
	private NavigationButton navButton;

	// Persisted lifetime totals.
	private int lifetimeResets;
	private long lifetimeWastedMillis;

	// Session totals (reset each client launch).
	private int sessionResets;
	private long sessionWastedMillis;

	// Per-raid state.
	private boolean inRaid;
	private boolean challengeMode;        // whether the current raid is Challenge Mode (only CM raids are counted)
	private boolean sawTekton;            // whether we reached the Tekton room this raid
	private boolean tektonCompleted;      // whether Tekton has been killed this raid (orange "Completed Tekton", timer keeps running)
	private boolean progressedPastTekton; // whether the player moved on to the next room (green, never counts as a reset)
	private WorldPoint tektonDeathLoc;    // where Tekton died, anchor for the "moved on" check
	private long raidStartMillis;

	@Provides
	TektonResetTrackerConfig provideConfig(ConfigManager configManager)
	{
		return configManager.getConfig(TektonResetTrackerConfig.class);
	}

	@Override
	protected void startUp()
	{
		lifetimeResets = config.lifetimeResets();
		lifetimeWastedMillis = config.lifetimeWastedMillis();
		sessionResets = 0;
		sessionWastedMillis = 0L;

		resetRaidState();

		panel = new TektonResetTrackerPanel(this);

		navButton = NavigationButton.builder()
			.tooltip("Tekton Reset Tracker")
			.icon(ImageUtil.loadImageResource(getClass(), "/com/tektonreset/panel_icon.png"))
			.priority(7)
			.panel(panel)
			.build();

		clientToolbar.addNavigation(navButton);
		refreshPanel();
	}

	@Override
	protected void shutDown()
	{
		if (navButton != null)
		{
			clientToolbar.removeNavigation(navButton);
		}
		navButton = null;
		panel = null;
	}

	/**
	 * Drive the raid lifecycle from the IN_RAID varbit. We only poll while logged in so a
	 * hop or logout mid-raid (which can briefly read the varbit as 0) doesn't look like the
	 * player leaving the raid.
	 */
	@Subscribe
	public void onGameTick(GameTick event)
	{
		if (client.getGameState() != GameState.LOGGED_IN)
		{
			return;
		}

		final boolean nowInRaid = client.getVarbitValue(VarbitID.RAIDS_CLIENT_INDUNGEON) == 1;

		if (nowInRaid && !inRaid)
		{
			startRaid();
		}
		else if (!nowInRaid && inRaid)
		{
			endRaid();
		}

		// Latch Challenge Mode for the current raid. Read it while we're still in the raid,
		// because the varbit clears once IN_RAID drops (by which point endRaid() already ran).
		if (inRaid && client.getVarbitValue(VarbitID.RAIDS_CHALLENGE_MODE) == 1)
		{
			challengeMode = true;
		}

		// Once Tekton is dead, watch whether the player walks off to the next room. If they do,
		// they committed to the raid (next room / completion) and this can never be a reset.
		if (inRaid && tektonCompleted && !progressedPastTekton && tektonDeathLoc != null)
		{
			final Player local = client.getLocalPlayer();
			final WorldPoint here = local == null ? null : local.getWorldLocation();
			if (here != null && here.getPlane() == tektonDeathLoc.getPlane()
				&& here.distanceTo(tektonDeathLoc) > PROGRESS_DISTANCE)
			{
				progressedPastTekton = true;
				refreshPanel();
			}
		}
	}

	@Subscribe
	public void onNpcSpawned(NpcSpawned event)
	{
		if (inRaid && TEKTON_NPC_IDS.contains(event.getNpc().getId()))
		{
			sawTekton = true;
		}
	}

	@Subscribe
	public void onNpcDespawned(NpcDespawned event)
	{
		final NPC npc = event.getNpc();
		// A Tekton that despawns while dead was killed: mark "Completed Tekton" (the timer keeps
		// running - you can still re-roll). Record where he died as the anchor for the moved-on
		// check. Walking out of the room (or his standard -> enraged swap) despawns him without isDead().
		if (inRaid && !tektonCompleted && npc.isDead() && TEKTON_NPC_IDS.contains(npc.getId()))
		{
			tektonCompleted = true;
			tektonDeathLoc = npc.getWorldLocation();
			refreshPanel();
		}
	}

	@Subscribe
	public void onGameStateChanged(GameStateChanged event)
	{
		final GameState state = event.getGameState();

		// A deliberate logout at Tekton is treated as a reset: bank the wasted time before clearing,
		// so bailing by logging out still counts.
		if (state == GameState.LOGIN_SCREEN)
		{
			final boolean reset = isCountableReset();
			final long elapsed = currentRaidMillis();
			resetRaidState();
			if (reset)
			{
				recordReset(elapsed);
			}
			else
			{
				refreshPanel();
			}
			return;
		}

		// A world hop or dropped connection is not a deliberate bail - the raid persists and the
		// player may reconnect - so we stop tracking WITHOUT counting. The next logged-in game tick
		// re-detects IN_RAID and resumes.
		if (state == GameState.HOPPING || state == GameState.CONNECTION_LOST)
		{
			resetRaidState();
			refreshPanel();
		}
	}

	@Subscribe
	public void onConfigChanged(ConfigChanged event)
	{
		if (TektonResetTrackerConfig.GROUP.equals(event.getGroup()))
		{
			refreshPanel();
		}
	}

	/**
	 * Refresh the panel periodically so the live "time in raid" stays current while a raid is
	 * in progress (we don't add it to totals until a reset is actually recorded).
	 */
	@Schedule(period = 1, unit = ChronoUnit.SECONDS)
	public void scheduledRefresh()
	{
		if (inRaid)
		{
			refreshPanel();
		}
	}

	// ----------------------------------- Raid lifecycle -----------------------------------

	private void startRaid()
	{
		resetRaidState();
		inRaid = true;
		raidStartMillis = System.currentTimeMillis();
	}

	/** Clear all per-raid tracking. */
	private void resetRaidState()
	{
		inRaid = false;
		challengeMode = false;
		sawTekton = false;
		tektonCompleted = false;
		progressedPastTekton = false;
		tektonDeathLoc = null;
		raidStartMillis = 0L;
	}

	private void endRaid()
	{
		// Count a reset when we reached Tekton in a Challenge Mode raid and left WITHOUT moving on
		// to the next room. That covers both leaving without a kill and the common case of killing
		// Tekton, seeing a slow time, and re-rolling. If the player progressed past Tekton (next
		// room or a completed raid) it is never a reset. Normal raids are ignored entirely.
		final boolean reset = isCountableReset();
		// Time wasted is the whole raid up to the moment you leave.
		final long elapsed = currentRaidMillis();

		resetRaidState();

		if (reset)
		{
			recordReset(elapsed); // also refreshes the panel
		}
		else
		{
			// Still refresh so the panel returns to "Not in a raid" - the periodic refresh only
			// runs while in a raid, so leaving a non-counting raid would otherwise stick.
			refreshPanel();
		}
	}

	/** A reset only counts in a CM raid where we reached Tekton and did not move on past him. */
	private boolean isCountableReset()
	{
		return challengeMode && sawTekton && !progressedPastTekton;
	}

	/** Add one reset and its wasted time to the lifetime + session totals, persist, and announce it. */
	private void recordReset(long elapsed)
	{
		lifetimeResets++;
		lifetimeWastedMillis += elapsed;
		sessionResets++;
		sessionWastedMillis += elapsed;

		config.lifetimeResets(lifetimeResets);
		config.lifetimeWastedMillis(lifetimeWastedMillis);

		// Only chat while logged in - a reset banked on logout has no chatbox to print to.
		if (config.chatMessage() && client.getGameState() == GameState.LOGGED_IN)
		{
			final String message = "Tekton CM reset #" + lifetimeResets + " - "
				+ DurationFormat.format(elapsed) + " wasted (lifetime: "
				+ DurationFormat.format(lifetimeWastedMillis) + ").";
			client.addChatMessage(ChatMessageType.GAMEMESSAGE, "", message, null);
		}

		refreshPanel();
	}

	// ----------------------------------- Stats getters -----------------------------------

	int getLifetimeResets()
	{
		return lifetimeResets;
	}

	long getLifetimeWastedMillis()
	{
		return lifetimeWastedMillis;
	}

	int getSessionResets()
	{
		return sessionResets;
	}

	long getSessionWastedMillis()
	{
		return sessionWastedMillis;
	}

	/** Whether a raid is currently being tracked (used to show the live timer in the panel). */
	boolean isInRaid()
	{
		return inRaid;
	}

	/** Milliseconds elapsed in the current raid (keeps running until you leave), or 0 when not raiding. */
	long getCurrentRaidMillis()
	{
		return currentRaidMillis();
	}

	private long currentRaidMillis()
	{
		return inRaid ? Math.max(0L, System.currentTimeMillis() - raidStartMillis) : 0L;
	}

	/** Whether the raid currently being tracked is Challenge Mode (only CM raids are counted). */
	boolean isCurrentRaidChallengeMode()
	{
		return challengeMode;
	}

	/** Whether Tekton has been killed this raid (orange "Completed Tekton"; the timer keeps running). */
	boolean isTektonCompleted()
	{
		return tektonCompleted;
	}

	/** Whether the player moved on past Tekton this raid (green "Moved past Tekton"; never a reset). */
	boolean hasProgressedPastTekton()
	{
		return progressedPastTekton;
	}

	/**
	 * Reset all counters (lifetime and session). Invoked from the side panel's Reset button.
	 */
	void reset()
	{
		lifetimeResets = 0;
		lifetimeWastedMillis = 0L;
		sessionResets = 0;
		sessionWastedMillis = 0L;
		config.lifetimeResets(0);
		config.lifetimeWastedMillis(0L);
		refreshPanel();
	}

	private void refreshPanel()
	{
		final TektonResetTrackerPanel p = panel;
		if (p != null)
		{
			SwingUtilities.invokeLater(p::update);
		}
	}
}
