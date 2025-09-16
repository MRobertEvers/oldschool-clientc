const arrVertexX = [];
const arrVertexY = [];
const arrVertexZ = [];

for (let i = 0; i < 768; i++) {
  arrVertexX.push(Math.floor(Math.random() * 2048));
  arrVertexY.push(Math.floor(Math.random() * 2048));
  arrVertexZ.push(Math.floor(Math.random() * 2048));
}
const arrFaceIndicesA = [];
const arrFaceIndicesB = [];
const arrFaceIndicesC = [];
for (let i = 0; i < 128; i++) {
  arrFaceIndicesA.push(Math.floor(Math.random() * 768));
  arrFaceIndicesB.push(Math.floor(Math.random() * 768));
  arrFaceIndicesC.push(Math.floor(Math.random() * 768));
}

console.log(
  `
    static int arrVertexX[768] = { ${arrVertexX.join(", ")} };
    static int arrVertexY[768] = { ${arrVertexY.join(", ")} };
    static int arrVertexZ[768] = { ${arrVertexZ.join(", ")} };
    static int arrFaceIndicesA[128] = { ${arrFaceIndicesA.join(", ")} };
    static int arrFaceIndicesB[128] = { ${arrFaceIndicesB.join(", ")} };
    static int arrFaceIndicesC[128] = { ${arrFaceIndicesC.join(", ")} };
    `
);
