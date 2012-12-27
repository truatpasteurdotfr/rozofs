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
#include <time.h>
#include <sys/stat.h>
#include <netinet/tcp.h>
#include <getopt.h>
#include <libconfig.h>
#include <limits.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>
#include <rpc/pmap_clnt.h>

#include <rozofs/rozofs.h>
#include <rozofs/common/log.h>
#include <rozofs/common/daemon.h>
#include <rozofs/common/xmalloc.h>
#include <rozofs/common/profile.h>
#include <rozofs/rpc/eproto.h>
#include <rozofs/rpc/epproto.h>

#include "config.h"
#include "exportd.h"
#include "export.h"
#include "monitor.h"
#include "econfig.h"
#include "volume.h"

#define EXPORTD_PID_FILE "exportd.pid"
/* Maximum open file descriptor number for exportd daemon */
#define EXPORTD_MAX_OPEN_FILES 5000

econfig_t exportd_config;
pthread_rwlock_t config_lock;

lv2_cache_t cache;

typedef struct export_entry {
    export_t export;
    list_t list;
} export_entry_t;

static list_t exports;
static pthread_rwlock_t exports_lock;

typedef struct volume_entry {
    volume_t volume;
    list_t list;
} volume_entry_t;

static list_t volumes;
static pthread_rwlock_t volumes_lock;

static pthread_t bal_vol_thread;
static pthread_t rm_bins_thread;
static pthread_t monitor_thread;

static char exportd_config_file[PATH_MAX] = EXPORTD_DEFAULT_CONFIG;

static SVCXPRT *exportd_svc = NULL;

extern void export_program_1(struct svc_req *rqstp, SVCXPRT * ctl_svc);

static SVCXPRT *exportd_profile_svc = 0;

extern void exportd_profile_program_1(struct svc_req *rqstp, SVCXPRT *ctl_svc);

DEFINE_PROFILING(epp_profiler_t) = {0};

static void *balance_volume_thread(void *v) {
    struct timespec ts = {8, 0};

    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

    for (;;) {
        list_t *p;

        if ((errno = pthread_rwlock_tryrdlock(&volumes_lock)) != 0) {
            warning("can lock volumes, balance_volume_thread deferred.");
            continue;
        }

        list_for_each_forward(p, &volumes) {
            volume_balance(&list_entry(p, volume_entry_t, list)->volume);
        }

        if ((errno = pthread_rwlock_unlock(&volumes_lock)) != 0) {
            severe("can unlock volumes, potential dead lock.");
        }

        nanosleep(&ts, NULL);
    }
    return 0;
}

int exports_remove_bins() {
    int status = -1;
    list_t *iterator;
    DEBUG_FUNCTION;

    list_for_each_forward(iterator, &exports) {
        export_entry_t *entry = list_entry(iterator, export_entry_t, list);
        if (export_rm_bins(&entry->export) != 0) {
            goto out;
        }
    }
    status = 0;
out:
    return status;
}

static void *remove_bins_thread(void *v) {
    struct timespec ts = {20, 0};

    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

    for (;;) {
        if (exports_remove_bins() != 0) {
            warning("remove_bins_thread failed: %s", strerror(errno));
        }
        nanosleep(&ts, NULL);
    }
    return 0;
}

static void *monitoring_thread(void *v) {
    struct timespec ts = {2, 0};
    list_t *p;

    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

    for (;;) {
        if ((errno = pthread_rwlock_tryrdlock(&volumes_lock)) != 0) {
            warning("can't lock volumes, monitoring_thread deferred.");
            continue;
        }

        gprofiler.nb_volumes = 0;
        list_for_each_forward(p, &volumes) {
            if (monitor_volume(&list_entry(p, volume_entry_t, list)->volume) != 0) {
                severe("monitor thread failed: %s", strerror(errno));
            }
            gprofiler.nb_volumes++;
        }

        if ((errno = pthread_rwlock_unlock(&volumes_lock)) != 0) {
            severe("can't unlock volumes, potential dead lock.");
            continue;
        }

        if ((errno = pthread_rwlock_tryrdlock(&exports_lock)) != 0) {
            warning("can't lock exports, monitoring_thread deferred.");
            continue;
        }

        gprofiler.nb_exports = 0;
        list_for_each_forward(p, &exports) {
            if (monitor_export(&list_entry(p, export_entry_t, list)->export) != 0) {
                severe("monitor thread failed: %s", strerror(errno));
            }
            gprofiler.nb_exports++;
        }

        if ((errno = pthread_rwlock_unlock(&exports_lock)) != 0) {
            severe("can't unlock exports, potential dead lock.");
            continue;
        }

        nanosleep(&ts, NULL);
    }
    return 0;
}

