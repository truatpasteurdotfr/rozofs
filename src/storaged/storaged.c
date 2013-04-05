/*
 Copyright (c) 2010 Fizians SAS. <http://www.fizians.com>
 This file is part of Rozofs.

 Rozofs is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published
 by the Free Software Foundation, version 2.

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
#include <time.h>
#include <limits.h>
#include <unistd.h>
#include <netdb.h>

#include <rozofs/common/log.h>
#include <rozofs/common/xmalloc.h>
#include <rozofs/common/list.h>
#include <rozofs/rozofs_srv.h>
#include <rozofs/common/daemon.h>
#include <rozofs/common/profile.h>
#include <rozofs/rpc/mproto.h>
#include <rozofs/rpc/sproto.h>
#include <rozofs/rpc/spproto.h>

#include "config.h"
#include "sconfig.h"
#include "storage.h"
#include "storaged.h"
#include "rbs.h"

#define STORAGED_PID_FILE "storaged.pid"

static char storaged_config_file[PATH_MAX] = STORAGED_DEFAULT_CONFIG;

static sconfig_t storaged_config;

static storage_t storaged_storages[STORAGES_MAX_BY_STORAGE_NODE] = {
    {0}
};

static char *storaged_hostname = NULL;

static uint16_t storaged_nrstorages = 0;

static SVCXPRT *storaged_svc = 0;

extern void storage_program_1(struct svc_req *rqstp, SVCXPRT *ctl_svc);

static SVCXPRT *storaged_monitoring_svc = 0;

extern void monitor_program_1(struct svc_req *rqstp, SVCXPRT *ctl_svc);

static SVCXPRT *storaged_profile_svc = 0;

extern void storaged_profile_program_1(struct svc_req *rqstp, SVCXPRT *ctl_svc);

uint32_t storaged_storage_ports[STORAGE_NODE_PORTS_MAX] = {0};

uint8_t storaged_nb_io_processes = 0;

DEFINE_PROFILING(spp_profiler_t) = {0};

// Rebuild storage variables

/* Need to start rebuild storage process */
uint8_t rbs_start_process = 0;
/* Export hostname */
static char rbs_export_hostname[ROZOFS_HOSTNAME_MAX];
/* Time in seconds between two attemps of rebuild */
#define TIME_BETWEEN_2_RB_ATTEMPS 30
/* First port to use for monitoring rebuild process */
uint16_t start_profil_port_for_rbs = 30000;

static int storaged_initialize() {
    int status = -1;
    list_t *p = NULL;
    DEBUG_FUNCTION;

    /* Initialize rozofs constants (redundancy) */
    rozofs_layout_initialize();

    storaged_nrstorages = 0;

    storaged_nb_io_processes = storaged_config.sproto_svc_nb;

    memcpy(storaged_storage_ports, storaged_config.ports,
            STORAGE_NODE_PORTS_MAX * sizeof (uint32_t));

    /* For each storage on configuration file */
    list_for_each_forward(p, &storaged_config.storages) {
        storage_config_t *sc = list_entry(p, storage_config_t, list);
        /* Initialize the storage */
        if (storage_initialize(storaged_storages + storaged_nrstorages++,
                sc->cid, sc->sid, sc->root) != 0) {
            severe("can't initialize storage (cid:%d : sid:%d) with path %s",
                    sc->cid, sc->sid, sc->root);
            goto out;
        }
    }

    status = 0;
out:
    return status;
}

static SVCXPRT *storaged_create_rpc_service(int port, char *host) {
    int sock;
    int one = 1;
    struct sockaddr_in sin;
    struct hostent *hp;

    if (host != NULL) {
        // get the IP address of the storage node
        if ((hp = gethostbyname(host)) == 0) {
            severe("gethostbyname failed for host : %s, %s", host,
                    strerror(errno));
            return NULL;
        }
        bcopy((char *) hp->h_addr, (char *) &sin.sin_addr, hp->h_length);
    } else {
        sin.sin_addr.s_addr = INADDR_ANY;
    }
    /* Give the socket a name. */
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);

    /* Create the socket. */
    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        severe("Can't create socket: %s.", strerror(errno));
        return NULL;
    }

    /* Set socket options */
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *) &one, sizeof (int));
    //setsockopt(sock, SOL_TCP, TCP_DEFER_ACCEPT, (char *) &one, sizeof (int));
    setsockopt(sock, SOL_TCP, TCP_NODELAY, (char *) &one, sizeof (int));

    /* Bind the socket */
    if (bind(sock, (struct sockaddr *) &sin, sizeof (struct sockaddr)) < 0) {
        severe("Couldn't bind to tcp port %d", port);
        return NULL;
    }

    /* Creates a TCP/IP-based RPC service transport */
    return svctcp_create(sock, ROZOFS_RPC_BUFFER_SIZE, ROZOFS_RPC_BUFFER_SIZE);

}

