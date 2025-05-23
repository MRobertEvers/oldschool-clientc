const fs = require("fs");

fs.readFile("anim.json", "utf8", (err, data) => {
  if (err) {
    console.error("Error reading file:", err);
    return;
  }

  try {
    const animData = JSON.parse(data);
    console.log("Full animation data:", animData);

    // Print array lengths
    console.log("\nArray lengths:");
    console.log("Number of animations:", animData.length);

    animData.forEach((anim, index) => {
      console.log(`\nAnimation ${anim.id}:`);
      console.log(`- Translator count: ${anim.translatorCount}`);
      console.log(`- Frame IDs length: ${anim.indexFrameIds.length}`);
      console.log(`- Types length: ${anim.framemap.types.length}`);
    });
  } catch (parseErr) {
    console.error("Error parsing JSON:", parseErr);
  }
});
