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
#include <sys/vfs.h>

#include <rozofs/rozofs_srv.h>
#include <rozofs/common/log.h>
#include <rozofs/common/xmalloc.h>
#include <rozofs/common/list.h>
#include <rozofs/common/rozofs_site.h>
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
#include "rbs.h"
#include "storaged_nblock_init.h"

#define STORAGE_REBUILD_PID_FILE "storage_rebuild"

static char storaged_config_file[PATH_MAX] = STORAGED_DEFAULT_CONFIG;

sconfig_t   storaged_config;

static storage_t storaged_storages[STORAGES_MAX_BY_STORAGE_NODE] = { { 0 } };

static char *storaged_hostname = NULL;
static int   storaged_geosite = 0xFFFFFFFF;

static uint16_t storaged_nrstorages = 0;
static fid_t    fid2rebuild={0};
static char    *fid2rebuild_string=NULL;

int   cid=-1;
int   sid=-1;

uint8_t storio_nb_threads = 0;
uint8_t storaged_nb_ports = 0;
uint8_t storaged_nb_io_processes = 0;

// Rebuild storage variables

/* Need to start rebuild storage process */
uint8_t rbs_start_process = 0;
/* Export hostname */
static char rbs_export_hostname[ROZOFS_HOSTNAME_MAX];
/* Device number */
static int rbs_device_number = -1;
/* Time in seconds between two attemps of rebuild */


static int storaged_initialize() {
    int status = -1;
    list_t *p = NULL;
    DEBUG_FUNCTION;

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
		sc->device.total,
		sc->device.mapper,
		sc->device.redundancy) != 0) {
            severe("can't initialize storage (cid:%d : sid:%d) with path %s",
                    sc->cid, sc->sid, sc->root);
            goto out;
        }
    }

    status = 0;
out:
    return status;
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
        if (rbs_sanity_check(rbs_export_hostname, storaged_geosite,
	        sc->cid, sc->sid, sc->root,
		sc->device.total,sc->device.mapper,sc->device.redundancy) != 0)
            goto out;
    }
    status = 0;
out:
    return status;
}

typedef struct _rbs_devices_t {
    uint32_t                     total; 
    uint32_t                     mapper;
    uint32_t                     redundancy;
} rbs_devices_t;

/** Structure used to store configuration for each storage to rebuild */
typedef struct rbs_stor_config {
    char export_hostname[ROZOFS_HOSTNAME_MAX]; ///< export hostname or IP.
    cid_t cid; //< unique id of cluster that owns this storage.
    sid_t sid; ///< unique id of this storage for one cluster.
    rbs_devices_t  device;    
    uint8_t stor_idx; ///< storage index used for display statistics.
    char root[PATH_MAX]; ///< absolute path.
} rbs_stor_config_t;

/** Send a reload signal to the storio
 *
 * @param nb: Number of entries.
 * @param v: table of storages configurations to rebuild.
 */
void send_reload_to_storio() {
  char command[128];

  if (storaged_hostname != NULL) {
      sprintf(command, "storio_reload -H %s", storaged_hostname);
  } else {
      sprintf(command, "storio_reload");
  } 
  if (system(command) < 0) {
      severe("%s %s",command, strerror(errno));
  }
}

/** Starts a thread for rebuild given storage(s)
 *
 * @param nb: Number of entries.
 * @param v: table of storages configurations to rebuild.
 */
