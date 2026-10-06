/*
 * Copyright (c) 2022, Buchus <http://github.com/MoreBuchus>
 * Copyright (c) 2021, dey0 <http://github.com/dey0> - Math to convert times
 * Copyright (c) 2020, Trevor <https://github.com/Trevor159> - Cox time script
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
package com.coxanalytics;

import com.coxanalytics.config.TimeStyle;
import com.coxanalytics.party.CoxTimePacket;
import com.google.inject.Provides;
import java.awt.image.BufferedImage;
import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.text.DecimalFormat;
import java.util.Optional;
import lombok.Getter;
import lombok.extern.slf4j.Slf4j;
import net.runelite.api.ChatMessageType;
import net.runelite.api.Client;
import net.runelite.api.GameState;
import net.runelite.api.NPC;
import net.runelite.api.NpcID;
import net.runelite.api.Point;
import net.runelite.api.VarPlayer;
import net.runelite.api.Varbits;
import net.runelite.api.events.ActorDeath;
import net.runelite.api.events.ChatMessage;
import net.runelite.api.events.ClientTick;
import net.runelite.api.events.GameObjectSpawned;
import net.runelite.api.events.GameStateChanged;
import net.runelite.api.events.GameTick;
import net.runelite.api.events.ScriptPostFired;
import net.runelite.api.events.VarbitChanged;
import net.runelite.api.widgets.ComponentID;
import net.runelite.api.widgets.Widget;
import net.runelite.client.RuneLite;
import net.runelite.client.callback.ClientThread;
import net.runelite.client.chat.ChatColorType;
import net.runelite.client.chat.ChatMessageBuilder;
import net.runelite.client.chat.ChatMessageManager;
import net.runelite.client.chat.QueuedMessage;
import net.runelite.client.config.ConfigManager;
import net.runelite.client.eventbus.Subscribe;
import net.runelite.client.events.ConfigChanged;
import net.runelite.client.party.PartyService;
import net.runelite.client.party.WSClient;
import net.runelite.client.plugins.Plugin;
import net.runelite.client.plugins.PluginDescriptor;
import net.runelite.client.plugins.PluginManager;
import net.runelite.client.ui.ClientToolbar;
import net.runelite.client.ui.NavigationButton;
import net.runelite.client.ui.overlay.OverlayManager;
import net.runelite.client.ui.overlay.tooltip.Tooltip;
import net.runelite.client.ui.overlay.tooltip.TooltipManager;
import net.runelite.client.util.ImageUtil;
import net.runelite.client.util.Text;
import javax.inject.Inject;

@Slf4j
@PluginDescriptor(
	name = "CoX Analytics",
	description = "Additional information for CoX sessions.",
	tags = {"cox", "floor", "splits", "olm", "time", "analytics", "session", "raid"}
)
public class CoxAnalyticsPlugin extends Plugin
{
	@Inject
	private Client client;

	@Inject
	private CoxAnalyticsConfig config;

	@Inject
	private CoxAnalyticsOverlay overlay;

	@Inject
	private OverlayManager overlayManager;

	@Inject
	private ClientThread clientThread;

	@Inject
	private ChatMessageManager chatMessageManager;

	@Inject
	private ClientToolbar clientToolbar;

	@Inject
	private TooltipManager tooltipManager;

	@Inject
	private PluginManager pluginManager;

	@Inject
	private PartyService partyService;

	@Inject
	private WSClient wsClient;

	private CoxPointsPanel pointsPanel;

	private NavigationButton button;

	private static final int COX_TIME_VAR = 6386;
	private static final int COX_POINT_WIDGET_SCRIPT = 1510;
	private static final DecimalFormat POINTS_FORMAT = new DecimalFormat("#,###");
	private static final String RAID_START_MESSAGE = "The raid has begun!";
	private static final String KC_MESSAGE = "Your completed Chambers of Xeric count is:";
	private static final String KC_MESSAGE_CM = "Your completed Chambers of Xeric Challenge Mode count is:";
	private static final String RAID_COMPLETE_MESSAGE = "Congratulations - your raid is complete!";
	private static final String COMBAT_ROOM_COMPLETE_MESSAGE = "Combat room ";
	private static final String PUZZLE_ROOM_COMPLETE_MESSAGE = "Puzzle ";

	@Getter
	private boolean inCox;
	@Getter
	private int regKC = 0;
	@Getter
	private int cmKC = 0;

	@Getter
	private int fastestTicks = -1;
	@Getter
	private int splitTicks = 0;
	@Getter
	private String splits = "";

	@Getter
	private int totalTeamPoints = 0;
	@Getter
	private int totalSoloPoints = 0;
	@Getter
	private int totalEndTicks = 0;
	@Getter
	private int realEndTicks = 0;
	@Getter
	private boolean realTicksFlag = false;

	@Getter
	private int upperTicks = -1;
	@Getter
	private int middleTicks = -1;
	@Getter
	private int lowerTicks = -1;
	@Getter
	private int olmStart = -1;
	@Getter
	private int endTicks = -1;

	@Getter
	private String upperFloorTime = "";
	@Getter
	private String middleFloorTime = "";
	@Getter
	private String lowerFloorTime = "";
	@Getter
	private String olmTime = "";

	public int olmPhase = 0;
	public int mageStart = -1;

	private String lastRecordedRoom = "";
	private String lastSeenRoom = "";

	@Getter
	private static final File TIMES_DIR = new File(RuneLite.RUNELITE_DIR, "cox-analytics");

	@Provides
	CoxAnalyticsConfig provideConfig(ConfigManager configManager)
	{
		return configManager.getConfig(CoxAnalyticsConfig.class);
	}

	@Override
	protected void startUp() throws Exception
	{
		buildPanel();
		overlayManager.add(overlay);
		clientThread.invoke(() -> hideWidget(config.replaceWidget()));

		wsClient.registerMessage(CoxTimePacket.class);
	}

	@Override
	protected void shutDown() throws Exception
	{
		clientToolbar.removeNavigation(button);
		overlayManager.remove(overlay);
		clientThread.invoke(() -> hideWidget(false));
		reset();

		wsClient.unregisterMessage(CoxTimePacket.class);
	}

	public void reset()
	{
		inCox = false;
		resetTimes();
	}

	public void resetTimes()
	{
		upperTicks = -1;
		middleTicks = -1;
		lowerTicks = -1;
		endTicks = -1;
		upperFloorTime = "";
		middleFloorTime = "";
		lowerFloorTime = "";
		olmTime = "";
		splits = "";
		splitTicks = 0;
		olmPhase = 0;
		mageStart = -1;
		lastRecordedRoom = "";
		lastSeenRoom = "";
	}

	private void buildPanel()
	{
		clientToolbar.removeNavigation(button);
		pointsPanel = injector.getInstance(CoxPointsPanel.class);
		pointsPanel.init();
		BufferedImage icon = ImageUtil.loadImageResource(getClass(), "cox.png");
		button = NavigationButton.builder()
			.tooltip("CoX Analytics")
			.icon(icon)
			.priority(config.panelPriority())
			.panel(pointsPanel)
			.build();
		if (config.ptsPanel())
		{
			clientToolbar.addNavigation(button);
		}
	}

	@Subscribe
	public void onConfigChanged(ConfigChanged e)
	{
		if (e.getGroup().equals("CoxAnalytics"))
		{
			if (e.getKey().equals("ptsPanel"))
			{
				if (config.ptsPanel())
				{
					clientToolbar.addNavigation(button);
					pointsPanel.revalidate();
				}
				else if (!config.ptsPanel())
				{
					clientToolbar.removeNavigation(button);
				}
			}
			else if (e.getKey().equals("panelPriority"))
			{
				buildPanel();
			}
			else if (e.getKey().equals("replaceWidget"))
			{
				hideWidget(config.replaceWidget());
			}
		}
	}

	@Subscribe
	public void onVarbitChanged(VarbitChanged e)
	{
		inCox = client.getVarbitValue(Varbits.IN_RAID) == 1;
	}

	@Subscribe
	private void onGameStateChanged(GameStateChanged e)
	{
		if (e.getGameState() == GameState.LOGGED_IN && client.getLocalPlayer() != null)
		{
			inCox = client.getVarbitValue(Varbits.IN_RAID) == 1;
		}
	}

	@Subscribe
	private void onGameTick(GameTick e)
	{
		if (client.getGameState() == GameState.LOGGED_IN && client.getLocalPlayer() != null && realTicksFlag)
		{
			realEndTicks++;
		}
	}

	@Subscribe
	public void onChatMessage(ChatMessage e) throws Exception
	{
		if (inCox)
		{
			String msg = Text.removeTags(e.getMessage());

			if (e.getType() == ChatMessageType.GAMEMESSAGE)
			{
				if (msg.startsWith(KC_MESSAGE) || msg.startsWith(KC_MESSAGE_CM))
				{
					String trimKcMsg = msg.substring(msg.indexOf(":")).replace(".", "");
					if (msg.startsWith(KC_MESSAGE))
					{
						regKC++;
						splits += "CoX KC" + trimKcMsg;
						pointsPanel.setSplits(splits);
						if (config.exportTimes())
						{
							exportTimes(false);
						}
					}
					else if (msg.startsWith(KC_MESSAGE_CM))
					{
						cmKC++;
						splits += "CoX CM KC" + trimKcMsg;
						pointsPanel.setSplits(splits);
						if (config.exportTimes())
						{
							exportTimes(true);
						}
					}

					updatePanel();
				}
			}
			else if (e.getType() == ChatMessageType.FRIENDSCHATNOTIFICATION)
			{
				if (msg.startsWith(COMBAT_ROOM_COMPLETE_MESSAGE) || msg.startsWith(PUZZLE_ROOM_COMPLETE_MESSAGE))
				{
					String room = msg.split("`")[1];
					int roomTicks = coxTimeVar() - splitTicks;
					if (splitTicks != coxTimeVar())
					{
						splitTicks = coxTimeVar();
					}

					String formattedTime = raidTime(roomTicks);

					recordSplit(room, formattedTime, "game");
				}
				else if (msg.contains("Olm phase") || msg.contains("Olm head"))
				{
					String cleanMsg = msg.replace(" duration", "").trim();
					String label;
					String time;

					if (cleanMsg.contains("Total"))
					{
						label = cleanMsg.substring(0, cleanMsg.indexOf("Total")).trim();
					}
					else
					{
						label = cleanMsg;
					}

					// Most Olm messages are "Label: Time" or "Label Time"
					// We'll split to ensure we get the time portion for the packet
					time = label.substring(label.lastIndexOf(" ") + 1);
					label = label.substring(0, label.lastIndexOf(" ")).trim();

					// Remove trailing ":" if it exists, recordSplit will add it back as needed
					if (label.endsWith(":"))
					{
						label = label.substring(0, label.length() - 1);
					}

					recordSplit(label, time, "game");
				}
				else if (msg.contains("level complete! Duration: "))
				{
					String floorLabel;
					String floorTime;

					if (msg.contains("Upper"))
					{
						upperTicks = coxTimeVar();
						getFloorTimes();
						floorLabel = "Floor 1";
						floorTime = upperFloorTime;
					}
					else if (msg.contains("Middle"))
					{
						middleTicks = coxTimeVar();
						getFloorTimes();
						floorLabel = "Floor 2";
						floorTime = middleFloorTime;
					}
					else // Lower
					{
						lowerTicks = coxTimeVar();
						olmStart = coxTimeVar();
						getFloorTimes();
						floorLabel = middleTicks != -1 ? "Floor 3" : "Floor 2";
						floorTime = lowerFloorTime;
					}

					if (splitTicks != coxTimeVar())
					{
						splitTicks = coxTimeVar();
					}

					recordSplit(floorLabel, floorTime, "game");
				}
				else if (msg.startsWith(RAID_START_MESSAGE))
				{
					resetTimes();
					if (!realTicksFlag)
					{
						realTicksFlag = true;
					}
				}
				else if (msg.startsWith(RAID_COMPLETE_MESSAGE))
				{
					int totalPoints = client.getVarbitValue(Varbits.TOTAL_POINTS);
					int personalPoints = client.getVarpValue(VarPlayer.RAIDS_PERSONAL_POINTS);
					int scale = client.getVarbitValue(CoxAnalyticsVarbits.RAID_PARTY_SIZE);

					endTicks = coxTimeVar();
					getFloorTimes();
					splits += "Olm: " + olmTime + "<br>Raid Completed: " + raidTime(endTicks) + " | Team Size: " + scale + "<br>";
					pointsPanel.setSplits(splits);

					totalEndTicks += endTicks;
					totalTeamPoints += totalPoints;
					totalSoloPoints += personalPoints;

					if (config.ptsHr())
					{
						String chatMessage = new ChatMessageBuilder()
							.append(ChatColorType.NORMAL)
							.append("Solo pts/hr: ")
							.append(ChatColorType.HIGHLIGHT)
							.append(getPointsPerHour(personalPoints))
							.append(ChatColorType.NORMAL)
							.append(", Team pts/hr: ")
							.append(ChatColorType.HIGHLIGHT)
							.append(getPointsPerHour(totalPoints))
							.build();

						chatMessageManager.queue(QueuedMessage.builder()
							.type(ChatMessageType.FRIENDSCHATNOTIFICATION)
							.runeLiteFormattedMessage(chatMessage)
							.build());
					}
				}
			}
		}
	}

	@Subscribe
	public void onActorDeath(ActorDeath e)
	{
		final Optional<Plugin> deoTimers = pluginManager.getPlugins().stream().filter(p -> p.getName().equals("CoX Timers")).findFirst();
		if (deoTimers.isPresent() && pluginManager.isPluginEnabled(deoTimers.get()) && config.showOlmMageHand())
		{
			if (e.getActor() instanceof NPC)
			{
				NPC npc = (NPC) e.getActor();
				if (npc.getId() == NpcID.GREAT_OLM_RIGHT_CLAW || npc.getId() == NpcID.GREAT_OLM_RIGHT_CLAW_7553)
				{
					if ((olmPhase + 1) < getOlmPhases())
					{
						if (mageStart != -1)
						{
							//Keep the colors and message consistent with de0's CoX Timers
							olmPhase++;
							String chatMessage = "Olm mage hand phase " + olmPhase + " duration: <col=ff0000>" + raidTime(coxTimeVar() - mageStart);
							client.addChatMessage(ChatMessageType.FRIENDSCHATNOTIFICATION, "", chatMessage, null);

							splits += "Olm mage hand phase " + olmPhase + ": " + raidTime(coxTimeVar() - mageStart) + "<br>";
							pointsPanel.setSplits(splits);
							mageStart = -1;
						}
					}
				}
			}
		}
	}

	private int getOlmPhases()
	{
		int scale = client.getVarbitValue(CoxAnalyticsVarbits.RAID_PARTY_SIZE);
		return 3 + (scale / 8);
	}

	@Subscribe
	public void onGameObjectSpawned(GameObjectSpawned e)
	{
		//Olm spawned
		if (e.getGameObject().getId() == 29881)
		{
			mageStart = coxTimeVar();
		}
	}

	@Subscribe
	public void onClientTick(ClientTick event)
	{
		if (coxTimeVar() > 0 && client.getVarbitValue(Varbits.RAID_STATE) < 5)
		{
			//Needs to run the script, otherwise it only updates the time every ~4 seconds
			client.runScript(2289, 0, 0, 0);

			Widget widget = client.getWidget(ComponentID.RAIDS_POINTS_INFOBOX);
			Point mousePosition = client.getMouseCanvasPosition();

			if (widget != null && !widget.isHidden() && widget.getBounds().contains(mousePosition.getX(), mousePosition.getY()))
			{
				if (config.splitsTooltip() && upperTicks != -1)
				{
					tooltipManager.add(new Tooltip(getFloorTimes()));
				}
			}
		}
	}

	@Subscribe
	public void onScriptPostFired(ScriptPostFired event)
	{
		if (event.getScriptId() == COX_POINT_WIDGET_SCRIPT && inCox)
		{
			Widget widget = client.getWidget(ComponentID.RAIDS_POINTS_INFOBOX);

			if (widget != null && !widget.isHidden())
			{
				widget.setHidden(config.replaceWidget());
			}
		}
	}

	@Subscribe
	public void onCoxTimePacket(CoxTimePacket packet)
	{
		log.info("Received CoX Party Packet: (Previous room: {} Room: {} Time: {})", packet.getLastRecordedRoomName(), packet.getRoomName(), packet.getTime());

		if (!config.acceptPartySplits()) return;
		// Either we have already recorded the split from a different party member,
		// or we were logged in and recorded it ourselves
		if (packet.getRoomName().equals(lastRecordedRoom)) return;

		// Verify that the split we're receiving is from a team member in the same raid
		if (!packet.getLastRecordedRoomName().equals(lastRecordedRoom)) return;

		// Verify that the split we're receiving is accurate
		// (person in the party saw the previous room, so the time between previous and current is correct)
		if (!packet.getLastSeenRoomName().equals(lastRecordedRoom)) return;

		recordSplit(packet.getRoomName(), packet.getTime(), "party");
	}

	private void recordSplit(String roomName, String formattedTime, String splitSource)
	{
		if (!validateSplit(formattedTime, splitSource))
		{
			// Update the last seen room even if the split was invalid, this will help validate the next time.
			// This is only accessible if the splitSource == "game".
			lastSeenRoom = roomName;
			return;
		}

		backfillFloorSplits(roomName, formattedTime, splitSource);

		addSplitToPanel(roomName, formattedTime, splitSource);
		if (splitSource.equals("game")) {
			sendSplitToParty(roomName, formattedTime);
			lastSeenRoom = roomName;
		}

		lastRecordedRoom = roomName;
	}

	// When a user logs out and logs back in during a raid, or is not present in a room to see the end,
	// they will see an incorrect room time that's equal to the total time or the sum of multiple missed room times.
	// If the user is accepting party splits, we can wait for a party split instead to fix it.
	private Boolean validateSplit(String formattedTime, String splitSource) {
		// If the user is not accepting party splits, we take a possibly invalid split as it's better than nothing
		if (!config.acceptPartySplits()) return true;
		// If the source is already the party, it's inherently trusted
		if (splitSource.equals("party")) return true;

		// If the player saw the previous room time in game, the current room time will be accurate
		if (lastRecordedRoom.equals(lastSeenRoom)) return true;

		return false;
	}

	// If we get a floor split from the party, update our tick variables so getFloorTimes() works.
	private void backfillFloorSplits(String roomName, String formattedTime, String splitSource) {
		if (splitSource.equals("party")) {
			int ticks = parseTicks(formattedTime);
			if (roomName.equals("Floor 1") && upperTicks == -1) {
				upperTicks = ticks;
			}
			else if (roomName.equals("Floor 2") && middleTicks == -1) {
				if (upperTicks != -1) {
					middleTicks = upperTicks + ticks;
					olmStart = middleTicks;
				}
			}
			else if (roomName.equals("Floor 3") && lowerTicks == -1) {
				if (middleTicks != -1) {
					lowerTicks = middleTicks + ticks;
					olmStart = lowerTicks;
				}
			}
		}
	}

	private void addSplitToPanel(String roomName, String formattedTime, String splitSource)
	{
		String entry = roomName + ": " + formattedTime + (splitSource.equals("party") ? " (party sync)" : "");
		splits += entry + "<br>";

		// Ensure the side panel reflects the change immediately
		if (pointsPanel != null)
		{
			pointsPanel.setSplits(splits);
		}
	}

	private void sendSplitToParty(String roomName, String formattedTime)
	{
		if (!config.sendPartySplits()) return;
		// Broadcast to party so logged-out members get it
		if (partyService.isInParty())
		{
			partyService.send(new CoxTimePacket(lastRecordedRoom, lastSeenRoom, roomName, formattedTime));
		}
	}

	private void hideWidget(boolean hidden)
	{
		if (client.getGameState() == GameState.LOGGED_IN && inCox)
		{
			final Widget widget = client.getWidget(ComponentID.RAIDS_POINTS_INFOBOX);
			if (widget != null)
			{
				widget.setHidden(hidden);
			}
		}
	}

	public String getFloorTimes()
	{
		String time = "";
		if (upperTicks != -1)
		{
			upperFloorTime = raidTime(upperTicks);
			time += "Floor 1: " + upperFloorTime;

			if (middleTicks == -1)
			{
				if (lowerTicks != -1)
				{
					lowerFloorTime = raidTime(lowerTicks - upperTicks);
					time += "</br>Floor 2: " + lowerFloorTime;
				}
			}
			else
			{
				middleFloorTime = raidTime(middleTicks - upperTicks);
				time += "</br>Floor 2: " + middleFloorTime;

				if (lowerTicks != -1)
				{
					lowerFloorTime = raidTime(lowerTicks - middleTicks);
					time += "</br>Floor 3: " + lowerFloorTime;
				}
			}

			if (endTicks != -1)
			{
				olmTime = raidTime(endTicks - olmStart);
				time += "</br>Olm: " + olmTime;
			}
		}
		return time;
	}

	public int coxTimeVar()
	{
		return client.getVarbitValue(COX_TIME_VAR);
	}

	public String raidTime(int ticks)
	{
		TimeStyle setting = config.timerStyle();
		boolean ingame_setting = client.getVarbitValue(11866) == 1;
		if (setting == TimeStyle.TICKS || (setting == TimeStyle.VARBIT && ingame_setting))
		{
			return to_mmss_precise(ticks);
		}

		return to_mmss(ticks);
	}

	public static String to_mmss(int ticks)
	{
		int m = ticks / 100;
		int s = (ticks - m * 100) * 6 / 10;
		return m + (s < 10 ? ":0" : ":") + s;
	}

	public static String to_mmss_precise(int ticks)
	{
		int min = ticks / 100;
		int tmp = (ticks - min * 100) * 6;
		int sec = tmp / 10;
		int sec_tenth = tmp - sec * 10;
		return min + (sec < 10 ? ":0" : ":") + sec + "." + sec_tenth;
	}

	private int parseTicks(String formattedTime) {
		try {
			String[] parts = formattedTime.split(":");
			if (parts.length < 2) return 0;

			int minutes = Integer.parseInt(parts[0]);
			double seconds = Double.parseDouble(parts[1]);

			double totalSeconds = (minutes * 60) + seconds;

			return (int) Math.round(totalSeconds / 0.6);
		} catch (Exception e) {
			log.error("Failed to parse party split time: {}", formattedTime);
			return 0;
		}
	}

	public String getPointsPerHour(int points)
	{
		return POINTS_FORMAT.format((float) (points / endTicks) * 6000);
	}

	private void updatePanel()
	{
		int totalKC = regKC + cmKC;

		if (endTicks != -1)
		{
			pointsPanel.setTeamPoints(totalTeamPoints);
			pointsPanel.setTeamVirtualPointsHour(totalTeamPoints, totalEndTicks);
			pointsPanel.setTeamPointsHour(totalTeamPoints, realEndTicks);
			pointsPanel.setAvgTeamPoints(totalTeamPoints, totalKC);

			pointsPanel.setSoloPoints(totalSoloPoints);
			pointsPanel.setSoloVirtualPointsHour(totalSoloPoints, totalEndTicks);
			pointsPanel.setSoloPointsHour(totalSoloPoints, realEndTicks);
			pointsPanel.setAvgSoloPoints(totalSoloPoints, totalKC);

			pointsPanel.setCompletions(regKC, cmKC);
			pointsPanel.setAvgTime(raidTime(totalEndTicks / totalKC));
			pointsPanel.setLastTime(raidTime(endTicks));
			pointsPanel.setTimeDif(fastestTicks != -1 ? fastestTicks - endTicks : 0);
		}

		if (fastestTicks == -1 || endTicks < fastestTicks)
		{
			fastestTicks = endTicks;
			pointsPanel.setFastestTime(raidTime(fastestTicks));
		}
	}

	public void resetPointsPanel()
	{
		regKC = 0;
		cmKC = 0;
		totalTeamPoints = 0;
		totalSoloPoints = 0;
		totalEndTicks = 0;
		fastestTicks = -1;
		realTicksFlag = false;
		realEndTicks = 0;

		pointsPanel.setTeamPoints(totalTeamPoints);
		pointsPanel.setTeamVirtualPointsHour(totalTeamPoints, totalEndTicks);
		pointsPanel.setTeamPointsHour(totalTeamPoints, realEndTicks);
		pointsPanel.setAvgTeamPoints(totalTeamPoints, regKC + cmKC);

		pointsPanel.setSoloPoints(totalSoloPoints);
		pointsPanel.setSoloVirtualPointsHour(totalSoloPoints, totalEndTicks);
		pointsPanel.setSoloPointsHour(totalSoloPoints, realEndTicks);
		pointsPanel.setAvgSoloPoints(totalSoloPoints, regKC + cmKC);

		pointsPanel.setCompletions(regKC, cmKC);
		pointsPanel.setFastestTime("00:00.0");
		pointsPanel.setLastTime("00:00.0");
		pointsPanel.setTimeDif(0);
	}

	public void resetSplitsPanel()
	{
		splits = "";
		pointsPanel.setSplits(splits);
	}

	private void exportTimes(boolean cm) throws IOException
	{
		if (!TIMES_DIR.exists() && !TIMES_DIR.mkdirs())
		{
			return;
		}

		String suffix = cm ? "_CmTimes.txt" : "_CoxTimes.txt";
		String fileName = client.getLocalPlayer().getName() + suffix;
		File file = TIMES_DIR.toPath().resolve(fileName).toFile();

		try (FileWriter writer = new FileWriter(file, true))
		{
			// Basic splits data
			StringBuilder output = new StringBuilder(splits.replace("<br>", "\r\n"));

			// Conditional Date Export
			if (config.exportDates())
			{
				output.append("\r\nDate: ").append(java.time.LocalDate.now());
			}

			// Separator lines
			output.append("\r\n")
					.append("------------------------------------------------------------------------------------------------\r\n")
					.append("------------------------------------------------------------------------------------------------\r\n");

			writer.write(output.toString());
		}
		catch (IOException e)
		{
			log.error("Could not export CoX times", e);
		}
	}
}