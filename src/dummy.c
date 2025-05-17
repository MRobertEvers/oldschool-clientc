// /**
// 	 * Decodes the slave checksum table contained in the specified
// 	 * {@link ByteBuffer}.
// 	 *
// 	 * @param buffer
// 	 *            The buffer.
// 	 * @return The slave checksum table.
// 	 */
// 	public static ReferenceTable decode(ByteBuffer buffer) {
// 		/* create a new table */
// 		ReferenceTable table = new ReferenceTable();

// 		/* read header */
// 		table.format = buffer.get() & 0xFF;
// 		if (table.format < 5 || table.format > 7) {
// 			throw new RuntimeException();
// 		}
// 		if (table.format >= 6) {
// 			table.version = buffer.getInt();
// 		}

// 		table.flags = buffer.get() & 0xFF;

// 		/* read the ids */
// 		int[] ids = new int[table.format >= 7 ? ByteBufferUtils.getSmartInt(buffer) :
// buffer.getShort() & 0xFFFF]; 		int accumulator = 0, size = -1; 		for (int i = 0; i < ids.length; i++)
// { 			int delta = table.format >= 7 ? ByteBufferUtils.getSmartInt(buffer) : buffer.getShort() &
// 0xFFFF; 			ids[i] = accumulator += delta; 			if (ids[i] > size) { 				size = ids[i];
// 			}
// 		}
// 		size++;
// 		// table.indices = ids;

// 		/* and allocate specific entries within that array */
// 		int index = 0;
// 		for (int id : ids) {
// 			table.entries.put(id, new Entry(index++));
// 		}

// 		/* read the identifiers if present */
// 		int[] identifiersArray = new int[size];
// 		if ((table.flags & FLAG_IDENTIFIERS) != 0) {
// 			for (int id : ids) {
// 				int identifier = buffer.getInt();
// 				identifiersArray[id] = identifier;
// 				table.entries.get(id).identifier = identifier;
// 			}
// 		}
// 		table.identifiers = new Identifiers(identifiersArray);

// 		/* read the CRC32 checksums */
// 		for (int id : ids) {
// 			table.entries.get(id).crc = buffer.getInt();
// 		}

// 		/* read another hash if present */
// 		if ((table.flags & FLAG_HASH) != 0) {
// 			for (int id : ids) {
// 				table.entries.get(id).hash = buffer.getInt();
// 			}
// 		}

// 		/* read the whirlpool digests if present */
// 		if ((table.flags & FLAG_WHIRLPOOL) != 0) {
// 			for (int id : ids) {
// 				buffer.get(table.entries.get(id).whirlpool);
// 			}
// 		}

// 		/* read the sizes of the archive */
// 		if ((table.flags & FLAG_SIZES) != 0) {
// 			for (int id : ids) {
// 				table.entries.get(id).compressed = buffer.getInt();
// 				table.entries.get(id).uncompressed = buffer.getInt();
// 			}
// 		}

// 		/* read the version numbers */
// 		for (int id : ids) {
// 			table.entries.get(id).version = buffer.getInt();
// 		}

// 		/* read the child sizes */
// 		int[][] members = new int[size][];
// 		for (int id : ids) {
// 			members[id] = new int[table.format >= 7 ? ByteBufferUtils.getSmartInt(buffer) :
// buffer.getShort() & 0xFFFF];
// 		}

// 		/* read the child ids */
// 		int[][] identifiers = new int[members.length][];
// 		for (int id : ids) {
// 			/* reset the accumulator and size */
// 			accumulator = 0;
// 			size = -1;

// 			/* loop through the array of ids */
// 			for (int i = 0; i < members[id].length; i++) {
// 				int delta = table.format >= 7 ? ByteBufferUtils.getSmartInt(buffer) :
// buffer.getShort() & 0xFFFF; 				members[id][i] = accumulator += delta; 				if (members[id][i] > size) {
// 					size = members[id][i];
// 				}
// 			}
// 			size++;

// 			identifiers[id] = new int[size];

// 			/* and allocate specific entries within the array */
// 			index = 0;
// 			for (int child : members[id]) {
// 				table.entries.get(id).entries.put(child, new ChildEntry(index++));
// 			}
// 		}

// 		/* read the child identifiers if present */
// 		if ((table.flags & FLAG_IDENTIFIERS) != 0) {
// 			for (int id : ids) {
// 				for (int child : members[id]) {
// 					int identifier = buffer.getInt();
// 					identifiers[id][child] = identifier;
// 					table.entries.get(id).entries.get(child).identifier = identifier;
// 				}
// 				table.entries.get(id).identifiers = new Identifiers(identifiers[id]);
// 			}
// 		}

// 		/* return the table we constructed */
// 		return table;
// 	}