static inline void * rebuild_storage_thread(int nb, rbs_stor_config_t *stor_confs,int parallel) {

    DEBUG_FUNCTION;
    int i = 0;
    char * dir;
    int    ret;
    int    result;

    /*
    ** Create a temporary directory to receive the list files
    */
    dir = get_rebuild_directory_name();   
    ret = mkdir(dir,ROZOFS_ST_BINS_FILE_MODE);
    if (ret != 0) {
      severe("Can not create directory %s : %s",dir, strerror(errno));
      return 0;
    }

    for (i = 0; i < nb; i++) {

        // Check if storage conf is empty
        if (stor_confs[i].cid == 0 && stor_confs[i].sid == 0) {
            continue;
        }
	
	// Send a reload signal to the storio to invalidate its cache table
	// and it error counters
	send_reload_to_storio();


        // Try to rebuild the storage until it's over
	result = -1;
        while (result == -1) {

            // Start rebuilding a cid/sid
            REBUILD_MSG("Start rebuild process for storage (cid=%u;sid=%u).",
	                 stor_confs[i].cid, stor_confs[i].sid);
	
	    result = rbs_rebuild_storage(stor_confs[i].export_hostname, 
	        		       storaged_geosite,
                		       stor_confs[i].cid, stor_confs[i].sid, stor_confs[i].root,
				       stor_confs[i].device.total,
				       stor_confs[i].device.mapper, 
				       stor_confs[i].device.redundancy, 
                		       stor_confs[i].stor_idx,
				       rbs_device_number,
				       parallel,
				       storaged_config_file,
				       fid2rebuild);

            if (result == -1) {
              // Probably a problem when connecting with other members
              // of this cluster
              REBUILD_MSG("can't rebuild storage (cid:%u;sid:%u) ! Next attempt in %d seconds.",
                      stor_confs[i].cid, stor_confs[i].sid,
                      TIME_BETWEEN_2_RB_ATTEMPS);

              sleep(TIME_BETWEEN_2_RB_ATTEMPS);
	      continue;
	    }
	    
	    
	    if (result == 0) {
              // Here the rebuild process is finish, so exit
              REBUILD_MSG("The rebuild process for storage (cid=%u;sid=%u) is successful.",
                      stor_confs[i].cid, stor_confs[i].sid);
	    }
	    else {
              // ABort 
              REBUILD_MSG("The rebuild process for storage (cid=%u;sid=%u) is aborted.",
                	stor_confs[i].cid, stor_confs[i].sid);
            }		      	    
	    
	      
        }
	 
	// Send a reload signal to the storio to invalidate its cache table
	// and it error counters
	send_reload_to_storio();

    }
    
    rmdir(get_rebuild_directory_name());
    return 0;

}

/*
**____________________________________________________
*/
/*
  Allocate a device for a file
  
   @param st: storage context
*/
uint32_t storio_device_mapping_allocate_device(storage_t * st) {
  struct statfs sfs;
  int           dev;
  uint64_t      max=0;
  int           choosen_dev=0;
  char          path[FILENAME_MAX];  
  
  for (dev = 0; dev < st->device_number; dev++) {

    sprintf(path, "%s/%d/", st->root, dev); 
               
    if (statfs(path, &sfs) != -1) {
      if (sfs.f_bfree > max) {
        max         = sfs.f_bfree;
	choosen_dev = dev;
      }
    }
  }  
  return choosen_dev;
}
/** Start one rebuild process for each storage to rebuild
 */
static inline void rbs_process_initialize(int parallel) {
    list_t *p = NULL;
    int i = 0;

    rbs_stor_config_t rbs_stor_configs[STORAGES_MAX_BY_STORAGE_NODE] ;
    memset(&rbs_stor_configs, 0,
            STORAGES_MAX_BY_STORAGE_NODE * sizeof(rbs_stor_config_t));

    DEBUG_FUNCTION;

    // For each storage in configuration file

    list_for_each_forward(p, &storaged_config.storages) {

        storage_config_t *sc = list_entry(p, storage_config_t, list);
	
	if ((cid!=-1)&&(sid!=-1)) {
	  if ((cid != sc->cid) || (sid != sc->sid)) continue; 
	}
	
	if ((rbs_device_number >= 0)&&(rbs_device_number >= sc->device.total)) {
	  continue;
	} 

        // Copy the configuration for the storage to rebuild
        strncpy(rbs_stor_configs[i].export_hostname, rbs_export_hostname,
        ROZOFS_HOSTNAME_MAX);
        rbs_stor_configs[i].cid = sc->cid;
        rbs_stor_configs[i].sid = sc->sid;
        rbs_stor_configs[i].stor_idx = i;
	rbs_stor_configs[i].device.total      = sc->device.total;
	rbs_stor_configs[i].device.mapper     = sc->device.mapper;
	rbs_stor_configs[i].device.redundancy = sc->device.redundancy;
        strncpy(rbs_stor_configs[i].root, sc->root, PATH_MAX);

        i++;
    }

    if (i==0) {
      fprintf(stderr, "storaged_rebuild failed !\nNo such cid/sid or device number.\n");    
      return;  
    }
    
    rebuild_storage_thread(i, rbs_stor_configs, parallel);
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

    storaged_release();

    closelog();
}


