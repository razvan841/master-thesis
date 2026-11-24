/*
* wip_client: HTTP/3 client for the thesis testbed, built on picoquic.
*
* Downloads a list of documents from wip_server (or any HTTP/3 server) and
* records the timing of every request:
*
*   wip_client [options] <server> [port]
*
*   --sizes 10000,1000000   request generated documents of these sizes (GET /<N>)
*   --paths /a.bin,/b.bin   request these paths instead
*   --repeat <n>            repeat the list n times (default 1)
*   --per-cnx <k>           requests per connection (default 1: every request
*                           on a new connection, i.e. a cold start; 0: all
*                           requests on a single connection)
*   --parallel              send the requests of a connection all at once
*                           (default: one after the other)
*   --gap-ms <ms>           idle time between connections (default 0)
*   --timeout-s <s>         give up on a connection after this time (default 60)
*   --req-log <file.csv>    one line per request (see below)
*   --label <text>          text written in the first column of --req-log
*
* Each connection uses a new QUIC context, so no session ticket or path state
* is carried from one connection to the next: every connection is a cold start.
*
* --req-log columns: absolute times are in microseconds since the epoch, as
* given by picoquic_current_time(); durations are in microseconds.
*   t_cnx_start      connection created (first Initial sent right after)
*   t_handshake      handshake done on the client side (1-RTT keys available)
*   t_ready          handshake confirmed by the server (HANDSHAKE_DONE)
*   t_request        request handed to the QUIC stack
*   t_first_byte     first byte of the response
*   t_last_byte      end of the response (FIN)
*   ttfb_us          t_first_byte - t_request
*   fct_req_us       t_last_byte - t_request
*   fct_cnx_us       t_last_byte - t_cnx_start (includes the handshake)
* The cnx_id column is the name of the qlog/binlog file of the connection, on
* both client and server, so rows can be matched with the server's --cc-log.
*
* The exit code is 0 if every request completed with status 200 and the
* expected size, 1 otherwise.
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
#include "picosocks.h"
#include "picoquic_packet_loop.h"
#include "picoquic_config.h"
#include "picoquic_binlog.h"
#include "picoquic_logger.h"
#include "picoquic_qlog.h"
#include "performance_log.h"
#include "h3zero.h"
#include "h3zero_common.h"
#include "wip_common.h"

#define CLIENT_DEFAULT_TIMEOUT_S 60
#define CLIENT_MAX_WAIT_US 100000 /* check timeouts and stop requests at least this often */
#define CLIENT_REQUEST_BUFFER 1024

static const char* req_log_header =
    "label,cnx_index,cnx_id,req_index,stream_id,path,expected_bytes,body_bytes,status,ok,"
    "t_cnx_start,t_handshake,t_ready,t_request,t_first_byte,t_last_byte,"
    "ttfb_us,fct_req_us,fct_cnx_us,error";

typedef struct st_client_request_t {
    char const* path;
    int64_t expected_bytes; /* N for "/N", -1 if unknown */
    uint64_t stream_id;
    h3zero_data_stream_state_t stream_state;
    uint64_t t_request;
    uint64_t t_first_byte;
    uint64_t t_last_byte;
    uint64_t body_bytes;
    char const* error;
    unsigned int is_started : 1;
    unsigned int is_done : 1;
    unsigned int flow_opened : 1;
} client_request_t;

typedef struct st_client_cnx_ctx_t {
    picoquic_cnx_t* cnx;
    client_request_t* req;
    size_t nb_req;
    size_t nb_started;
    size_t nb_done;
    int parallel;
    int h3_initialized;
    uint64_t t_start;
    uint64_t t_handshake;
    uint64_t t_ready;
    uint64_t timeout;
    char const* end_reason;
} client_cnx_ctx_t;

static volatile sig_atomic_t stop_requested = 0;

static void handle_signal(int sig)
{
    (void)sig;
    stop_requested = 1;
}

