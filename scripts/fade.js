function fade(n) {
  n = (n << 13) ^ n;
  return (n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff;
}

/**
 * Fades x,y and returns a value between 0 and 255.
 */
function fadexy(x, y) {
  return (fade(y * 57 + x) >> 19) & 0xff;
}

const TEST_COORDS = [
  [0, 0],
  [1, 0],
  [0, 1],
  [1, 1],
  [0, 2],
  [1, 2],
  [2, 0],
  [2, 1],
  [2, 2],
];

for (const [x, y] of TEST_COORDS) {
  console.log(fadexy(x, y));
}
