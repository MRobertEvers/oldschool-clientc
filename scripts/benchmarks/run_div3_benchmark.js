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
const EXECUTABLE = path.join(__dirname, "benchmark_div3_trick");

// Data structures to store results
const results = {
  regular: [], // Regular division
  mulshift: [], // Multiplication and shift trick
  muloverflow: [], // Multiplication with overflow trick
};

// Helper function to parse benchmark output
function parseBenchmarkOutput(output) {
  const lines = output.toString().split("\n");
  const timings = {};
  let correctnessCheck = false;

  lines.forEach((line) => {
    if (line.includes("Correctness check passed!")) {
      correctnessCheck = true;
    } else if (line.includes("Regular division")) {
      timings.regular = parseFloat(line.split(":")[1].split("ms")[0]);
    } else if (line.includes("Mul-shift trick")) {
      timings.mulshift = parseFloat(line.split(":")[1].split("ms")[0]);
    } else if (line.includes("Mul-overflow trick")) {
      timings.muloverflow = parseFloat(line.split(":")[1].split("ms")[0]);
    }
  });

  return { timings, correctnessCheck };
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
console.log(`Running ${ITERATIONS} iterations of division by 3 benchmark...`);
console.log("This may take a while...\n");

let allTestsPassed = true;

for (let i = 0; i < ITERATIONS; i++) {
  process.stdout.write(`Progress: ${i + 1}/${ITERATIONS}\r`);
  const output = execSync(EXECUTABLE);
  const { timings, correctnessCheck } = parseBenchmarkOutput(output);

  if (!correctnessCheck) {
    allTestsPassed = false;
    console.error("\nWarning: Correctness check failed in iteration", i + 1);
  }

  results.regular.push(timings.regular);
  results.mulshift.push(timings.mulshift);
  results.muloverflow.push(timings.muloverflow);
}

console.log("\n\nResults:\n");

if (!allTestsPassed) {
  console.log("⚠️  Warning: Some correctness checks failed!\n");
}

// Calculate and display statistics
const methods = ["regular", "mulshift", "muloverflow"];
const methodNames = {
  regular: "Regular division",
  mulshift: "Mul-shift trick",
  muloverflow: "Mul-overflow trick",
};
const stats = {};

methods.forEach((method) => {
  stats[method] = calculateStats(results[method]);
  console.log(
    `${methodNames[method].padEnd(20)} Mean: ${stats[method].mean.toFixed(
      3
    )} ± ${stats[method].ci95.toFixed(3)} ms`
  );
  console.log(`${"".padEnd(20)} Min:  ${stats[method].min.toFixed(3)} ms`);
  console.log(`${"".padEnd(20)} Max:  ${stats[method].max.toFixed(3)} ms`);
  console.log(
    `${"".padEnd(20)} StdDev: ${stats[method].stdDev.toFixed(3)} ms\n`
  );
});

// Perform t-tests between all pairs
console.log("Statistical Significance (t-test):\n");

const pairs = [
  ["regular", "mulshift"],
  ["regular", "muloverflow"],
  ["mulshift", "muloverflow"],
];

pairs.forEach(([method1, method2]) => {
  const { t, df } = tTest(results[method1], results[method2]);
  const mean1 = stats[method1].mean;
  const mean2 = stats[method2].mean;
  const diff = (
    (Math.abs(mean1 - mean2) / Math.min(mean1, mean2)) *
    100
  ).toFixed(2);

  console.log(`${methodNames[method1]} vs ${methodNames[method2]}:`);
  console.log(`t-statistic: ${t.toFixed(4)}`);
  console.log(`Degrees of freedom: ${df}`);
  console.log(`Relative difference: ${diff}%`);

  // Critical value for t (approximation for 95% confidence)
  const significant = Math.abs(t) > 2.0;
  console.log(`Statistically significant? ${significant ? "YES" : "NO"}\n`);
});

// Simple ASCII visualization
console.log("Performance Visualization (each █ = 10 milliseconds):\n");

methods.forEach((method) => {
  const mean = stats[method].mean;
  const bars = "█".repeat(Math.round(mean / 10));
  console.log(
    `${methodNames[method].padEnd(20)} ${bars} (${mean.toFixed(3)} ms)`
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
console.log(`- ${stats[fastestMethod].mean.toFixed(3)} ms on average`);
console.log(
  `Most Consistent Implementation: ${methodNames[mostConsistentMethod]}`
);
console.log(`- StdDev: ${stats[mostConsistentMethod].stdDev.toFixed(3)} ms`);
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
    `- Speed: ${stats[bestOverallMethod].mean.toFixed(3)} ms (${
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
    `- Consistency: StdDev ${stats[bestOverallMethod].stdDev.toFixed(3)} ms (${
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
  regular: "Simple n/3 division, relies on compiler optimization",
  mulshift: "Uses (n * 21845) >> 16 trick, avoids division",
  muloverflow:
    "Uses 64-bit multiplication with magic number 0xAAAAAAAB and right shift by 33",
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
    tTests: pairs.map(([m1, m2]) => {
      const { t, df } = tTest(results[m1], results[m2]);
      return {
        comparison: `${methodNames[m1]} vs ${methodNames[m2]}`,
        tStatistic: t,
        degreesOfFreedom: df,
        significant: Math.abs(t) > 2.0,
      };
    }),
  },
  correctnessChecks: {
    allPassed: allTestsPassed,
  },
};

const logFile = saveBenchmarkResults("div3_trick", completeResults);
console.log(`\nFull results saved to: ${logFile}`);

// clang -O3 benchmark_div3_trick.c -o benchmark_div3_trick && node run_div3_benchmark.js
