const { execSync } = require("child_process");

const RUNS = 1000;
const times = [];

console.log(`Running ./a.out ${RUNS} times...`);

for (let i = 1; i <= RUNS; i++) {
  try {
    // console.log(`Run ${i}/${RUNS}`);
    const output = execSync("./a.out", { encoding: "utf-8", cwd: __dirname });

    // Extract time from output like "Time elapsed: 284750 ns"
    const match = output.match(/Time elapsed:\s*(\d+)\s*ns/);
    if (match) {
      const timeNs = parseInt(match[1]);
      times.push(timeNs);
      //   console.log(`  Time elapsed: ${timeNs} ns`);
    } else {
      console.log(
        `  Warning: Could not parse time from output: ${output.trim()}`
      );
    }
  } catch (error) {
    console.error(`Error on run ${i}:`, error.message);
  }
}

if (times.length > 0) {
  const sum = times.reduce((a, b) => a + b, 0);
  const average = sum / times.length;
  const min = Math.min(...times);
  const max = Math.max(...times);

  console.log("\n=== RESULTS ===");
  console.log(`Successful runs: ${times.length}/${RUNS}`);
  console.log(`Average: ${average.toFixed(2)} ns`);
  console.log(`Minimum: ${min} ns`);
  console.log(`Maximum: ${max} ns`);
  console.log(`Range: ${max - min} ns`);
  console.log(
    `Standard deviation: ${Math.sqrt(
      times.map((t) => Math.pow(t - average, 2)).reduce((a, b) => a + b, 0) /
        times.length
    ).toFixed(2)} ns`
  );
} else {
  console.log("No successful runs to analyze.");
}
