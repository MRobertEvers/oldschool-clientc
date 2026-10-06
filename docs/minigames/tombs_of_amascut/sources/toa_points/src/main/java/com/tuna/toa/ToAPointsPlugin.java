package com.tuna.toa;

import com.google.inject.Provides;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import net.runelite.api.*;
import net.runelite.api.coords.LocalPoint;
import net.runelite.api.coords.WorldPoint;
import net.runelite.api.events.*;
import net.runelite.api.events.GameTick;
import net.runelite.api.widgets.Widget;
import net.runelite.client.callback.ClientThread;
import net.runelite.client.config.ConfigManager;
import net.runelite.client.eventbus.Subscribe;
import net.runelite.client.plugins.Plugin;
import net.runelite.client.plugins.PluginDescriptor;
import net.runelite.client.ui.overlay.OverlayManager;

import java.util.List;

import static com.tuna.toa.ToARegion.*;

@Slf4j
@PluginDescriptor(
		name = "ToA Points Overlay"
)
public class ToAPointsPlugin extends Plugin {

	@Inject
	private Client client;

	@Inject
	private ToAPointsOverlay overlay;

	@Inject
	private ToAPointsConfig config;

	@Inject
	private OverlayManager overlayManager;

	@Inject
	private ClientThread clientThread;

	public static double totalPoints = 5000;

	public static double roomPoints = 0;

	public static int invocationLevel = 0;

	public static int partySize = 0;

	boolean inRaid = false;

	private ToARegion currentRegion = null;

	public static int getInvocationLevel()
	{
		return invocationLevel;
	}

	@Provides
	ToAPointsConfig provideConfig(ConfigManager configManager)
	{
		return configManager.getConfig(ToAPointsConfig.class);
	}

	public static double getTotalPoints()
	{
		return totalPoints;
	}

	public static double getRoomPoints()
	{
		return roomPoints;
	}
	@Override
	protected void startUp() throws Exception
	{
		overlayManager.add(overlay);

	}

	@Override
	protected void shutDown() throws Exception
	{
		overlayManager.remove(overlay);

		reset();
	}

	@Subscribe
	public void onHitsplatApplied(HitsplatApplied hitsplatApplied)
	{
		Actor target = hitsplatApplied.getActor();
		Hitsplat hitsplat = hitsplatApplied.getHitsplat();

		//p2 warden and palm are weird. So take the damage done to warden/palm divide it by group members then multiply by the modifier.
		//Isn't super accurate, but it'll be as close as it can get currently.
		if(!hitsplat.isMine() && !hitsplat.isOthers() && !target.getName().equals("Kephri") &&
		(hitsplat.getHitsplatType() == 53 || hitsplat.getHitsplatType() == 55 || hitsplat.getHitsplatType() == 11))
		{
			NPC npc = (NPC) target;
			List<Player> teamMembers = client.getPlayers();

			int averageHitSplat = hitsplat.getAmount() / teamMembers.size();

			//p2 warden and palm are 2.0 modifier
			averageHitSplat = averageHitSplat * 2;
			roomPoints = roomPoints + averageHitSplat;

			//because points are split between members, we lower the point cap
			//in the crondis puzzle. Mostly to prevent misleading/fake boosting.
			//In solos, we just go with the point cap given to us from the blog.
			if(hitsplat.getHitsplatType() == 11 && roomPoints >= 10000 && teamMembers.size() != 1){
				roomPoints = 10000;
			}
			else if(teamMembers.size() == 1 && hitsplat.getHitsplatType() == 11 && roomPoints >= 20000) {
				roomPoints = 20000;
			}
		}
		else if (!hitsplat.isMine() || target == client.getLocalPlayer())
		{
			//do nothing
		}
		else
		{
			NPC npc = (NPC) target;
			pointCalc(hitsplat, npc);
		}

	}

	@Subscribe
	public void onGameTick(GameTick e)
	{
		LocalPoint lp = client.getLocalPlayer().getLocalLocation();
		int newRegionID = lp == null ? -1 : WorldPoint.fromLocalInstance(client, lp).getRegionID();
		ToARegion newRegion = ToARegion.fromRegionID(newRegionID);
		//we are within ToA
		if(newRegion != null && newRegion != ToARegion.TOA_LOBBY){
			inRaid = true;
			overlayManager.add(overlay);

			Widget invoWidget = client.getWidget(31522858);

			if(invoWidget != null) {
				String invoLevel = invoWidget.getText();
				invocationLevel = Integer.parseInt(invoLevel.replaceAll("[^0-9]", ""));
			}

		}
		else{
			inRaid = false;
			overlayManager.remove(overlay);
		}

		//still in the raid, but we moved to a new area
		if(newRegion != currentRegion && inRaid)
		{

			if(config.puzzlePointsAssumption()){

				switch(newRegion)
				{
					case BOSS_BABA:
						totalPoints = totalPoints + 450;
						break;
					case BOSS_KEPHRI:
						totalPoints = totalPoints + 300;
						break;
				}

			}
			//if we didnt just leave the nexus, or loot room add mvp points
			if( config.mvpAssumption())
			{
				mvpAssumption(currentRegion, newRegion);
			}

			currentRegion = newRegion;

			if(currentRegion != null)
			{
				totalPoints = totalPoints + roomPoints;
				roomPoints = 0;

				//hard cap on total points
				if(totalPoints > 64000){
					totalPoints = 64000;
				}
			}
		}
		currentRegion = newRegion;
	}

