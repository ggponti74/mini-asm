#ifndef SRCLOC_H
#define SRCLOC_H

// Name of the source file currently being assembled (the top-level file or
// the file of the innermost INCLUDE). Error messages print it so a line
// number can be traced back to the right file.
extern const char *g_src_file;

#endif // SRCLOC_H
