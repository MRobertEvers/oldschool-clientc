// Generate cosine table for indices 1-2048
// Formula: cos(i * 0.0030679615) * 65536
// where 0.0030679615 = 2 * PI / 2048

const cosTable = [];

// 0.0030679615 = 2 * PI / 2048
const angleStep = 0.0030679615;

for (let i = 1; i <= 2048; i++) {
  const value = Math.cos(i * angleStep) * (1 << 16);
  cosTable.push(Math.round(value));
}

console.log(`static const int g_cos_table[2048] = { ${cosTable.join(", ")} };`);