eid_t *exports_lookup_id(ep_path_t path) {
    list_t *iterator;
    char export_path[PATH_MAX];
    DEBUG_FUNCTION;

    if (!realpath(path, export_path)) {
        return NULL;
    }

    if ((errno = pthread_rwlock_rdlock(&exports_lock)) != 0) {
        severe("can lock exports.");
        return NULL;
    }

    list_for_each_forward(iterator, &exports) {
        export_entry_t *entry = list_entry(iterator, export_entry_t, list);
        if (strcmp(entry->export.root, export_path) == 0) {
            if ((errno = pthread_rwlock_unlock(&exports_lock)) != 0) {
                severe("can unlock exports, potential dead lock.");
                return NULL;
            }
            return &entry->export.eid;
        }
    }

    if ((errno = pthread_rwlock_unlock(&exports_lock)) != 0) {
        severe("can unlock exports, potential dead lock.");
        return NULL;
    }
    errno = EINVAL;
    return NULL;
}

export_t *exports_lookup_export(eid_t eid) {
    list_t *iterator;
    DEBUG_FUNCTION;

    if ((errno = pthread_rwlock_rdlock(&exports_lock)) != 0) {
        severe("can lock exports.");
        return NULL;
    }

    list_for_each_forward(iterator, &exports) {
        export_entry_t *entry = list_entry(iterator, export_entry_t, list);
        if (eid == entry->export.eid) {
            if ((errno = pthread_rwlock_unlock(&exports_lock)) != 0) {
                severe("can unlock exports, potential dead lock.");
                return NULL;
            }
            return &entry->export;
        }
    }

    if ((errno = pthread_rwlock_unlock(&exports_lock)) != 0) {
        severe("can unlock exports, potential dead lock.");
        return NULL;
    }
    errno = EINVAL;
    return NULL;
}

volume_t *volumes_lookup_volume(vid_t vid) {
    list_t *iterator;
    DEBUG_FUNCTION;

    if ((errno = pthread_rwlock_rdlock(&volumes_lock)) != 0) {
        severe("can't lock volumes.");
        return NULL;
    }

    list_for_each_forward(iterator, &volumes) {
        volume_entry_t *entry = list_entry(iterator, volume_entry_t, list);
        if (vid == entry->volume.vid) {
            if ((errno = pthread_rwlock_unlock(&volumes_lock)) != 0) {
                severe("can't unlock volumes, potential dead lock.");
                return NULL;
            }
            return &entry->volume;
        }
    }

    if ((errno = pthread_rwlock_unlock(&volumes_lock)) != 0) {
        severe("can't unlock volumes, potential dead lock.");
        return NULL;
    }

    errno = EINVAL;
    return NULL;
}

int exports_initialize() {
    list_init(&exports);
    if (pthread_rwlock_init(&exports_lock, NULL) != 0) {
        return -1;
    }
    return 0;
}

int volumes_initialize() {
    list_init(&volumes);
    if (pthread_rwlock_init(&volumes_lock, NULL) != 0) {
        return -1;
    }
    return 0;
}

void exports_release() {
    list_t *p, *q;

    list_for_each_forward_safe(p, q, &exports) {
        export_entry_t *entry = list_entry(p, export_entry_t, list);
        export_release(&entry->export);
        list_remove(p);
        free(entry);
    }

    if ((errno = pthread_rwlock_destroy(&exports_lock)) != 0) {
        severe("can't release exports lock: %s", strerror(errno));
    }
}