/** Check each storage to rebuild
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
static int rbs_check() {
    list_t *p = NULL;
    int status = -1;
    DEBUG_FUNCTION;

    // For each storage present on configuration file

    list_for_each_forward(p, &storaged_config.storages) {
        storage_config_t *sc = list_entry(p, storage_config_t, list);

        // Sanity check for rebuild this storage
        if (rbs_sanity_check(rbs_export_hostname, sc->cid, sc->sid,
                sc->root) != 0)
            goto out;
    }
    status = 0;
out:
    return status;
}

/** Structure used to store configuration for each storage to rebuild */
typedef struct rbs_stor_config {
    char export_hostname[ROZOFS_HOSTNAME_MAX]; ///< export hostname or IP.
    cid_t cid; //< unique id of cluster that owns this storage.
    sid_t sid; ///< unique id of this storage for one cluster.
    char root[PATH_MAX]; ///< absolute path.
} rbs_stor_config_t;

/** Starts a thread for rebuild a given storage 
 *
 * @param v: configuration of storage to rebuild.
 */
static void *rebuild_storage_thread(void *v) {

    DEBUG_FUNCTION;

    // Get the storage configuration
    rbs_stor_config_t *stor_conf = (rbs_stor_config_t*) v;

    info("Start rebuild process (pid=%d) for storage (cid=%u;sid=%u).",
            getpid(), stor_conf->cid, stor_conf->sid);

    // Try to rebuild the storage until it's over
    while (rbs_rebuild_storage(stor_conf->export_hostname, stor_conf->cid,
            stor_conf->sid, stor_conf->root) != 0) {
        // Probably a problem when connecting with other members
        // of this cluster
        severe("can't rebuild storage (cid:%u;sid:%u) with path %s,"
                " next attempt in %d seconds",
                stor_conf->cid, stor_conf->sid, stor_conf->root,
                TIME_BETWEEN_2_RB_ATTEMPS);

        sleep(TIME_BETWEEN_2_RB_ATTEMPS);
    }

    // Here the rebuild process is finish, so exit
    info("The rebuild process for storage (cid=%u;sid=%u) was completed"
            " successfully.", stor_conf->cid, stor_conf->sid);
    exit(EXIT_SUCCESS);
}

/** Start one rebuild process for each storage to rebuild
 */
static void rbs_process_initialize() {
    list_t *p = NULL;
    int i = 0;
    uint16_t profil_rbs_port = 0;

    DEBUG_FUNCTION;

    // For each storage on configuration file

    list_for_each_forward(p, &storaged_config.storages) {

        storage_config_t *sc = list_entry(p, storage_config_t, list);
        int pid = -1;
        profil_rbs_port = start_profil_port_for_rbs + i;

        // Create child process
        if (!(pid = fork())) {

            // Here it's the child process
            pthread_t thread;
            rbs_stor_config_t stor_conf;

            // Copy the configuration for the storage to rebuild
            strncpy(stor_conf.export_hostname, rbs_export_hostname,
                    ROZOFS_HOSTNAME_MAX);
            stor_conf.cid = sc->cid;
            stor_conf.sid = sc->sid;
            strncpy(stor_conf.root, sc->root, PATH_MAX);

            // Create pthread for start the rebuild task
            if ((errno = pthread_create(&thread, NULL, rebuild_storage_thread,
                    &stor_conf)) != 0) {
                severe("can't create thread for rebuild storage (cid=%u;sid=%u)"
                        ": %s",
                        sc->cid, sc->sid, strerror(errno));
            }

            // Lauch RPC service for monitor this process.
            // Associates STORAGED_PROFILE_PROGRAM, STORAGED_PROFILE_VERSION
            // with their service dispatch procedure.
            // Here protocol is zero, the service is not registered with
            // the portmap service

            if ((storaged_profile_svc =
                    storaged_create_rpc_service(profil_rbs_port,
                    storaged_hostname)) == NULL) {
                fatal("can't create rebuild monitoring service on port: %d",
                        profil_rbs_port);
            }

            if (!svc_register(storaged_profile_svc, STORAGED_PROFILE_PROGRAM,
                    STORAGED_PROFILE_VERSION, storaged_profile_program_1, 0)) {
                fatal("can't register service : %s", strerror(errno));
            }

            info("create rebuild monitoring service on port: %d",
                    profil_rbs_port);

            // Waits for RPC requests to arrive!
            svc_run();
            // NOT REACHED
        } else {
            // Set monitoring values just for the master process
            SET_PROBE_VALUE(rb_process_ports[i], profil_rbs_port);
            SET_PROBE_VALUE(rbs_cids[i], sc->cid);
            SET_PROBE_VALUE(rbs_sids[i], sc->sid);
            i++;
        }
    }
}

