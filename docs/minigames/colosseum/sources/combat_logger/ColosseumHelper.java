package com.combatlogger.encounters;

import com.combatlogger.LogQueueManager;
import net.runelite.api.ActorSpotAnim;
import net.runelite.api.Client;
import net.runelite.api.NPC;
import net.runelite.api.events.AnimationChanged;
import net.runelite.api.events.GameTick;
import net.runelite.api.events.NpcDespawned;
import net.runelite.api.events.ScriptPreFired;
import net.runelite.api.gameval.AnimationID;
import net.runelite.api.gameval.NpcID;
import net.runelite.api.gameval.SpotanimID;
import net.runelite.api.gameval.VarbitID;
import net.runelite.client.eventbus.Subscribe;
import net.runelite.client.util.Text;

import javax.inject.Inject;
import javax.inject.Singleton;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Collectors;

@Singleton
public class ColosseumHelper
{
	/** No ScriptID gameval exists for the handicap selection script. */
	private static final int COLOSSEUM_MODIFIER_SELECTION_SCRIPT = 4931;
	private static final Pattern WAVE_START_PATTERN = Pattern.compile("Wave:\\s*(\\d+)", Pattern.CASE_INSENSITIVE);

	private enum ManticoreStyle
	{
		MAGE,
		RANGE,
		MELEE
	}

	private static final class ManticoreState
	{
		ManticoreStyle style;
		int attacksRemaining;
	}

	private final Client client;
	private final LogQueueManager logQueueManager;

	private final List<Integer> waveModifierOptions = new ArrayList<>(3);
	private final Map<Integer, ManticoreState> manticoresByIndex = new HashMap<>();
	private boolean inColosseum;

	@Inject
	ColosseumHelper(Client client, LogQueueManager logQueueManager)
	{
		this.client = client;
		this.logQueueManager = logQueueManager;
	}

	public void setInColosseum(boolean inColosseum)
	{
		if (this.inColosseum && !inColosseum)
		{
			reset();
		}
		this.inColosseum = inColosseum;
	}

	public void handleWaveStarted(String message)
	{
		if (!inColosseum || waveModifierOptions.isEmpty())
		{
			return;
		}

		Matcher matcher = WAVE_START_PATTERN.matcher(Text.removeTags(message));
		if (!matcher.find())
		{
			return;
		}

		logModifierChoice();
	}

	/**
	 * Manticore throw anim is shared across styles; styles are logged as specials instead.
	 */
	public boolean shouldSuppressAttackAnimation(NPC npc, int animationId)
	{
		return npc.getId() == NpcID.COLOSSEUM_MANTICORE
				&& animationId == AnimationID.NPC_MANTICORE_01_TRIPLE_THROW;
	}

	@Subscribe
	public void onScriptPreFired(ScriptPreFired event)
	{
		if (!inColosseum || event.getScriptId() != COLOSSEUM_MODIFIER_SELECTION_SCRIPT)
		{
			return;
		}

		waveModifierOptions.clear();
		Object[] scriptArgs = event.getScriptEvent().getArguments();
		waveModifierOptions.add((Integer) scriptArgs[2]);
		waveModifierOptions.add((Integer) scriptArgs[3]);
		waveModifierOptions.add((Integer) scriptArgs[4]);
	}

	@Subscribe
	public void onAnimationChanged(AnimationChanged event)
	{
		if (!inColosseum || !(event.getActor() instanceof NPC))
		{
			return;
		}

		NPC npc = (NPC) event.getActor();
		if (npc.getId() != NpcID.COLOSSEUM_MANTICORE)
		{
			return;
		}

		if (npc.getAnimation() == AnimationID.NPC_MANTICORE_01_TRIPLE_THROW)
		{
			ManticoreState state = manticoresByIndex.computeIfAbsent(npc.getIndex(), i -> new ManticoreState());
			state.attacksRemaining = 3;
		}
	}

	@Subscribe
	public void onNpcDespawned(NpcDespawned event)
	{
		NPC npc = event.getNpc();
		if (npc != null)
		{
			manticoresByIndex.remove(npc.getIndex());
		}
	}

	@Subscribe
	public void onGameTick(GameTick event)
	{
		if (!inColosseum || manticoresByIndex.isEmpty())
		{
			return;
		}

		Iterator<Map.Entry<Integer, ManticoreState>> iterator = manticoresByIndex.entrySet().iterator();
		while (iterator.hasNext())
		{
			Map.Entry<Integer, ManticoreState> entry = iterator.next();
			NPC npc = findNpcByIndex(entry.getKey());
			if (npc == null || npc.getId() != NpcID.COLOSSEUM_MANTICORE || npc.isDead())
			{
				iterator.remove();
				continue;
			}

			ManticoreState state = entry.getValue();
			if (state.attacksRemaining > 0)
			{
				String special = specialForStyle(state.style);
				if (special != null)
				{
					logSpecial(npc, special);
				}
				state.attacksRemaining--;
				state.style = null;
			}

			updateStyle(npc, state);
		}
	}

	private void updateStyle(NPC npc, ManticoreState state)
	{
		if (state.style != null)
		{
			return;
		}

		int anim = npc.getAnimation();
		if (anim != AnimationID.NPC_MANTICORE_01_TRIPLE_CHARGE
				&& anim != AnimationID.NPC_MANTICORE_01_TRIPLE_THROW)
		{
			return;
		}

		for (ActorSpotAnim spotAnim : npc.getSpotAnims())
		{
			int id = spotAnim.getId();
			if (id == SpotanimID.VFX_MANTICORE_01_PROJECTILE_MAGIC_01)
			{
				state.style = ManticoreStyle.MAGE;
				return;
			}
			if (id == SpotanimID.VFX_MANTICORE_01_PROJECTILE_RANGED_01)
			{
				state.style = ManticoreStyle.RANGE;
				return;
			}
			if (id == SpotanimID.VFX_MANTICORE_01_PROJECTILE_MELEE_01)
			{
				state.style = ManticoreStyle.MELEE;
				return;
			}
		}
	}

	private static String specialForStyle(ManticoreStyle style)
	{
		if (style == null)
		{
			return null;
		}
		switch (style)
		{
			case MAGE:
				return "MANTICORE_MAGE";
			case RANGE:
				return "MANTICORE_RANGE";
			default:
				return "MANTICORE_MELEE";
		}
	}

	private NPC findNpcByIndex(int index)
	{
		for (NPC npc : client.getNpcs())
		{
			if (npc.getIndex() == index)
			{
				return npc;
			}
		}
		return null;
	}

	private void logSpecial(NPC npc, String special)
	{
		String source = npc.getId() + "-" + npc.getIndex();
		logQueueManager.queue(String.format("%s attack special %s", source, special));
	}

	private void logModifierChoice()
	{
		int selectedIndex = client.getVarbitValue(VarbitID.COLOSSEUM_SELECTED_MODIFIER) - 1;
		if (selectedIndex < 0 || selectedIndex >= waveModifierOptions.size())
		{
			waveModifierOptions.clear();
			return;
		}

		int chosenModifier = waveModifierOptions.get(selectedIndex);
		String options = waveModifierOptions.stream()
				.map(String::valueOf)
				.collect(Collectors.joining(","));

		logQueueManager.queue(String.format("COLOSSEUM_MODIFIER\t%d\t%s", chosenModifier, options));

		waveModifierOptions.clear();
	}

	private void reset()
	{
		waveModifierOptions.clear();
		manticoresByIndex.clear();
	}
}
