package com.infernostats;

import lombok.AllArgsConstructor;
import net.runelite.api.gameval.NpcID;

import java.util.Map;

@AllArgsConstructor
public final class NylocasType {

  public enum Style {
    MELEE, RANGE, MAGE
  }

  public enum Mode {
    NORMAL,
    HARD,
    STORY
  }

  public enum Size {
    SMALL,
    BIG
  }

  public final Mode mode;
  public final Style style;
  public final Size size;

  private static final Map<Integer, NylocasType> NYLO_ID_TO_INFO = Map.ofEntries(
      // Normal Mode Smalls
      Map.entry(NpcID.TOB_NYLOCAS_INCOMING_MELEE, new NylocasType(Mode.NORMAL, Style.MELEE, Size.SMALL)),
      Map.entry(NpcID.TOB_NYLOCAS_FIGHTING_MELEE, new NylocasType(Mode.NORMAL, Style.MELEE, Size.SMALL)),
      Map.entry(NpcID.TOB_NYLOCAS_INCOMING_RANGED, new NylocasType(Mode.NORMAL, Style.RANGE, Size.SMALL)),
      Map.entry(NpcID.TOB_NYLOCAS_FIGHTING_RANGED, new NylocasType(Mode.NORMAL, Style.RANGE, Size.SMALL)),
      Map.entry(NpcID.TOB_NYLOCAS_INCOMING_MAGIC, new NylocasType(Mode.NORMAL, Style.MAGE, Size.SMALL)),
      Map.entry(NpcID.TOB_NYLOCAS_FIGHTING_MAGIC, new NylocasType(Mode.NORMAL, Style.MAGE, Size.SMALL)),
      // Normal Mode Bigs
      Map.entry(NpcID.TOB_NYLOCAS_BIG_INCOMING_MELEE, new NylocasType(Mode.NORMAL, Style.MELEE, Size.BIG)),
      Map.entry(NpcID.TOB_NYLOCAS_BIG_FIGHTING_MELEE, new NylocasType(Mode.NORMAL, Style.MELEE, Size.BIG)),
      Map.entry(NpcID.TOB_NYLOCAS_BIG_INCOMING_RANGED, new NylocasType(Mode.NORMAL, Style.RANGE, Size.BIG)),
      Map.entry(NpcID.TOB_NYLOCAS_BIG_FIGHTING_RANGED, new NylocasType(Mode.NORMAL, Style.RANGE, Size.BIG)),
      Map.entry(NpcID.TOB_NYLOCAS_BIG_INCOMING_MAGIC, new NylocasType(Mode.NORMAL, Style.MAGE, Size.BIG)),
      Map.entry(NpcID.TOB_NYLOCAS_BIG_FIGHTING_MAGIC, new NylocasType(Mode.NORMAL, Style.MAGE, Size.BIG)),

      // Hard Mode Smalls
      Map.entry(NpcID.TOB_NYLOCAS_INCOMING_MELEE_HARD, new NylocasType(Mode.HARD, Style.MELEE, Size.SMALL)),
      Map.entry(NpcID.TOB_NYLOCAS_FIGHTING_MELEE_HARD, new NylocasType(Mode.HARD, Style.MELEE, Size.SMALL)),
      Map.entry(NpcID.TOB_NYLOCAS_INCOMING_RANGED_HARD, new NylocasType(Mode.HARD, Style.RANGE, Size.SMALL)),
      Map.entry(NpcID.TOB_NYLOCAS_FIGHTING_RANGED_HARD, new NylocasType(Mode.HARD, Style.RANGE, Size.SMALL)),
      Map.entry(NpcID.TOB_NYLOCAS_INCOMING_MAGIC_HARD, new NylocasType(Mode.HARD, Style.MAGE, Size.SMALL)),
      Map.entry(NpcID.TOB_NYLOCAS_FIGHTING_MAGIC_HARD, new NylocasType(Mode.HARD, Style.MAGE, Size.SMALL)),
      // Hard Mode Bigs
      Map.entry(NpcID.TOB_NYLOCAS_BIG_INCOMING_MELEE_HARD, new NylocasType(Mode.HARD, Style.MELEE, Size.BIG)),
      Map.entry(NpcID.TOB_NYLOCAS_BIG_FIGHTING_MELEE_HARD, new NylocasType(Mode.HARD, Style.MELEE, Size.BIG)),
      Map.entry(NpcID.TOB_NYLOCAS_BIG_INCOMING_RANGED_HARD, new NylocasType(Mode.HARD, Style.RANGE, Size.BIG)),
      Map.entry(NpcID.TOB_NYLOCAS_BIG_FIGHTING_RANGED_HARD, new NylocasType(Mode.HARD, Style.RANGE, Size.BIG)),
      Map.entry(NpcID.TOB_NYLOCAS_BIG_INCOMING_MAGIC_HARD, new NylocasType(Mode.HARD, Style.MAGE, Size.BIG)),
      Map.entry(NpcID.TOB_NYLOCAS_BIG_FIGHTING_MAGIC_HARD, new NylocasType(Mode.HARD, Style.MAGE, Size.BIG)),

      // Story Mode Smalls
      Map.entry(NpcID.TOB_NYLOCAS_INCOMING_MELEE_STORY, new NylocasType(Mode.STORY, Style.MELEE, Size.SMALL)),
      Map.entry(NpcID.TOB_NYLOCAS_FIGHTING_MELEE_STORY, new NylocasType(Mode.STORY, Style.MELEE, Size.SMALL)),
      Map.entry(NpcID.TOB_NYLOCAS_INCOMING_RANGED_STORY, new NylocasType(Mode.STORY, Style.RANGE, Size.SMALL)),
      Map.entry(NpcID.TOB_NYLOCAS_FIGHTING_RANGED_STORY, new NylocasType(Mode.STORY, Style.RANGE, Size.SMALL)),
      Map.entry(NpcID.TOB_NYLOCAS_INCOMING_MAGIC_STORY, new NylocasType(Mode.STORY, Style.MAGE, Size.SMALL)),
      Map.entry(NpcID.TOB_NYLOCAS_FIGHTING_MAGIC_STORY, new NylocasType(Mode.STORY, Style.MAGE, Size.SMALL)),
      // Story Mode Bigs
      Map.entry(NpcID.TOB_NYLOCAS_BIG_INCOMING_MELEE_STORY, new NylocasType(Mode.STORY, Style.MELEE, Size.BIG)),
      Map.entry(NpcID.TOB_NYLOCAS_BIG_FIGHTING_MELEE_STORY, new NylocasType(Mode.STORY, Style.MELEE, Size.BIG)),
      Map.entry(NpcID.TOB_NYLOCAS_BIG_INCOMING_RANGED_STORY, new NylocasType(Mode.STORY, Style.RANGE, Size.BIG)),
      Map.entry(NpcID.TOB_NYLOCAS_BIG_FIGHTING_RANGED_STORY, new NylocasType(Mode.STORY, Style.RANGE, Size.BIG)),
      Map.entry(NpcID.TOB_NYLOCAS_BIG_INCOMING_MAGIC_STORY, new NylocasType(Mode.STORY, Style.MAGE, Size.BIG)),
      Map.entry(NpcID.TOB_NYLOCAS_BIG_FIGHTING_MAGIC_STORY, new NylocasType(Mode.STORY, Style.MAGE, Size.BIG)));

  public static NylocasType getNyloType(int npcId) {
    return NYLO_ID_TO_INFO.get(npcId);
  }

  public static boolean isRangeNylocas(int npcId) {
    NylocasType info = getNyloType(npcId);
    if (info == null) {
      return false;
    }
    return info.style == NylocasType.Style.RANGE;
  }

  public static boolean isMageNylocas(int npcId) {
    NylocasType info = getNyloType(npcId);
    if (info == null) {
      return false;
    }
    return info.style == NylocasType.Style.MAGE;
  }
}