static void storaged_release() {
    DEBUG_FUNCTION;
    int i;
    list_t *p, *q;

    for (i = 0; i < storaged_nrstorages; i++) {
        storage_release(&storaged_storages[i]);
    }
    storaged_nrstorages = 0;

    // Free config

    list_for_each_forward_safe(p, q, &storaged_config.storages) {

        storage_config_t *s = list_entry(p, storage_config_t, list);
        free(s);
    }
}

storage_t *storaged_lookup(cid_t cid, sid_t sid) {
    storage_t *st = 0;
    DEBUG_FUNCTION;

    st = storaged_storages;
    do {
        if ((st->cid == cid) && (st->sid == sid))
            goto out;
    } while (st++ != storaged_storages + storaged_nrstorages);
    errno = EINVAL;
    st = 0;
out:
    return st;
}

static void on_stop() {
    DEBUG_FUNCTION;

    svc_exit();

    if (storaged_monitoring_svc) {
        svc_unregister(MONITOR_PROGRAM, MONITOR_VERSION);
        pmap_unset(MONITOR_PROGRAM, MONITOR_VERSION);
        svc_destroy(storaged_monitoring_svc);
        storaged_monitoring_svc = NULL;
    }

    if (storaged_profile_svc) {
        svc_unregister(STORAGED_PROFILE_PROGRAM, STORAGED_PROFILE_VERSION);
        pmap_unset(STORAGED_PROFILE_PROGRAM, STORAGED_PROFILE_VERSION);
        svc_destroy(storaged_profile_svc);
        storaged_profile_svc = NULL;
    }

    if (storaged_svc && !storaged_monitoring_svc) {
        svc_unregister(STORAGE_PROGRAM, STORAGE_VERSION);
        pmap_unset(STORAGE_PROGRAM, STORAGE_VERSION);
        svc_destroy(storaged_svc);
        storaged_svc = NULL;
    }

    rozofs_layout_release();

    storaged_release();

    info("stopped.");
    closelog();
}

static void on_start() {
    int i = 0;
    int sock;
    int one = 1;

    DEBUG_FUNCTION;

    // Initialization of the storage configuration
    if (storaged_initialize() != 0) {
        fatal("can't initialize storaged: %s.", strerror(errno));
        return;
    }

    // Start rebuild storage process(es) if necessary
    if (rbs_start_process == 1) {
        rbs_process_initialize();
        SET_PROBE_VALUE(nb_rb_processes, list_size(&storaged_config.storages));
    } else {
        SET_PROBE_VALUE(nb_rb_processes, 0);
    }

    SET_PROBE_VALUE(uptime, time(0));
    strncpy((char*) gprofiler.vers, VERSION, 20);
    SET_PROBE_VALUE(nb_io_processes, storaged_nb_io_processes);

    // Create io process(es)
    for (i = 0; i < storaged_nb_io_processes; i++) {
        int pid;

        // Create child process
        if (!(pid = fork())) {

            // Associates STORAGE_PROGRAM, STORAGE_VERSION and
            // STORAGED_PROFILE_PROGRAM, STORAGED_PROFILE_VERSION
            // with their service dispatch procedure.
            // Here protocol is zero, the service is not registered with
            // the portmap service

            if ((storaged_svc = storaged_create_rpc_service
                    (storaged_storage_ports[i], storaged_hostname)) == NULL) {
                    fatal("can't create IO storaged service on port: %d",
                            storaged_storage_ports[i]);
                }

            if (!svc_register(storaged_svc, STORAGE_PROGRAM, STORAGE_VERSION,
                    storage_program_1, 0)) {
                fatal("can't register IO service : %s", strerror(errno));
            }

            if ((storaged_profile_svc = storaged_create_rpc_service
                    (storaged_storage_ports[i] + 1000,
                    storaged_hostname)) == NULL) {
                fatal("can't create IO monitoring service on port: %d",
                        storaged_storage_ports[i] + 1000);
            }

            if (!svc_register(storaged_profile_svc, STORAGED_PROFILE_PROGRAM,
                    STORAGED_PROFILE_VERSION, storaged_profile_program_1, 0)) {
                fatal("can't register service : %s", strerror(errno));
            }

            // Waits for RPC requests to arrive!
            info("running io service (pid=%d, port=%d).",
                    getpid(), storaged_storage_ports[i]);
            svc_run();
            // NOT REACHED
        } else {
            // Set monitoring values just for the master process
            SET_PROBE_VALUE(io_process_ports[i],
                    (uint16_t) storaged_storage_ports[i] + 1000);
        }
    }

    // Create internal monitoring service
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *) &one, sizeof (int));
    setsockopt(sock, SOL_TCP, TCP_DEFER_ACCEPT, (char *) &one, sizeof (int));
    setsockopt(sock, SOL_TCP, TCP_NODELAY, (char *) &one, sizeof (int));

    if ((storaged_monitoring_svc = svctcp_create(sock,
            ROZOFS_RPC_BUFFER_SIZE, ROZOFS_RPC_BUFFER_SIZE)) == NULL) {
        fatal("can't create internal monitoring service.");
        return;
    }

    // Destroy portmap mapping
    pmap_unset(MONITOR_PROGRAM, MONITOR_VERSION); // in case !

    // Associates MONITOR_PROGRAM and MONITOR_VERSION
    // with the service dispatch procedure, monitor_program_1.
    // Here protocol is no zero, the service is registered with
    // the portmap service

    if (!svc_register(storaged_monitoring_svc, MONITOR_PROGRAM,
            MONITOR_VERSION, monitor_program_1, IPPROTO_TCP)) {
        fatal("can't register service : %s", strerror(errno));
        return;
    }

    // Create profiling service for main process
    if ((storaged_profile_svc = svctcp_create(RPC_ANYSOCK, 0, 0)) == NULL) {
        severe("can't create profiling service.");
    }

    pmap_unset(STORAGED_PROFILE_PROGRAM, STORAGED_PROFILE_VERSION); // in case !

    if (!svc_register(storaged_profile_svc, STORAGED_PROFILE_PROGRAM,
            STORAGED_PROFILE_VERSION,
            storaged_profile_program_1, IPPROTO_TCP)) {
        severe("can't register service : %s", strerror(errno));
    }

    // Waits for RPC requests to arrive!
    info("running.");
    svc_run();
    // NOT REACHED
}

