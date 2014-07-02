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
#include <rozofs/rpc/mproto.h>
#include <rozofs/rpc/sproto.h>
#include <rozofs/rpc/spproto.h>
#include <rozofs/core/rozofs_core_files.h>
#include <rozofs/core/rozofs_ip_utilities.h>
#include <rozofs/rozofs_timer_conf.h>

#include "config.h"
#include "sconfig.h"
#include "storage.h"
#include "sconfig.h"
#include "rbs.h"
#include "rbs_eclient.h"


sconfig_t   storaged_config;

static storage_t storaged_storages[STORAGES_MAX_BY_STORAGE_NODE] = { { 0 } };
static uint16_t storaged_nrstorages = 0;


uint8_t storio_nb_threads = 0;
uint8_t storaged_nb_ports = 0;
uint8_t storaged_nb_io_processes = 0;

// Rebuild storage variables


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
int storaged_rebuild_list(char * fid_list) {
  int        fd = -1;
  int        nbJobs=0;
  int        nbSuccess=0;
  list_t     cluster_entries;
  uint64_t   offset;
  rozofs_rebuild_header_file_t  st2rebuild;
  rozofs_rebuild_entry_file_t   file_entry;
  rpcclt_t   rpcclt_export;
  int        ret;
  uint8_t    rozofs_safe,rozofs_forward; 
  uint8_t    prj;
  int        device_id;
  int        spare;
  char       path[FILENAME_MAX];
  char     * pExport_hostname = NULL;
      
  fd = open(fid_list,O_RDWR);
  if (fd < 0) {
      severe("Can not open file %s %s",fid_list,strerror(errno));
      goto error;
  }
  
  if (pread(fd,&st2rebuild,sizeof(rozofs_rebuild_header_file_t),0) 
        != sizeof(rozofs_rebuild_header_file_t)) {
      severe("Can not read st2rebuild in file %s %s",fid_list,strerror(errno));
      goto error;
  }  

  // Initialize the list of storage config
  if (sconfig_initialize(&storaged_config) != 0) {
      severe("Can't initialize storaged config: %s.\n",strerror(errno));
      goto error;
  }
  
  // Read the configuration file
  if (sconfig_read(&storaged_config, st2rebuild.config_file) != 0) {
      severe("Failed to parse storage configuration file %s : %s.\n",st2rebuild.config_file,strerror(errno));
      goto error;
  }

  // Initialization of the storage configuration
  if (storaged_initialize() != 0) {
      severe("can't initialize storaged: %s.", strerror(errno));
      goto error;
  }


  // Initialize the list of cluster(s)
  list_init(&cluster_entries);

  // Try to get the list of storages for this cluster ID
  pExport_hostname = rbs_get_cluster_list(&rpcclt_export, 
                           st2rebuild.export_hostname, 
			   st2rebuild.site,
			   st2rebuild.storage.cid, 
			   &cluster_entries);			   
  if (pExport_hostname == NULL) {			   
      severe("Can't get list of others cluster members from export server (%s) for storage to rebuild (cid:%u; sid:%u): %s\n",
              st2rebuild.export_hostname, 
	      st2rebuild.storage.cid, 
	      st2rebuild.storage.sid, 
	      strerror(errno));
      goto error;
  }
    
  // Get connections for this given cluster
  if (rbs_init_cluster_cnts(&cluster_entries, st2rebuild.storage.cid, st2rebuild.storage.sid) != 0) {
      severe("Can't get cnx server for storage to rebuild (cid:%u; sid:%u): %s\n",
              st2rebuild.storage.cid, st2rebuild.storage.sid, strerror(errno));
      goto error;
  }  

  REBUILD_MSG("  %s rebuild start",fid_list);


  nbJobs    = 0;
  nbSuccess = 0;
  offset = sizeof(rozofs_rebuild_header_file_t);

  while (pread(fd,&file_entry,sizeof(file_entry),offset) == sizeof(file_entry)) {
    rb_entry_t re;
  
    offset += sizeof(file_entry);
    
    if (file_entry.todo == 0) continue;

    nbJobs++;
        
    // Compute the rozofs constants for this layout
    uint8_t rozofs_inverse = rozofs_get_rozofs_inverse(file_entry.layout);

#if 0
  {
    char fid_string[128];
    uuid_unparse(file_entry.fid,fid_string);
    info("rebuilding FID %s",fid_string);
  }  
#endif
    
    memcpy(re.fid,file_entry.fid, sizeof(re.fid));
    memcpy(re.dist_set_current,file_entry.dist_set_current, sizeof(re.dist_set_current));
    re.bsize = file_entry.bsize;
    re.layout = file_entry.layout;
    re.storages = NULL;

    // Get storage connections for this entry
    if (rbs_get_rb_entry_cnts(&re, 
                              &cluster_entries, 
                              st2rebuild.storage.cid, 
			      st2rebuild.storage.sid,
                              rozofs_inverse) != 0) {
        severe( "rbs_get_rb_entry_cnts failed: %s", strerror(errno));
        continue; // Try with the next
    }

    // Get rozofs layout parameters
    rozofs_safe = rozofs_get_rozofs_safe(file_entry.layout);
    rozofs_forward = rozofs_get_rozofs_forward(file_entry.layout);

    // Compute the proj_id to rebuild
    // Check if the storage to rebuild is
    // a spare for this entry
    for (prj = 0; prj < rozofs_safe; prj++) {
        if (re.dist_set_current[prj] == st2rebuild.storage.sid)  break;
    }  
    if (prj >= rozofs_forward) spare = 1;
    else                       spare = 0;

    // Build the full path of directory that contains the bins file
    device_id = -1; // The device must be allocated
    if (storage_dev_map_distribution_write(&st2rebuild.storage,  &device_id,  
                                     re.bsize, re.fid, re.layout, re.dist_set_current, spare,
                                     path, 0) == NULL) {
      char fid_string[128];
      uuid_unparse(re.fid,fid_string);
      severe("rbs_restore_one_rb_entry spare %d FID %s",spare, fid_string);
      continue;      
    }  

    // Check that this directory already exists, otherwise it will be created
    if (storage_create_dir(path) < 0) continue;

    // Build the path of bins file
    storage_complete_path_with_fid(re.fid, path);

    
    if (file_entry.unlink) {
      unlink(path);
    }
    

    // Restore this entry
    if (spare == 1) {
      ret = rbs_restore_one_spare_entry(&st2rebuild.storage, &re, path, device_id, prj);      
    }
    else {
      ret = rbs_restore_one_rb_entry(&st2rebuild.storage, &re, path, device_id, prj);
    }
    
    
    // Free storages cnt
    if (re.storages != NULL) {
      free(re.storages);
      re.storages = NULL;
    }  
         
    if (ret != 0) {
        //severe( "rbs_restore_one_rb_entry failed: %s", strerror(errno));
        continue; // Try with the next
    }
    
   
    nbSuccess++;
    if ((nbSuccess % (16*1024)) == 0) {
      REBUILD_MSG("  ~ %s %d/%d",fid_list,nbSuccess,nbJobs);
    } 
    file_entry.todo = 0;
    
    if (pwrite(fd, &file_entry, sizeof(file_entry), offset-sizeof(file_entry))<0) {
      severe("pwrite size %lu offset %llu %s",(unsigned long int)sizeof(file_entry), 
             (unsigned long long int) offset-sizeof(file_entry), strerror(errno));
    }
  }
  
  close(fd);
  fd = -1;   

  if (nbSuccess == nbJobs) {
    unlink(fid_list);
    REBUILD_MSG(". %s rebuild success of %d files",fid_list,nbSuccess);    
    return 0;
  }
    
  REBUILD_MSG("! %s rebuild failed %d/%d",fid_list,nbJobs-nbSuccess,nbJobs);

  
error:
  if (fd != -1) close(fd);   
  return 1;
}

