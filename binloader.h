#ifndef BFG_BINLOADER_H
#define BFG_BINLOADER_H

#include <stdbool.h>
#include <stdio.h>

#include "util.h"

extern void _bitstream_not_found(const char *repr, const char *fn);
extern FILE *open_xilinx_bitstream(const char *dname, const char *repr, const char *fwfile, unsigned long *out_len);
extern bool load_bitstream_intelhex(bytes_t *out, const char *dname, const char *repr, const char *fn);
extern bool load_bitstream_bytes(bytes_t *out, const char *dname, const char *repr, const char *fileprefix);

#endif
