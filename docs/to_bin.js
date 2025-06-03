const fs = require("fs");

const tiles = JSON.parse(fs.readFileSync("all_tiles.json", "utf8"));
const tile_count = tiles.length;

// Create a dynamic buffer that grows as needed
let buffer = Buffer.alloc(0);
let offset = 0;

// Helper function to write to buffer and grow if needed
function writeToBuffer(value, size) {
  const newSize = offset + size;
  if (newSize > buffer.length) {
    const newBuffer = Buffer.alloc(Math.max(buffer.length * 2, newSize));
    buffer.copy(newBuffer);
    buffer = newBuffer;
  }
  if (size === 4) {
    buffer.writeInt32LE(value, offset);
  }
  offset += size;
}

// write the number of tiles
writeToBuffer(tile_count, 4);

// write each tile
for (const tile of tiles) {
  writeToBuffer(tile.level, 4);
  writeToBuffer(tile.x, 4);
  writeToBuffer(tile.y, 4);
  writeToBuffer(tile.shape, 4);
  writeToBuffer(tile.rotation, 4);
  writeToBuffer(tile.textureId, 4);
  writeToBuffer(tile.underlayRgb, 4);
  writeToBuffer(tile.overlayRgb, 4);

  // Write vertex count and vertices
  writeToBuffer(tile.vertices.length, 4);

  for (const { x, y, z } of tile.vertices) {
    writeToBuffer(x, 4);
    writeToBuffer(y, 4);
    writeToBuffer(z, 4);
  }

  writeToBuffer(tile.faces.length, 4);
  for (const face of tile.faces) {
    for (let i = 0; i < face.vertices.length; i++) {
      const vertexIndex = tile.vertices.findIndex(
        (v) =>
          v.x === face.vertices[i].x &&
          v.y === face.vertices[i].y &&
          v.z === face.vertices[i].z
      );
      writeToBuffer(vertexIndex, 4);
    }
    writeToBuffer(face.vertices[0].hsl, 4);
  }
}

// Trim the buffer to the actual size used
buffer = buffer.slice(0, offset);

fs.writeFileSync("tiles_data.bin", buffer);
