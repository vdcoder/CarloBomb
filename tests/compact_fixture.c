/*
 * compact_fixture FIXED.frb OUT.frb
 *
 * For the test of the commands: a compact file that holds the table of
 * FIXED.frb and names another table as its source.  Every byte of it is
 * valid, and every REV in it emits its string; only the checksum it records
 * for its source is not the checksum of the table it holds.  `carlobomb bomb
 * verify` and `bomb expand` must say so.
 */
#include <stdio.h>

#include "compact.h"

int main(int argc, char **argv) {
    FrbFileInfo info;
    FrbTable t;
    FrbError e;
    if (argc != 3) {
        fprintf(stderr, "usage: compact_fixture FIXED.frb OUT.frb\n");
        return 2;
    }
    e = frb_file_read(argv[1], &t, &info, NULL);
    if (e == FRB_OK) {
        e = frb_compact_write(argv[2], &t, info.version, info.table_crc ^ 0x00010000u);
        frb_table_destroy(&t);
    }
    if (e != FRB_OK) {
        fprintf(stderr, "compact_fixture: %s\n", frb_error_str(e));
        return 1;
    }
    return 0;
}