void usage() {

    printf("Rozofs storage daemon - %s\n", VERSION);
    printf("Usage: storaged [OPTIONS]\n\n");
    printf("\t-h, --help\tprint this message.\n");
    printf("\t-H,--host storaged hostname\t\tused to build to the pid name (default: none) \n");
    printf("\t-c, --config\tconfig file to use (default: %s).\n",
            STORAGED_DEFAULT_CONFIG);
    printf("\t-r, --rebuild\t export hostname.\n");
}

int main(int argc, char *argv[]) {
    int c;
    char pid_name[256];
    static struct option long_options[] = {
        { "help", no_argument, 0, 'h'},
        {"config", required_argument, 0, 'c'},
        {"rebuild", required_argument, 0, 'r'},
        { "host", required_argument, 0, 'H'},
        { 0, 0, 0, 0}
    };

    storaged_hostname = NULL;

    while (1) {

        int option_index = 0;
        c = getopt_long(argc, argv, "hc:r:H:", long_options, &option_index);

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
            case 'r':
                if (strncpy(rbs_export_hostname, optarg, ROZOFS_HOSTNAME_MAX)
                        == NULL) {
                    fprintf(stderr, "storaged failed: %s %s\n", optarg,
                            strerror(errno));
                    exit(EXIT_FAILURE);
                }
                rbs_start_process = 1;
                break;
            case 'H':
                storaged_hostname = strdup(optarg);
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
    // Initialize the list of storage config
    if (sconfig_initialize(&storaged_config) != 0) {
        fprintf(stderr, "Can't initialize storaged config: %s.\n",
                strerror(errno));
        goto error;
    }
    // Read the configuration file
    if (sconfig_read(&storaged_config, storaged_config_file) != 0) {
        fprintf(stderr, "Failed to parse storage configuration file: %s.\n",
                strerror(errno));
        goto error;
    }
    // Check the configuration
    if (sconfig_validate(&storaged_config) != 0) {
        fprintf(stderr, "Inconsistent storage configuration file: %s.\n",
                strerror(errno));
        goto error;
    }
    // Check rebuild storage configuration if necessary
    if (rbs_start_process == 1) {
        if (rbs_check() != 0)
            goto error;
    }
    openlog("storaged", LOG_PID, LOG_DAEMON);

    char *pid_name_p = pid_name;
    if (storaged_hostname != NULL) {
        pid_name_p += sprintf(pid_name_p, "%s_", storaged_hostname);
    }
    sprintf(pid_name_p, "%s", STORAGED_PID_FILE);
    daemon_start(pid_name, on_start, on_stop, NULL);

    exit(0);
error:
    fprintf(stderr, "Can't start storaged. See logs for more details.\n");
    exit(EXIT_FAILURE);
}
