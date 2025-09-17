const { execSync } = require("child_process");
const fs = require("fs");
const os = require("os");
const path = require("path");

function getSystemInfo() {
  const info = {
    platform: os.platform(),
    arch: os.arch(),
    cpus: os.cpus(),
    totalMemory: os.totalmem(),
    freeMemory: os.freemem(),
  };

  // Get more detailed CPU info on macOS
  if (os.platform() === "darwin") {
    try {
      info.detailedCPU = execSync("sysctl -n machdep.cpu.brand_string")
        .toString()
        .trim();
      info.cpuCores = execSync("sysctl -n hw.physicalcpu_max")
        .toString()
        .trim();
      info.cpuThreads = execSync("sysctl -n hw.logicalcpu_max")
        .toString()
        .trim();

      // Try to get GPU info
      const systemProfiler = execSync(
        "system_profiler SPDisplaysDataType"
      ).toString();
      const gpuMatch = systemProfiler.match(/Chipset Model: (.+?)(?:\n|$)/);
      info.gpu = gpuMatch ? gpuMatch[1].trim() : "Unknown";
    } catch (e) {
      console.error("Error getting detailed system info:", e.message);
    }
  }
  // Add Linux and Windows specific info gathering here if needed

  return info;
}

function formatSystemInfo(info) {
  const cpuInfo = info.detailedCPU || info.cpus[0].model;
  const memoryGB = (info.totalMemory / (1024 * 1024 * 1024)).toFixed(1);

  return `System Information:
• CPU: ${cpuInfo}
• Physical Cores: ${info.cpuCores || info.cpus.length}
• Logical Cores: ${info.cpuThreads || info.cpus.length}
• Memory: ${memoryGB}GB
• GPU: ${info.gpu || "N/A"}
• Platform: ${info.platform} ${os.release()}
• Architecture: ${info.arch}`;
}

function saveBenchmarkResults(benchmarkName, results) {
  const logDir = path.join(__dirname, "benchmark_logs");
  if (!fs.existsSync(logDir)) {
    fs.mkdirSync(logDir);
  }

  const timestamp = new Date().toISOString().replace(/[:.]/g, "-");
  const filename = path.join(logDir, `${benchmarkName}_${timestamp}.txt`);

  const systemInfo = getSystemInfo();
  const formattedInfo = formatSystemInfo(systemInfo);

  // Convert results object to string, handling circular references
  const resultsStr = JSON.stringify(
    results,
    (key, value) => {
      if (key === "systemInfo") {
        return undefined; // Skip full system info in results
      }
      return value;
    },
    2
  );

  const content = `${formattedInfo}

Timestamp: ${new Date().toISOString()}
Benchmark: ${benchmarkName}

${resultsStr}`;

  fs.writeFileSync(filename, content);
  return filename;
}

module.exports = {
  getSystemInfo,
  formatSystemInfo,
  saveBenchmarkResults,
};
