package bosses;

import java.awt.event.KeyEvent;
import java.util.ArrayList;
import java.util.List;

import GUI.loadouts.EquipmentLoadout.Gears;
import GUI.loadouts.choices.PrayerGroups;
import simple.api.actions.SimpleObjectActions;
import simple.api.coords.WorldArea;
import simple.api.coords.WorldPoint;

public class PestilentBloat extends TobBoss {

	WorldArea bossRegion = new WorldArea(new WorldPoint(3288, 4455, 0), new WorldPoint(3304, 4440, 0));

	WorldArea northRegion = new WorldArea(new WorldPoint(3292, 4451, 0), new WorldPoint(3299, 4455, 0));
	WorldArea eastRegion = new WorldArea(new WorldPoint(3298, 4444, 0), new WorldPoint(3303, 4451, 0));
	WorldArea westRegion = new WorldArea(new WorldPoint(3288, 4444, 0), new WorldPoint(3292, 4451, 0));
	WorldArea southRegion = new WorldArea(new WorldPoint(3292, 4444, 0), new WorldPoint(3299, 4440, 0));

	WorldPoint northTile = new WorldPoint(3292, 4450, 0);
	WorldPoint eastTile = new WorldPoint(3298, 4451, 0);
	WorldPoint southTile = new WorldPoint(3299, 4445, 0);
	WorldPoint westTile = new WorldPoint(3293, 4444, 0);

	long startTime = System.currentTimeMillis();
	long elapsedTime = System.currentTimeMillis() - startTime;
	long elapsedSeconds = elapsedTime / 1000;

	long lastOccurence;

	boolean goToSafeSpots;

	public PestilentBloat() {
		super(new int[] { 8359 }, 13125);
	}

	@Override
	public boolean move() {

		if (ctx.npcs.populate().filter(getBoss()).next().getAnimation() != 8082 && goToBoss() || lastOccurence == 0
				|| System.currentTimeMillis() - lastOccurence >= 11000) {
			final WorldPoint bossLocation = ctx.npcs.populate().filter(getIds()).next().getLocation();
			if (bossLocation.within(northRegion)) {
				ctx.pathing.step(northTile);
			} else if (bossLocation.within(eastRegion)) {
				ctx.pathing.step(eastTile);
			} else if (bossLocation.within(westRegion)) {
				ctx.pathing.step(westTile);
			} else if (bossLocation.within(southRegion)) {
				ctx.pathing.step(southTile);
			}
			return true;
		}
		if (ctx.npcs.populate().filter(getBoss()).next().getAnimation() == 8082) {
			lastOccurence = System.currentTimeMillis();
		}
		return false;
	}

	@Override
	public boolean getToNextRoom() {
		final WorldPoint secondBarrier = new WorldPoint(3288, 4447, 0);
		if (ctx.pathing.inArea(bossRegion) && !ctx.pathing.onTile(secondBarrier)) {
			ctx.pathing.step(secondBarrier);
		} else if (!ctx.objects.populate().filter(32755).isEmpty() && ctx.pathing.onTile(secondBarrier)) {
			ctx.objects.nearest().next().interact(SimpleObjectActions.FIRST);
			ctx.onCondition(() -> !ctx.pathing.inArea(bossRegion));
		} else if (!ctx.objects.populate().filter("Formidable passage").isEmpty()) {
			ctx.objects.nearest().next().interact(SimpleObjectActions.FIRST);
		}
		return false;
	}

	@Override
	public boolean goToBoss() {
		if (ctx.objects.populate().filter(32755).isEmpty() && !ctx.pathing.inArea(bossRegion)) {
			ctx.pathing.step(new WorldPoint(3305, 4448, 0));
		} else if (!ctx.objects.populate().filter(32755).isEmpty() && !ctx.pathing.inArea(bossRegion)) {
			ctx.objects.nearest().next().interact(SimpleObjectActions.FIRST);
			ctx.onCondition(() -> ctx.dialogue.dialogueOpen());
			if (ctx.npcs.populate().filter(8359).next().getLocation().within(southRegion)) {
				ctx.keyboard.clickKey(KeyEvent.VK_1);
			}
		}
		return ctx.pathing.inArea(bossRegion);
	}

	@Override
	public List<PrayerGroups> getPrayers() {
		final ArrayList<PrayerGroups> prayers = new ArrayList<>();
		prayers.add(PrayerGroups.MELEE_PRAYER);
		return prayers;
	}

	@Override
	public Gears getLoadout() {
		return Gears.MELEE;
	}

}
