## Java client IN-WORLD, D3D enabled

| partition | samples | share of total |
|---|---|---|
| idle AWT pump | 4929 | 71.9 % |
| title screen / boot / login | 457 | 6.7 % |
| **in-world work** | **1466** | **21.4 %** |

Percentages below are of the 1466 in-world work samples.

### In-world work by subsystem (self)

| subsystem | share | samples |
|---|---|---|
| 2D raster (Pix2D/Pix8/PixFont) | 65.28 % | 957 |
| present / blit | 13.98 % | 205 |
| client logic | 8.05 % | 118 |
| network | 7.37 % | 108 |
| other | 1.98 % | 29 |
| scene / world / entities | 1.30 % | 19 |
| 3D raster (Pix3D/Model) | 1.09 % | 16 |
| awt / cursor | 0.68 % | 10 |
| cache decompress / io | 0.27 % | 4 |

### In-world inclusive (method anywhere on the stack)

| rank | inclusive | self | method |
|---|---|---|---|
| 1 | 83.83 % | 0.00 % | `java.lang.Thread.run` |
| 2 | 81.86 % | 0.07 % | `jagex2.client.GameShell.run` |
| 3 | 81.79 % | 0.00 % | `jagex2.client.Client.run` |
| 4 | 70.67 % | 0.20 % | `jagex2.client.Client.mainredraw` |
| 5 | 70.46 % | 0.20 % | `jagex2.client.Client.gameDraw` |
| 6 | 70.12 % | 0.34 % | `jagex2.client.Client.gameDrawMain` |
| 7 | 65.14 % | 65.14 % | `jagex2.graphics.Pix2D.cls` |
| 8 | 12.89 % | 12.89 % | `sun.java2d.d3d.D3DRenderQueue.flushBuffer` |
| 9 | 12.89 % | 0.00 % | `sun.java2d.d3d.D3DRenderQueue.flushNow` |
| 10 | 12.48 % | 0.00 % | `sun.java2d.d3d.D3DBlitLoops.Blit` |
| 11 | 12.48 % | 0.00 % | `sun.java2d.d3d.D3DSwToSurfaceBlit.Blit` |
| 12 | 12.48 % | 0.00 % | `sun.java2d.pipe.DrawImage.blitSurfaceData` |
| 13 | 12.48 % | 0.00 % | `sun.java2d.pipe.DrawImage.renderImageCopy` |
| 14 | 12.48 % | 0.00 % | `sun.java2d.pipe.DrawImage.copyImage` |
| 15 | 12.48 % | 0.00 % | `sun.java2d.SunGraphics2D.drawImage` |
| 16 | 12.48 % | 0.00 % | `sun.awt.image.ImageRepresentation.drawToBufImage` |
| 17 | 10.85 % | 0.14 % | `jagex2.client.Client.mainloop` |
| 18 | 10.57 % | 0.61 % | `jagex2.client.Client.gameLoop` |
| 19 | 3.75 % | 3.75 % | `jagex2.io.ClientStream.write` |
| 20 | 2.59 % | 0.41 % | `jagex2.client.Client.tcpIn` |
| 21 | 1.64 % | 0.07 % | `java.net.SocketOutputStream.socketWrite` |
| 22 | 1.64 % | 0.00 % | `java.net.SocketOutputStream.write` |
| 23 | 1.64 % | 0.00 % | `jagex2.io.ClientStream.run` |
| 24 | 1.57 % | 1.57 % | `java.net.SocketOutputStream.socketWrite0` |
| 25 | 1.36 % | 0.20 % | `java.net.SocketInputStream.available` |
| 26 | 1.36 % | 0.00 % | `jagex2.io.ClientStream.available` |
| 27 | 1.23 % | 0.55 % | `jagex2.client.Client.addPlayers` |
| 28 | 1.23 % | 0.27 % | `jagex2.client.Client.moveEntity` |

