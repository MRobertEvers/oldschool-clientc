#!/usr/bin/env node
/**
 * Reads a binary file and writes it out as a C array in a .c file.
 * Usage: node bin_to_c_array.js [input.bin] [output.c]
 * Default: build/contiguous_buffer.bin -> build/contiguous_buffer.c
 */

const fs = require("fs");
const path = require("path");

const projectRoot = path.resolve(__dirname, "..");
const defaultInput = path.join(projectRoot, "build", "contiguous_buffer.bin");
const defaultOutput = path.join(projectRoot, "build", "contiguous_buffer.c");

const inputPath = process.argv[2] || defaultInput;
const outputPath = process.argv[3] || defaultOutput;

const bytes = fs.readFileSync(inputPath);
const basename = path.basename(outputPath, ".c");
const arrayName = basename.replace(/[^a-zA-Z0-9_]/g, "_");

const lines = [];
const bytesPerLine = 12;

for (let i = 0; i < bytes.length; i += bytesPerLine) {
  const chunk = bytes.slice(i, i + bytesPerLine);
  const hexBytes = Array.from(chunk).map(
    (b) => "0x" + b.toString(16).padStart(2, "0"),
  );
  lines.push("    " + hexBytes.join(", "));
}

const cSource = `/* Auto-generated from ${path.basename(inputPath)} - do not edit */
#include <stddef.h>

const unsigned char ${arrayName}[] = {
${lines.join(",\n")}
};

const size_t ${arrayName}_len = ${bytes.length};
`;

fs.writeFileSync(outputPath, cSource, "utf8");
console.log(`Wrote ${bytes.length} bytes to ${outputPath}`);