	public void mvpAssumption(ToARegion leftRegion, ToARegion enteredRegion)
	{
		//We left a puzzle room and entered the boss room, add mvp points for the puzzle
		if(leftRegion != null)
		{
			switch (leftRegion)
			{
				case PUZZLE_MONKEY:
				{
					if(enteredRegion == BOSS_BABA)
					{
						totalPoints = totalPoints + 300;
					}
				}
				case PUZZLE_CRONDIS:
				{
					if(enteredRegion == BOSS_ZEBAK)
					{
						totalPoints = totalPoints + 300;
					}
				}
				case PUZZLE_SCABARAS:
				{
					if(enteredRegion == BOSS_KEPHRI)
					{
						totalPoints = totalPoints + 300;
					}
				}
				case PUZZLE_HET:
				{
					if(enteredRegion == BOSS_AKKHA)
					{
						totalPoints = totalPoints + 300;
					}
				}
				//wardens don't drop an item so assumption is needed.
				case BOSS_WARDEN_FINAL:
				{
					if(enteredRegion == CHEST_ROOM)
					{
						totalPoints = totalPoints + 300;
					}
				}
			}
		}
	}

	@Subscribe
	public void onItemSpawned(ItemSpawned itemSpawned)
	{

		TileItem item = itemSpawned.getItem();

		List<Player> teamMembers = client.getPlayers();

		if(item.getId() == 27221 && currentRegion == BOSS_BABA)
		{
			totalPoints = totalPoints + (300 * teamMembers.size());
		}
		else if (item.getId() == 27223 && currentRegion == BOSS_AKKHA)
		{
			totalPoints = totalPoints + (300 * teamMembers.size());
		}
		else if (item.getId() == 27219 && currentRegion == BOSS_ZEBAK)
		{
			totalPoints = totalPoints + (300 * teamMembers.size());
		}
		else if (item.getId() == 27214 && currentRegion == BOSS_KEPHRI)
		{
			totalPoints = totalPoints + (300 * teamMembers.size());
		}
	}

	@Subscribe
	public void onActorDeath(ActorDeath actorDeath)
	{
		if (actorDeath.getActor() == client.getLocalPlayer())
		{

			List<Player> teamMembers = client.getPlayers();
			
			if(teamMembers.size() == 1) //dying in a solo means no chance of room completion
			{
				roomPoints = 0; 
			}
			
			double pointLoss = totalPoints * 20;
			pointLoss = pointLoss/100;

			if(pointLoss < 1000)
			{
				pointLoss = 1000;
			}
			if (totalPoints < 1000)
			{
				totalPoints = 0;
			}
			else
			{
				totalPoints = totalPoints - pointLoss;
			}

		}
	}

	public void pointCalc(Hitsplat hitsplat, NPC target)
	{

		double modHit = 0;
		double modifier = 0;
		int rawHit = hitsplat.getAmount();

		ToANpc currentTarget = ToANpc.fromNpcID(target.getId());

		if(currentTarget == null){
			modifier = 1;
		}
		else {

			switch (currentTarget) {
				case BABOON_BRAWLER:
				case BABOON_THROWER:
				case BABOON_MAGE:
				case BABOON_SHAMAN:
				case BABOON_THRALL:
				case BABOON_CURSED:
				case BABOON_VOLATILE: {
					modifier = 1.2;
					break;
				}

				case BABA:

				case WARDEN_TUMEKEN_RANGE:
				case WARDEN_TUMEKEN_MAGE:
				case WARDEN_ELIDINIS_MAGE:
				case WARDEN_ELIDINIS_RANGE: {
					modifier = 2.0;
					break;
				}

				case BOULDER: {
					modifier = 0.0;
					break;
				}

				case HET_OBELISK:
				case WARDEN_TUMEKEN_FINAL:
				case WARDEN_ELIDINIS_FINAL: {
					modifier = 2.5;
					break;
				}

				case ZEBAK:
				case ZEBAK_ENRAGED:
				case WARDEN_OBELISK: {
					modifier = 1.5;
					break;
				}

				case SCARAB_ARCANE:
				case SCARAB_SPITTING:
				case SCARAB_SOLDIER: {
					modifier = 0.5;
					break;
				}

				case WARDEN_ELIDINIS_INACTIVE_P1:
				case WARDEN_ELIDINIS_INACTIVE_P2:
				case WARDEN_ELIDINIS_INACTIVE_P3:
				case WARDEN_TUMEKEN_INACTIVE_P1:
				case WARDEN_TUMEKEN_INACTIVE_P2:
				case WARDEN_TUMEKEN_INACTIVE_P3:
				case WARDEN_CORE_TUMEKEN:
				case WARDEN_CORE_ELIDINIS: {
					modifier = 0;
					break;
				}

				default: {
					modifier = 1.0;
					break;
				}


			}
		}

		modHit = rawHit * modifier;


		roomPoints = roomPoints + modHit;

		//hard cap on room points
		if(roomPoints > 20000)
		{
			roomPoints = 20000;
		}

	}

	@Subscribe
	private void onGameStateChanged(GameStateChanged event)
	{

		if (event.getGameState() == GameState.LOGGED_IN
		    && client.getLocalPlayer() != null && !inRaid)
		{
			reset();
		}
	}

	public void reset()
	{
		roomPoints = 0;
		currentRegion = null;
		totalPoints = 5000;
		inRaid = false;
	}


}
