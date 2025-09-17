const { execSync } = require("child_process");
const path = require("path");

// Configuration
const ITERATIONS = 30; // Number of times to run the benchmark
const EXECUTABLE = path.join(__dirname, "benchmark_face_sort");

// Data structures to store results
const results = {
  fullscan: [],
  qsort: [],
  insert: [],
};

// Helper function to parse benchmark output
function parseBenchmarkOutput(output) {
  const lines = output.toString().split("\n");
  const timings = {};

  lines.forEach((line) => {
    if (line.includes("Pipeline Fullscan:")) {
      timings.fullscan = parseFloat(line.split(":")[1].replace("us", ""));
    } else if (line.includes("Pipeline Qsort:")) {
      timings.qsort = parseFloat(line.split(":")[1].replace("us", ""));
    } else if (line.includes("Pipeline Insert:")) {
      timings.insert = parseFloat(line.split(":")[1].replace("us", ""));
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
console.log(`Running ${ITERATIONS} iterations of face sort benchmark...`);
console.log("This may take a while...\n");

for (let i = 0; i < ITERATIONS; i++) {
  process.stdout.write(`Progress: ${i + 1}/${ITERATIONS}\r`);
  const output = execSync(EXECUTABLE);
  const timings = parseBenchmarkOutput(output);

  results.fullscan.push(timings.fullscan);
  results.qsort.push(timings.qsort);
  results.insert.push(timings.insert);
}

console.log("\n\nResults:\n");

// Calculate and display statistics
const algorithms = ["fullscan", "qsort", "insert"];
const stats = {};

algorithms.forEach((algo) => {
  stats[algo] = calculateStats(results[algo]);
  console.log(
    `${algo.padEnd(10)} Mean: ${stats[algo].mean.toFixed(1)} ± ${stats[
      algo
    ].ci95.toFixed(1)} µs`
  );
  console.log(`${"".padEnd(10)} Min:  ${stats[algo].min.toFixed(1)} µs`);
  console.log(`${"".padEnd(10)} Max:  ${stats[algo].max.toFixed(1)} µs`);
  console.log(`${"".padEnd(10)} StdDev: ${stats[algo].stdDev.toFixed(1)} µs\n`);
});

// Perform t-tests between all pairs
console.log("Statistical Significance (t-test):\n");

const pairs = [
  ["fullscan", "qsort"],
  ["fullscan", "insert"],
  ["qsort", "insert"],
];

pairs.forEach(([algo1, algo2]) => {
  const { t, df } = tTest(results[algo1], results[algo2]);
  const mean1 = stats[algo1].mean;
  const mean2 = stats[algo2].mean;
  const diff = (
    (Math.abs(mean1 - mean2) / Math.min(mean1, mean2)) *
    100
  ).toFixed(2);

  console.log(`${algo1} vs ${algo2}:`);
  console.log(`t-statistic: ${t.toFixed(4)}`);
  console.log(`Degrees of freedom: ${df}`);
  console.log(`Relative difference: ${diff}%`);

  // Critical value for t (approximation for 95% confidence)
  const significant = Math.abs(t) > 2.0;
  console.log(`Statistically significant? ${significant ? "YES" : "NO"}\n`);
});

// Simple ASCII visualization
console.log("Performance Visualization (each █ = 1 microsecond):\n");

algorithms.forEach((algo) => {
  const mean = stats[algo].mean;
  const bars = "█".repeat(Math.round(mean)); // One bar per microsecond
  console.log(`${algo.padEnd(10)} ${bars} (${mean.toFixed(1)} µs)`);
});

// Determine the best algorithm
console.log("\nAlgorithm Recommendation:\n");

// Find fastest algorithm
const fastestAlgo = algorithms.reduce((a, b) =>
  stats[a].mean < stats[b].mean ? a : b
);

// Find most consistent algorithm (lowest stddev)
const mostConsistentAlgo = algorithms.reduce((a, b) =>
  stats[a].stdDev < stats[b].stdDev ? a : b
);

// Calculate a composite score (lower is better)
// Normalize both mean and stddev to 0-1 range and combine with weights
const scores = {};
const maxMean = Math.max(...algorithms.map((a) => stats[a].mean));
const maxStdDev = Math.max(...algorithms.map((a) => stats[a].stdDev));

algorithms.forEach((algo) => {
  // Weight speed 70% and consistency 30%
  scores[algo] =
    0.7 * (stats[algo].mean / maxMean) + 0.3 * (stats[algo].stdDev / maxStdDev);
});

const bestOverallAlgo = algorithms.reduce((a, b) =>
  scores[a] < scores[b] ? a : b
);

// Output recommendations
console.log(`Fastest Algorithm: ${fastestAlgo}`);
console.log(`- ${stats[fastestAlgo].mean.toFixed(1)} µs on average`);
console.log(`Most Consistent Algorithm: ${mostConsistentAlgo}`);
console.log(`- StdDev: ${stats[mostConsistentAlgo].stdDev.toFixed(1)} µs`);
console.log(`\nBest Overall Algorithm: ${bestOverallAlgo}`);
console.log(
  "Based on weighted combination of speed (70%) and consistency (30%)"
);

// Provide detailed reasoning
console.log("\nDetailed Analysis:");
if (bestOverallAlgo === fastestAlgo && bestOverallAlgo === mostConsistentAlgo) {
  console.log(
    `${bestOverallAlgo} is the clear winner, being both the fastest and most consistent.`
  );
} else {
  console.log(`${bestOverallAlgo} is recommended because:`);
  console.log(
    `- Speed: ${stats[bestOverallAlgo].mean.toFixed(3)} ms (${
      fastestAlgo === bestOverallAlgo
        ? "fastest"
        : (
            ((stats[bestOverallAlgo].mean - stats[fastestAlgo].mean) /
              stats[fastestAlgo].mean) *
            100
          ).toFixed(1) +
          "% slower than " +
          fastestAlgo
    })`
  );
  console.log(
    `- Consistency: StdDev ${stats[bestOverallAlgo].stdDev.toFixed(6)} sec (${
      mostConsistentAlgo === bestOverallAlgo
        ? "most consistent"
        : (
            ((stats[bestOverallAlgo].stdDev -
              stats[mostConsistentAlgo].stdDev) /
              stats[mostConsistentAlgo].stdDev) *
            100
          ).toFixed(1) +
          "% more variable than " +
          mostConsistentAlgo
    })`
  );
}

// Add implementation complexity note
const complexityNote = {
  fullscan: "Simple linear scan",
  qsort: "Uses standard library qsort, O(n log n)",
  insert: "Insertion sort for depths, O(n²) but good for small n",
};

console.log(`\nImplementation Note: ${complexityNote[bestOverallAlgo]}`);

// clang -O3 benchmark_face_sort.c -o benchmark_face_sort && node run_face_sort_benchmark.js
