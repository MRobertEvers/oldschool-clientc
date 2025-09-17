const { execSync } = require("child_process");
const path = require("path");
const {
  getSystemInfo,
  formatSystemInfo,
  saveBenchmarkResults,
} = require("./benchmark_utils");

// Get system information
const systemInfo = getSystemInfo();
console.log(formatSystemInfo(systemInfo));
console.log("\n" + "=".repeat(80) + "\n");

// Configuration
const ITERATIONS = 30; // Number of times to run the benchmark
const EXECUTABLE = path.join(__dirname, "benchmark_dot_product");

// Data structures to store results
const results = {
  v1: [], // oriented area (condensed form)
  v2: [], // expanded cross product
};

// Helper function to parse benchmark output
function parseBenchmarkOutput(output) {
  const lines = output.toString().split("\n");
  const timings = {};

  lines.forEach((line) => {
    if (line.includes("v1 avg time:")) {
      timings.v1 = parseFloat(line.split(":")[1].split("ns")[0]);
    } else if (line.includes("v2 avg time:")) {
      timings.v2 = parseFloat(line.split(":")[1].split("ns")[0]);
    }
  });

  return timings;
}

// Statistical functions
function calculateStats(data) {
  const n = data.length;
  const mean = data.reduce((a, b) => a + b, 0) / n;
  const variance =
    data.reduce((a, b) => a + Math.pow(b - mean, 2), 0) / (n - 1);
  const stdDev = Math.sqrt(variance);
  const stderr = stdDev / Math.sqrt(n);
  const ci95 = 1.96 * stderr; // 95% confidence interval

  return {
    mean,
    stdDev,
    ci95,
    min: Math.min(...data),
    max: Math.max(...data),
  };
}

// T-test implementation
function tTest(data1, data2) {
  const n1 = data1.length;
  const n2 = data2.length;
  const mean1 = data1.reduce((a, b) => a + b, 0) / n1;
  const mean2 = data2.reduce((a, b) => a + b, 0) / n2;

  const var1 = data1.reduce((a, b) => a + Math.pow(b - mean1, 2), 0) / (n1 - 1);
  const var2 = data2.reduce((a, b) => a + Math.pow(b - mean2, 2), 0) / (n2 - 1);

  const pooledStdErr = Math.sqrt(var1 / n1 + var2 / n2);
  const t = (mean1 - mean2) / pooledStdErr;

  // Degrees of freedom (simplified)
  const df = n1 + n2 - 2;

  return { t, df };
}

// Run benchmarks
console.log(`Running ${ITERATIONS} iterations of dot product benchmark...`);
console.log("This may take a while...\n");

for (let i = 0; i < ITERATIONS; i++) {
  process.stdout.write(`Progress: ${i + 1}/${ITERATIONS}\r`);
  const output = execSync(EXECUTABLE);
  const timings = parseBenchmarkOutput(output);

  results.v1.push(timings.v1);
  results.v2.push(timings.v2);
}

console.log("\n\nResults:\n");

// Calculate and display statistics
const versions = ["v1", "v2"];
const stats = {};

versions.forEach((ver) => {
  stats[ver] = calculateStats(results[ver]);
  console.log(
    `${ver.padEnd(10)} Mean: ${stats[ver].mean.toFixed(1)} ± ${stats[
      ver
    ].ci95.toFixed(1)} ns`
  );
  console.log(`${"".padEnd(10)} Min:  ${stats[ver].min.toFixed(1)} ns`);
  console.log(`${"".padEnd(10)} Max:  ${stats[ver].max.toFixed(1)} ns`);
  console.log(`${"".padEnd(10)} StdDev: ${stats[ver].stdDev.toFixed(1)} ns\n`);
});

// Perform t-test
console.log("Statistical Significance (t-test):\n");

const { t, df } = tTest(results.v1, results.v2);
const mean1 = stats.v1.mean;
const mean2 = stats.v2.mean;
const diff = ((Math.abs(mean1 - mean2) / Math.min(mean1, mean2)) * 100).toFixed(
  2
);

console.log("v1 vs v2:");
console.log(`t-statistic: ${t.toFixed(4)}`);
console.log(`Degrees of freedom: ${df}`);
console.log(`Relative difference: ${diff}%`);

// Critical value for t (approximation for 95% confidence)
const significant = Math.abs(t) > 2.0;
console.log(`Statistically significant? ${significant ? "YES" : "NO"}\n`);

// Simple ASCII visualization
console.log("Performance Visualization (each █ = 10 nanoseconds):\n");

