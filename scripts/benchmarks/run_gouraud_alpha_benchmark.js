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
const EXECUTABLE = path.join(__dirname, "benchmark_gouraud_alpha");

// Data structures to store results
const results = {
  edge: [], // Edge-based interpolation
  barycentric: [], // Barycentric interpolation
};

// Helper function to parse benchmark output
function parseBenchmarkOutput(output) {
  const lines = output.toString().split("\n");
  const timings = {};

  lines.forEach((line) => {
    if (line.includes("Alpha Gouraud OLD (edge color):")) {
      timings.edge = parseFloat(line.split(":")[1].split("us")[0].trim());
    } else if (line.includes("Alpha Gouraud NEW (barycentric):")) {
      timings.barycentric = parseFloat(
        line.split(":")[1].split("us")[0].trim()
      );
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
console.log(`Running ${ITERATIONS} iterations of Gouraud alpha benchmark...`);
console.log("This may take a while...\n");

for (let i = 0; i < ITERATIONS; i++) {
  process.stdout.write(`Progress: ${i + 1}/${ITERATIONS}\r`);
  const output = execSync(EXECUTABLE);
  const timings = parseBenchmarkOutput(output);

  results.edge.push(timings.edge);
  results.barycentric.push(timings.barycentric);
}

console.log("\n\nResults:\n");

// Calculate and display statistics
const methods = ["edge", "barycentric"];
const methodNames = {
  edge: "Edge-based interpolation",
  barycentric: "Barycentric interpolation",
};
const stats = {};

methods.forEach((method) => {
  stats[method] = calculateStats(results[method]);
  console.log(
    `${methodNames[method].padEnd(25)} Mean: ${stats[method].mean.toFixed(
      3
    )} ± ${stats[method].ci95.toFixed(3)} µs`
  );
  console.log(`${"".padEnd(25)} Min:  ${stats[method].min.toFixed(3)} µs`);
  console.log(`${"".padEnd(25)} Max:  ${stats[method].max.toFixed(3)} µs`);
  console.log(
    `${"".padEnd(25)} StdDev: ${stats[method].stdDev.toFixed(3)} µs\n`
  );
});

// Perform t-test between methods
console.log("Statistical Significance (t-test):\n");

const { t, df } = tTest(results.edge, results.barycentric);
const mean1 = stats.edge.mean;
const mean2 = stats.barycentric.mean;
const diff = ((Math.abs(mean1 - mean2) / Math.min(mean1, mean2)) * 100).toFixed(
  2
);

console.log(`${methodNames.edge} vs ${methodNames.barycentric}:`);
console.log(`t-statistic: ${t.toFixed(4)}`);
console.log(`Degrees of freedom: ${df}`);
console.log(`Relative difference: ${diff}%`);

// Critical value for t (approximation for 95% confidence)
const significant = Math.abs(t) > 2.0;
console.log(`Statistically significant? ${significant ? "YES" : "NO"}\n`);

// Simple ASCII visualization
console.log("Performance Visualization (each █ = 50 microseconds):\n");

methods.forEach((method) => {
  const mean = stats[method].mean;
  const bars = "█".repeat(Math.round(mean / 50));
  console.log(
    `${methodNames[method].padEnd(25)} ${bars} (${mean.toFixed(3)} µs)`
  );
});

// Determine the best method
console.log("\nImplementation Recommendation:\n");

// Find fastest method
const fastestMethod = methods.reduce((a, b) =>
  stats[a].mean < stats[b].mean ? a : b
);

// Find most consistent method (lowest stddev)
const mostConsistentMethod = methods.reduce((a, b) =>
  stats[a].stdDev < stats[b].stdDev ? a : b
);

// Calculate a composite score (lower is better)
// Normalize both mean and stddev to 0-1 range and combine with weights
const scores = {};
const maxMean = Math.max(...methods.map((m) => stats[m].mean));
const maxStdDev = Math.max(...methods.map((m) => stats[m].stdDev));

methods.forEach((method) => {
  // Weight speed 70% and consistency 30%
  scores[method] =
    0.7 * (stats[method].mean / maxMean) +
    0.3 * (stats[method].stdDev / maxStdDev);
});

const bestOverallMethod = methods.reduce((a, b) =>
  scores[a] < scores[b] ? a : b
);

// Output recommendations
console.log(`Fastest Implementation: ${methodNames[fastestMethod]}`);
console.log(`- ${stats[fastestMethod].mean.toFixed(3)} µs on average`);
console.log(
  `Most Consistent Implementation: ${methodNames[mostConsistentMethod]}`
);
console.log(`- StdDev: ${stats[mostConsistentMethod].stdDev.toFixed(3)} µs`);
console.log(`\nBest Overall Implementation: ${methodNames[bestOverallMethod]}`);
console.log(
  "Based on weighted combination of speed (70%) and consistency (30%)"
);

// Provide detailed reasoning
console.log("\nDetailed Analysis:");
if (
  bestOverallMethod === fastestMethod &&
  bestOverallMethod === mostConsistentMethod
) {
  console.log(
    `${methodNames[bestOverallMethod]} is the clear winner, being both the fastest and most consistent.`
  );
} else {
  console.log(`${methodNames[bestOverallMethod]} is recommended because:`);
  console.log(
    `- Speed: ${stats[bestOverallMethod].mean.toFixed(3)} µs (${
      fastestMethod === bestOverallMethod
        ? "fastest"
        : (
            ((stats[bestOverallMethod].mean - stats[fastestMethod].mean) /
              stats[fastestMethod].mean) *
            100
          ).toFixed(1) +
          "% slower than " +
          methodNames[fastestMethod]
    })`
  );
  console.log(
    `- Consistency: StdDev ${stats[bestOverallMethod].stdDev.toFixed(3)} µs (${
      mostConsistentMethod === bestOverallMethod
        ? "most consistent"
        : (
            ((stats[bestOverallMethod].stdDev -
              stats[mostConsistentMethod].stdDev) /
              stats[mostConsistentMethod].stdDev) *
            100
          ).toFixed(1) +
          "% more variable than " +
          methodNames[mostConsistentMethod]
    })`
  );
}

// Add implementation complexity note
const complexityNote = {
  edge: "Uses edge-walking with linear color interpolation along edges and scanlines",
  barycentric:
    "Uses barycentric coordinates for direct color interpolation at each pixel",
};

console.log(`\nImplementation Note: ${complexityNote[bestOverallMethod]}`);

// Save complete results to file
const completeResults = {
  systemInfo,
  iterations: ITERATIONS,
  rawResults: results,
  statistics: stats,
  analysis: {
    fastestMethod: {
      name: methodNames[fastestMethod],
      mean: stats[fastestMethod].mean,
      stdDev: stats[fastestMethod].stdDev,
    },
    mostConsistentMethod: {
      name: methodNames[mostConsistentMethod],
      mean: stats[mostConsistentMethod].mean,
      stdDev: stats[mostConsistentMethod].stdDev,
    },
    bestOverallMethod: {
      name: methodNames[bestOverallMethod],
      mean: stats[bestOverallMethod].mean,
      stdDev: stats[bestOverallMethod].stdDev,
    },
    tTest: {
      comparison: `${methodNames.edge} vs ${methodNames.barycentric}`,
      tStatistic: t,
      degreesOfFreedom: df,
      significant: Math.abs(t) > 2.0,
    },
  },
};

const logFile = saveBenchmarkResults("gouraud_alpha", completeResults);
console.log(`\nFull results saved to: ${logFile}`);

// clang -O3 benchmark_gouraud_alpha.c -o benchmark_gouraud_alpha && node run_gouraud_alpha_benchmark.js