void usage() {

    printf("Storage node rebuild - RozoFS %s\n", VERSION);
    printf("Usage: storage_rebuild [OPTIONS]\n\n");
    printf("   -h, --help                \tPrint this message.\n");
    printf("   -H, --host=storaged-host  \tSpecify the hostname to rebuild (optional)\n");
    printf("   -c, --config=config-file  \tSpecify config file to use\n");
    printf("                             \t(default: %s).\n",STORAGED_DEFAULT_CONFIG);
    printf("   -r, --rebuild=exportd-host\tlist of \'/\' separated host where exportd is running\n");
    printf("   -d, --device=device-number\tDevice number to rebuild.\n");
    printf("                             \tAll devices are rebuilt when omitted.\n");
    printf("   -s, --sid=<cid/sid>       \tCluster and storage identifier to rebuild.\n");
    printf("                             \tAll <cid/sid> are rebuilt when omitted.\n");
    printf("   -f, --fid=<FID>           \tSpecify one FID to rebuild. -s must also be set.\n");
    printf("   -p, --parallel            \tNumber of rebuild processes in parallel per cid/sid\n");
    printf("                             \t(default is %d, maximum is %d)\n",DEFAULT_PARALLEL_REBUILD_PER_SID,MAXIMUM_PARALLEL_REBUILD_PER_SID);   
    printf("   -g, --geosite             \tTo force site number in case of geo-replication\n");
}

