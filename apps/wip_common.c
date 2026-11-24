/*
* Helpers shared by wip_server and wip_client (thesis testbed).
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WINDOWS
#include <windows.h>
#else
#include <errno.h>
#include <time.h>
#endif

#include "picoquic.h"
#include "wip_common.h"

int wip_extract_long_options(int argc, char** argv, wip_long_opt_t* opts, size_t nb_opts)
{
    int new_argc = 1;

    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--", 2) == 0 && argv[i][2] != 0) {
            char const* name = argv[i] + 2;
            size_t k = 0;

            while (k < nb_opts && strcmp(name, opts[k].name) != 0) {
                k++;
            }
            if (k >= nb_opts) {
                fprintf(stderr, "Unknown option: %s\n", argv[i]);
                return -1;
            }
            if (opts[k].has_value) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "Option %s needs a value\n", argv[i]);
                    return -1;
                }
                opts[k].value = argv[++i];
            }
            else {
                opts[k].value = "1";
            }
        }
        else {
            argv[new_argc++] = argv[i];
        }
    }
    argv[new_argc] = NULL;

    return new_argc;
}

char const* wip_cid_to_hex(picoquic_connection_id_t cid, char* buf, size_t buf_size)
{
    static const char hex[] = "0123456789abcdef";
    size_t x = 0;

    for (size_t i = 0; i < cid.id_len && x + 2 < buf_size; i++) {
        buf[x++] = hex[(cid.id[i] >> 4) & 0x0F];
        buf[x++] = hex[cid.id[i] & 0x0F];
    }
    if (buf_size > 0) {
        buf[x] = 0;
    }
    return buf;
}

FILE* wip_open_csv(char const* file_name, char const* header)
{
    FILE* F = fopen(file_name, "a");

    if (F != NULL) {
        if (fseek(F, 0, SEEK_END) == 0 && ftell(F) == 0) {
            fprintf(F, "%s\n", header);
        }
    }
    else {
        fprintf(stderr, "Cannot open log file %s\n", file_name);
    }
    return F;
}

void wip_print_cc_algorithms(FILE* F)
{
    fprintf(F, "Available congestion control algorithms:");
    for (size_t i = 0; i < picoquic_nb_congestion_control_algorithms; i++) {
        fprintf(F, "%s%s", (i == 0) ? " " : ", ",
            picoquic_congestion_control_algorithms[i]->congestion_algorithm_id);
    }
    fprintf(F, "\n");
}

void wip_sleep_ms(uint64_t ms)
{
#ifdef _WINDOWS
    Sleep((DWORD)ms);
#else
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000);
    ts.tv_nsec = (long)((ms % 1000) * 1000000);
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
        /* interrupted by a signal: sleep for the remaining time */
    }
#endif
}
