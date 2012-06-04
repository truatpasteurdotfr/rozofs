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

#include "config.h"
#include "xmalloc.h"
#include "log.h"
#include "list.h"
#include "daemon.h"
#include "storage.h"
#include "sproto.h"
#include "sconfig.h"

#define STORAGED_PID_FILE "storaged.pid"

static char storaged_config_file[PATH_MAX] = STORAGED_DEFAULT_CONFIG;
static sconfig_t storaged_config;
static storage_t *storaged_storages = NULL;
static uint16_t storaged_nrstorages = 0;
extern void storage_program_1(struct svc_req *rqstp, SVCXPRT * ctl_svc);
static SVCXPRT *storaged_svc = NULL;

static int storaged_initialize() {
    int status = -1;
    list_t *p;
    DEBUG_FUNCTION;

    if (rozofs_initialize(storaged_config.layout) != 0) {
        severe("can't initialize rozofs");
        goto out;
    }

    storaged_storages = xmalloc(list_size(&storaged_config.storages) *
            sizeof (storage_t));
    storaged_nrstorages = 0;
    list_for_each_forward(p, &storaged_config.storages) {
        storage_config_t *sc = list_entry(p, storage_config_t, list);
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

static void on_start() {
    int sock;
    int one = 1;
    DEBUG_FUNCTION;

    if (storaged_initialize() != 0) {
        fatal("can't initialize storaged: %s.", strerror(errno));
        return;
    }

    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *) &one, sizeof (int));
    setsockopt(sock, SOL_TCP, TCP_DEFER_ACCEPT, (char *) &one, sizeof (int));
    setsockopt(sock, SOL_TCP, TCP_NODELAY, (char *) &one, sizeof (int));

    // SET NONBLOCK
    int value = 1;
    int oldflags = fcntl(sock, F_GETFL, 0);
    /* If reading the flags failed, return error indication now. */
    if (oldflags < 0) {
        return;
    }
    /* Set just the flag we want to set. */
    if (value != 0) {
        oldflags |= O_NONBLOCK;
    } else {
        oldflags &= ~O_NONBLOCK;
    }
    /* Store modified flag word in the descriptor. */
    fcntl(sock, F_SETFL, oldflags);

    if ((storaged_svc =
            svctcp_create(sock, ROZOFS_RPC_BUFFER_SIZE,
            ROZOFS_RPC_BUFFER_SIZE)) == NULL) {
        fatal("can't create service.");
        return;
    }

    pmap_unset(STORAGE_PROGRAM, STORAGE_VERSION); // in case !

    if (!svc_register
            (storaged_svc, STORAGE_PROGRAM, STORAGE_VERSION, storage_program_1,
            IPPROTO_TCP)) {
        fatal("can't register service : %s", strerror(errno));
        return;
    }

    info("running.");
    svc_run();
}

static void on_stop() {
    DEBUG_FUNCTION;

    svc_exit();
    svc_unregister(STORAGE_PROGRAM, STORAGE_VERSION);
    pmap_unset(STORAGE_PROGRAM, STORAGE_VERSION);
    if (storaged_svc) {
        svc_destroy(storaged_svc);
        storaged_svc = NULL;
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

    if (sconfig_initialize(&storaged_config) != 0) {
        fprintf(stderr, "can't initialize storaged config: %s.\n",
                strerror(errno));
        goto error;
    }
    if (sconfig_read(&storaged_config, storaged_config_file) != 0) {
        fprintf(stderr, "failed to parse configuration file: %s.\n",
                strerror(errno));
        goto error;
    }
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
