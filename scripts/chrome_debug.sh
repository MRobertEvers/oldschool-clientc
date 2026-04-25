#!/bin/bash
# Launch Chrome with remote debugging enabled for VSCode attachment
# Run this script, then open the Emscripten build output (e.g. build/index.html) over your static server.
# Then use "Attach to Chrome (WASM)" debug configuration in VSCode

echo "Launching Chrome with remote debugging on port 9222..."
echo "After Chrome opens, navigate to your served web_client page (e.g. http://localhost:8000/index.html)."
echo "Then in VSCode, use the 'Attach to Chrome (WASM)' debug configuration"
echo ""

/Applications/Google\ Chrome.app/Contents/MacOS/Google\ Chrome --remote-debugging-port=9222





