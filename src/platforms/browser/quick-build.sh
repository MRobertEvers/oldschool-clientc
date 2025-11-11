#!/bin/bash
# Quick build script - just builds GameIO without any fancy output

cd "$(dirname "$0")"
npm run build
mkdir -p ../../../public/build
cp -f dist/GameIO.js* dist/GameIO.d.ts ../../../public/build/ 2>/dev/null || cp -f dist/GameIO.js ../../../public/build/
echo "✓ GameIO built and deployed to public/build/"


