package com.infernostats;

import lombok.Value;

@Value
public class NylocasHealth {
  final int bigHP;
  final int smallHP;
  final int smSmallHP;
  final int smBigHP;

  private static final NylocasHealth[] HP_BY_PARTY_SIZE = new NylocasHealth[6];

  static {
    HP_BY_PARTY_SIZE[1] = new NylocasHealth(16, 8, 2, 3);
    HP_BY_PARTY_SIZE[2] = new NylocasHealth(16, 8, 3, 5);
    HP_BY_PARTY_SIZE[3] = new NylocasHealth(16, 8, 6, 9);
    HP_BY_PARTY_SIZE[4] = new NylocasHealth(19, 9, 8, 12);
    HP_BY_PARTY_SIZE[5] = new NylocasHealth(22, 11, 10, 15);
  }

  public static int getStoryModeSmall(int partySize) {
    return HP_BY_PARTY_SIZE[partySize].smSmallHP;
  }

  public static int getStoryModeBig(int partySize) {
    return HP_BY_PARTY_SIZE[partySize].smBigHP;
  }

  public static int getNormalModeSmall(int partySize) {
    return HP_BY_PARTY_SIZE[partySize].smallHP;
  }

  public static int getNormalModeBig(int partySize) {
    return HP_BY_PARTY_SIZE[partySize].bigHP;
  }
}