/* "/12345" -> 12345, anything else -> -1 */
static int64_t expected_size_of_path(char const* path)
{
    int64_t x = 0;

    if (path[0] != '/' || path[1] == 0) {
        return -1;
    }
    for (char const* p = path + 1; *p != 0; p++) {
        if (*p < '0' || *p > '9') {
            return -1;
        }
        x = 10 * x + (*p - '0');
    }
    return x;
}

static client_request_t* client_find_request(client_cnx_ctx_t* ctx, uint64_t stream_id)
{
    for (size_t i = 0; i < ctx->nb_req; i++) {
        if (ctx->req[i].is_started && ctx->req[i].stream_id == stream_id) {
            return &ctx->req[i];
        }
    }
    return NULL;
}

static int client_send_request(client_cnx_ctx_t* ctx, client_request_t* req)
{
    uint8_t buffer[CLIENT_REQUEST_BUFFER];
    size_t request_length = 0;
    int ret;

    req->stream_id = picoquic_get_next_local_stream_id(ctx->cnx, 0);
    ret = h3zero_client_create_stream_request(buffer, sizeof(buffer),
        (uint8_t const*)req->path, strlen(req->path), 0, ctx->cnx->sni, &request_length);
    if (ret == 0) {
        req->t_request = picoquic_current_time();
        ret = picoquic_add_to_stream_with_ctx(ctx->cnx, req->stream_id, buffer, request_length, 1, req);
    }
    if (ret == 0) {
        req->is_started = 1;
        ctx->nb_started++;
    }
    else {
        req->error = "cannot send request";
    }
    return ret;
}

/* Start the HTTP/3 session if needed, then the next request(s). */
static int client_start_requests(client_cnx_ctx_t* ctx)
{
    int ret = 0;

    if (!ctx->h3_initialized) {
        ret = h3zero_protocol_init(ctx->cnx);
        ctx->h3_initialized = 1;
    }
    while (ret == 0 && ctx->nb_started < ctx->nb_req &&
        (ctx->parallel || ctx->nb_started == ctx->nb_done)) {
        ret = client_send_request(ctx, &ctx->req[ctx->nb_started]);
    }
    return ret;
}

static int client_finish_request(client_cnx_ctx_t* ctx, client_request_t* req, uint64_t current_time, char const* error)
{
    int ret = 0;

    req->t_last_byte = current_time;
    req->is_done = 1;
    if (error != NULL && req->error == NULL) {
        req->error = error;
    }
    ctx->nb_done++;
    picoquic_unlink_app_stream_ctx(ctx->cnx, req->stream_id);

    if (ctx->nb_done >= ctx->nb_req) {
        ctx->end_reason = "done";
        ret = picoquic_close(ctx->cnx, 0);
    }
    else if (!ctx->parallel) {
        ret = client_start_requests(ctx);
    }
    return ret;
}

