#!/usr/bin/env node

/**
 * Basic smoke test - just verify the module loads and exports are correct
 */

console.log('Testing cachelib-js module...\n');

try {
    const { CacheLib, CACHE_MODE_DAT1, CACHE_MODE_DAT2 } = require('./index.js');
    
    // Check exports exist
    console.log('✓ Module loaded successfully');
    
    if (typeof CacheLib !== 'function') {
        throw new Error('CacheLib is not a constructor function');
    }
    console.log('✓ CacheLib is a constructor');
    
    if (typeof CACHE_MODE_DAT1 !== 'number') {
        throw new Error('CACHE_MODE_DAT1 is not a number');
    }
    console.log(`✓ CACHE_MODE_DAT1 = ${CACHE_MODE_DAT1}`);
    
    if (typeof CACHE_MODE_DAT2 !== 'number') {
        throw new Error('CACHE_MODE_DAT2 is not a number');
    }
    console.log(`✓ CACHE_MODE_DAT2 = ${CACHE_MODE_DAT2}`);
    
    // Test that constructor validates arguments
    console.log('\nTesting error handling...');
    
    try {
        new CacheLib();
        console.error('✗ Should have thrown error for missing options');
        process.exit(1);
    } catch (e) {
        console.log('✓ Constructor properly validates arguments');
    }
    
    try {
        new CacheLib({ mode: 999, directory: '/tmp' });
        console.error('✗ Should have thrown error for invalid mode');
        process.exit(1);
    } catch (e) {
        console.log('✓ Constructor validates cache mode');
    }
    
    console.log('\n✓ All basic tests passed!');
    console.log('\nTo test with a real cache, run:');
    console.log('  node example.js /path/to/cache dat2');
    
} catch (error) {
    console.error('✗ Error:', error.message);
    console.error(error.stack);
    process.exit(1);
}