void volumes_release() {
    list_t *p, *q;

    list_for_each_forward_safe(p, q, &volumes) {
        volume_entry_t *entry = list_entry(p, volume_entry_t, list);
        volume_release(&entry->volume);
        list_remove(p);
        free(entry);
    }
    if ((errno = pthread_rwlock_destroy(&volumes_lock)) != 0) {
        severe("can't release volumes lock: %s", strerror(errno));
    }
}

static int load_layout_conf() {
    int status = -1;
    DEBUG_FUNCTION;

    if (rozofs_initialize(exportd_config.layout) != 0) {
        severe("can't initialise rozofs layout: %s\n",
                strerror(errno));
        goto out;
    }
    status = 0;
out:
    return status;
}

static int load_volumes_conf() {
    list_t *p, *q, *r;
    DEBUG_FUNCTION;

    // For each volume
    list_for_each_forward(p, &exportd_config.volumes) {
        volume_config_t *vconfig = list_entry(p, volume_config_t, list);
        volume_entry_t *ventry = 0;

        // Memory allocation for this volume
        ventry = (volume_entry_t *) xmalloc(sizeof (volume_entry_t));

        // Initialize the volume
        volume_initialize(&ventry->volume, vconfig->vid);

        // For each cluster of this volume
        list_for_each_forward(q, &vconfig->clusters) {
            cluster_config_t *cconfig = list_entry(q, cluster_config_t, list);

            // Memory allocation for this cluster
            cluster_t *cluster = (cluster_t *) xmalloc(sizeof (cluster_t));
            cluster_initialize(cluster, cconfig->cid, 0, 0);

            list_for_each_forward(r, &cconfig->storages) {
                storage_node_config_t *sconfig = list_entry(r, storage_node_config_t, list);
                volume_storage_t *vs = (volume_storage_t *) xmalloc(sizeof (volume_storage_t));
                volume_storage_initialize(vs, sconfig->sid, sconfig->host);
                list_push_back(&cluster->storages, &vs->list);
            }
            // Add this cluster to the list of this volume
            list_push_back(&ventry->volume.clusters, &cluster->list);
        }
        // Add this volume to the list of volume
        list_push_back(&volumes, &ventry->list);
    }

    return 0;
}

static int load_exports_conf() {
    int status = -1;
    list_t *p;
    DEBUG_FUNCTION;

    // For each export

    list_for_each_forward(p, &exportd_config.exports) {
        export_config_t *econfig = list_entry(p, export_config_t, list);
        export_entry_t *entry = xmalloc(sizeof (export_entry_t));
        volume_t *volume;

        list_init(&entry->list);

        if (!(volume = volumes_lookup_volume(econfig->vid))) {
            severe("can't lookup volume for vid %d: %s\n",
                    econfig->vid, strerror(errno));
        }

        if (export_is_valid(econfig->root) != 0) {
            // try to create it
            if (export_create(econfig->root) != 0) {
                severe("can't create export with path %s: %s\n",
                        econfig->root, strerror(errno));
                goto out;
            }
        }

        // Initialize export
        if (export_initialize(&entry->export, volume, &cache, econfig->eid,
                econfig->root, econfig->md5, econfig->squota,
                econfig->hquota) != 0) {
            severe("can't initialize export with path %s: %s\n",
                    econfig->root, strerror(errno));
            goto out;
        }

        // Add this export to the list of exports
        list_push_back(&exports, &entry->list);
    }

    status = 0;
out:
    return status;
}

static int exportd_initialize() {
    DEBUG_FUNCTION;

    if ((errno = pthread_rwlock_init(&config_lock, NULL)) != 0)
        fatal("can't initialize lock for config: %s", strerror(errno));

    // Initialize lv2 cache
    lv2_cache_initialize(&cache);

    // Initialize list of volume(s)
    if (volumes_initialize() != 0)
        fatal("can't initialize volume: %s", strerror(errno));

    // Initialize list of exports
    if (exports_initialize() != 0)
        fatal("can't initialize exports: %s", strerror(errno));

    // Initialize monitoring
    if (monitor_initialize() != 0)
        fatal("can't initialize monitoring: %s", strerror(errno));

    // Load configuration
    if (load_layout_conf() != 0)
        fatal("can't load layout");

    if (load_volumes_conf() != 0)
        fatal("can't load volume");

    if (load_exports_conf() != 0)
        fatal("can't load exports");

    if (pthread_create(&bal_vol_thread, NULL, balance_volume_thread, NULL) !=
            0)
        fatal("can't create balancing thread %s", strerror(errno));

    if (pthread_create(&rm_bins_thread, NULL, remove_bins_thread, NULL) != 0)
        fatal("can't create remove files thread %s", strerror(errno));

    if (pthread_create(&monitor_thread, NULL, monitoring_thread, NULL) != 0)
        fatal("can't create monitoring thread %s", strerror(errno));

    return 0;
}

