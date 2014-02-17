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

#include <rozofs/rozofs_srv.h>
#include <rozofs/rozofs_debug_ports.h>
#include <rozofs/common/log.h>
#include <rozofs/common/xmalloc.h>
#include <rozofs/common/list.h>
#include <rozofs/common/daemon.h>
#include <rozofs/common/profile.h>
#include <rozofs/rpc/mproto.h>
#include <rozofs/rpc/sproto.h>
#include <rozofs/rpc/spproto.h>
#include <rozofs/core/rozofs_core_files.h>
#include <rozofs/core/rozofs_ip_utilities.h>
#include <rozofs/rozofs_timer_conf.h>

#include "config.h"
#include "sconfig.h"
#include "storage.h"
#include "storaged.h"
#include "sconfig.h"
#include "storaged_nblock_init.h"

#define STORAGED_PID_FILE "storaged"

static char storaged_config_file[PATH_MAX] = STORAGED_DEFAULT_CONFIG;

sconfig_t storaged_config;

static storage_t storaged_storages[STORAGES_MAX_BY_STORAGE_NODE] = { { 0 } };

static char *storaged_hostname = NULL;

static uint16_t storaged_nrstorages = 0;

static SVCXPRT *storaged_monitoring_svc = 0;

static SVCXPRT *storaged_profile_svc = 0;

uint8_t storio_nb_threads = 0;
uint8_t storaged_nb_ports = 0;
uint8_t storaged_nb_io_processes = 0;

DEFINE_PROFILING(spp_profiler_t) = {0};


static int storaged_initialize() {
    int status = -1;
    list_t *p = NULL;
    DEBUG_FUNCTION;

    /* Initialize rozofs constants (redundancy) */
    rozofs_layout_initialize();

    storaged_nrstorages = 0;

    storaged_nb_io_processes = 1;
    
    storio_nb_threads = storaged_config.nb_disk_threads;

    storaged_nb_ports = storaged_config.io_addr_nb;

    /* For each storage on configuration file */
    list_for_each_forward(p, &storaged_config.storages) {
        storage_config_t *sc = list_entry(p, storage_config_t, list);
        /* Initialize the storage */
        if (storage_initialize(storaged_storages + storaged_nrstorages++,
                sc->cid, sc->sid, sc->root,
		storaged_config.device.total,
		storaged_config.device.mapper,
		storaged_config.device.redundancy) != 0) {
            severe("can't initialize storage (cid:%d : sid:%d) with path %s",
                    sc->cid, sc->sid, sc->root);
            goto out;
        }
    }

    status = 0;
out:
    return status;
}
/*
**____________________________________________________
*/
/*
  Allocate a device for a file
  
   @param st: storage context
*/
uint32_t storio_device_mapping_allocate_device(storage_t * st) {
  severe("storaged should not call storio_device_mapping_allocate_device");
  return -1;
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
storage_t *storaged_next(storage_t * st) {
    DEBUG_FUNCTION;

    if (storaged_nrstorages == 0) return NULL;
    if (st == NULL) return storaged_storages;

    st++;
    if (st < storaged_storages + storaged_nrstorages) return st;
    return NULL;
}
static void on_stop() {
    DEBUG_FUNCTION;
    char cmd[128];
   
    /*
    ** Kill every instance of storio of this host
    */
    if (storaged_hostname) 
      sprintf(cmd,"storio_killer.sh -H %s", storaged_hostname);
    else 
      sprintf(cmd,"storio_killer.sh"); 
    system(cmd);
    
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

    rozofs_layout_release();

    storaged_release();

    info("stopped.");
    closelog();
}

char storage_process_filename[NAME_MAX];

static void on_start() {
    char cmd[128];
    char * p;
        
    DEBUG_FUNCTION;

    storage_process_filename[0] = 0;

    // Initialization of the storage configuration
    if (storaged_initialize() != 0) {
        fatal("can't initialize storaged: %s.", strerror(errno));
        return;
    }

    SET_PROBE_VALUE(nb_rb_processes, 0);

    SET_PROBE_VALUE(uptime, time(0));
    strncpy((char*) gprofiler.vers, VERSION, 20);
    SET_PROBE_VALUE(nb_io_processes, storio_nb_threads);
    
    // Create storio process(es)

    // Set monitoring values just for the master process
    //SET_PROBE_VALUE(io_process_ports[i],(uint16_t) storaged_storage_ports[i] + 1000);
    
    /*
    ** 1rst kill storio in case it is already running
    */
    p = cmd;
    if (storaged_hostname) 
      sprintf(cmd,"storio_killer.sh -H %s", storaged_hostname);
    else 
      sprintf(cmd,"storio_killer.sh"); 

    // Launch killer script
    system(cmd);

    /*
    ** Then start storio
    */
    p = cmd;
    p += sprintf(p, "storio_starter.sh storio -i 1 -c %s ", storaged_config_file);
    if (storaged_hostname) p += sprintf (p, "-H %s", storaged_hostname);
    p += sprintf(p, "&");

    // Launch storio_starter script
    system(cmd);

    // Create the debug thread of the parent
    storaged_start_conf_param_t conf;     
    conf.instance_id = 0;
    conf.io_port     = 0;
    conf.debug_port  = rzdbg_default_base_port + RZDBG_STORAGED_PORT;
    /* Try to get debug port from /etc/services */    
    conf.debug_port = get_service_port("rozo_storaged_dbg",NULL,conf.debug_port);

    if (storaged_hostname != NULL) strcpy(conf.hostname, storaged_hostname);
    else conf.hostname[0] = 0;
    
    storaged_start_nb_th(&conf);
}

void usage() {

    printf("RozoFS storage daemon - %s\n", VERSION);
    printf("Usage: storaged [OPTIONS]\n\n");
    printf("   -h, --help\t\t\tprint this message.\n");
    printf("   -H, --host=storaged-host\tspecify the hostname to use for build pid name (default: none).\n");
    printf("   -c, --config=config-file\tspecify config file to use (default: %s).\n",
            STORAGED_DEFAULT_CONFIG);
}

int main(int argc, char *argv[]) {
    int c;
    char pid_name[256];
    static struct option long_options[] = {
        { "help", no_argument, 0, 'h'},
        { "config", required_argument, 0, 'c'},
        { "host", required_argument, 0, 'H'},
        { 0, 0, 0, 0}
    };

    // Init of the timer configuration
    rozofs_tmr_init_configuration();

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
    openlog("storaged", LOG_PID, LOG_DAEMON);

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

    char *pid_name_p = pid_name;
    if (storaged_hostname != NULL) {
        sprintf(pid_name_p, "%s_%s.pid", STORAGED_PID_FILE, storaged_hostname);
    } else {
        sprintf(pid_name_p, "%s.pid", STORAGED_PID_FILE);
    }

    daemon_start("storaged", storaged_config.nb_cores, pid_name, on_start,
            on_stop, NULL);

    exit(0);
error:
    fprintf(stderr, "Can't start storaged. See logs for more details.\n");
    exit(EXIT_FAILURE);
}
