package bosses;

import java.awt.event.KeyEvent;
import java.util.ArrayList;
import java.util.List;

import GUI.loadouts.EquipmentLoadout.Gears;
import GUI.loadouts.choices.PrayerGroups;
import simple.api.actions.SimpleObjectActions;
import simple.api.coords.WorldArea;
import simple.api.coords.WorldPoint;

public class TheMaidenofSugadinti extends TobBoss {

	WorldArea bossRegion = new WorldArea(new WorldPoint(3185, 4436, 0), new WorldPoint(3160, 4455, 0));

	public TheMaidenofSugadinti() {
		super(new int[] { 8360 }, 12869, 12613);
	}

	// TODO ADD STEP TO CLOSEST TILE TO BOSS

	@Override
	public boolean move() {
		if (pathing.checkFreeTiles(1579)) {
			return true;
		}
		return false;
	}

	@Override
	public boolean getToNextRoom() {
		final WorldPoint[] path = { new WorldPoint(3190, 4446, 0), new WorldPoint(3191, 4440, 0),
				new WorldPoint(3191, 4434, 0), new WorldPoint(3185, 4431, 0), new WorldPoint(3177, 4431, 0),
				new WorldPoint(3176, 4424, 0) };

		if (ctx.pathing.inArea(bossRegion) && !ctx.objects.populate().filter(32755).isEmpty()) {
			ctx.objects.nearest().next().interact(SimpleObjectActions.FIRST);
			ctx.onCondition(() -> ctx.dialogue.dialogueOpen());
			ctx.keyboard.clickKey(KeyEvent.VK_1);
		} else if (!ctx.objects.populate().filter("Formidable passage").isEmpty()) {
			ctx.objects.nearest().next().interact(SimpleObjectActions.FIRST);
		} else if (!ctx.pathing.inArea(bossRegion)) {
			ctx.pathing.walkPath(path);
		}
		return false;
	}

	@Override
	public List<PrayerGroups> getPrayers() {
		final ArrayList<PrayerGroups> prayers = new ArrayList<>();
		prayers.add(PrayerGroups.MELEE_PRAYER);
		prayers.add(PrayerGroups.PROTECT_FROM_MAGIC);
		return prayers;
	}

	@Override
	public Gears getLoadout() {
		return Gears.RANGED;
	}

	@Override
	public boolean goToBoss() {
		final WorldPoint[] path = { new WorldPoint(3217, 4452, 0), new WorldPoint(3213, 4449, 0),
				new WorldPoint(3208, 4446, 0), new WorldPoint(3202, 4446, 0), new WorldPoint(3197, 4446, 0),
				new WorldPoint(3193, 4446, 0), new WorldPoint(3189, 4446, 0) };
		if (ctx.objects.populate().filter(32755).isEmpty() && !ctx.pathing.inArea(bossRegion)) {
			ctx.pathing.walkPath(path);
		} else if (!ctx.objects.populate().filter(32755).isEmpty() && !ctx.pathing.inArea(bossRegion)) {
			handleBarrier();
		} else if (ctx.npcs.populate().filter(getIds()).filterWithin(5).isEmpty()) {
			pathing.goTo(3170, 4446, 0);
		}
		return ctx.pathing.inArea(new WorldArea(new WorldPoint(3175, 4457, 0), new WorldPoint(3160, 4435, 0)));
	}

}
