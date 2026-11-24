/*
* wip_server: HTTP/3 server for the thesis testbed, built on picoquic.
*
* Every connection uses the congestion control algorithm chosen at startup
* with "-G <name>" or "--cc <name>". The server answers HTTP/3 requests (and
* HTTP/0.9 "hq-interop" requests) with picoquic's own server code:
*
*   GET /<N>        returns N bytes of generated content, e.g. GET /1000000
*   GET /<file>     returns a file from the folder given with -w <dir>
*
* Logs for congestion control performance:
*
*   --cc-log <csv>  samples of the congestion controller state of every open
*                   connection, every --sample-us microseconds (default 10 ms):
*                   cwnd, bytes in flight, RTTs, pacing rate, bandwidth
*                   estimates, bytes sent, losses, slow start exit, CC state.
*   -F <csv>        picoquic performance log: one summary line per connection.
*   -q <dir>        one qlog per connection (every ACK, every cwnd change).
*   -b <dir>        one binary log per connection (convert with picolog_t).
*
* All other picoquic options (-p port, -c cert, -k key, -0 no GSO, ...) work as
* in picoquicdemo; run with -h for the list. The server runs until it receives
* SIGINT or SIGTERM, then closes its connections and flushes the logs.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <signal.h>
#ifdef _WINDOWS
#include "getopt.h"
#else
#include <unistd.h>
#endif

#include "picoquic.h"
#include "picoquic_internal.h"
#include "picoquic_utils.h"
#include "picoquic_packet_loop.h"
#include "picoquic_config.h"
#include "picoquic_binlog.h"
#include "picoquic_logger.h"
#include "picoquic_qlog.h"
#include "performance_log.h"
#include "h3zero_common.h"
#include "demoserver.h"
#include "wip_common.h"

#ifndef WIP_PICOQUIC_DIR
#define WIP_PICOQUIC_DIR "picoquic"
#endif

#define SERVER_DEFAULT_SAMPLE_US 10000
#define SERVER_MAX_WAIT_US 100000 /* check for a stop request at least this often */
#define SERVER_FLUSH_INTERVAL_US 1000000

static const char* cc_log_header =
    "label,time_us,cnx_time_us,cnx_id,state,cc_algo,"
    "cwin,bytes_in_transit,smoothed_rtt_us,rtt_min_us,rtt_sample_us,rtt_variant_us,"
    "pacing_rate_Bps,bandwidth_estimate_Bps,peak_bandwidth_estimate_Bps,"
    "bytes_sent,packets_sent,packets_lost,timer_losses,spurious_losses,"
    "slow_start_exited,cc_state,cc_param";

typedef struct st_server_ctx_t {
    FILE* cc_log;
    char const* label;
    uint64_t sample_interval;
    uint64_t next_sample_time;
    uint64_t next_flush_time;
    uint64_t nb_samples;
} server_ctx_t;

static volatile sig_atomic_t stop_requested = 0;

static void handle_signal(int sig)
{
    (void)sig;
    stop_requested = 1;
}

/* Write one row per open connection with the state of its congestion
 * controller on the default path. */
