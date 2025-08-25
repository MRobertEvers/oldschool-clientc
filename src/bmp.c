#include "bmp.h"

#include <stdio.h>

void
bmp_write_file(const char* filename, int* pixels, int width, int height)
{
    FILE* file = fopen(filename, "wb");
    if( !file )
    {
        printf("Failed to open file for writing: %s\n", filename);
        return;
    }

    // BMP header (14 bytes)
    unsigned char header[14] = {
        'B', 'M',       // Magic number
        0,   0,   0, 0, // File size (to be filled)
        0,   0,         // Reserved
        0,   0,         // Reserved
        54,  0,   0, 0  // Pixel data offset
    };

    // DIB header (40 bytes)
    unsigned char dib_header[40] = {
        40, 0, 0, 0, // DIB header size
        0,  0, 0, 0, // Width (to be filled)
        0,  0, 0, 0, // Height (to be filled)
        1,  0,       // Planes
        32, 0,       // Bits per pixel
        0,  0, 0, 0, // Compression
        0,  0, 0, 0, // Image size
        0,  0, 0, 0, // X pixels per meter
        0,  0, 0, 0, // Y pixels per meter
        0,  0, 0, 0, // Colors in color table
        0,  0, 0, 0  // Important color count
    };

    // Fill in width and height
    dib_header[4] = width & 0xFF;
    dib_header[5] = (width >> 8) & 0xFF;
    dib_header[6] = (width >> 16) & 0xFF;
    dib_header[7] = (width >> 24) & 0xFF;
    dib_header[8] = height & 0xFF;
    dib_header[9] = (height >> 8) & 0xFF;
    dib_header[10] = (height >> 16) & 0xFF;
    dib_header[11] = (height >> 24) & 0xFF;

    // Calculate file size
    int pixel_data_size = width * height * 4; // 4 bytes per pixel (32-bit)
    int file_size = 54 + pixel_data_size;     // 54 = header size (14 + 40)
    header[2] = file_size & 0xFF;
    header[3] = (file_size >> 8) & 0xFF;
    header[4] = (file_size >> 16) & 0xFF;
    header[5] = (file_size >> 24) & 0xFF;

    // Write headers
    fwrite(header, 1, 14, file);
    fwrite(dib_header, 1, 40, file);

    // Write pixel data (BMP is stored bottom-up)
    for( int y = height - 1; y >= 0; y-- )
    {
        for( int x = 0; x < width; x++ )
        {
            int pixel = pixels[y * width + x];
            // Source is RGBA, need to convert to BGRA for BMP
            unsigned char r = (pixel >> 16) & 0xFF; // Red (was incorrectly reading as Blue)
            unsigned char g = (pixel >> 8) & 0xFF;  // Green
            unsigned char b = pixel & 0xFF;         // Blue (was incorrectly reading as Red)
            unsigned char a = (pixel >> 24) & 0xFF; // Alpha
            // Write in BGRA order for BMP
            fwrite(&b, 1, 1, file);
            fwrite(&g, 1, 1, file);
            fwrite(&r, 1, 1, file);
            fwrite(&a, 1, 1, file);
        }
    }

    fclose(file);
}