versions.forEach((ver) => {
  const mean = stats[ver].mean;
  const bars = "█".repeat(Math.round(mean / 10)); // Scale for visualization
  console.log(`${ver.padEnd(10)} ${bars} (${mean.toFixed(1)} ns)`);
});

// Determine the best version
console.log("\nImplementation Recommendation:\n");

// Find fastest version
const fastestVer = versions.reduce((a, b) =>
  stats[a].mean < stats[b].mean ? a : b
);

// Find most consistent version (lowest stddev)
const mostConsistentVer = versions.reduce((a, b) =>
  stats[a].stdDev < stats[b].stdDev ? a : b
);

// Calculate a composite score (lower is better)
// Normalize both mean and stddev to 0-1 range and combine with weights
const scores = {};
const maxMean = Math.max(...versions.map((v) => stats[v].mean));
const maxStdDev = Math.max(...versions.map((v) => stats[v].stdDev));

versions.forEach((ver) => {
  // Weight speed 70% and consistency 30%
  scores[ver] =
    0.7 * (stats[ver].mean / maxMean) + 0.3 * (stats[ver].stdDev / maxStdDev);
});

const bestOverallVer = versions.reduce((a, b) =>
  scores[a] < scores[b] ? a : b
);

// Output recommendations
console.log(`Fastest Implementation: ${fastestVer}`);
console.log(`- ${stats[fastestVer].mean.toFixed(1)} ns on average`);
console.log(`Most Consistent Implementation: ${mostConsistentVer}`);
console.log(`- StdDev: ${stats[mostConsistentVer].stdDev.toFixed(1)} ns`);
console.log(`\nBest Overall Implementation: ${bestOverallVer}`);
console.log(
  "Based on weighted combination of speed (70%) and consistency (30%)"
);

// Provide detailed reasoning
console.log("\nDetailed Analysis:");
if (bestOverallVer === fastestVer && bestOverallVer === mostConsistentVer) {
  console.log(
    `${bestOverallVer} is the clear winner, being both the fastest and most consistent.`
  );
} else {
  console.log(`${bestOverallVer} is recommended because:`);
  console.log(
    `- Speed: ${stats[bestOverallVer].mean.toFixed(1)} ns (${
      fastestVer === bestOverallVer
        ? "fastest"
        : (
            ((stats[bestOverallVer].mean - stats[fastestVer].mean) /
              stats[fastestVer].mean) *
            100
          ).toFixed(1) +
          "% slower than " +
          fastestVer
    })`
  );
  console.log(
    `- Consistency: StdDev ${stats[bestOverallVer].stdDev.toFixed(1)} ns (${
      mostConsistentVer === bestOverallVer
        ? "most consistent"
        : (
            ((stats[bestOverallVer].stdDev - stats[mostConsistentVer].stdDev) /
              stats[mostConsistentVer].stdDev) *
            100
          ).toFixed(1) +
          "% more variable than " +
          mostConsistentVer
    })`
  );
}

// Add implementation complexity note
const complexityNote = {
  v1: "Oriented area (condensed form): xa*(yb-yc) + xb*(yc-ya) + xc*(ya-yb)",
  v2: "Expanded cross product: (xa-xb)*(yc-yb) - (ya-yb)*(xc-xb)",
};

console.log(`\nImplementation Note: ${complexityNote[bestOverallVer]}`);

// Save complete results to file
const completeResults = {
  systemInfo,
  iterations: ITERATIONS,
  rawResults: results,
  statistics: stats,
  analysis: {
    fastestVersion: {
      name: fastestVer,
      mean: stats[fastestVer].mean,
      stdDev: stats[fastestVer].stdDev,
    },
    mostConsistentVersion: {
      name: mostConsistentVer,
      mean: stats[mostConsistentVer].mean,
      stdDev: stats[mostConsistentVer].stdDev,
    },
    bestOverallVersion: {
      name: bestOverallVer,
      mean: stats[bestOverallVer].mean,
      stdDev: stats[bestOverallVer].stdDev,
    },
    tTests: [["v1", "v2"]].map(([v1, v2]) => {
      const { t, df } = tTest(results[v1], results[v2]);
      return {
        comparison: `${v1} vs ${v2}`,
        tStatistic: t,
        degreesOfFreedom: df,
        significant: Math.abs(t) > 2.0,
      };
    }),
  },
};

const logFile = saveBenchmarkResults("dot_product", completeResults);
console.log(`\nFull results saved to: ${logFile}`);

// clang -O3 benchmark_dot_product.c -o benchmark_dot_product && node run_dot_product_benchmark.js