static void on_stop() {
    DEBUG_FUNCTION;   

    rozofs_layout_release();
    storaged_release();
    closelog();
}

char * utility_name=NULL;
char * input_file_name = NULL;
void usage() {

    printf("RozoFS storage rebuilder - %s\n", VERSION);
    printf("Usage: %s [OPTIONS]\n\n",utility_name);
    printf("   -h, --help\t\t\tprint this message.\n");
    printf("   -f, --fids=<filename> \tA file name containing the fid list to rebuild.\n");    
}

int main(int argc, char *argv[]) {
    int c;
    
    static struct option long_options[] = {
        { "help", no_argument, 0, 'h'},
        { "fids", required_argument, 0, 'f'},	
        { 0, 0, 0, 0}
    };

    // Get utility name
    utility_name = basename(argv[0]);   

    // Init of the timer configuration
    rozofs_tmr_init_configuration();
   
    while (1) {

        int option_index = 0;
        c = getopt_long(argc, argv, "hH:f:", long_options, &option_index);

        if (c == -1)
            break;

        switch (c) {

            case 'h':
                usage();
                exit(EXIT_SUCCESS);
                break;
                break;
            case 'f':
		input_file_name = optarg;
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
    openlog("RBS_LIST", LOG_PID, LOG_DAEMON);
    
    
    /*
    ** Check parameter consistency
    */
    if (input_file_name == NULL){
        fprintf(stderr, "storage_rebuilder failed. Missing --files option.\n");
        exit(EXIT_FAILURE);
    }  
 

    // Start rebuild storage   
    if (storaged_rebuild_list(input_file_name) != 0) goto error;    
    on_stop();
    exit(EXIT_SUCCESS);
    
error:
    exit(EXIT_FAILURE);
}
