/* Browser-only convenience wrapper. Keep this separate from the renderer so
 * Node-side projection/SSR tests do not need a DOM global. */

import { createElement } from 'react';
import { createRoot } from 'react-dom/client';

import { InterfacePreview, assertViewTreeStore } from './react_tree_renderer.js';

/** Mount a preview root and return an explicit update/unmount handle. */
export function mountInterfacePreview(container, initialProps) {
    if( !container || typeof container !== 'object' )
        throw new TypeError('mountInterfacePreview requires a DOM container');
    assertViewTreeStore(initialProps?.store);
    const root = createRoot(container);
    let props = { ...initialProps };
    const render = () => root.render(createElement(InterfacePreview, props));
    render();
    return Object.freeze({
        update(nextProps) {
            props = { ...props, ...nextProps };
            assertViewTreeStore(props.store);
            render();
        },
        unmount() { root.unmount(); },
    });
}
