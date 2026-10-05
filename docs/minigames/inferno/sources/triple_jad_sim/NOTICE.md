# Third-party notices

This project is an unofficial fan-made simulator. Old School RuneScape, RuneScape and all game content, artwork, models, animations, audio and trademarks are the property of Jagex Limited. This project is not affiliated with or endorsed by Jagex.

Portions of the cache-decoding code are derived from the projects below. Each ported file carries a header comment naming its origin.

## rs-map-viewer

https://github.com/dennisdev/rs-map-viewer

BSD 2-Clause License

Copyright (c) 2022-2023, dennisdev
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

## RuneLite (format reference)

https://github.com/runelite/runelite — BSD 2-Clause. Used as the reference for cache formats; no code copied verbatim unless a file header says so.

## RuneStar fonts

https://github.com/RuneStar/fonts — CC0 1.0 Universal (public domain dedication).

`public/fonts/RuneScape-Plain-11.ttf`, `RuneScape-Plain-12.ttf`, `RuneScape-Bold-12.ttf` and `RuneScape-Quill-8.ttf` are unmodified copies made by `scripts/build-ui-assets.ts`. `public/fonts/RuneScape-Small.ttf` is not part of that set: the script traces it from the game cache's own bitmap font (font 494, "p11_full") the same way (one unit square per glyph pixel).

## RuneLite resource packs

https://github.com/melkypie/resource-packs — BSD 2-Clause License (same terms as the rs-map-viewer notice above; copyright the respective pack authors).

`public/assets/ui/packs/pack-browntown/` ("OSRS Wiki Browntown" by Nichy / nickyGyul), `pack-toblite/` ("TOBlite" by degradee / Sayolko) and `pack-duckscape/` ("DuckScape" by degradee / Sayolko) are copies of those packs' sprites, renamed to the sprite paths the chrome uses by `scripts/build-ui-assets.ts`. `pack-vanilla/`, `public/assets/items/` and the other `public/assets/ui/` sprites are exported from the game cache (Jagex).
