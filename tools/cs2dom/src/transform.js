/*
 * TypeScript in, executable module out — with the operators redirected.
 *
 * The interesting part is not the transpile, it is that `energy / 100` in a .tsx
 * has to survive as *division* rather than as NaN. JavaScript cannot overload an
 * operator, so this rewrites each one the compiler understands into a call:
 *
 *     energy / 100          ->  __op('/', energy, 100)
 *     pct <= 20             ->  __op('<=', pct, 20)
 *     low ? red : green     ->  __cond(low, () => red, () => green)
 *     a && b                ->  __logic('&&', a, () => b)
 *     `used ${pct}%`        ->  __tmpl(['used ', '%'], [pct])
 *
 * Those helpers compute normally when nothing symbolic is involved, so ordinary
 * TypeScript in a component stays ordinary TypeScript — `2 + 2` is 4 at build time
 * and never reaches the emitter. Only operators with a CS2 spelling are rewritten;
 * `instanceof`, `in`, bit operators and compound assignment are left exactly as
 * they are, so they behave like the JavaScript they are, whatever they are applied
 * to.
 *
 * The thunks around ternary and && branches matter: a symbolic condition cannot
 * choose a branch here, so both have to be reachable without being run first.
 */

import ts from 'typescript';

const BINARY_OPS = new Map([
    [ts.SyntaxKind.PlusToken, '+'],
    [ts.SyntaxKind.MinusToken, '-'],
    [ts.SyntaxKind.AsteriskToken, '*'],
    [ts.SyntaxKind.SlashToken, '/'],
    [ts.SyntaxKind.PercentToken, '%'],
    [ts.SyntaxKind.LessThanToken, '<'],
    [ts.SyntaxKind.GreaterThanToken, '>'],
    [ts.SyntaxKind.LessThanEqualsToken, '<='],
    [ts.SyntaxKind.GreaterThanEqualsToken, '>='],
    [ts.SyntaxKind.EqualsEqualsToken, '=='],
    [ts.SyntaxKind.EqualsEqualsEqualsToken, '==='],
    [ts.SyntaxKind.ExclamationEqualsToken, '!='],
    [ts.SyntaxKind.ExclamationEqualsEqualsToken, '!=='],
]);

/** The globals the rewritten code calls. loader.js supplies them. */
export const HELPERS = ['__jsx', '__fragment', '__op', '__logic', '__not', '__cond', '__tmpl'];

function operatorTransformer(context) {
    const { factory } = context;

    return (sourceFile) => {
        const visit = (node) => {
            node = ts.visitEachChild(node, visit, context);

            if( ts.isBinaryExpression(node) ) {
                const op = BINARY_OPS.get(node.operatorToken.kind);
                if( op ) {
                    return factory.createCallExpression(
                        factory.createIdentifier('__op'), undefined,
                        [factory.createStringLiteral(op), node.left, node.right]);
                }
                if( node.operatorToken.kind === ts.SyntaxKind.AmpersandAmpersandToken ||
                    node.operatorToken.kind === ts.SyntaxKind.BarBarToken ) {
                    const spelling =
                        node.operatorToken.kind === ts.SyntaxKind.AmpersandAmpersandToken ? '&&' : '||';
                    return factory.createCallExpression(
                        factory.createIdentifier('__logic'), undefined,
                        [factory.createStringLiteral(spelling), node.left, thunk(factory, node.right)]);
                }
                return node;
            }

            if( ts.isConditionalExpression(node) ) {
                return factory.createCallExpression(
                    factory.createIdentifier('__cond'), undefined,
                    [node.condition, thunk(factory, node.whenTrue), thunk(factory, node.whenFalse)]);
            }

            if( ts.isPrefixUnaryExpression(node) &&
                node.operator === ts.SyntaxKind.ExclamationToken ) {
                return factory.createCallExpression(
                    factory.createIdentifier('__not'), undefined, [node.operand]);
            }

            if( ts.isTemplateExpression(node) ) {
                const strings = [factory.createStringLiteral(node.head.text)];
                const values = [];
                for( const span of node.templateSpans ) {
                    values.push(span.expression);
                    strings.push(factory.createStringLiteral(span.literal.text));
                }
                return factory.createCallExpression(
                    factory.createIdentifier('__tmpl'), undefined,
                    [factory.createArrayLiteralExpression(strings),
                     factory.createArrayLiteralExpression(values)]);
            }

            return node;
        };

        return ts.visitNode(sourceFile, visit);
    };
}

function thunk(factory, expression) {
    return factory.createArrowFunction(
        undefined, undefined, [], undefined,
        factory.createToken(ts.SyntaxKind.EqualsGreaterThanToken),
        expression);
}

/**
 * Compile one .ts/.tsx source to CommonJS.
 *
 * Types are erased, not checked — a project that wants type checking runs `tsc
 * --noEmit` against the same files, which is what `cs2dom check` does. Erasing here
 * keeps a build from needing a full program for what is, at this stage, a transpile.
 */
export function compileSource(source, fileName) {
    const result = ts.transpileModule(source, {
        fileName,
        compilerOptions: {
            target: ts.ScriptTarget.ES2022,
            module: ts.ModuleKind.CommonJS,
            jsx: ts.JsxEmit.React,
            jsxFactory: '__jsx',
            jsxFragmentFactory: '__fragment',
            esModuleInterop: true,
            sourceMap: false,
        },
        transformers: { before: [operatorTransformer] },
        reportDiagnostics: true,
    });

    const fatal = (result.diagnostics || []).filter((d) => d.category === ts.DiagnosticCategory.Error);
    if( fatal.length ) {
        const message = fatal
            .map((d) => `${fileName}: ${ts.flattenDiagnosticMessageText(d.messageText, ' ')}`)
            .join('\n');
        throw new Error(message);
    }
    return result.outputText;
}