static int client_callback(picoquic_cnx_t* cnx, uint64_t stream_id, uint8_t* bytes, size_t length,
    picoquic_call_back_event_t fin_or_event, void* callback_ctx, void* v_stream_ctx)
{
    int ret = 0;
    client_cnx_ctx_t* ctx = (client_cnx_ctx_t*)callback_ctx;
    client_request_t* req = (client_request_t*)v_stream_ctx;
    uint64_t current_time = picoquic_current_time();

    switch (fin_or_event) {
    case picoquic_callback_stream_data:
    case picoquic_callback_stream_fin:
        if (req == NULL) {
            req = client_find_request(ctx, stream_id);
        }
        if (req == NULL || req->is_done) {
            /* Control and QPACK streams opened by the server: not needed here. */
            break;
        }
        if (length > 0 && req->t_first_byte == 0) {
            req->t_first_byte = current_time;
        }
        if (length > 0) {
            uint8_t* bytes_max = bytes + length;
            while (bytes != NULL && bytes < bytes_max) {
                size_t available_data = 0;
                uint64_t error_found = 0;

                bytes = h3zero_parse_data_stream(bytes, bytes_max, &req->stream_state, &available_data, &error_found);
                if (bytes == NULL) {
                    ret = client_finish_request(ctx, req, current_time, "HTTP/3 parse error");
                    if (ret == 0) {
                        ret = picoquic_close(cnx, error_found);
                    }
                    break;
                }
                if (available_data > 0) {
                    if (!req->flow_opened) {
                        /* Open the stream flow control for large responses, as picoquic's demo client does. */
                        req->flow_opened = 1;
                        if (req->stream_state.current_frame_length >= 0x100000 && cnx->cnx_state == picoquic_state_ready) {
                            ret = picoquic_open_flow_control(cnx, stream_id, req->stream_state.current_frame_length);
                        }
                    }
                    req->body_bytes += available_data;
                    bytes += available_data;
                }
            }
        }
        if (ret == 0 && fin_or_event == picoquic_callback_stream_fin && !req->is_done) {
            ret = client_finish_request(ctx, req, current_time, NULL);
        }
        break;
    case picoquic_callback_stream_reset:
    case picoquic_callback_stop_sending:
        if (req == NULL) {
            req = client_find_request(ctx, stream_id);
        }
        if (req != NULL && !req->is_done) {
            ret = client_finish_request(ctx, req, current_time, "stream reset by server");
        }
        break;
    case picoquic_callback_almost_ready:
        if (ctx->t_handshake == 0) {
            ctx->t_handshake = current_time;
        }
        ret = client_start_requests(ctx);
        break;
    case picoquic_callback_ready:
        if (ctx->t_handshake == 0) {
            ctx->t_handshake = current_time;
        }
        ctx->t_ready = current_time;
        ret = client_start_requests(ctx);
        break;
    case picoquic_callback_close:
    case picoquic_callback_application_close:
    case picoquic_callback_stateless_reset:
        if (ctx->end_reason == NULL) {
            ctx->end_reason = "closed by server";
        }
        break;
    default:
        break;
    }
    return ret;
}

static int client_loop_cb(picoquic_quic_t* quic, picoquic_packet_loop_cb_enum cb_mode,
    void* callback_ctx, void* callback_arg)
{
    client_cnx_ctx_t* ctx = (client_cnx_ctx_t*)callback_ctx;
    picoquic_state_enum state;
    (void)quic;

    switch (cb_mode) {
    case picoquic_packet_loop_ready: {
        picoquic_packet_loop_options_t* options = (picoquic_packet_loop_options_t*)callback_arg;
        options->do_time_check = 1;
        break;
    }
    case picoquic_packet_loop_time_check: {
        packet_loop_time_check_arg_t* time_check = (packet_loop_time_check_arg_t*)callback_arg;
        if (time_check->delta_t > CLIENT_MAX_WAIT_US) {
            time_check->delta_t = CLIENT_MAX_WAIT_US;
        }
        break;
    }
    default:
        break;
    }

    state = picoquic_get_cnx_state(ctx->cnx);
    /* Stop once the connection is closing: our CONNECTION_CLOSE has been
     * sent, or the server closed the connection. */
    if (state > picoquic_state_disconnecting) {
        return PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP;
    }
    if (picoquic_current_time() > ctx->t_start + ctx->timeout) {
        ctx->end_reason = "timeout";
        return PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP;
    }
    if (stop_requested) {
        ctx->end_reason = "interrupted";
        return PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP;
    }
    return 0;
}

typedef struct st_client_run_t {
    picoquic_quic_config_t* config;
    struct sockaddr_storage server_addr;
    char const* sni;
    char const* label;
    FILE* req_log;
    int parallel;
    uint64_t timeout;
    int nb_failed;
} client_run_t;

static int request_is_ok(client_request_t const* req)
{
    return req->is_done && req->error == NULL && req->stream_state.header.status == 200 &&
        (req->expected_bytes < 0 || req->body_bytes == (uint64_t)req->expected_bytes);
}

