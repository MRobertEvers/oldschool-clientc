# GameIO Build Instructions

This document describes how to build and deploy the GameIO TypeScript module.

## Overview

GameIO.ts is a TypeScript module that provides WebSocket-based archive loading with IndexedDB caching. The module needs to be compiled to JavaScript and deployed to `public/build/` for use in the browser.

## Build Scripts

### Option 1: Using the build script (Recommended)

From the browser platform directory:

```bash
cd src/platforms/browser
./build.sh
```

From the project root:

```bash
./scripts/build-gameio.sh
```

### Option 2: Using npm scripts

From the browser platform directory:

```bash
cd src/platforms/browser

# Build and deploy in one command
npm run build:deploy

# Or build then deploy separately
npm run build
npm run deploy
```

## What the Build Does

1. **Compiles TypeScript**: Converts `src/GameIO.ts` to ES2020 JavaScript
2. **Generates Source Maps**: Creates `.js.map` files for debugging
3. **Generates Type Declarations**: Creates `.d.ts` files for TypeScript consumers
4. **Copies to public/build**: Deploys compiled files to the web server directory

## Output Files

After building, the following files are available in `public/build/`:

- `GameIO.js` - Compiled JavaScript module (ES2020)
- `GameIO.js.map` - Source map for debugging
- `GameIO.d.ts` - TypeScript type declarations

## Development Workflow

### Watch Mode (Auto-rebuild on changes)

For active development, use watch mode:

```bash
cd src/platforms/browser
npm run watch
```

Then in another terminal, manually copy files when needed:

```bash
npm run deploy
```

### One-time Build

For deployment or testing:

```bash
./build.sh
```

## Integration with HTML

The compiled module is imported in `public/shell.html`:

```javascript
import { GameIO, RequestStatus } from "./GameIO.js";
```

## Troubleshooting

### "Cannot find module" error

- Make sure you've run the build script first
- Check that `public/GameIO.js` exists
- Verify the import path in shell.html is correct

### TypeScript compilation errors

- Check `src/platforms/browser/tsconfig.json` for configuration
- Ensure all dependencies are installed: `npm install`
- Review the TypeScript source for syntax errors

### Build script permission denied

- Make sure the script is executable: `chmod +x build.sh`

## Directory Structure

```
3d-raster/
├── scripts/
│   └── build-gameio.sh          # Root-level convenience script
├── src/platforms/browser/
│   ├── src/
│   │   └── GameIO.ts           # Source TypeScript file
│   ├── dist/                   # Build output (intermediate)
│   │   ├── GameIO.js
│   │   ├── GameIO.js.map
│   │   └── GameIO.d.ts
│   ├── build.sh                # Main build script
│   ├── package.json
│   └── tsconfig.json
└── public/
    ├── build/                  # Deployment directory (served)
    │   ├── GameIO.js           # Deployed module
    │   ├── GameIO.js.map
    │   └── GameIO.d.ts
    └── shell.html              # HTML page that imports GameIO
```

## Dependencies

- Node.js (for npm)
- TypeScript 5.0+ (installed via npm)

To install dependencies:

```bash
cd src/platforms/browser
npm install
```