static void server_sample_connections(server_ctx_t* ctx, picoquic_quic_t* quic, uint64_t current_time)
{
    for (picoquic_cnx_t* cnx = picoquic_get_first_cnx(quic); cnx != NULL; cnx = picoquic_get_next_cnx(cnx)) {
        picoquic_state_enum state = picoquic_get_cnx_state(cnx);
        picoquic_path_t* path_x = (cnx->nb_paths > 0) ? cnx->path[0] : NULL;
        picoquic_path_quality_t quality;
        uint64_t cc_state = 0;
        uint64_t cc_param = 0;
        char cid_text[2 * PICOQUIC_CONNECTION_ID_MAX_SIZE + 1];
        char const* cc_name = "";

        if (state >= picoquic_state_disconnected || path_x == NULL) {
            continue;
        }
        memset(&quality, 0, sizeof(quality));
        picoquic_get_default_path_quality(cnx, &quality);
        if (cnx->congestion_alg != NULL) {
            cc_name = cnx->congestion_alg->congestion_algorithm_id;
            if (cnx->congestion_alg->alg_observe != NULL && path_x->congestion_alg_state != NULL) {
                cnx->congestion_alg->alg_observe(path_x, &cc_state, &cc_param);
            }
        }

        fprintf(ctx->cc_log,
            "%s,%" PRIu64 ",%" PRIu64 ",%s,%d,%s,"
            "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ","
            "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ","
            "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ","
            "%d,%" PRIu64 ",%" PRIu64 "\n",
            ctx->label, current_time, current_time - picoquic_get_cnx_start_time(cnx),
            wip_cid_to_hex(picoquic_get_logging_cnxid(cnx), cid_text, sizeof(cid_text)),
            (int)state, cc_name,
            quality.cwin, quality.bytes_in_transit, quality.rtt, quality.rtt_min, quality.rtt_sample, quality.rtt_variant,
            quality.pacing_rate, path_x->bandwidth_estimate, path_x->peak_bandwidth_estimate,
            quality.bytes_sent, quality.sent, quality.lost, quality.timer_losses, quality.spurious_losses,
            (int)path_x->is_ssthresh_initialized, cc_state, cc_param);
        ctx->nb_samples++;
    }

    if (current_time >= ctx->next_flush_time) {
        fflush(ctx->cc_log);
        ctx->next_flush_time = current_time + SERVER_FLUSH_INTERVAL_US;
    }
}

static int server_loop_cb(picoquic_quic_t* quic, picoquic_packet_loop_cb_enum cb_mode,
    void* callback_ctx, void* callback_arg)
{
    server_ctx_t* ctx = (server_ctx_t*)callback_ctx;
    uint64_t current_time;

    switch (cb_mode) {
    case picoquic_packet_loop_ready: {
        picoquic_packet_loop_options_t* options = (picoquic_packet_loop_options_t*)callback_arg;
        options->do_time_check = 1;
        fprintf(stdout, "Server ready.\n");
        fflush(stdout);
        break;
    }
    case picoquic_packet_loop_time_check: {
        /* Shorten the wait so that samples are taken on time while
         * connections are open, and a stop request is seen quickly. */
        packet_loop_time_check_arg_t* time_check = (packet_loop_time_check_arg_t*)callback_arg;
        int64_t max_wait = SERVER_MAX_WAIT_US;

        if (ctx->cc_log != NULL && picoquic_get_first_cnx(quic) != NULL) {
            int64_t until_sample = (ctx->next_sample_time > time_check->current_time) ?
                (int64_t)(ctx->next_sample_time - time_check->current_time) : 0;
            if (until_sample < max_wait) {
                max_wait = until_sample;
            }
        }
        if (time_check->delta_t > max_wait) {
            time_check->delta_t = max_wait;
        }
        break;
    }
    default:
        break;
    }

    current_time = picoquic_current_time();
    if (ctx->cc_log != NULL && current_time >= ctx->next_sample_time) {
        server_sample_connections(ctx, quic, current_time);
        ctx->next_sample_time = current_time + ctx->sample_interval;
    }

    return (stop_requested) ? PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP : 0;
}

