/*
  Copyright (c) 2010 Fizians SAS. <http://www.fizians.com>
  This file is part of Rozofs.

  Rozofs is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  Rozofs is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <sys/stat.h>
#include <netinet/tcp.h>
#include <sys/resource.h>
#include <unistd.h>
#include <libintl.h>
#include <sys/poll.h>
#include <rpc/rpc.h>
#include <rpc/pmap_clnt.h>
#include <libconfig.h>
#include <getopt.h>
#include <inttypes.h>

#include "config.h"
#include "xmalloc.h"
#include "log.h"
#include "list.h"
#include "daemon.h"
#include "storage.h"
#include "mproto.h"
#include "sproto.h"
#include "sconfig.h"

#define STORAGED_PID_FILE "storaged.pid"

static char storaged_config_file[PATH_MAX] = STORAGED_DEFAULT_CONFIG;
static sconfig_t storaged_config;
static storage_t *storaged_storages = NULL;
static uint16_t storaged_nrstorages = 0;
extern void storage_program_1(struct svc_req *rqstp, SVCXPRT * ctl_svc);
extern void monitor_program_1(struct svc_req *rqstp, SVCXPRT * ctl_svc);
static SVCXPRT *monitor_svc = NULL;
uint32_t ports[STORAGE_NODE_PORTS_MAX];
uint8_t process_nb;

static int storaged_initialize() {
    int status = -1;
    list_t *p = NULL;
    DEBUG_FUNCTION;

    /* Initialize rozofs constants (redundancy) */
    if (rozofs_initialize(storaged_config.layout) != 0) {
        severe("can't initialize rozofs");
        goto out;
    }

    storaged_storages = xmalloc(list_size(&storaged_config.storages) *
            sizeof (storage_t));

    storaged_nrstorages = 0;

    process_nb = storaged_config.sproto_svc_nb;
    memcpy(ports, storaged_config.ports, STORAGE_NODE_PORTS_MAX * sizeof (uint32_t));

    /* For each storage on configuration file */
    list_for_each_forward(p, &storaged_config.storages) {
        storage_config_t *sc = list_entry(p, storage_config_t, list);
        /* Initialize the storage */
        if (storage_initialize(storaged_storages + storaged_nrstorages++,
                sc->sid, sc->root) != 0) {
            severe("can't initialize storage (sid:%d) with path %s",
                    sc->sid, sc->root);
            goto out;
        }
    }

    status = 0;
out:
    return status;
}

static void storaged_release() {
    DEBUG_FUNCTION;

    if (storaged_storages) {
        storage_t *st = storaged_storages;
        while (st != storaged_storages + storaged_nrstorages)
            storage_release(st++);
        free(storaged_storages);
        storaged_nrstorages = 0;
        storaged_storages = 0;
    }
}

storage_t *storaged_lookup(sid_t sid) {
    storage_t *st = 0;
    DEBUG_FUNCTION;

    st = storaged_storages;
    do {
        if (st->sid == sid)
            goto out;
    } while (st++ != storaged_storages + storaged_nrstorages);
    errno = EINVAL;
    st = 0;
out:
    return st;
}

static SVCXPRT *create_tcp_service(uint32_t port) {
    int sock;
    int one = 1;
    SVCXPRT *storaged_svc = NULL;
    struct sockaddr_in sin;

    /* Give the socket a name. */
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    sin.sin_addr.s_addr = INADDR_ANY;

    /* Create the socket. */
    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        fatal("Can't create socket: %s.", strerror(errno));
        goto out;
    }

    /* Set socket options */
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *) &one, sizeof (int));
    setsockopt(sock, SOL_TCP, TCP_DEFER_ACCEPT, (char *) &one, sizeof (int));
    setsockopt(sock, SOL_TCP, TCP_NODELAY, (char *) &one, sizeof (int));

    /* Bind the socket */
    if (bind(sock, (struct sockaddr *) &sin, sizeof (struct sockaddr)) < 0) {
        fatal("Couldn't bind to tcp port %d", port);
        goto out;
    }

    /* Creates a TCP/IP-based RPC service transport */
    if ((storaged_svc = svctcp_create(sock, ROZOFS_RPC_BUFFER_SIZE, ROZOFS_RPC_BUFFER_SIZE)) == NULL) {
        fatal("Can't create TCP/IP-based RPC service transport");
        goto out;
    }

out:
    return storaged_svc;
}

