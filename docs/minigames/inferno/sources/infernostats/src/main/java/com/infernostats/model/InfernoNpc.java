package com.infernostats.model;

import java.awt.*;
import java.util.Arrays;
import java.util.Map;
import java.util.Optional;
import java.util.stream.Collectors;

public enum InfernoNpc {
  BAT("Jal-MejRah", 2, Color.GRAY, "bat"),
  BLOB("Jal-Ak", 3, Color.YELLOW, "blob"),
  MELEE("Jal-ImKot", 4, Color.ORANGE, "melee"),
  RANGER("Jal-Xil", 3, Color.GREEN, "ranger"),
  MAGER("Jal-Zek", 4, Color.RED, "mager");

  private static final Map<String, InfernoNpc> BY_NAME = Arrays.stream(values())
      .collect(Collectors.toMap(e -> e.name, e -> e));

  public final String name;
  public final int size;
  public final Color color;
  public final String urlParam;

  InfernoNpc(String name, int size, Color color, String urlParam) {
    this.name = name;
    this.size = size;
    this.color = color;
    this.urlParam = urlParam;
  }

  public static Optional<InfernoNpc> fromName(String name) {
    return Optional.ofNullable(BY_NAME.get(name));
  }
}