static void client_log_requests(client_run_t* run, client_cnx_ctx_t* ctx, int cnx_index, size_t first_req_index, char const* cid_text)
{
    for (size_t i = 0; i < ctx->nb_req; i++) {
        client_request_t* req = &ctx->req[i];
        int ok = request_is_ok(req);
        uint64_t ttfb = (req->t_first_byte > req->t_request && req->t_request > 0) ? req->t_first_byte - req->t_request : 0;
        uint64_t fct_req = (ok) ? req->t_last_byte - req->t_request : 0;
        uint64_t fct_cnx = (ok) ? req->t_last_byte - ctx->t_start : 0;

        if (!ok) {
            run->nb_failed++;
        }
        if (run->req_log != NULL) {
            fprintf(run->req_log,
                "%s,%d,%s,%zu,%" PRIu64 ",%s,%" PRId64 ",%" PRIu64 ",%d,%d,"
                "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ","
                "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%s\n",
                run->label, cnx_index, cid_text, first_req_index + i, req->stream_id, req->path,
                req->expected_bytes, req->body_bytes, req->stream_state.header.status, ok,
                ctx->t_start, ctx->t_handshake, ctx->t_ready, req->t_request, req->t_first_byte, req->t_last_byte,
                ttfb, fct_req, fct_cnx, (req->error != NULL) ? req->error : "");
        }
        if (ok) {
            fprintf(stdout, "%s %s: %" PRIu64 " bytes, ttfb %.3f ms, fct %.3f ms (%.3f ms with handshake)\n",
                cid_text, req->path, req->body_bytes, ttfb / 1000.0, fct_req / 1000.0, fct_cnx / 1000.0);
        }
        else {
            fprintf(stdout, "%s %s: FAILED (%s, status %d, %" PRIu64 " bytes)\n", cid_text, req->path,
                (req->error != NULL) ? req->error : "unexpected status or size", req->stream_state.header.status, req->body_bytes);
        }
    }
    if (run->req_log != NULL) {
        fflush(run->req_log);
    }
}

/* One connection, on a fresh QUIC context, for requests req[0..nb_req-1]. */
static int client_run_connection(client_run_t* run, client_request_t* req, size_t nb_req, int cnx_index, size_t first_req_index)
{
    int ret = 0;
    picoquic_quic_config_t* config = run->config;
    picoquic_quic_t* quic;
    client_cnx_ctx_t ctx;
    char cid_text[2 * PICOQUIC_CONNECTION_ID_MAX_SIZE + 1] = { 0 };

    memset(&ctx, 0, sizeof(ctx));
    ctx.req = req;
    ctx.nb_req = nb_req;
    ctx.parallel = run->parallel;
    ctx.timeout = run->timeout;

    quic = picoquic_create_and_configure(config, NULL, NULL, picoquic_current_time(), NULL);
    if (quic == NULL) {
        fprintf(stderr, "Could not create the QUIC context.\n");
        return -1;
    }
    if (config->root_trust_file == NULL) {
        /* Testbed: the server uses a self-signed test certificate. */
        picoquic_set_null_verifier(quic);
    }
    if (config->qlog_dir != NULL) {
        ret = picoquic_set_qlog(quic, config->qlog_dir);
    }
    if (ret == 0 && config->bin_dir != NULL) {
        ret = picoquic_set_binlog(quic, config->bin_dir);
    }
    if (ret == 0 && config->log_file != NULL) {
        ret = picoquic_set_textlog(quic, config->log_file);
    }
    if (ret == 0 && config->performance_log != NULL) {
        ret = picoquic_perflog_setup(quic, config->performance_log);
    }

    if (ret == 0) {
        ctx.t_start = picoquic_current_time();
        ctx.cnx = picoquic_create_cnx(quic, picoquic_null_connection_id, picoquic_null_connection_id,
            (struct sockaddr*)&run->server_addr, ctx.t_start, config->proposed_version, run->sni, "h3", 1);
        if (ctx.cnx == NULL) {
            fprintf(stderr, "Could not create the connection.\n");
            ret = -1;
        }
        else {
            picoquic_set_callback(ctx.cnx, client_callback, &ctx);
            wip_cid_to_hex(picoquic_get_logging_cnxid(ctx.cnx), cid_text, sizeof(cid_text));
            ret = picoquic_start_client_cnx(ctx.cnx);
        }
    }

    if (ret == 0) {
        picoquic_packet_loop_param_t param;

        memset(&param, 0, sizeof(param));
        param.local_port = config->local_port;
        param.local_af = run->server_addr.ss_family;
        param.dest_if = config->dest_if;
        param.socket_buffer_size = config->socket_buffer_size;
        param.do_not_use_gso = config->do_not_use_gso;

        ret = picoquic_packet_loop_v2(quic, &param, client_loop_cb, &ctx);
        if (ret == PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP) {
            ret = 0;
        }
    }

    if (ctx.cnx != NULL) {
        uint64_t err;
        char const* reason = (ctx.end_reason != NULL) ? ctx.end_reason : "connection ended";

        for (size_t i = 0; i < nb_req; i++) {
            if (!req[i].is_done) {
                req[i].error = reason;
            }
        }
        if ((err = picoquic_get_local_error(ctx.cnx)) != 0) {
            fprintf(stdout, "%s: local error 0x%" PRIx64 " (%s)\n", cid_text, err, picoquic_error_name(err));
        }
        if ((err = picoquic_get_remote_error(ctx.cnx)) != 0) {
            fprintf(stdout, "%s: remote error 0x%" PRIx64 " (%s)\n", cid_text, err, picoquic_error_name(err));
        }
        client_log_requests(run, &ctx, cnx_index, first_req_index, cid_text);
    }

    /* Deleting the context deletes the connection and writes its line in the
     * performance log. */
    picoquic_free(quic);
    for (size_t i = 0; i < nb_req; i++) {
        h3zero_delete_data_stream_state(&req[i].stream_state);
    }

    return ret;
}

