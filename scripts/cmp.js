const fs = require("fs");

function compareFiles(file1, file2) {
  const lines1 = fs.readFileSync(file1, "utf8").split("\n");
  const lines2 = fs.readFileSync(file2, "utf8").split("\n");

  if (lines1.length !== lines2.length) {
    console.log(
      `Files have different number of lines: ${file1} has ${lines1.length} lines, ${file2} has ${lines2.length} lines`
    );
    return false;
  }

  let filesMatch = true;
  for (let i = 0; i < lines1.length; i++) {
    if (lines1[i] !== lines2[i]) {
      console.log(`Mismatch at line ${i + 1}:`);
      console.log(`${file1}: ${lines1[i]}`);
      console.log(`${file2}: ${lines2[i]}`);
      filesMatch = false;
    }
  }

  if (filesMatch) {
    console.log("Files match exactly");
    return true;
  }
  return false;
}

// if (process.argv.length !== 4) {
//   console.log("Usage: node cmp.js <file1> <file2>");
//   process.exit(1);
// }

const file1 = "./scripts/map_50_50_0.txt";
const file2 = "./scripts/mapme_50_50.txt";
compareFiles(file1, file2);
