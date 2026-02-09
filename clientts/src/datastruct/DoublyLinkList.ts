import DoublyLinkable from '#/datastruct/DoublyLinkable.js';

export default class DoublyLinkList {
    readonly sentinel: DoublyLinkable = new DoublyLinkable();
    cursor: DoublyLinkable | null = null;

    constructor() {
        this.sentinel.next2 = this.sentinel;
        this.sentinel.prev2 = this.sentinel;
    }

    push(node: DoublyLinkable): void {
        if (node.prev2) {
            node.unlink2();
        }

        node.prev2 = this.sentinel.prev2;
        node.next2 = this.sentinel;
        if (node.prev2) {
            node.prev2.next2 = node;
        }
        node.next2.prev2 = node;
    }

    pop(): DoublyLinkable | null {
        const node: DoublyLinkable | null = this.sentinel.next2;
        if (node === this.sentinel) {
            return null;
        } else {
            node?.unlink2();
            return node;
        }
    }

    head() {
        const node: DoublyLinkable | null = this.sentinel.next2;
        if (node === this.sentinel) {
            this.cursor = null;
            return null;
        }

        this.cursor = node?.next2 || null;
        return node;
    }

    next() {
        const node: DoublyLinkable | null = this.cursor;
        if (node === this.sentinel) {
            this.cursor = null;
            return null;
        }

        this.cursor = node?.next2 || null;
        return node;
    }

    size() {
        let count = 0;
        for (let node = this.sentinel.next2; node !== this.sentinel && node; node = node.next2) {
            count++;
        }
        return count;
    }
}