static void on_start() {
    int i = 0;
    DEBUG_FUNCTION;

    /* Initialization of the storage configuration */
    if (storaged_initialize() != 0) {
        fatal("can't initialize storaged: %s.", strerror(errno));
        return;
    }

    /* Launching storaged processes */
    for (i = 0; i < process_nb; i++) {

        /* Create child process */
        if (!fork()) {

            /* This is the child process. */
            SVCXPRT * storage_svc = NULL;

            if ((storage_svc = create_tcp_service(ports[i])) == NULL) {
                severe("create_tcp_service failed");
                return;
            }

            /* Associates STORAGE_PROGRAM and STORAGE_PROGRAM
             * with the service dispatch procedure, storage_program_1.
             * Here protocol is zero, the service is not registered with
             *  the portmap service */
            if (!svc_register(storage_svc, STORAGE_PROGRAM, STORAGE_VERSION, storage_program_1, 0)) {
                fatal("can't register service : %s", strerror(errno));
                return;
            }

            /* Waits for RPC requests to arrive ! */
            info("running storage service (pid=%d, port=%"PRIu32").", getpid(), ports[i]);
            svc_run();
            /* NOT REACHED */
        }
    }

    int sock;
    int one = 1;
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *) &one, sizeof (int));
    setsockopt(sock, SOL_TCP, TCP_DEFER_ACCEPT, (char *) &one, sizeof (int));
    setsockopt(sock, SOL_TCP, TCP_NODELAY, (char *) &one, sizeof (int));

    if ((monitor_svc =
            svctcp_create(sock, ROZOFS_RPC_BUFFER_SIZE,
            ROZOFS_RPC_BUFFER_SIZE)) == NULL) {
        fatal("can't create service.");
        return;
    }

    /* Destroy portmap mapping */
    pmap_unset(MONITOR_PROGRAM, MONITOR_VERSION); // in case !

    /* Associates MONITOR_PROGRAM and MONITOR_VERSION
     * with the service dispatch procedure, monitor_program_1.
     * Here protocol is no zero, the service is registered with 
     * the portmap service */
    if (!svc_register(monitor_svc, MONITOR_PROGRAM, MONITOR_VERSION,
            monitor_program_1,
            IPPROTO_TCP)) {
        fatal("can't register service : %s", strerror(errno));
        return;
    }

    /* Waits for RPC requests to arrive ! */
    info("running monitor storage service.");
    svc_run();
    /* NOT REACHED */
}

static void on_stop() {
    DEBUG_FUNCTION;

    svc_exit();
    svc_unregister(MONITOR_PROGRAM, MONITOR_VERSION);
    pmap_unset(MONITOR_PROGRAM, MONITOR_VERSION);
    if (monitor_svc) {
        svc_destroy(monitor_svc);
        monitor_svc = NULL;
    }
    storaged_release();
    rozofs_release();
    info("stopped.");
    closelog();
}

void usage() {
    printf("Rozofs storage daemon - %s\n", VERSION);
    printf("Usage: storaged [OPTIONS]\n\n");
    printf("\t-h, --help\tprint this message.\n");
    printf("\t-c, --config\tconfig file to use (default: %s).\n",
            STORAGED_DEFAULT_CONFIG);
};

int main(int argc, char *argv[]) {
    int c;
    static struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"config", required_argument, 0, 'c'},
        {0, 0, 0, 0}
    };

    while (1) {

        int option_index = 0;
        c = getopt_long(argc, argv, "hc:", long_options, &option_index);

        if (c == -1)
            break;

        switch (c) {

            case 'h':
                usage();
                exit(EXIT_SUCCESS);
                break;
            case 'c':
                if (!realpath(optarg, storaged_config_file)) {
                    fprintf(stderr, "storaged failed: %s %s\n", optarg,
                            strerror(errno));
                    exit(EXIT_FAILURE);
                }
                break;
            case '?':
                usage();
                exit(EXIT_SUCCESS);
                break;
            default:
                usage();
                exit(EXIT_FAILURE);
                break;
        }
    }
    /* Initialize the list of storage config */
    if (sconfig_initialize(&storaged_config) != 0) {
        fprintf(stderr, "can't initialize storaged config: %s.\n",
                strerror(errno));
        goto error;
    }
    /* Read the configuration file */
    if (sconfig_read(&storaged_config, storaged_config_file) != 0) {
        fprintf(stderr, "failed to parse configuration file: %s.\n",
                strerror(errno));
        goto error;
    }
    /* Check the configuration */
    if (sconfig_validate(&storaged_config) != 0) {
        fprintf(stderr, "inconsistent configuration file: %s.\n",
                strerror(errno));
        goto error;
    }

    openlog("storaged", LOG_PID, LOG_DAEMON);
    daemon_start(STORAGED_PID_FILE, on_start, on_stop, NULL);
    exit(0);
error:
    fprintf(stderr, "see log for more details.\n");
    exit(EXIT_FAILURE);
}
