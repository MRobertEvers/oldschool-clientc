// void decodeType2(ModelDefinition def, byte[] var1)
// {
//     boolean var2 = false;  // hasTexturedFaces
//     boolean var3 = false;  // hasFaceTextures
//     InputStream var4 = new InputStream(var1);  // mainStream
//     InputStream var5 = new InputStream(var1);  // vertexXStream
//     InputStream var6 = new InputStream(var1);  // vertexYStream
//     InputStream var7 = new InputStream(var1);  // vertexZStream
//     InputStream var8 = new InputStream(var1);  // packedVertexGroupsStream
//     var4.setOffset(var1.length - 23);
//     int var9 = var4.readUnsignedShort();  // vertexCount
//     int var10 = var4.readUnsignedShort();  // faceCount
//     int var11 = var4.readUnsignedByte();   // textureCount
//     int var12 = var4.readUnsignedByte();   // hasFaceRenderTypes
//     int var13 = var4.readUnsignedByte();   // faceRenderPriority
//     int var14 = var4.readUnsignedByte();   // hasFaceTransparencies
//     int var15 = var4.readUnsignedByte();   // hasPackedTransparencyVertexGroups
//     int var16 = var4.readUnsignedByte();   // hasPackedVertexGroups
//     int var17 = var4.readUnsignedByte();   // hasAnimayaGroups
//     int var18 = var4.readUnsignedShort();  // vertexXDataByteCount
//     int var19 = var4.readUnsignedShort();  // vertexYDataByteCount
//     int var20 = var4.readUnsignedShort();  // vertexZDataByteCount
//     int var21 = var4.readUnsignedShort();  // faceIndexDataByteCount
//     int var22 = var4.readUnsignedShort();  // faceColorDataByteCount
//     byte var23 = 0;  // offsetOfVertexFlags
//     int var24 = var23 + var9;  // dataOffset = offsetOfVertexFlags + vertexCount
//     int var25 = var24;  // offsetOfFaceIndices
//     var24 += var10;  // dataOffset += faceCount
//     int var26 = var24;  // offsetOfFaceRenderPriorities
//     if( var13 == 255 )  // faceRenderPriority
//     {
//         var24 += var10;  // dataOffset += faceCount
//     }
//
//     int var27 = var24;  // offsetOfPackedTransparencyVertexGroups
//     if( var15 == 1 )  // hasPackedTransparencyVertexGroups
//     {
//         var24 += var10;  // dataOffset += faceCount
//     }
//
//     int var28 = var24;  // offsetOfFaceRenderTypes
//     if( var12 == 1 )  // hasFaceRenderTypes
//     {
//         var24 += var10;  // dataOffset += faceCount
//     }
//
//     int var29 = var24;  // offsetOfFaceColors
//     var24 += var22;  // dataOffset += faceColorDataByteCount
//     int var30 = var24;  // offsetOfFaceTransparencies
//     if( var14 == 1 )  // hasFaceTransparencies
//     {
//         var24 += var10;  // dataOffset += faceCount
//     }
//
//     int var31 = var24;  // offsetOfFaceIndexData
//     var24 += var21;  // dataOffset += faceIndexDataByteCount
//     int var32 = var24;  // offsetOfFaceColorsOrFaceTextures
//     var24 += var10 * 2;  // dataOffset += faceCount * 2
//     int var33 = var24;  // offsetOfTextureIndices
//     var24 += var11 * 6;  // dataOffset += textureCount * 6
//     int var34 = var24;  // offsetOfVertexXData
//     var24 += var18;  // dataOffset += vertexXDataByteCount
//     int var35 = var24;  // offsetOfVertexYData
//     var24 += var19;  // dataOffset += vertexYDataByteCount
//     int var10000 = var24 + var20;  // offsetOfVertexZData = dataOffset + vertexZDataByteCount
//     def.vertexCount = var9;
//     def.faceCount = var10;
//     def.numTextureFaces = var11;
//     def.vertexX = new int[var9];
//     def.vertexY = new int[var9];
//     def.vertexZ = new int[var9];
//     def.faceIndices1 = new int[var10];
//     def.faceIndices2 = new int[var10];
//     def.faceIndices3 = new int[var10];
//     if( var11 > 0 )
//     {
//         def.textureRenderTypes = new byte[var11];
//         def.texIndices1 = new short[var11];
//         def.texIndices2 = new short[var11];
//         def.texIndices3 = new short[var11];
//     }
//
//     if( var16 == 1 )  // hasPackedVertexGroups
//     {
//         def.packedVertexGroups = new int[var9];
//     }
//
//     if( var12 == 1 )  // hasFaceRenderTypes
//     {
//         def.faceRenderTypes = new byte[var10];
//         def.textureCoords = new byte[var10];
//         def.faceTextures = new short[var10];
//     }
//
//     if( var13 == 255 )  // faceRenderPriority
//     {
//         def.faceRenderPriorities = new byte[var10];
//     }
//     else
//     {
//         def.priority = (byte)var13;
//     }
//
//     if( var14 == 1 )  // hasFaceTransparencies
//     {
//         def.faceTransparencies = new byte[var10];
//     }
//
//     if( var15 == 1 )  // hasPackedTransparencyVertexGroups
//     {
//         def.packedTransparencyVertexGroups = new int[var10];
//     }
//
//     if( var17 == 1 )  // hasAnimayaGroups
//     {
//         def.animayaGroups = new int[var9][];
//         def.animayaScales = new int[var9][];
//     }
//
//     def.faceColors = new short[var10];
//     var4.setOffset(var23);  // offsetOfVertexFlags
//     var5.setOffset(var34);  // offsetOfVertexXData
//     var6.setOffset(var35);  // offsetOfVertexYData
//     var7.setOffset(var10000);  // offsetOfVertexZData
//     var8.setOffset(var27);  // offsetOfPackedTransparencyVertexGroups
//     int var37 = 0;  // previousVertexX
//     int var38 = 0;  // previousVertexY
//     int var39 = 0;  // previousVertexZ
//
//     int var40;
//     int var41;
//     int var42;
//     int var43;
//     int var44;
//     for( var40 = 0; var40 < var9; ++var40 )  // vertexCount
//     {
//         var41 = var4.readUnsignedByte();  // vertexFlags
//         var42 = 0;  // deltaX
//         if( (var41 & 1) != 0 )
//         {
//             var42 = var5.readShortSmart();  // deltaX = vertexXStream.readShortSmart()
//         }
//
//         var43 = 0;  // deltaY
//         if( (var41 & 2) != 0 )
//         {
//             var43 = var6.readShortSmart();  // deltaY = vertexYStream.readShortSmart()
//         }
//
//         var44 = 0;  // deltaZ
//         if( (var41 & 4) != 0 )
//         {
//             var44 = var7.readShortSmart();  // deltaZ = vertexZStream.readShortSmart()
//         }
//
//         def.vertexX[var40] = var37 + var42;  // vertexX[i] = previousVertexX + deltaX
//         def.vertexY[var40] = var38 + var43;  // vertexY[i] = previousVertexY + deltaY
//         def.vertexZ[var40] = var39 + var44;  // vertexZ[i] = previousVertexZ + deltaZ
//         var37 = def.vertexX[var40];  // previousVertexX = vertexX[i]
//         var38 = def.vertexY[var40];  // previousVertexY = vertexY[i]
//         var39 = def.vertexZ[var40];  // previousVertexZ = vertexZ[i]
//         if( var16 == 1 )  // hasPackedVertexGroups
//         {
//             def.packedVertexGroups[var40] = var8.readUnsignedByte();
//         }
//     }
//
//     if( var17 == 1 )  // hasAnimayaGroups
//     {
//         for( var40 = 0; var40 < var9; ++var40 )  // vertexCount
//         {
//             var41 = var8.readUnsignedByte();  // groupCount
//             def.animayaGroups[var40] = new int[var41];
//             def.animayaScales[var40] = new int[var41];
//
//             for( var42 = 0; var42 < var41; ++var42 )  // groupCount
//             {
//                 def.animayaGroups[var40][var42] = var8.readUnsignedByte();  // groupIndex
//                 def.animayaScales[var40][var42] = var8.readUnsignedByte();  // groupScale
//             }
//         }
//     }
//
//     var4.setOffset(var32);  // offsetOfFaceColorsOrFaceTextures
//     var5.setOffset(var28);  // offsetOfFaceRenderTypes
//     var6.setOffset(var26);  // offsetOfFaceRenderPriorities
//     var7.setOffset(var30);  // offsetOfFaceTransparencies
//     var8.setOffset(var27);  // offsetOfPackedTransparencyVertexGroups
//
//     for( var40 = 0; var40 < var10; ++var40 )  // faceCount
//     {
//         def.faceColors[var40] = (short)var4.readUnsignedShort();
//         if( var12 == 1 )  // hasFaceRenderTypes
//         {
//             var41 = var5.readUnsignedByte();  // faceTextureFlags
//             if( (var41 & 1) == 1 )
//             {
//                 def.faceRenderTypes[var40] = 1;
//                 var2 = true;  // hasTexturedFaces = true
//             }
//             else
//             {
//                 def.faceRenderTypes[var40] = 0;
//             }
//
//             if( (var41 & 2) == 2 )
//             {
//                 def.textureCoords[var40] = (byte)(var41 >> 2);
//                 def.faceTextures[var40] = def.faceColors[var40];
//                 def.faceColors[var40] = 127;
//                 if( def.faceTextures[var40] != -1 )
//                 {
//                     var3 = true;  // hasFaceTextures = true
//                 }
//             }
//             else
//             {
//                 def.textureCoords[var40] = -1;
//                 def.faceTextures[var40] = -1;
//             }
//         }
//
//         if( var13 == 255 )  // faceRenderPriority
//         {
//             def.faceRenderPriorities[var40] = var6.readByte();
//         }
//
//         if( var14 == 1 )  // hasFaceTransparencies
//         {
//             def.faceTransparencies[var40] = var7.readByte();
//         }
//
//         if( var15 == 1 )  // hasPackedTransparencyVertexGroups
//         {
//             def.packedTransparencyVertexGroups[var40] = var8.readUnsignedByte();
//         }
//     }
//
//     var4.setOffset(var31);  // offsetOfFaceIndexData
//     var5.setOffset(var25);  // offsetOfFaceIndices
//     var40 = 0;  // previousIndex1
//     var41 = 0;  // previousIndex2
//     var42 = 0;  // previousIndex3
//     var43 = 0;  // previousIndex3 (reused)
//
//     int var45;
//     int var46;
//     for( var44 = 0; var44 < var10; ++var44 )  // faceCount
//     {
//         var45 = var5.readUnsignedByte();  // compressionType
//         if( var45 == 1 )
//         {
//             var40 = var4.readShortSmart() + var43;  // previousIndex1 = readShortSmart() +
//             previousIndex3 var41 = var4.readShortSmart() + var40;  // previousIndex2 =
//             readShortSmart() + previousIndex1 var42 = var4.readShortSmart() + var41;  //
//             previousIndex3 = readShortSmart() + previousIndex2 var43 = var42;  // previousIndex3
//             = previousIndex3 def.faceIndices1[var44] = var40; def.faceIndices2[var44] = var41;
//             def.faceIndices3[var44] = var42;
//         }
//
//         if( var45 == 2 )
//         {
//             var41 = var42;  // previousIndex2 = previousIndex3
//             var42 = var4.readShortSmart() + var43;  // previousIndex3 = readShortSmart() +
//             previousIndex3 var43 = var42;  // previousIndex3 = previousIndex3
//             def.faceIndices1[var44] = var40;
//             def.faceIndices2[var44] = var41;
//             def.faceIndices3[var44] = var42;
//         }
//
//         if( var45 == 3 )
//         {
//             var40 = var42;  // previousIndex1 = previousIndex3
//             var42 = var4.readShortSmart() + var43;  // previousIndex3 = readShortSmart() +
//             previousIndex3 var43 = var42;  // previousIndex3 = previousIndex3
//             def.faceIndices1[var44] = var40;
//             def.faceIndices2[var44] = var41;
//             def.faceIndices3[var44] = var42;
//         }
//
//         if( var45 == 4 )
//         {
//             var46 = var40;  // temp = previousIndex1
//             var40 = var41;  // previousIndex1 = previousIndex2
//             var41 = var46;  // previousIndex2 = temp
//             var42 = var4.readShortSmart() + var43;  // previousIndex3 = readShortSmart() +
//             previousIndex3 var43 = var42;  // previousIndex3 = previousIndex3
//             def.faceIndices1[var44] = var40;
//             def.faceIndices2[var44] = var46;
//             def.faceIndices3[var44] = var42;
//         }
//     }
//
//     var4.setOffset(var33);  // offsetOfTextureIndices
//
//     for( var44 = 0; var44 < var11; ++var44 )  // textureCount
//     {
//         def.textureRenderTypes[var44] = 0;
//         def.texIndices1[var44] = (short)var4.readUnsignedShort();
//         def.texIndices2[var44] = (short)var4.readUnsignedShort();
//         def.texIndices3[var44] = (short)var4.readUnsignedShort();
//     }
//
//     if( def.textureCoords != null )
//     {
//         boolean var47 = false;  // hasDifferentTextureIndices
//
//         for( var45 = 0; var45 < var10; ++var45 )  // faceCount
//         {
//             var46 = def.textureCoords[var45] & 255;  // textureIndex
//             if( var46 != 255 )
//             {
//                 if( def.faceIndices1[var45] == (def.texIndices1[var46] & '\uffff') &&
//                     def.faceIndices2[var45] == (def.texIndices2[var46] & '\uffff') &&
//                     def.faceIndices3[var45] == (def.texIndices3[var46] & '\uffff') )
//                 {
//                     def.textureCoords[var45] = -1;
//                 }
//                 else
//                 {
//                     var47 = true;  // hasDifferentTextureIndices = true
//                 }
//             }
//         }
//
//         if( !var47 )  // !hasDifferentTextureIndices
//         {
//             def.textureCoords = null;
//         }
//     }
//
//     if( !var3 )  // !hasFaceTextures
//     {
//         def.faceTextures = null;
//     }
//
//     if( !var2 )  // !hasTexturedFaces
//     {
//         def.faceRenderTypes = null;
//     }
// }
