/*
* Helpers shared by wip_server and wip_client (thesis testbed).
*/

#ifndef WIP_COMMON_H
#define WIP_COMMON_H

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include "picoquic.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIP_DEFAULT_PORT 4433

/* A long option, written "--name value", or "--name" for a flag. */
typedef struct st_tb_long_opt_t {
    char const* name; /* without the leading "--" */
    int has_value;    /* 1 if the option takes a value */
    char const* value; /* set when the option is present; "1" for flags */
} wip_long_opt_t;

/* Remove the long options listed in opts from argv and store their values.
 * Short options and positional arguments are kept, in order, for getopt().
 * Returns the new argc, or -1 on an unknown "--" option or a missing value. */
int wip_extract_long_options(int argc, char** argv, wip_long_opt_t* opts, size_t nb_opts);

/* Connection ID as lowercase hex: the same name picoquic uses for qlog and
 * binlog files, so that CSV rows can be matched with those logs. */
char const* wip_cid_to_hex(picoquic_connection_id_t cid, char* buf, size_t buf_size);

/* Open a CSV file in append mode. If the file is new or empty, write the
 * header line first. Returns NULL on error. */
FILE* wip_open_csv(char const* file_name, char const* header);

/* Print the names of the registered congestion control algorithms. */
void wip_print_cc_algorithms(FILE* F);

/* Sleep for the given number of milliseconds. */
void wip_sleep_ms(uint64_t ms);

#ifdef __cplusplus
}
#endif

#endif /* WIP_COMMON_H */
