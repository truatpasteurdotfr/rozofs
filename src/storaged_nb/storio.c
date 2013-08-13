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
#include <rozofs/rozofs_timer_conf.h>
#include "storaged_nblock_init.h"

#define STORAGED_PID_FILE "storaged"
int     storio_instance = 0;
static char storaged_config_file[PATH_MAX] = STORAGED_DEFAULT_CONFIG;

static sconfig_t storaged_config;

static storage_t storaged_storages[STORAGES_MAX_BY_STORAGE_NODE] = {
    {0}
};

static char *storaged_hostname = NULL;

static uint16_t storaged_nrstorages = 0;

static SVCXPRT *storaged_svc = 0;

extern void storage_program_1(struct svc_req *rqstp, SVCXPRT *ctl_svc);

static SVCXPRT *storaged_profile_svc = 0;

extern void storaged_profile_program_1(struct svc_req *rqstp, SVCXPRT *ctl_svc);

uint32_t storaged_storage_ports[STORAGE_NODE_PORTS_MAX] = {0};

uint8_t storaged_nb_io_processes = 0;

DEFINE_PROFILING(spp_profiler_t) = {0};

    uint32_t ipadd_hostformat ; /**< storio IP address in host format */
    uint16_t  port_hostformat ;/**< storio port in host format */

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

static SVCXPRT *storaged_create_rpc_service(int port, char *host,uint32_t instance) {
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
        uint32_t ip_addr = ntohl(sin.sin_addr.s_addr);
        ip_addr += ((instance + 1) * 0x100);
        sin.sin_addr.s_addr = htonl(ip_addr);                
    } else {
        sin.sin_addr.s_addr = INADDR_ANY;
    }
    /* Give the socket a name. */
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    ipadd_hostformat = ntohl(sin.sin_addr.s_addr);
    port_hostformat = ntohs(sin.sin_port);
#if 0
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
        severe("Couldn't bind to tcp port %d: %s", port, strerror(errno));
        return NULL;
    }

    /* Creates a TCP/IP-based RPC service transport */
    return svctcp_create(sock, ROZOFS_RPC_STORAGE_BUFFER_SIZE, ROZOFS_RPC_STORAGE_BUFFER_SIZE);
#endif
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

char storage_process_filename[NAME_MAX];

/**
 *  Signal catching
 */

static void storaged_handle_signal(int sig) {
    unlink(storage_process_filename);
    closelog();
    signal(sig, SIG_DFL);
    raise(sig);
}

static void on_start(uint16_t port_io) {
    pthread_t thread;
    storaged_start_conf_param_t conf;

    DEBUG_FUNCTION;

    signal(SIGCHLD, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGILL, storaged_handle_signal);
    signal(SIGSTOP, storaged_handle_signal);
    signal(SIGABRT, storaged_handle_signal);
    signal(SIGSEGV, storaged_handle_signal);
    signal(SIGKILL, storaged_handle_signal);
    signal(SIGTERM, storaged_handle_signal);
    signal(SIGQUIT, storaged_handle_signal);


    /*
    ** Save the process PID un PID directory 
    */

    storage_process_filename[0] = 0;
    char *pid_name_p = storage_process_filename;
    if (storaged_hostname != NULL) {
        sprintf(pid_name_p, "%s%s_%s:%d.pid", DAEMON_PID_DIRECTORY, STORAGED_PID_FILE, storaged_hostname, port_io);
    } else {
        sprintf(pid_name_p, "%s%s:%d.pid", DAEMON_PID_DIRECTORY, STORAGED_PID_FILE, port_io);
    }
    int ppfd;
    if ((ppfd = open(storage_process_filename, O_RDWR | O_CREAT, 0640)) < 0) {
        severe("can't open process file");
    } else {
        char str[10];
        sprintf(str, "%d\n", getpid());
        write(ppfd, str, strlen(str));
        close(ppfd);
    }

    /**
     * start the non blocking thread
     */
    
    conf.instance_id = storio_instance;
    conf.io_port     = port_io;
    conf.debug_port  = rzdbg_default_base_port + RZDBG_STORAGED_PORT + storio_instance;
    if (storaged_hostname != NULL) 
      strcpy(conf.hostname, storaged_hostname);
    else 
      conf.hostname[0] = 0;
      
      storaged_start_nb_blocking_th(&conf);
      
#if 0 // FDL
    if ((errno = pthread_create(&thread, NULL, (void*) storaged_start_nb_blocking_th, &conf)) != 0) {
        fatal("can't create non blocking thread: %s", strerror(errno));
    }
    
    
    // Associates STORAGE_PROGRAM, STORAGE_VERSION and
    // STORAGED_PROFILE_PROGRAM, STORAGED_PROFILE_VERSION
    // with their service dispatch procedure.
    // Here protocol is zero, the service is not registered with
    // the portmap service

    if ((storaged_svc = storaged_create_rpc_service(port_io, storaged_hostname,(storio_instance-1))) == NULL) {
        fatal("can't create IO storaged service on port: %d",port_io);
    }

    if (!svc_register(storaged_svc, STORAGE_PROGRAM, STORAGE_VERSION,storage_program_1, 0)) {
        fatal("can't register IO service : %s", strerror(errno));
    }

    if ((storaged_profile_svc = storaged_create_rpc_service(port_io + 1000,storaged_hostname,0)) == NULL) {
        severe("can't create IO profile service on port: %d",port_io + 1000);
    }

    if (!svc_register(storaged_profile_svc, STORAGED_PROFILE_PROGRAM, STORAGED_PROFILE_VERSION, storaged_profile_program_1, 0)) {
        fatal("can't register service : %s", strerror(errno));
    }

    // Waits for RPC requests to arrive!
    info("running io service (pid=%d, port=%d).",getpid(), port_io);
    svc_run();
    // NOT REACHED
#endif
    info("running io service (pid=%d, port=%d).",getpid(), port_io);

}


