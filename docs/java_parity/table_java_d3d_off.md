## Java client IN-WORLD, D3D disabled (GDI blit)

| partition | samples | share of total |
|---|---|---|
| idle AWT pump | 4992 | 81.8 % |
| title screen / boot / login | 533 | 8.7 % |
| **in-world work** | **577** | **9.5 %** |

Percentages below are of the 577 in-world work samples.

### In-world work by subsystem (self)

| subsystem | share | samples |
|---|---|---|
| 2D raster (Pix2D/Pix8/PixFont) | 46.45 % | 268 |
| client logic | 16.12 % | 93 |
| network | 7.97 % | 46 |
| other | 6.76 % | 39 |
| present / blit | 6.59 % | 38 |
| awt / cursor | 5.37 % | 31 |
| 3D raster (Pix3D/Model) | 4.51 % | 26 |
| scene / world / entities | 3.29 % | 19 |
| cache decompress / io | 2.95 % | 17 |

### In-world inclusive (method anywhere on the stack)

| rank | inclusive | self | method |
|---|---|---|---|
| 1 | 81.28 % | 0.00 % | `java.lang.Thread.run` |
| 2 | 79.55 % | 0.17 % | `jagex2.client.GameShell.run` |
| 3 | 79.38 % | 0.00 % | `jagex2.client.Client.run` |
| 4 | 59.97 % | 0.17 % | `jagex2.client.Client.mainredraw` |
| 5 | 59.79 % | 0.00 % | `jagex2.client.Client.gameDraw` |
| 6 | 58.58 % | 0.52 % | `jagex2.client.Client.gameDrawMain` |
| 7 | 45.75 % | 45.75 % | `jagex2.graphics.Pix2D.cls` |
| 8 | 18.72 % | 0.17 % | `jagex2.client.Client.mainloop` |
| 9 | 18.02 % | 0.87 % | `jagex2.client.Client.gameLoop` |
| 10 | 6.59 % | 1.04 % | `jagex2.client.Client.tcpIn` |
| 11 | 5.89 % | 0.00 % | `jagex2.graphics.PixMap.draw` |
| 12 | 5.03 % | 0.00 % | `sun.java2d.pipe.DrawImage.copyImage` |
| 13 | 5.03 % | 0.00 % | `sun.java2d.SunGraphics2D.drawImage` |
| 14 | 4.85 % | 4.85 % | `sun.java2d.windows.GDIBlitLoops.nativeBlit` |
| 15 | 4.85 % | 0.00 % | `sun.java2d.windows.GDIBlitLoops.Blit` |
| 16 | 4.85 % | 0.00 % | `sun.java2d.pipe.DrawImage.blitSurfaceData` |
| 17 | 4.85 % | 0.00 % | `sun.java2d.pipe.DrawImage.renderImageCopy` |
| 18 | 4.85 % | 0.00 % | `sun.awt.image.ImageRepresentation.drawToBufImage` |
| 19 | 4.16 % | 0.17 % | `java.awt.EventQueue.dispatchEventImpl` |
| 20 | 3.99 % | 0.00 % | `java.awt.EventQueue.access$500` |
| 21 | 3.99 % | 0.00 % | `java.awt.EventQueue$3.run` |
| 22 | 3.99 % | 1.39 % | `jagex2.client.Client.addNpcs` |
| 23 | 3.99 % | 0.17 % | `jagex2.dash3d.World.renderAll` |
| 24 | 3.81 % | 0.00 % | `java.security.AccessController.doPrivileged` |
| 25 | 3.81 % | 0.17 % | `jagex2.dash3d.World.method90` |
| 26 | 3.12 % | 0.00 % | `sun.awt.GlobalCursorManager._updateCursor` |
| 27 | 2.77 % | 0.00 % | `java.awt.Window.dispatchEventImpl` |
| 28 | 2.77 % | 0.00 % | `java.awt.Component.dispatchEvent` |

