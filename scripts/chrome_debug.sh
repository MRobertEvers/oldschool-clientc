#!/bin/bash
# Launch Chrome with remote debugging enabled for VSCode attachment
# Run this script, then navigate to http://localhost:8000/build.em/emscripten_sdl2_webgl1.html
# Then use "Attach to Chrome (WASM)" debug configuration in VSCode

echo "Launching Chrome with remote debugging on port 9222..."
echo "After Chrome opens, navigate to: http://localhost:8000/build.em/emscripten_sdl2_webgl1.html"
echo "Then in VSCode, use the 'Attach to Chrome (WASM)' debug configuration"
echo ""

/Applications/Google\ Chrome.app/Contents/MacOS/Google\ Chrome --remote-debugging-port=9222