/* Split a comma separated list. The returned array points into a copy of
 * the text, which the caller frees with the array. */
static char** split_list(char const* text, size_t* nb_items, char** copy)
{
    size_t n = 1;
    char** items;
    char* p;

    *copy = picoquic_string_duplicate(text);
    if (*copy == NULL) {
        return NULL;
    }
    for (char const* c = text; *c != 0; c++) {
        if (*c == ',') {
            n++;
        }
    }
    items = (char**)malloc(n * sizeof(char*));
    if (items == NULL) {
        return NULL;
    }
    *nb_items = 0;
    p = *copy;
    while (p != NULL) {
        char* comma = strchr(p, ',');
        if (comma != NULL) {
            *comma = 0;
        }
        if (*p != 0) {
            items[(*nb_items)++] = p;
        }
        p = (comma != NULL) ? comma + 1 : NULL;
    }
    return items;
}

static void usage(void)
{
    fprintf(stderr, "wip_client: HTTP/3 client for the thesis testbed\n\n");
    fprintf(stderr, "Usage: wip_client [options] <server> [port (default %d)]\n\n", WIP_DEFAULT_PORT);
    fprintf(stderr, "Main options:\n");
    fprintf(stderr, "  --sizes <n1,n2,...>   request generated documents of these sizes (GET /<n>)\n");
    fprintf(stderr, "  --paths <p1,p2,...>   request these paths instead\n");
    fprintf(stderr, "  --repeat <n>          repeat the list n times (default 1)\n");
    fprintf(stderr, "  --per-cnx <k>         requests per connection (default 1 = cold start each time, 0 = all on one)\n");
    fprintf(stderr, "  --parallel            send the requests of a connection all at once\n");
    fprintf(stderr, "  --gap-ms <ms>         idle time between connections (default 0)\n");
    fprintf(stderr, "  --timeout-s <s>       give up on a connection after this time (default %d)\n", CLIENT_DEFAULT_TIMEOUT_S);
    fprintf(stderr, "  --req-log <file.csv>  one line per request with all timings\n");
    fprintf(stderr, "  --label <text>        text written in the first column of --req-log\n");
    fprintf(stderr, "  -F <file.csv>         per connection summary (picoquic performance log)\n");
    fprintf(stderr, "  -q <dir>, -b <dir>    qlog / binary log per connection\n");
    fprintf(stderr, "  -n <sni>              server name sent in the handshake\n");
    fprintf(stderr, "  -t <file>             verify the server certificate with this root (default: no verification)\n\n");
    picoquic_config_usage_file(stderr);
}