static void exportd_release() {

    pthread_cancel(bal_vol_thread);
    pthread_cancel(rm_bins_thread);
    pthread_cancel(monitor_thread);

    if ((errno = pthread_rwlock_destroy(&config_lock)) != 0) {
        severe("can't release config lock: %s", strerror(errno));
    }

    monitor_release();
    exports_release();
    volumes_release();
    econfig_release(&exportd_config);
    lv2_cache_release(&cache);
    rozofs_release();
}

static void on_start() {
    int sock;
    int one = 1;
    struct rlimit rls;
    DEBUG_FUNCTION;

    if (exportd_initialize() != 0) {
        fatal("can't initialize exportd.");
    }

    /*
     * Metadata service
     */
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

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

    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *) &one, sizeof (int));
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char *) &one, sizeof (int));
    setsockopt(sock, SOL_TCP, TCP_DEFER_ACCEPT, (char *) &one, sizeof (int));


    /* Change the value of the maximum file descriptor number
     * that can be opened by this process.*/

    rls.rlim_cur = EXPORTD_MAX_OPEN_FILES;
    rls.rlim_max = EXPORTD_MAX_OPEN_FILES;
    if (setrlimit(RLIMIT_NOFILE, &rls) < 0) {
        warning("Failed to change open files limit to %u", EXPORTD_MAX_OPEN_FILES);
    }

    // XXX Buffers sizes hard coded
    exportd_svc = svctcp_create(sock, ROZOFS_RPC_BUFFER_SIZE,
            ROZOFS_RPC_BUFFER_SIZE);
    if (exportd_svc == NULL) {
        fatal("can't create service %s", strerror(errno));
    }

    pmap_unset(EXPORT_PROGRAM, EXPORT_VERSION); // in case !

    if (!svc_register
            (exportd_svc, EXPORT_PROGRAM, EXPORT_VERSION, export_program_1,
            IPPROTO_TCP)) {
        fatal("can't register service %s", strerror(errno));
    }

    /*
     * Profiling service
     */
    if ((exportd_profile_svc = svctcp_create(RPC_ANYSOCK, 0, 0)) == NULL ) {
        severe("can't create profiling service.");
    }
    pmap_unset(EXPORTD_PROFILE_PROGRAM, EXPORTD_PROFILE_VERSION); // in case !

    if (!svc_register(exportd_profile_svc, EXPORTD_PROFILE_PROGRAM,
            EXPORTD_PROFILE_VERSION, exportd_profile_program_1, IPPROTO_TCP)) {
        severe("can't register service : %s", strerror(errno));
    }

    SET_PROBE_VALUE(uptime, time(0));
    strcpy((char *)gprofiler.vers, VERSION);

    info("running.");
    svc_run();
}

static void on_stop() {
    DEBUG_FUNCTION;

    svc_exit();

    svc_unregister(EXPORT_PROGRAM, EXPORT_VERSION);
    pmap_unset(EXPORT_PROGRAM, EXPORT_VERSION);
    if (exportd_svc) {
        svc_destroy(exportd_svc);
        exportd_svc = NULL;
    }

    svc_unregister(EXPORTD_PROFILE_PROGRAM, EXPORTD_PROFILE_VERSION);
    pmap_unset(EXPORTD_PROFILE_PROGRAM, EXPORTD_PROFILE_VERSION);
    if (exportd_profile_svc) {
        svc_destroy(exportd_profile_svc);
        exportd_profile_svc = NULL;
    }

    exportd_release();

    info("stopped.");
    closelog();
}