static void usage(void)
{
    fprintf(stderr, "wip_server: HTTP/3 server for the thesis testbed\n\n");
    fprintf(stderr, "Usage: wip_server -G <cc_algo> [options]\n\n");
    fprintf(stderr, "Main options:\n");
    fprintf(stderr, "  -G <name> | --cc <name>   congestion control algorithm (required)\n");
    fprintf(stderr, "  -p <port>                 UDP port (default %d)\n", WIP_DEFAULT_PORT);
    fprintf(stderr, "  -w <dir>                  also serve files from this folder\n");
    fprintf(stderr, "  -c <file> -k <file>       certificate and key (default: picoquic test certificate)\n");
    fprintf(stderr, "  --cc-log <file.csv>       congestion control samples of every connection\n");
    fprintf(stderr, "  --sample-us <us>          sampling interval for --cc-log (default %d)\n", SERVER_DEFAULT_SAMPLE_US);
    fprintf(stderr, "  --label <text>            text written in the first column of --cc-log\n");
    fprintf(stderr, "  -F <file.csv>             per connection summary (picoquic performance log)\n");
    fprintf(stderr, "  -q <dir>                  qlog per connection\n");
    fprintf(stderr, "  -b <dir>                  binary log per connection\n");
    fprintf(stderr, "  -0                        do not use UDP GSO\n\n");
    picoquic_config_usage_file(stderr);
}