int main(int argc, char *argv[]) {
    int c;
    int  parallel = DEFAULT_PARALLEL_REBUILD_PER_SID;
    
    static struct option long_options[] = {
        { "help", no_argument, 0, 'h'},
        { "config", required_argument, 0, 'c'},
        { "rebuild", required_argument, 0, 'r'},
        { "device", required_argument, 0, 'd'},
        { "host", required_argument, 0, 'H'},
        { "sid", required_argument, 0, 's'},
        { "fid", required_argument, 0, 'f'},
        { "parallel", required_argument, 0, 'p'},	
        { "geosite", required_argument, 0, 'g'},
        { 0, 0, 0, 0}
    };

    // Init of the timer configuration
    rozofs_tmr_init_configuration();
    storaged_hostname = NULL;
    
    storaged_geosite = rozofs_get_local_site();
    if (storaged_geosite == -1) {
      storaged_geosite = 0;
    }


    while (1) {

        int option_index = 0;
        c = getopt_long(argc, argv, "hc:d:r:H:f:p:s:f:g:", long_options, &option_index);

        if (c == -1)
            break;

        switch (c) {

            case 'h':
                usage();
                exit(EXIT_SUCCESS);
                break;
            case 'c':
                if (!realpath(optarg, storaged_config_file)) {
                    fprintf(stderr, "storaged_rebuild failed !\nNo such configuration file %s.\n",optarg);
                    exit(EXIT_FAILURE);
                }
                break;
            case 'r':
                if (strncpy(rbs_export_hostname, optarg, ROZOFS_HOSTNAME_MAX)
                        == NULL) {
                    fprintf(stderr, "storage_rebuild failed !\nBad host name %s\n", optarg);
                    exit(EXIT_FAILURE);
                }
                rbs_start_process = 1;
                break;
            case 's':
                {
		  int ret;
		  ret = sscanf(optarg,"%d/%d",&cid,&sid);
		  if (ret != 2) {
		    fprintf(stderr, "storage_rebuild failed !\n-s option requires cid/sid.\n");
                    exit(EXIT_FAILURE);
                  }
                }
                break;	
            case 'f':
                {
		  int ret;

		  fid2rebuild_string = optarg;
		  ret = uuid_parse(fid2rebuild_string,fid2rebuild);
		  if (ret != 0) {
		    fprintf(stderr, "storage_rebuild failed !\nBad FID format %s.\n", optarg);
                    exit(EXIT_FAILURE);
                  }
		  rbs_device_number = -2; // To tell one FID to rebuild 
                }
                break;							
            case 'd':
	        {
		  int ret;
                  ret = sscanf(optarg,"%d", &rbs_device_number);
                  if (ret <= 0) { 
                      fprintf(stderr, "storage_rebuild failed !\nBad device number %s.\n", optarg);
                      exit(EXIT_FAILURE);
                  }
		}
                break;			
            case 'g':
	        {
		  int ret;
                  ret = sscanf(optarg,"%d", &storaged_geosite);
                  if (ret <= 0) { 
                      fprintf(stderr, "storage_rebuild failed !\nBad site number %s.\n", optarg);
                      exit(EXIT_FAILURE);
                  }
		  if ((storaged_geosite!=0)&&(storaged_geosite!=1)) { 
                      fprintf(stderr, "storage_rebuild failed !\nSite number must be within [0:1] instead of %s.\n", optarg);
                      exit(EXIT_FAILURE);
                  }
		}
                break;
	    case 'p':
	        {
		  int ret;

		  ret = sscanf(optarg,"%d", &parallel);
                  if (ret <= 0) { 
                      fprintf(stderr, "storage_rebuild failed !\nBad --parallel value %s.\n", optarg);
                      exit(EXIT_FAILURE);
                  }
		  if (parallel > MAXIMUM_PARALLEL_REBUILD_PER_SID) {
                      fprintf(stderr, "Assume maximum parallel value of %d\n", MAXIMUM_PARALLEL_REBUILD_PER_SID);
		      parallel = MAXIMUM_PARALLEL_REBUILD_PER_SID;
                  }
		}
                break;
            case 'H':
                storaged_hostname = optarg;
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
    openlog("RBS", LOG_PID, LOG_DAEMON);
    
    {
      char command[256];
      char * p = command;
      int i;
      
      for (i=0; i< argc; i++) p += sprintf(p, "%s ", argv[i]);
      info("%s",command);
    }
    
    /*
    ** Check parameter consistency
    */
    if (rbs_start_process == 0){
        fprintf(stderr, "storage_rebuild failed !\nMissing --rebuild option.\n");
        exit(EXIT_FAILURE);
    } 
    /*
    ** When FID is given, eid and cid/sid is mandatory
    */ 
    if (fid2rebuild_string) {
      if ((cid==-1)&&(sid==-1)) {
        fprintf(stderr, "storage_rebuild failed !\n--fid option requires --sid option too.\n");
        exit(EXIT_FAILURE);      
      }
    }

    // Initialize the list of storage config
    if (sconfig_initialize(&storaged_config) != 0) {
        fprintf(stderr, "storage_rebuild failed !\nCan't initialize storaged config.\n");
        goto error;
    }
    // Read the configuration file
    if (sconfig_read(&storaged_config, storaged_config_file) != 0) {
        fprintf(stderr, "storage_rebuild failed !\nFailed to parse storage configuration file %s.\n",storaged_config_file);
        goto error;
    }
    // Check the configuration
    if (sconfig_validate(&storaged_config) != 0) {
        fprintf(stderr, "storage_rebuild failed !\nInconsistent storage configuration file %s.\n",storaged_config_file);
        goto error;
    }
    // Check rebuild storage configuration if necessary
    if (rbs_check() != 0) goto error;


    // Initialization of the storage configuration
    if (storaged_initialize() != 0) {
        fprintf(stderr, "storage_rebuild failed !\nCan't initialize storaged.\n");
        goto error;
    }
    
    // Start rebuild storage   
    rbs_process_initialize(parallel);
    on_stop();
    
    exit(0);
error:
    fprintf(stderr, "Can't start storage_rebuild. See logs for more details.\n");
    exit(EXIT_FAILURE);
}