static void on_hup() {
    econfig_t new;
    list_t *p, *q;

    info("hup signal received.");
    // sanity check
    if (econfig_initialize(&new) != 0) {
        severe("can't initialize exportd config: %s.", strerror(errno));
        goto error;
    }

    if (econfig_read(&new, exportd_config_file) != 0) {
        severe("failed to parse configuration file: %s.", strerror(errno));
        goto error;
    }

    if (econfig_validate(&new) != 0) {
        severe("invalid configuration file: %s.", strerror(errno));
        goto error;
    }

    econfig_release(&new);

    // do the job
    if ((errno = pthread_rwlock_wrlock(&config_lock)) != 0) {
        severe("can't lock config: %s", strerror(errno));
        goto error;
    }

    if (econfig_read(&exportd_config, exportd_config_file) != 0) {
        severe("failed to parse configuration file: %s.", strerror(errno));
        goto error;
    }

    if ((errno = pthread_rwlock_wrlock(&volumes_lock)) != 0) {
        severe("can't lock volumes: %s", strerror(errno));
        goto error;
    }

    list_for_each_forward_safe(p, q, &volumes) {
        volume_entry_t *entry = list_entry(p, volume_entry_t, list);
        volume_release(&entry->volume);
        list_remove(p);
        free(entry);
    }

    load_volumes_conf();

    // volumes lock should be released before loading exports config
    // since load_exports_conf calls volume_lookup_volume which
    // needs to acquire volumes lock
    if ((errno = pthread_rwlock_unlock(&volumes_lock)) != 0) {
        severe("can't unlock volumes: %s", strerror(errno));
        goto error;
    }

    list_for_each_forward(p, &volumes) {
        volume_balance(&list_entry(p, volume_entry_t, list)->volume);
    }

    if ((errno = pthread_rwlock_wrlock(&exports_lock)) != 0) {
        severe("can't lock exports: %s", strerror(errno));
        goto error;
    }

    list_for_each_forward_safe(p, q, &exports) {
        export_entry_t *entry = list_entry(p, export_entry_t, list);
        export_release(&entry->export);
        list_remove(p);
        free(entry);
    }

    load_exports_conf();

    if ((errno = pthread_rwlock_unlock(&exports_lock)) != 0) {
        severe("can't unlock exports: %s", strerror(errno));
        goto error;
    }

    if ((errno = pthread_rwlock_unlock(&config_lock)) != 0) {
        severe("can't unlock config: %s", strerror(errno));
        goto error;
    }

    info("reloaded.");
    goto out;
error:
    pthread_rwlock_unlock(&exports_lock);
    pthread_rwlock_unlock(&volumes_lock);
    pthread_rwlock_unlock(&config_lock);
    severe("reload failed.");
out:
    econfig_release(&new);
    return;
}

static void usage() {
    printf("Rozofs export daemon - %s\n", VERSION);
    printf("Usage: exportd [OPTIONS]\n\n");
    printf("\t-h, --help\tprint this message.\n");
    printf("\t-c, --config\tconfiguration file to use (default: %s).\n",
            EXPORTD_DEFAULT_CONFIG);
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
                if (!realpath(optarg, exportd_config_file)) {
                    fprintf(stderr,
                            "exportd failed: configuration file: %s: %s\n",
                            optarg, strerror(errno));
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

    if (econfig_initialize(&exportd_config) != 0) {
        fprintf(stderr, "can't initialize exportd config: %s.\n",
                strerror(errno));
        goto error;
    }
    if (econfig_read(&exportd_config, exportd_config_file) != 0) {
        fprintf(stderr, "failed to parse configuration file: %s.\n",
                strerror(errno));
        goto error;
    }
    if (econfig_validate(&exportd_config) != 0) {
        fprintf(stderr, "inconsistent configuration file: %s.\n",
                strerror(errno));
        goto error;
    }

    openlog("exportd", LOG_PID, LOG_DAEMON);
    daemon_start(EXPORTD_PID_FILE, on_start, on_stop, on_hup);

    exit(0);
error:
    fprintf(stderr, "see log for more details.\n");
    exit(EXIT_FAILURE);
}