void usage() {

    printf("Rozofs storage daemon - %s\n", VERSION);
    printf("Usage: storaged [OPTIONS]\n\n");
    printf("\t-h, --help\tprint this message.\n");
    printf("\t-i,--instance instance number\t\tstorage io instance number \n");
    printf("\t-H,--host storaged hostname\t\tused to build to the pid name (default: none) \n");
    printf("\t-c, --config\tconfig file to use (default: %s).\n",
            STORAGED_DEFAULT_CONFIG);
}

int main(int argc, char *argv[]) {
    int c;
    int val;
    static struct option long_options[] = {
        { "help", no_argument, 0, 'h'},
        {"config", required_argument, 0, 'c'},
        { "host", required_argument, 0, 'H'},
        { "instance", required_argument, 0, 'i'},
        { 0, 0, 0, 0}
    };

    /*
     ** init of the timer configuration
     */
    rozofs_tmr_init_configuration();

    storaged_hostname = NULL;

    while (1) {

        int option_index = 0;
        c = getopt_long(argc, argv, "hc:r:H:i:", long_options, &option_index);

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
            case 'H':
                storaged_hostname = strdup(optarg);
                break;
	    case 'i':	
                errno = 0;
                val = (int) strtol(optarg, (char **) NULL, 10);
                if (errno != 0) {
                    strerror(errno);
                    usage();
                    exit(EXIT_FAILURE);
                }
                storio_instance = val;
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
    
    char name[32];
    sprintf(name,"storio%d",storio_instance);
    openlog(name, LOG_PID, LOG_DAEMON);
        
    // Initialize the list of storage config
    if (sconfig_initialize(&storaged_config) != 0) {
        fatal( "Can't initialize storaged config: %s.\n",strerror(errno));
    }
    // Read the configuration file
    if (sconfig_read(&storaged_config, storaged_config_file) != 0) {
        fatal("Failed to parse storage configuration file: %s.\n",strerror(errno));
    }
    // Check the configuration
    if (sconfig_validate(&storaged_config) != 0) {
        fatal( "Inconsistent storage configuration file: %s.\n",strerror(errno));
    }
    // Initialization of the storage configuration
    if (storaged_initialize() != 0) {
        fatal("can't initialize storaged: %s.", strerror(errno));
    }

    SET_PROBE_VALUE(uptime, time(0));
    strncpy((char*) gprofiler.vers, VERSION, 20);
    SET_PROBE_VALUE(nb_io_processes, storaged_nb_io_processes);
    
    
    on_start(storaged_storage_ports[storio_instance-1]);
    return 0;
}
