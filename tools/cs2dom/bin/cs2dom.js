#!/usr/bin/env node
import { main } from '../src/cli.js';

try {
    const code = await main(process.argv.slice(2));
    process.exit(code ?? 0);
} catch( error ) {
    process.stderr.write(`cs2dom: ${error.message}\n`);
    if( process.env.CS2DOM_TRACE ) process.stderr.write(`${error.stack}\n`);
    process.exit(1);
}