int main(int argc, char** argv)
{
    int ret = 0;
    int opt;
    picoquic_quic_config_t config;
    char option_string[512];
    client_run_t run;
    char const* server_name = NULL;
    int server_port = WIP_DEFAULT_PORT;
    int is_name = 0;
    uint64_t repeat = 1;
    uint64_t per_cnx = 1;
    uint64_t gap_ms = 0;
    char** items = NULL;
    char* items_copy = NULL;
    size_t nb_items = 0;
    client_request_t* req = NULL;
    size_t nb_req = 0;
    char** size_paths = NULL;
    enum { LO_SIZES = 0, LO_PATHS, LO_REPEAT, LO_PER_CNX, LO_PARALLEL, LO_GAP_MS, LO_TIMEOUT_S,
        LO_REQ_LOG, LO_LABEL, LO_CC, LO_HELP, LO_NB };
    wip_long_opt_t long_opts[LO_NB] = {
        { "sizes", 1, NULL },
        { "paths", 1, NULL },
        { "repeat", 1, NULL },
        { "per-cnx", 1, NULL },
        { "parallel", 0, NULL },
        { "gap-ms", 1, NULL },
        { "timeout-s", 1, NULL },
        { "req-log", 1, NULL },
        { "label", 1, NULL },
        { "cc", 1, NULL },
        { "help", 0, NULL }
    };

#ifdef _WINDOWS
    WSADATA wsaData = { 0 };
    (void)WSA_START(MAKEWORD(2, 2), &wsaData);
#endif

    memset(&run, 0, sizeof(run));
    run.config = &config;
    run.label = "";
    run.timeout = (uint64_t)CLIENT_DEFAULT_TIMEOUT_S * 1000000;

    picoquic_register_all_congestion_control_algorithms();
    picoquic_config_init(&config);

    /* Command line */
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
    if (ret == 0) {
        if (optind < argc) {
            server_name = argv[optind++];
        }
        if (optind < argc) {
            server_port = atoi(argv[optind++]);
        }
        if (server_name == NULL || server_port <= 0 || optind < argc) {
            usage();
            ret = -1;
        }
    }
    if (ret == 0 && long_opts[LO_CC].value != NULL) {
        ret = picoquic_config_set_option(&config, picoquic_option_CC_ALGO, long_opts[LO_CC].value);
    }
    if (ret == 0 && config.cc_algo_id != NULL && picoquic_get_congestion_algorithm(config.cc_algo_id) == NULL) {
        fprintf(stderr, "Unknown congestion control algorithm: %s\n", config.cc_algo_id);
        wip_print_cc_algorithms(stderr);
        ret = -1;
    }
    if (ret == 0) {
        if (long_opts[LO_REPEAT].value != NULL) {
            repeat = strtoull(long_opts[LO_REPEAT].value, NULL, 10);
        }
        if (long_opts[LO_PER_CNX].value != NULL) {
            per_cnx = strtoull(long_opts[LO_PER_CNX].value, NULL, 10);
        }
        if (long_opts[LO_GAP_MS].value != NULL) {
            gap_ms = strtoull(long_opts[LO_GAP_MS].value, NULL, 10);
        }
        if (long_opts[LO_TIMEOUT_S].value != NULL) {
            run.timeout = strtoull(long_opts[LO_TIMEOUT_S].value, NULL, 10) * 1000000;
        }
        if (long_opts[LO_LABEL].value != NULL) {
            run.label = long_opts[LO_LABEL].value;
        }
        run.parallel = (long_opts[LO_PARALLEL].value != NULL);
        if (repeat == 0 || run.timeout == 0) {
            fprintf(stderr, "--repeat and --timeout-s must be at least 1.\n");
            ret = -1;
        }
    }

    /* Request list: the sizes or paths, repeated. */
    if (ret == 0) {
        int use_sizes = (long_opts[LO_SIZES].value != NULL);
        if (use_sizes == (long_opts[LO_PATHS].value != NULL)) {
            fprintf(stderr, "Give either --sizes or --paths.\n");
            ret = -1;
        }
        else if ((items = split_list(use_sizes ? long_opts[LO_SIZES].value : long_opts[LO_PATHS].value,
            &nb_items, &items_copy)) == NULL || nb_items == 0) {
            fprintf(stderr, "Empty or invalid request list.\n");
            ret = -1;
        }
        else if (use_sizes) {
            /* Turn "1000" into "/1000" */
            size_paths = (char**)calloc(nb_items, sizeof(char*));
            for (size_t i = 0; ret == 0 && size_paths != NULL && i < nb_items; i++) {
                size_t len = strlen(items[i]) + 2;
                if ((size_paths[i] = (char*)malloc(len)) == NULL) {
                    ret = -1;
                }
                else {
                    (void)snprintf(size_paths[i], len, "/%s", items[i]);
                    if (expected_size_of_path(size_paths[i]) < 0) {
                        fprintf(stderr, "Invalid size: %s\n", items[i]);
                        ret = -1;
                    }
                }
            }
            if (size_paths == NULL) {
                ret = -1;
            }
        }
    }
    if (ret == 0) {
        nb_req = nb_items * (size_t)repeat;
        if ((req = (client_request_t*)calloc(nb_req, sizeof(client_request_t))) == NULL) {
            ret = -1;
        }
        for (size_t i = 0; ret == 0 && i < nb_req; i++) {
            req[i].path = (size_paths != NULL) ? size_paths[i % nb_items] : items[i % nb_items];
            req[i].expected_bytes = expected_size_of_path(req[i].path);
        }
    }

    /* Server address and name */
    if (ret == 0) {
        if (picoquic_get_server_address(server_name, server_port, &run.server_addr, &is_name) != 0) {
            fprintf(stderr, "Cannot resolve %s port %d\n", server_name, server_port);
            ret = -1;
        }
        run.sni = (config.sni != NULL) ? config.sni : ((is_name) ? server_name : PICOQUIC_TEST_SNI);
    }
    if (ret == 0 && long_opts[LO_REQ_LOG].value != NULL) {
        if ((run.req_log = wip_open_csv(long_opts[LO_REQ_LOG].value, req_log_header)) == NULL) {
            ret = -1;
        }
    }

    /* Run the connections */
    if (ret == 0) {
        size_t next_req = 0;
        int cnx_index = 0;

        signal(SIGINT, handle_signal);
        signal(SIGTERM, handle_signal);

        if (per_cnx == 0) {
            fprintf(stdout, "wip_client: %s port %d, %zu requests, all on one connection, %s\n",
                server_name, server_port, nb_req, (run.parallel) ? "parallel" : "sequential");
        }
        else {
            fprintf(stdout, "wip_client: %s port %d, %zu requests, %" PRIu64 " per connection, %s\n",
                server_name, server_port, nb_req, per_cnx, (run.parallel) ? "parallel" : "sequential");
        }

        while (next_req < nb_req && !stop_requested) {
            size_t n = (per_cnx == 0 || per_cnx > nb_req - next_req) ? nb_req - next_req : (size_t)per_cnx;

            if (cnx_index > 0 && gap_ms > 0) {
                wip_sleep_ms(gap_ms);
            }
            if (client_run_connection(&run, &req[next_req], n, cnx_index, next_req) != 0) {
                fprintf(stderr, "Connection %d failed to run.\n", cnx_index);
            }
            next_req += n;
            cnx_index++;
        }
        if (next_req < nb_req) {
            run.nb_failed += (int)(nb_req - next_req);
        }
        fprintf(stdout, "%zu requests, %d failed.\n", nb_req, run.nb_failed);
    }

    /* Clean up */
    if (run.req_log != NULL) {
        fclose(run.req_log);
    }
    if (size_paths != NULL) {
        for (size_t i = 0; i < nb_items; i++) {
            free(size_paths[i]);
        }
        free(size_paths);
    }
    free(req);
    free(items);
    free(items_copy);
    picoquic_config_clear(&config);

    return (ret == 0 && run.nb_failed == 0) ? 0 : 1;
}
