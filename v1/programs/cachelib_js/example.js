#!/usr/bin/env node

/**
 * Simple smoke test for cachelib-js
 * 
 * Usage: node example.js <cache-directory> [mode]
 *   mode: 'dat1' or 'dat2' (default: dat2)
 * 
 * Example: node example.js /path/to/cache dat2
 */

const { CacheLib, CACHE_MODE_DAT1, CACHE_MODE_DAT2 } = require('./index.js');

function main() {
    const args = process.argv.slice(2);
    
    if (args.length < 1) {
        console.error('Usage: node example.js <cache-directory> [mode]');
        console.error('  mode: "dat1" or "dat2" (default: dat2)');
        process.exit(1);
    }
    
    const directory = args[0];
    const modeStr = args[1] || 'dat2';
    
    // Parse mode
    let mode;
    if (modeStr === 'dat1') {
        mode = CACHE_MODE_DAT1;
    } else if (modeStr === 'dat2') {
        mode = CACHE_MODE_DAT2;
    } else {
        console.error(`Invalid mode: ${modeStr}. Must be 'dat1' or 'dat2'`);
        process.exit(1);
    }
    
    console.log(`Opening cache from: ${directory}`);
    console.log(`Mode: ${modeStr} (${mode})`);
    
    try {
        // Create cache instance
        const cache = new CacheLib({ mode, directory });
        console.log('✓ Cache opened successfully');
        
        // Try to load a simple archive
        // For DAT2: Try loading archive 0 from table 2 (CONFIGS)
        // For DAT1: Try loading archive 1 from table 0 (CONFIGS)
        const tableId = mode === CACHE_MODE_DAT2 ? 2 : 0;
        const archiveId = mode === CACHE_MODE_DAT2 ? 0 : 1;
        
        console.log(`\nAttempting to load archive ${archiveId} from table ${tableId}...`);
        
        const archive = cache.loadArchive(tableId, archiveId);
        
        if (archive && Buffer.isBuffer(archive)) {
            console.log(`✓ Archive loaded successfully`);
            console.log(`  Archive size: ${archive.length} bytes`);
            
            // Show first few bytes
            const preview = archive.slice(0, Math.min(16, archive.length));
            console.log(`  First ${preview.length} bytes: ${preview.toString('hex')}`);
        } else {
            console.error('✗ Archive loading failed: result is not a Buffer');
        }
        
        // Free the cache
        cache.free();
        console.log('\n✓ Cache freed successfully');
        
    } catch (error) {
        console.error('✗ Error:', error.message);
        process.exit(1);
    }
    
    console.log('\n✓ All tests passed');
}

main();
