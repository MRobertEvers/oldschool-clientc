/*
 * React-owned boundary around the retained cache-accurate stage painter.
 *
 * The controller is an external store whose snapshot changes only after the
 * final chunk of a worker TreeDelta has committed. React owns the surface and
 * schedules the retained painter from a layout effect. Keeping the expensive
 * cache sprite/font/model reconciliation behind one component lets that work
 * remain cooperatively sliced while authored widgets can use InterfacePreview
 * and ordinary React components beside it.
 */

import {
    createElement,
    useCallback,
    useLayoutEffect,
    useRef,
    useSyncExternalStore,
} from 'react';
import { createRoot } from 'react-dom/client';

export function assertCommittedStageStore(store) {
    if( !store || typeof store.subscribeStage !== 'function' ||
        typeof store.getStageSnapshot !== 'function' )
        throw new TypeError(
            'committed stage store must implement subscribeStage() and getStageSnapshot()');
    return store;
}

export function RetainedInterfaceStage({ store, onCommit, className = '' }) {
    assertCommittedStageStore(store);
    const subscribe = useCallback((listener) => store.subscribeStage(listener), [store]);
    const getSnapshot = useCallback(() => store.getStageSnapshot(), [store]);
    const snapshot = useSyncExternalStore(subscribe, getSnapshot, getSnapshot);
    const surface = useRef(null);

    useLayoutEffect(() => {
        onCommit?.(snapshot, surface.current);
    }, [snapshot, onCommit]);

    return createElement('div', {
        ref: surface,
        className: `cs2dom-retained-stage${className ? ` ${className}` : ''}`,
        'data-react-stage-revision': snapshot.revision,
        style: {
            position: 'relative',
            width: '100%',
            height: '100%',
            overflow: 'hidden',
        },
    });
}

export function mountRetainedInterfaceStage(container, initialProps) {
    if( !container || typeof container !== 'object' )
        throw new TypeError('mountRetainedInterfaceStage requires a DOM container');
    assertCommittedStageStore(initialProps?.store);
    const root = createRoot(container);
    let props = { ...initialProps };
    const render = () => root.render(createElement(RetainedInterfaceStage, props));
    render();
    return Object.freeze({
        update(nextProps) {
            props = { ...props, ...nextProps };
            assertCommittedStageStore(props.store);
            render();
        },
        unmount() { root.unmount(); },
    });
}
