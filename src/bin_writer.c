#include <stdio.h>

#include "bin_writer.h"

void write_bin(const char *filename, const OutputBuffer *buf)
{
    if (!buf)
        return;

    FILE *f = fopen(filename, "wb");
    if (!f)
    {
        perror("fopen");
        return;
    }
    if (buf->size > 0 && fwrite(buf->data, 1, buf->size, f) != buf->size)
    {
        perror("fwrite");
    }
    fclose(f);
}
