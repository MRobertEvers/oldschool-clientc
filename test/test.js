const net = require("net");

// Configuration
const HOST = "127.0.0.1";
const PORT = 4949;

// Create TCP client
const client = new net.Socket();

client.connect(PORT, HOST, () => {
  console.log(`Connected to ${HOST}:${PORT}`);

  // Read request (expect 8 bytes: 2 x 32-bit integers)

  // Write two 32 bit integers to the server
  // htonl
  const tableNum = 255;
  const archiveNum = 7;
  const buffer = Buffer.from([
    1,
    tableNum & 0xff,
    (archiveNum >> 24) & 0xff,
    (archiveNum >> 16) & 0xff,
    (archiveNum >> 8) & 0xff,
    archiveNum & 0xff,
  ]);

  client.write(buffer);
});

// Handle data received from server
client.on("data", (data) => {
  // Decode the first 4 bytes as the data size (big-endian)
  if (data.length >= 4) {
    const status = data.readUInt32BE(0);
    console.log("Status from server:", status);
  }

  if (data.length >= 8) {
    const dataSize = data.readUInt32BE(4);
    console.log("Data size from server:", dataSize);
  } else {
    console.log("Data received is too short to contain a size.");
  }

  // Close the connection after receiving data
  client.destroy();
});

// Handle connection close
client.on("close", () => {
  console.log("Connection closed");
});

// Handle errors
client.on("error", (err) => {
  console.error("Error:", err.message);
});
