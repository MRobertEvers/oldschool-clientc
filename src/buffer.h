#ifndef BUFFER_H
#define BUFFER_H

struct Buffer
{
    char* data;
    int data_size;
    int position;
};

int read_8(struct Buffer* buffer);
int read_16(struct Buffer* buffer);
int read_24(struct Buffer* buffer);
int read_32(struct Buffer* buffer);

char* read_string(struct Buffer* buffer);

// public int readBigSmart2()
// {
// 	if (peek() < 0)
// 	{
// 		return readInt() & Integer.MAX_VALUE; // and off sign bit
// 	}
// 	int value = readUnsignedShort();
// 	return value == 32767 ? -1 : value;
int read_big_smart2(struct Buffer* buffer);

int read_unsigned_short_smart_minus_one(struct Buffer* buffer);

void bump(int count, struct Buffer* buffer);

int readto(char* out, int out_size, int len, struct Buffer* buffer);

#endif