int main(int argc, char** argv)
{
    int ret = 0;
    int opt;
    picoquic_quic_config_t config;
    char option_string[512];
    char default_cert_file[512];
    char default_key_file[512];
    server_ctx_t server_ctx;
    picohttp_server_parameters_t file_param;
    picoquic_quic_t* qserver = NULL;
    enum { LO_CC = 0, LO_CC_LOG, LO_SAMPLE_US, LO_LABEL, LO_HELP, LO_NB };
    wip_long_opt_t long_opts[LO_NB] = {
        { "cc", 1, NULL },
        { "cc-log", 1, NULL },
        { "sample-us", 1, NULL },
        { "label", 1, NULL },
        { "help", 0, NULL }
    };

#ifdef _WINDOWS
    WSADATA wsaData = { 0 };
    (void)WSA_START(MAKEWORD(2, 2), &wsaData);
#endif

    memset(&server_ctx, 0, sizeof(server_ctx));
    server_ctx.sample_interval = SERVER_DEFAULT_SAMPLE_US;
    server_ctx.label = "";

    picoquic_register_all_congestion_control_algorithms();
    picoquic_config_init(&config);

    /* Command line: long options first, then the standard picoquic options. */
    if ((argc = wip_extract_long_options(argc, argv, long_opts, LO_NB)) < 0) {
        usage();
        return 1;
    }
    if (long_opts[LO_HELP].value != NULL) {
        usage();
        return 0;
    }
    ret = picoquic_config_option_letters(option_string, sizeof(option_string), NULL);
    while (ret == 0 && (opt = getopt(argc, argv, option_string)) != -1) {
        if (opt == 'h') {
            usage();
            return 0;
        }
        if (picoquic_config_command_line(opt, &optind, argc, (char const**)argv, optarg, &config) != 0) {
            usage();
            ret = -1;
        }
    }
    if (ret == 0 && optind < argc) {
        fprintf(stderr, "Unexpected argument: %s\n", argv[optind]);
        usage();
        ret = -1;
    }

    /* Congestion control: required, and must be a registered algorithm. */
    if (ret == 0 && long_opts[LO_CC].value != NULL) {
        ret = picoquic_config_set_option(&config, picoquic_option_CC_ALGO, long_opts[LO_CC].value);
    }
    if (ret == 0) {
        if (config.cc_algo_id == NULL) {
            fprintf(stderr, "Choose a congestion control algorithm with -G <name> or --cc <name>.\n");
            wip_print_cc_algorithms(stderr);
            ret = -1;
        }
        else if (picoquic_get_congestion_algorithm(config.cc_algo_id) == NULL) {
            fprintf(stderr, "Unknown congestion control algorithm: %s\n", config.cc_algo_id);
            wip_print_cc_algorithms(stderr);
            ret = -1;
        }
    }

    /* Our options */
    if (ret == 0 && long_opts[LO_SAMPLE_US].value != NULL) {
        server_ctx.sample_interval = strtoull(long_opts[LO_SAMPLE_US].value, NULL, 10);
        if (server_ctx.sample_interval == 0) {
            fprintf(stderr, "Invalid sampling interval: %s\n", long_opts[LO_SAMPLE_US].value);
            ret = -1;
        }
    }
    if (long_opts[LO_LABEL].value != NULL) {
        server_ctx.label = long_opts[LO_LABEL].value;
    }
    if (ret == 0 && long_opts[LO_CC_LOG].value != NULL) {
        if ((server_ctx.cc_log = wip_open_csv(long_opts[LO_CC_LOG].value, cc_log_header)) == NULL) {
            ret = -1;
        }
    }

    /* Defaults: port, and picoquic's test certificate. */
    if (ret == 0) {
        if (config.server_port == 0) {
            config.server_port = WIP_DEFAULT_PORT;
        }
        if (config.server_cert_file == NULL) {
            (void)snprintf(default_cert_file, sizeof(default_cert_file), "%s/certs/cert.pem", WIP_PICOQUIC_DIR);
            ret = picoquic_config_set_option(&config, picoquic_option_CERT, default_cert_file);
        }
        if (ret == 0 && config.server_key_file == NULL) {
            (void)snprintf(default_key_file, sizeof(default_key_file), "%s/certs/key.pem", WIP_PICOQUIC_DIR);
            ret = picoquic_config_set_option(&config, picoquic_option_KEY, default_key_file);
        }
    }

    /* QUIC context. picoquic's demo ALPN selection gives HTTP/3 connections
     * the h3zero server callback, which serves files and generated content. */
    if (ret == 0) {
        memset(&file_param, 0, sizeof(file_param));
        file_param.web_folder = config.www_dir;

        ret = picoquic_server_set_context(&qserver, &config, picoquic_current_time(), NULL, &file_param,
            picoquic_demo_server_callback_select_alpn);
        if (ret != 0) {
            fprintf(stderr, "Could not create the server context (certificate %s, key %s).\n",
                config.server_cert_file, config.server_key_file);
        }
    }
    if (ret == 0 && config.qlog_dir != NULL) {
        ret = picoquic_set_qlog(qserver, config.qlog_dir);
    }
    if (ret == 0 && config.bin_dir != NULL) {
        ret = picoquic_set_binlog(qserver, config.bin_dir);
    }
    if (ret == 0 && config.log_file != NULL) {
        ret = picoquic_set_textlog(qserver, config.log_file);
    }

    if (ret == 0) {
        picoquic_packet_loop_param_t param;

        fprintf(stdout, "wip_server: port %d, cc %s, GSO %s, files from %s\n",
            (int)config.server_port, config.cc_algo_id, (config.do_not_use_gso) ? "off" : "on",
            (config.www_dir != NULL) ? config.www_dir : "(generated content only)");
        if (server_ctx.cc_log != NULL) {
            fprintf(stdout, "CC samples every %" PRIu64 " us to %s\n", server_ctx.sample_interval,
                long_opts[LO_CC_LOG].value);
        }
        fflush(stdout);

        signal(SIGINT, handle_signal);
        signal(SIGTERM, handle_signal);

        memset(&param, 0, sizeof(param));
        param.local_port = config.server_port;
        param.local_af = 0;
        param.dest_if = config.dest_if;
        param.socket_buffer_size = config.socket_buffer_size;
        param.do_not_use_gso = config.do_not_use_gso;

        ret = picoquic_packet_loop_v2(qserver, &param, server_loop_cb, &server_ctx);
        if (ret == PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP) {
            ret = 0;
        }
    }

    /* Freeing the context deletes the remaining connections, which writes
     * their lines in the performance log. */
    if (qserver != NULL) {
        picoquic_free(qserver);
    }
    if (server_ctx.cc_log != NULL) {
        fclose(server_ctx.cc_log);
        fprintf(stdout, "Wrote %" PRIu64 " CC samples.\n", server_ctx.nb_samples);
    }
    picoquic_config_clear(&config);

    fprintf(stdout, "Server exit, ret = %d\n", ret);
    return (ret == 0) ? 0 : 1;
}
