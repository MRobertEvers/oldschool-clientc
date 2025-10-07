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
const ITERATIONS = 20; // Number of times to run the benchmark
const EXECUTABLE = path.join(__dirname, "benchmark_reciprocal_division");

// Data structures to store results
const results = {
    integer: [], // Regular integer division
    float: [], // Regular float division  
    sse_fast: [], // SSE fast reciprocal
    sse_refined: [], // SSE fast reciprocal with Newton-Raphson refinement
};

// Helper function to parse benchmark output
function parseBenchmarkOutput(output) {
    const lines = output.toString().split("\n");
    const timings = {};
    const accuracy = {};

    lines.forEach((line) => {
        if (line.includes("Integer Division:")) {
            const match = line.match(/Integer Division:\s+([\d.]+)\s+μs/);
            if (match) {
                timings.integer = parseFloat(match[1]);
            }
        } else if (line.includes("Float Division:")) {
            const match = line.match(/Float Division:\s+([\d.]+)\s+μs/);
            if (match) {
                timings.float = parseFloat(match[1]);
            }
        } else if (line.includes("SSE Fast Reciprocal:") && !line.includes("Refined")) {
            const match = line.match(/SSE Fast Reciprocal:\s+([\d.]+)\s+μs/);
            if (match) {
                timings.sse_fast = parseFloat(match[1]);
            }
        } else if (line.includes("SSE Fast Reciprocal (Refined):")) {
            const match = line.match(/SSE Fast Reciprocal \(Refined\):\s+([\d.]+)\s+μs/);
            if (match) {
                timings.sse_refined = parseFloat(match[1]);
            }
        } else if (line.includes("Average absolute error:")) {
            const match = line.match(/Average absolute error:\s+([\d.]+)/);
            if (match) {
                accuracy.avgAbsError = parseFloat(match[1]);
            }
        } else if (line.includes("Maximum absolute error:")) {
            const match = line.match(/Maximum absolute error:\s+([\d.]+)/);
            if (match) {
                accuracy.maxAbsError = parseFloat(match[1]);
            }
        } else if (line.includes("Average relative error:")) {
            const match = line.match(/Average relative error:\s+([\d.]+)\s+\(([\d.]+)%\)/);
            if (match) {
                accuracy.avgRelError = parseFloat(match[1]);
                accuracy.avgRelErrorPercent = parseFloat(match[2]);
            }
        } else if (line.includes("Maximum relative error:")) {
            const match = line.match(/Maximum relative error:\s+([\d.]+)\s+\(([\d.]+)%\)/);
            if (match) {
                accuracy.maxRelError = parseFloat(match[1]);
                accuracy.maxRelErrorPercent = parseFloat(match[2]);
            }
        }
    });

    return { timings, accuracy };
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
console.log(`Running ${ITERATIONS} iterations of reciprocal division benchmark...`);
console.log("This may take a while...\n");

let accuracyData = null; // Store accuracy data from first run

for (let i = 0; i < ITERATIONS; i++) {
    process.stdout.write(`Progress: ${i + 1}/${ITERATIONS}\r`);
    const output = execSync(EXECUTABLE);
    const { timings, accuracy } = parseBenchmarkOutput(output);

    // Store accuracy data from first iteration
    if (i === 0) {
        accuracyData = accuracy;
    }

    results.integer.push(timings.integer);
    results.float.push(timings.float);
    results.sse_fast.push(timings.sse_fast);
    results.sse_refined.push(timings.sse_refined);
}

console.log("\n\nBenchmark Results:\n");

// Calculate and display statistics
const methods = ["integer", "float", "sse_fast", "sse_refined"];
const methodNames = {
    integer: "Integer Division",
    float: "Float Division",
    sse_fast: "SSE Fast Reciprocal",
    sse_refined: "SSE Fast Reciprocal (Refined)",
};
const stats = {};

methods.forEach((method) => {
    stats[method] = calculateStats(results[method]);
    console.log(
        `${methodNames[method].padEnd(30)} Mean: ${stats[method].mean.toFixed(
            1
        )} ± ${stats[method].ci95.toFixed(1)} μs`
    );
    console.log(`${"".padEnd(30)} Min:  ${stats[method].min.toFixed(1)} μs`);
    console.log(`${"".padEnd(30)} Max:  ${stats[method].max.toFixed(1)} μs`);
    console.log(
        `${"".padEnd(30)} StdDev: ${stats[method].stdDev.toFixed(1)} μs\n`
    );
});

// Display accuracy information
if (accuracyData) {
    console.log("SSE Fast Reciprocal Accuracy Analysis:\n");
    console.log(`Average Absolute Error: ${accuracyData.avgAbsError?.toFixed(6) || 'N/A'}`);
    console.log(`Maximum Absolute Error: ${accuracyData.maxAbsError?.toFixed(6) || 'N/A'}`);
    console.log(`Average Relative Error: ${accuracyData.avgRelErrorPercent?.toFixed(4) || 'N/A'}%`);
    console.log(`Maximum Relative Error: ${accuracyData.maxRelErrorPercent?.toFixed(4) || 'N/A'}%\n`);
}

// Performance comparisons
console.log("Performance Comparisons:\n");

// SSE vs Integer
const sseVsInt = tTest(results.sse_fast, results.integer);
const speedupSSEvsInt = stats.integer.mean / stats.sse_fast.mean;
console.log(`SSE Fast vs Integer Division:`);
console.log(`  Speedup: ${speedupSSEvsInt.toFixed(2)}x ${speedupSSEvsInt > 1 ? 'faster' : 'slower'}`);
console.log(`  t-statistic: ${sseVsInt.t.toFixed(4)}`);
console.log(`  Statistically significant? ${Math.abs(sseVsInt.t) > 2.0 ? "YES" : "NO"}\n`);

// SSE vs Float
const sseVsFloat = tTest(results.sse_fast, results.float);
const speedupSSEvsFloat = stats.float.mean / stats.sse_fast.mean;
console.log(`SSE Fast vs Float Division:`);
console.log(`  Speedup: ${speedupSSEvsFloat.toFixed(2)}x ${speedupSSEvsFloat > 1 ? 'faster' : 'slower'}`);
console.log(`  t-statistic: ${sseVsFloat.t.toFixed(4)}`);
console.log(`  Statistically significant? ${Math.abs(sseVsFloat.t) > 2.0 ? "YES" : "NO"}\n`);

// SSE Refined vs SSE Fast
const refinedVsFast = tTest(results.sse_refined, results.sse_fast);
const speedupRefinedVsFast = stats.sse_fast.mean / stats.sse_refined.mean;
console.log(`SSE Refined vs SSE Fast:`);
console.log(`  Speedup: ${speedupRefinedVsFast.toFixed(2)}x ${speedupRefinedVsFast > 1 ? 'faster' : 'slower'}`);
console.log(`  t-statistic: ${refinedVsFast.t.toFixed(4)}`);
console.log(`  Statistically significant? ${Math.abs(refinedVsFast.t) > 2.0 ? "YES" : "NO"}\n`);

// Simple ASCII visualization
console.log("Performance Visualization (each █ = 500 microseconds):\n");

methods.forEach((method) => {
    const mean = stats[method].mean;
    const bars = "█".repeat(Math.max(1, Math.round(mean / 500)));
    console.log(
        `${methodNames[method].padEnd(30)} ${bars} (${mean.toFixed(1)} μs)`
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
    // Weight speed 80% and consistency 20%
    scores[method] =
        0.8 * (stats[method].mean / maxMean) +
        0.2 * (stats[method].stdDev / maxStdDev);
});

const bestOverallMethod = methods.reduce((a, b) =>
    scores[a] < scores[b] ? a : b
);

// Output recommendations
console.log(`Fastest Implementation: ${methodNames[fastestMethod]}`);
console.log(`- ${stats[fastestMethod].mean.toFixed(1)} μs on average`);
console.log(
    `Most Consistent Implementation: ${methodNames[mostConsistentMethod]}`
);
console.log(`- StdDev: ${stats[mostConsistentMethod].stdDev.toFixed(1)} μs`);
console.log(`\nBest Overall Implementation: ${methodNames[bestOverallMethod]}`);
console.log(
    "Based on weighted combination of speed (80%) and consistency (20%)"
);

// Provide detailed analysis
console.log("\nDetailed Analysis:");

// Accuracy vs Performance trade-off analysis
if (accuracyData) {
    console.log("\nAccuracy vs Performance Trade-off:");
    console.log(`- SSE Fast Reciprocal offers ${speedupSSEvsFloat.toFixed(2)}x speedup over float division`);
    console.log(`- Average relative error: ${accuracyData.avgRelErrorPercent?.toFixed(4) || 'N/A'}%`);
    console.log(`- Maximum relative error: ${accuracyData.maxRelErrorPercent?.toFixed(4) || 'N/A'}%`);

    if (accuracyData.avgRelErrorPercent && accuracyData.avgRelErrorPercent < 1.0) {
        console.log("- Excellent accuracy for most applications");
    } else if (accuracyData.avgRelErrorPercent && accuracyData.avgRelErrorPercent < 5.0) {
        console.log("- Good accuracy for many applications");
    } else {
        console.log("- Consider accuracy requirements for your use case");
    }
}

// Use case recommendations
console.log("\nUse Case Recommendations:");
console.log("- For maximum accuracy: Use float division");
console.log("- For maximum performance: Use SSE fast reciprocal");
console.log("- For balanced performance/accuracy: Use SSE refined reciprocal");
console.log("- For integer results only: Use integer division");

// Save complete results to file
const completeResults = {
    systemInfo,
    iterations: ITERATIONS,
    rawResults: results,
    statistics: stats,
    accuracy: accuracyData,
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
        speedups: {
            sseVsInteger: speedupSSEvsInt,
            sseVsFloat: speedupSSEvsFloat,
            refinedVsFast: speedupRefinedVsFast,
        },
        tTests: {
            sseVsInteger: {
                tStatistic: sseVsInt.t,
                significant: Math.abs(sseVsInt.t) > 2.0,
            },
            sseVsFloat: {
                tStatistic: sseVsFloat.t,
                significant: Math.abs(sseVsFloat.t) > 2.0,
            },
            refinedVsFast: {
                tStatistic: refinedVsFast.t,
                significant: Math.abs(refinedVsFast.t) > 2.0,
            },
        },
    },
};

const logFile = saveBenchmarkResults("reciprocal_division", completeResults);
console.log(`\nFull results saved to: ${logFile}`);

// clang -O3 -msse benchmark_reciprocal_division.c -o benchmark_reciprocal_division && node run_reciprocal_division_benchmark.js
