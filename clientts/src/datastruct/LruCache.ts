import DoublyLinkable from '#/datastruct/DoublyLinkable.js';
import HashTable from '#/datastruct/HashTable.js';
import DoublyLinkList from '#/datastruct/DoublyLinkList.js';

export default class LruCache {
    readonly capacity: number;
    readonly hashtable: HashTable = new HashTable(1024);
    readonly cacheHistory: DoublyLinkList = new DoublyLinkList();
    cacheAvailable: number;

    constructor(size: number) {
        this.capacity = size;
        this.cacheAvailable = size;
    }

    get(key: bigint): DoublyLinkable | null {
        const node: DoublyLinkable | null = this.hashtable.get(key) as DoublyLinkable | null;
        if (node) {
            this.cacheHistory.push(node);
        }
        return node;
    }

    put(key: bigint, value: DoublyLinkable): void {
        if (this.cacheAvailable === 0) {
            const node: DoublyLinkable | null = this.cacheHistory.pop();
            node?.unlink();
            node?.unlink2();
        } else {
            this.cacheAvailable--;
        }
        this.hashtable.put(key, value);
        this.cacheHistory.push(value);
    }

    clear(): void {
        // eslint-disable-next-line no-constant-condition
        while (true) {
            const node: DoublyLinkable | null = this.cacheHistory.pop();
            if (!node) {
                this.cacheAvailable = this.capacity;
                return;
            }
            node.unlink();
            node.unlink2();
        }
    }
}
