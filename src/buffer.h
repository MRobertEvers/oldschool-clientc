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

void bump(int count, struct Buffer* buffer);

int readto(char* out, int out_size, int len, struct Buffer* buffer);

#endif
