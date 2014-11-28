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
#include <dirent.h> 
#include <sys/wait.h>

#include <rozofs/rozofs_srv.h>
#include <rozofs/common/log.h>
#include <rozofs/common/xmalloc.h>
#include <rozofs/common/list.h>
#include <rozofs/common/rozofs_site.h>
#include <rozofs/rpc/mproto.h>
#include <rozofs/rpc/sproto.h>
#include <rozofs/rpc/spproto.h>
#include <rozofs/rpc/eproto.h>
#include <rozofs/core/rozofs_core_files.h>
#include <rozofs/core/rozofs_ip_utilities.h>
#include <rozofs/core/uma_dbg_api.h>
#include <rozofs/rozofs_timer_conf.h>

#include "config.h"
#include "sconfig.h"
#include "storage.h"
#include "storaged.h"
#include "sconfig.h"
#include "rbs.h"
#include "storaged_nblock_init.h"
#include "rbs_sclient.h"
#include "rbs_eclient.h"

#define STORAGE_REBUILD_PID_FILE "storage_rebuild"

static char storaged_config_file[PATH_MAX] = STORAGED_DEFAULT_CONFIG;

sconfig_t   storaged_config;

static storage_t storaged_storages[STORAGES_MAX_BY_STORAGE_NODE] = { { 0 } };

static char *storaged_hostname = NULL;
static int   storaged_geosite = 0xFFFFFFFF;

static uint16_t storaged_nrstorages = 0;
static fid_t    fid2rebuild={0};
uint64_t            rb_fid_table_count=0;

char              * fid2rebuild_string=NULL;
char                command[1024];
static rozofs_rebuild_header_file_t st2rebuild;
static storage_t * storage_to_rebuild = &st2rebuild.storage;
/* RPC client for exports server */
static rpcclt_t rpcclt_export;

/* List of cluster(s) */
static list_t cluster_entries;
uint32_t    max_reloop=-1;
uint32_t    run_loop=0;
int   cid=-1;
int   sid=-1;
int   parallel = DEFAULT_PARALLEL_REBUILD_PER_SID;

int     nb_rbs_entry=0;
typedef struct rbs_monitor_s {
  uint8_t  cid;
  uint8_t  sid;
  uint8_t  layout;
  uint32_t nb_files;
  uint32_t done_files;
  uint64_t written_spare;
  uint64_t written;
  uint64_t read_spare;
  uint64_t read;  
} RBS_MONITOR_S;
RBS_MONITOR_S rbs_monitor[128];


char header_msg[512]={0};
int  header_msg_size=0;

int rbs_index=0;
  
uint8_t storio_nb_threads = 0;
uint8_t storaged_nb_ports = 0;
uint8_t storaged_nb_io_processes = 0;

// Rebuild storage variables

/* Need to start rebuild storage process */
uint8_t rbs_start_process = 0;
/* Export hostname */
static char rbs_export_hostname[ROZOFS_HOSTNAME_MAX];
/* Device number */
int rbs_device_number = -1;

/* Does the rebuild requires a device relocation */
int relocate=0;

int current_file_index = 0;


/*____________________________________________________
   Rebuild monitoring
*/


char                rbs_monitor_file_path[ROZOFS_PATH_MAX]={0};

typedef struct rbs_monitor_file_list_s {    
  char          name[64];  
  uint64_t      mtime;
} RBS_MONITOR_FILE_LIST_S;

#define RBS_MONITOR_MAX_FILES   32
static RBS_MONITOR_FILE_LIST_S rbs_monitor_file_list[RBS_MONITOR_MAX_FILES];
/*
**____________________________________________________
** Purge excedent files
*/
void rbs_monitor_purge(void) {
  struct dirent * dirItem;
  struct stat     statBuf;
  DIR           * dir;
  uint32_t        nb,idx;
  uint32_t        older;
  char file_path[FILENAME_MAX];
  char dir_path[FILENAME_MAX];


  sprintf(dir_path, "%s/storage_rebuild/", DAEMON_PID_DIRECTORY);
  
  /* Open core file directory */ 
  dir=opendir(dir_path);
  if (dir==NULL) return;

  nb = 0;

  while ((dirItem=readdir(dir))!= NULL) {
    
    /* Skip . and .. */ 
    if (dirItem->d_name[0] == '.') continue;

    sprintf(file_path,"%s/%s", dir_path, dirItem->d_name); 

    /* Get file date */ 
    if (stat(file_path,&statBuf) < 0) {   
      severe("rbs_monitor_purge : stat(%s) %s",file_path,strerror(errno));
      unlink(file_path);	           
    }
      
    /* Maximum number of file not yet reached. Just register this one */
    if (nb < RBS_MONITOR_MAX_FILES) {
      rbs_monitor_file_list[nb].mtime = statBuf.st_mtime;
      strcpy(rbs_monitor_file_list[nb].name,file_path);      
      nb ++;
      continue;
    }

    /* Maximum number of file is reached. Remove the older */     

    /* Find older in already registered list */ 
    older = 0;
    for (idx=1; idx < RBS_MONITOR_MAX_FILES; idx ++) {
      if (rbs_monitor_file_list[idx].mtime < rbs_monitor_file_list[older].mtime) older = idx;
    }

    /* 
    ** If older in list is older than the last one read, 
    ** the last one read replaces the older in the array and the older is removed
    */
    if (rbs_monitor_file_list[older].mtime < (uint32_t)statBuf.st_mtime) {
      unlink(rbs_monitor_file_list[older].name);	
      rbs_monitor_file_list[older].mtime = statBuf.st_mtime;
      strcpy(rbs_monitor_file_list[older].name, file_path);
      continue;
    }
    /*
    ** Else the last read is removed 
    */
    unlink(file_path);
  }
  closedir(dir);  
}
/*
**____________________________________________________
** Create or re-create the monitoring file for this rebuild process
*/
#define HEADER "\
# This file was generated by storage_rebuild(8) version: %s.\n\
# All changes to this file will be lost.\n"

time_t loc_time;
char   initial_date[80];

int rbs_monitor_update(char * rebuild_status, int cid, int sid) {
    int status = -1;
    int fd = -1;
    char str1[32];
    char str2[32];
    char * pChar;
    struct tm date;
    int i;
    uint32_t nb_files=0;
    uint32_t done_files=0;
    uint64_t written=0;
    uint64_t written_spare=0;
    uint64_t read_spare=0;
    uint64_t read=0;
      
    if (rbs_monitor_file_path[0] == 0) {
    
      pChar = rbs_monitor_file_path;
      pChar += sprintf(pChar, "%s/storage_rebuild/", DAEMON_PID_DIRECTORY);

      if (access(rbs_monitor_file_path,W_OK) == -1) {
        mkdir(rbs_monitor_file_path,S_IRWXU | S_IROTH);
      }	

      loc_time=time(NULL);
      localtime_r(&loc_time,&date); 
      
      pChar += sprintf(pChar, "%4.4d:%2.2d:%2.2d_%2.2d:%2.2d:%2.2d_%d", 
                       date.tm_year+1900,date.tm_mon+1,date.tm_mday,
		       date.tm_hour, date.tm_min, date.tm_sec,
		       getpid());
      ctime_r(&loc_time,initial_date);      
    } 
    
    if ((fd = open(rbs_monitor_file_path, O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU | S_IROTH)) < 0) {
        severe("can't open %s", rbs_monitor_file_path);
        goto out;
    }

    if (header_msg_size == 0) {
      char * pt = header_msg;
      pt += sprintf(pt, HEADER, VERSION);
      pt += sprintf(pt, "starting  : %s", initial_date);
      pt += sprintf(pt, "command   : %s\n", command);
      pt += sprintf(pt, "parallel  : %d\n", parallel); 
      header_msg_size = pt - header_msg;
    }
    write(fd,header_msg,header_msg_size);
    
    if (cid == 0) {
      dprintf(fd, "status    : %s\n", rebuild_status);    
    }
    else {
      dprintf(fd, "status    : %s %d/%d\n", rebuild_status, cid, sid);
    }        
    dprintf(fd, "loop      : %d\n", run_loop);
    
    for (i=0; i<nb_rbs_entry; i++) {
      dprintf(fd, "* cid/sid : %d/%d\n", rbs_monitor[i].cid, rbs_monitor[i].sid);
      dprintf(fd, "            - layout   : %d\n",rbs_monitor[i].layout);
      dprintf(fd, "            - nb files : %d/%d\n", rbs_monitor[i].done_files, rbs_monitor[i].nb_files);
      nb_files   += rbs_monitor[i].nb_files;
      done_files += rbs_monitor[i].done_files;

      dprintf(fd, "            - written  : %llu\n", (long long unsigned int)rbs_monitor[i].written);
      written += rbs_monitor[i].written;

      dprintf(fd, "                         . nominal : %llu\n", 
                               (long long unsigned int)rbs_monitor[i].written-rbs_monitor[i].written_spare);
      dprintf(fd, "                         . spare   : %llu\n",
                               (long long unsigned int)rbs_monitor[i].written_spare);
      written_spare += rbs_monitor[i].written_spare;
      
      dprintf(fd, "            - read     : %llu\n", (long long unsigned int)rbs_monitor[i].read);
      read += rbs_monitor[i].read;
      dprintf(fd, "                         . nominal : %llu\n",
                              (long long unsigned int)rbs_monitor[i].read-rbs_monitor[i].read_spare);
      read_spare += rbs_monitor[i].read_spare; 
      dprintf(fd, "                         . spare   : %llu\n", 
                               (long long unsigned int)rbs_monitor[i].read_spare);   
    }

    uint32_t sec=time(NULL)-loc_time;
    if (sec == 0) sec = 1;

    dprintf(fd, "* total   :\n");
    dprintf(fd, "            - nb files : %d/%d\n", done_files, nb_files);
    uma_dbg_byte2String(written, str1); 
    uma_dbg_byte2String(written/sec, str2); 
    dprintf(fd, "            - written  : %s (%s/s)\n", str1, str2);
    uma_dbg_byte2String(written-written_spare, str1);       
    dprintf(fd, "                         . nominal : %s\n",str1);    
    uma_dbg_byte2String(written_spare, str1);    
    dprintf(fd, "                         . spare   : %s\n",str1);
    uma_dbg_byte2String(read, str1);
    uma_dbg_byte2String(read/sec, str2);    
    dprintf(fd, "            - read     : %s (%s/s)\n",str1, str2);
    uma_dbg_byte2String(read-read_spare, str1);       
    dprintf(fd, "                         . nominal : %s\n",str1);         
    uma_dbg_byte2String(read_spare, str1);
    dprintf(fd, "                         . spare   : %s\n",str1);        
     
    if (sec<60) {
      dprintf(fd, "delay     : %u sec\n",sec);        
    }
    else {
      uint32_t min=sec/60;  
      sec = sec % 60;
      if (min<60) {
	dprintf(fd, "delay     : %u min %2.2u sec\n",min, sec);        
      }
      else {      
	int hour=min/60; 
	min = min % 60;
	dprintf(fd, "delay     : %u hours %2.2u min %2.2u sec\n",hour, min, sec);  
      }	      
    }
    status = 0;
out:
    if (fd > 0) close(fd);

    return status;
}
void rbs_monitor_display() {
  char cmdString[256];

  if (rbs_monitor_file_path[0] == 0) return;

  sprintf(cmdString,"cat %s",rbs_monitor_file_path);
  system(cmdString);
}
    
/*
**--------------------FID hash table
*/

/*
** FID hash table to prevent registering 2 times
**   the same FID for rebuilding
*/
#define FID_TABLE_HASH_SIZE  (16*1024)

#define FID_MAX_ENTRY      31
typedef struct _rb_fid_entries_t {
    int                        count;
    int                        padding;
    struct _rb_fid_entries_t * next;   
    fid_t                      fid[FID_MAX_ENTRY];
    uint8_t                    chunk[FID_MAX_ENTRY];
} rb_fid_entries_t;

rb_fid_entries_t ** rb_fid_table=NULL;


/*
**
*/
static inline unsigned int fid_hash(void *key) {
    uint32_t hash = 0;
    uint8_t *c;
    for (c = key; c != key + 16; c++)
        hash = *c + (hash << 6) + (hash << 16) - hash;
    return hash % FID_TABLE_HASH_SIZE;
}
static inline void rb_hash_table_initialize() {
  int size;
  
  size = sizeof(void *)*FID_TABLE_HASH_SIZE;
  rb_fid_table = malloc(size);
  memset(rb_fid_table,0,size);
  rb_fid_table_count = 0;
}

int rb_hash_table_search(fid_t fid) {
  int      i;
  unsigned int idx = fid_hash(fid);
  rb_fid_entries_t * p;
  fid_t            * pF;
  
  p = rb_fid_table[idx];
  
  while (p != NULL) {
    pF = &p->fid[0];
    for (i=0; i < p->count; i++,pF++) {
      if (memcmp(fid, pF, sizeof (fid_t)) == 0) return 1;
    }
    p = p->next;
  }
  return 0;
}
int rb_hash_table_search_chunk(fid_t fid,int chunk) {
  int      i;
  unsigned int idx = fid_hash(fid);
  rb_fid_entries_t * p;
  fid_t            * pF;
  uint8_t          * pC;
  
  p = rb_fid_table[idx];
  
  while (p != NULL) {
    pF = &p->fid[0];
    pC = &p->chunk[0];
    
    for (i=0; i < p->count; i++,pF++,pC++) {
      if (*pC != chunk) continue;
      if (memcmp(fid, pF, sizeof (fid_t)) == 0) return 1;
    }
    p = p->next;
  }
  return 0;
}
rb_fid_entries_t * rb_hash_table_new(idx) {
  rb_fid_entries_t * p;
    
  p = (rb_fid_entries_t*) malloc(sizeof(rb_fid_entries_t));
  p->count = 0;
  p->next = rb_fid_table[idx];
  rb_fid_table[idx] = p;
  
  return p;
}
rb_fid_entries_t * rb_hash_table_get(idx) {
  rb_fid_entries_t * p;
    
  p = rb_fid_table[idx];
  if (p == NULL)                 p = rb_hash_table_new(idx);
  if (p->count == FID_MAX_ENTRY) p = rb_hash_table_new(idx);  
  return p;
}
void rb_hash_table_insert(fid_t fid) {
  unsigned int idx = fid_hash(fid);
  rb_fid_entries_t * p;
  
  p = rb_hash_table_get(idx);
  memcpy(p->fid[p->count],fid,sizeof(fid_t));
  p->count++;
  rb_fid_table_count++;
}
void rb_hash_table_insert_chunk(fid_t fid, int chunk) {
  unsigned int idx = fid_hash(fid);
  rb_fid_entries_t * p;
  
  p = rb_hash_table_get(idx);
  memcpy(p->fid[p->count],fid,sizeof(fid_t));
  p->chunk[p->count] = chunk;
  p->count++;
  rb_fid_table_count++;
}
void rb_hash_table_delete() {
  int idx;
  rb_fid_entries_t * p, * pNext;
  
  if (rb_fid_table == NULL) return;
  
  for (idx = 0; idx < FID_TABLE_HASH_SIZE; idx++) {
    
    p = rb_fid_table[idx];
    while (p != NULL) {
      pNext = p->next;
      free(p);
      p = pNext;
    }
  }
  
  free(rb_fid_table);
  rb_fid_table = NULL;
}






/** Write the storage to rebuild information in the heaer
 *  of the rebuild file one the layout is known
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
static int rbs_write_st2rebuild(uint8_t layout,int *cfgfd) {
  int            idx;
  int            ret;

  /*
  ** Update layout in st2rebuild
  */
  st2rebuild.layout = layout;
  rbs_monitor[rbs_index].layout = layout;
           
  /*
  ** Write st2rebuild in file
  */
  for (idx=0; idx < parallel; idx++) {

    ret = write(cfgfd[idx],&st2rebuild,sizeof(st2rebuild));
    if (ret != sizeof(st2rebuild)) {
      severe("Can not write header in file %s", strerror(errno));
      return -1;      
    }
  }   
  return 0;
}    

/** Retrieves the list of bins files to rebuild from a storage
 *
 * @param rb_stor: storage contacted.
 * @param cid: unique id of cluster that owns this storage.
 * @param sid: the unique id for the storage to rebuild.
 *
 * @return: 0  -1 otherwise (errno is set)
 */
int rbs_get_rb_entry_list_one_storage(rb_stor_t *rb_stor, cid_t cid,
        sid_t sid, int *cfgfd, int failed) {
    int status = -1;
    uint16_t slice = 0;
    uint8_t spare = 0;
    uint8_t device = 0;
    uint64_t cookie = 0;
    uint8_t eof = 0;
    sid_t dist_set[ROZOFS_SAFE_MAX];
    bins_file_rebuild_t * children = NULL;
    bins_file_rebuild_t * iterator = NULL;
    bins_file_rebuild_t * free_it = NULL;
    int            ret;
    rozofs_rebuild_entry_file_t file_entry;
    
    DEBUG_FUNCTION;

  
    memset(dist_set, 0, sizeof (sid_t) * ROZOFS_SAFE_MAX);

    // While the end of the list is not reached
    while (eof == 0) {

        // Send a request to storage to get the list of bins file(s)
        if (rbs_get_rb_entry_list(&rb_stor->mclient, cid, rb_stor->sid, sid,
                &device, &spare, &slice, &cookie, &children, &eof) != 0) {
            severe("rbs_get_rb_entry_list failed: %s\n", strerror(errno));
            goto out;
        }

        iterator = children;

        // For each entry 
        while (iterator != NULL) {
	
	   /*
	   ** Check that not too much storages are failed for this layout
	   */
	   if (failed) {
	     switch(iterator->layout) {
	     
	       case LAYOUT_2_3_4:
	         if (failed>1) {
		   severe("%d failed storages on LAYOUT_2_3_4",failed);
		   return -1;
		 }
		 break;
	       case LAYOUT_4_6_8:
	         if (failed>2) {
		   severe("%d failed storages on LAYOUT_4_6_8",failed);
		   return -1;
		 }
		 break;	
	       case LAYOUT_8_12_16:
	         if (failed>2) {
		   severe("%d failed storages on LAYOUT_8_12_16",failed);
		   return -1;
		 }
		 break;	
	       default:	         	 		 	 
		 severe("Unexpected layout %d",iterator->layout);
		 return -1;
	     }
	     failed = 0;
	   }

           // Verify if this entry is already present in list
	    if (rb_hash_table_search(iterator->fid) == 0) { 

	        /*
		** Layout not yet filled in st2rebuild. It is time
		** to update it
		*/
	        if (st2rebuild.layout == 0xFF) {
		  ret = rbs_write_st2rebuild(iterator->layout,cfgfd);
		  if (ret != 0) goto out;
                }
			    
                rb_hash_table_insert(iterator->fid);
			
		memcpy(file_entry.fid,iterator->fid, sizeof (fid_t));
		file_entry.layout      = iterator->layout;
		file_entry.bsize       = iterator->bsize;
        	file_entry.todo        = 1;    
	        file_entry.relocate    = 0;		    
		file_entry.block_start = 0;  
		file_entry.block_end   = -1;  
        	memcpy(file_entry.dist_set_current, iterator->dist_set_current, sizeof (sid_t) * ROZOFS_SAFE_MAX);	    

        	ret = write(cfgfd[current_file_index],&file_entry,sizeof(file_entry)); 
		if (ret != sizeof(file_entry)) {
		  severe("can not write file cid%d sid%d %d %s",cid,sid,current_file_index,strerror(errno));
		}	    
		current_file_index++;
		if (current_file_index >= parallel) current_file_index = 0; 		
		
            }

            free_it = iterator;
            iterator = iterator->next;
            free(free_it);
        }
      }

    status = 0;
out:      


    return status;
}








int rbs_initialize(cid_t cid, sid_t sid, const char *storage_root, 
                   uint32_t dev, uint32_t dev_mapper, uint32_t dev_red) {
    int status = -1;
    DEBUG_FUNCTION;

    // Initialize the storage to rebuild 
    if (storage_initialize(storage_to_rebuild, cid, sid, storage_root,
		dev,
		dev_mapper,
		dev_red) != 0)
        goto out;

    // Initialize the list of cluster(s)
    list_init(&cluster_entries);

    status = 0;
out:
    return status;
}
/*
**____________________________________________________
** Append messages to the monitoring file
*/
int storage_rebuild_monitor_append(char * msg) {
    int status = -1;
    int fd = -1;
    char path[FILENAME_MAX];
    DEBUG_FUNCTION;

    sprintf(path, "%s/storage_rebuild/rbs.%d", DAEMON_PID_DIRECTORY, getpid());
    if ((fd = open(path, O_APPEND, S_IRWXU | S_IROTH)) < 0) {
        severe("can't open %s", path);
        goto out;
    }

    dprintf(fd, "%s\n", msg);

    status = 0;
out:
    if (fd > 0) close(fd);
    return status;
}
/*
**____________________________________________________
** Append messages to the monitoring file
*/
int storage_rebuild_monitor_append_int(char * msg, int val) {
    int status = -1;
    int fd = -1;
    char path[FILENAME_MAX];
    DEBUG_FUNCTION;

    sprintf(path, "%s/storage_rebuild/rbs.%d", DAEMON_PID_DIRECTORY, getpid());
    if ((fd = open(path, O_APPEND, S_IRWXU | S_IROTH)) < 0) {
        severe("can't open %s", path);
        goto out;
    }

    dprintf(fd, "%s: %d\n", msg, val);
    status = 0;
    info("%s opened",path);
    close(fd);
out:
    return status;
}

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
int rbs_sanity_check(const char *export_host_list, int site, cid_t cid, sid_t sid,
        const char *root, uint32_t dev, uint32_t dev_mapper, uint32_t dev_red) {

    int status = -1;
    char * pExport_host = 0;

    DEBUG_FUNCTION;

    // Try to initialize the storage to rebuild
    if (rbs_initialize(cid, sid, root, dev, dev_mapper, dev_red) != 0) {
        // Probably a path problem
        REBUILD_FAILED("Can't initialize rebuild storage (cid:%u; sid:%u;"
                " path:%s): %s\n", cid, sid, root, strerror(errno));
        goto out;
    }
    
    // Try to get the list of storages for this cluster ID
    pExport_host = rbs_get_cluster_list(&rpcclt_export, export_host_list, 
                                        site, cid, &cluster_entries);
    if (pExport_host == NULL) {	    
        REBUILD_FAILED("Can't get list of others cluster members from export"
                " server (%s) for storage to rebuild (cid:%u; sid:%u): %s\n",
                export_host_list, cid, sid, strerror(errno));
        goto out;
    }

    // Check the list of cluster
    if (rbs_check_cluster_list(&cluster_entries, cid, sid) != 0) {
        REBUILD_FAILED("No such storage (sid=%u) in cluster with cid=%u\n", sid, cid);
        goto out;
    }

    status = 0;

out:
    // Free cluster(s) list
    rbs_release_cluster_list(&cluster_entries);

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

typedef enum RBS_STATUS_e {
  RBS_STATUS_BUILD_JOB_LIST,
  RBS_STATUS_PROCESSING_LIST,
  RBS_STATUS_FAILED,
  RBS_STATUS_ABORTED,
  RBS_STATUS_SUCCESS 
} RBS_STATUS_E;

/** Structure used to store configuration for each storage to rebuild */
typedef struct rbs_stor_config {
    char export_hostname[ROZOFS_HOSTNAME_MAX]; ///< export hostname or IP.
    cid_t cid; //< unique id of cluster that owns this storage.
    RBS_STATUS_E status;
    sid_t sid; ///< unique id of this storage for one cluster.
    rbs_devices_t  device;    
    uint8_t stor_idx; ///< storage index used for display statistics.
    char root[PATH_MAX]; ///< absolute path.
} rbs_stor_config_t;

/** Retrieves the list of bins files to rebuild for a given storage
 *
 * @param cluster_entries: list of cluster(s).
 * @param cid: unique id of cluster that owns this storage.
 * @param sid: the unique id for the storage to rebuild.
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
static int rbs_get_rb_entry_list_one_cluster(list_t * cluster_entries,
        cid_t cid, sid_t sid, int failed) {
    list_t       *p, *q;
    int            status = -1;
    char         * dir;
    char           filename[FILENAME_MAX];
    int            idx;
    int            cfgfd[MAXIMUM_PARALLEL_REBUILD_PER_SID];
        
    /*
    ** Create FID list file files
    */
    dir = get_rebuild_sid_directory_name(cid,sid);
    for (idx=0; idx < parallel; idx++) {
    
      sprintf(filename,"%s/storage.%2.2d", dir, idx);

      cfgfd[idx] = open(filename,O_CREAT | O_TRUNC | O_WRONLY, 0640);
      if (cfgfd[idx] == -1) {
	severe("Can not open file %s %s", filename, strerror(errno));
	return -1;
      }
    }    


    list_for_each_forward(p, cluster_entries) {

        rb_cluster_t *clu = list_entry(p, rb_cluster_t, list);

        if (clu->cid == cid) {

            list_for_each_forward(q, &clu->storages) {

                rb_stor_t *rb_stor = list_entry(q, rb_stor_t, list);

                if (rb_stor->sid == sid)
                    continue;

		if (rb_stor->mclient.rpcclt.client == NULL)
		    continue;   

                // Get the list of bins files to rebuild for this storage
                if (rbs_get_rb_entry_list_one_storage(rb_stor, cid, sid,cfgfd, failed) != 0) {

                    severe("rbs_get_rb_entry_list_one_storage failed: %s\n",
                            strerror(errno));
                    goto out;
                }
            }
        }
    }

    status = 0;
out:
    for (idx=0; idx < parallel; idx++) {
      close(cfgfd[idx]);
    }  
    return status;
}

/** Send a reload signal to the storio
 *
 * @param nb: Number of entries.
 * @param v: table of storages configurations to rebuild.
 */
void send_reload_to_storio(int cid) {
  char command[128];

  if (storaged_hostname != NULL) {
      sprintf(command, "storio_reload -H %s -i %d", storaged_hostname, cid);
  } else {
      sprintf(command, "storio_reload -i %d", cid);
  } 
  if (system(command) < 0) {
      severe("%s %s",command, strerror(errno));
  }
}
/** Build a list with just one FID
 *
 * @param cid: unique id of cluster that owns this storage.
 * @param sid: the unique id for the storage to rebuild.
 * @param fid2rebuild: the FID to rebuild
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
static int rbs_build_one_fid_list(cid_t cid, sid_t sid, uint8_t layout, uint8_t bsize, uint8_t * dist, fid_t fid2rebuild) {
  int            fd; 
  rozofs_rebuild_entry_file_t file_entry;
  char         * dir;
  char           filename[FILENAME_MAX];
  int            ret;
  int            i;
  
  /*
  ** Create FID list file files
  */
  dir = get_rebuild_sid_directory_name(cid,sid);

  sprintf(filename,"%s/%s", dir, fid2rebuild_string);
      
  fd = open(filename,O_CREAT | O_TRUNC | O_WRONLY, 0640);
  if (fd == -1) {
    severe("Can not open file %s %s", filename, strerror(errno));
    return -1;
  }

  st2rebuild.layout = layout;    
  ret = write(fd,&st2rebuild,sizeof(st2rebuild));
  if (ret != sizeof(st2rebuild)) {
    severe("Can not write header in file %s %s", filename, strerror(errno));
    return -1;      
  }  

  memcpy(file_entry.fid,fid2rebuild, sizeof (fid_t));
  file_entry.layout      = layout;
  file_entry.bsize       = bsize;  
  file_entry.todo        = 1;      
  file_entry.relocate    = 1;  
  file_entry.block_start = 0;  
  file_entry.block_end   = -1;  
  
  for(i=0; i<ROZOFS_SAFE_MAX; i++) {
    file_entry.dist_set_current[i] = dist[i];
  }    

  ret = write(fd,&file_entry,sizeof(file_entry)); 
  if (ret != sizeof(file_entry)) {
    severe("can not write file cid%d sid%d %s",cid,sid,strerror(errno));
    return -1;
  }  
  
  close(fd);
  return 0;   
}
/** Rebuild list just produced 
 *
 */
int rbs_do_list_rebuild(int cid, int sid) {
  char         * dirName;
  char           cmd[FILENAME_MAX];
  DIR           *dir;
  struct dirent *file;
  int            total;
  int            status;
  int            failure;
  int            success;
  int            fd[128];
  char           fname[128];
  int            i;
  struct timespec timeout;
  sigset_t        mask;
  sigset_t        orig_mask; 

  sigemptyset (&mask);
  sigaddset (&mask, SIGCHLD); 
  if (sigprocmask(SIG_BLOCK, &mask, &orig_mask) < 0) {
    severe("sigprocmask %s", strerror(errno));
    return 1;
  }  /*

  ** Start one rebuild process par rebuild file
  */
  dirName = get_rebuild_sid_directory_name(cid,sid);
  
  /*
  ** Open this directory
  */
  dir = opendir(dirName);
  if (dir == NULL) {
    if (errno == ENOENT) return 0;
    severe("opendir(%s) %s", dirName, strerror(errno));
    return -1;
  } 	  
  /*
  ** Loop on distibution sub directories
  */
  total = 0;
  while ((file = readdir(dir)) != NULL) {
    pid_t pid;
  
    if (strcmp(file->d_name,".")==0)  continue;
    if (strcmp(file->d_name,"..")==0) continue;
    
    sprintf(fname,"%s/%s",dirName,file->d_name);
    fd[total] = open(fname, O_RDONLY);

    pid = fork();
    
    if (pid == 0) {
      sprintf(cmd,"storage_list_rebuilder -f %s/%s", dirName, file->d_name);
      status = system(cmd);
      if (status == 0) exit(0);
      exit(-1);
    }
      
    total++;
    
  }
  closedir(dir);
  
  failure = 0;
  success = 0;
  
  while (total > (failure+success)) {
    ROZOFS_RBS_COUNTERS_T counters;
    int                   ret;

    timeout.tv_sec  = 60;
    timeout.tv_nsec = 0;
    
    ret = sigtimedwait(&mask, NULL, &timeout);
    if (ret < 0) {
      if (errno != EAGAIN) continue;
    }  

     
    /* Check for rebuild sub processes status */    
    while (waitpid(-1,&status,WNOHANG) > 0) {
      status = WEXITSTATUS(status);
      if (status != 0) failure++;
      else             success++;
    }
      
    rbs_monitor[rbs_index].done_files      = 0;
    rbs_monitor[rbs_index].written         = 0;
    rbs_monitor[rbs_index].written_spare   = 0;
    rbs_monitor[rbs_index].read            = 0;
    rbs_monitor[rbs_index].read_spare      = 0;

    for (i=0; i< total; i++) {
      if (pread(fd[i], &counters, sizeof(counters), 0) == sizeof(counters)) {
        rbs_monitor[rbs_index].done_files      += counters.done_files;
        rbs_monitor[rbs_index].written         += counters.written;
        rbs_monitor[rbs_index].written_spare   += counters.written_spare;	
	rbs_monitor[rbs_index].read            += counters.read;
	rbs_monitor[rbs_index].read_spare      += counters.read_spare;
      }
    }
  }

  if (failure != 0) {
    severe("%d list rebuild processes failed upon %d",failure,total);
    return -1;
  }
  return 0;
}
/** Retrieves the list of bins files to rebuild from the available disks
 *
 * @param cluster_entries: list of cluster(s).
 * @param cid: unique id of cluster that owns this storage.
 * @param sid: the unique id for the storage to rebuild.
 * @param device: the missing device identifier 
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
static int rbs_build_device_missing_list_one_cluster(cid_t cid, 
						     sid_t sid,
						     int device_to_rebuild) {
  char           dir_path[FILENAME_MAX];						     
  char           slicepath[FILENAME_MAX];						     
  char           filepath[FILENAME_MAX];						     
  int            device_it;
  int            spare_it;
  DIR           *dir1;
  struct dirent *file;
  int            fd; 
  size_t         nb_read;
  rozofs_stor_bins_file_hdr_t file_hdr; 
  rozofs_rebuild_entry_file_t file_entry;
  int            idx;
  char         * dir;
  char           filename[FILENAME_MAX];
  int            cfgfd[MAXIMUM_PARALLEL_REBUILD_PER_SID];
  int            ret;
  int            slice;
  uint8_t        chunk;  

  /*
  ** Create FID list file files
  */
  dir = get_rebuild_sid_directory_name(cid,sid);
  for (idx=0; idx < parallel; idx++) {

    sprintf(filename,"%s/device%d.%2.2d", dir, device_to_rebuild, idx);
      
    cfgfd[idx] = open(filename,O_CREAT | O_TRUNC | O_WRONLY, 0640);
    if (cfgfd[idx] == -1) {
      severe("Can not open file %s %s", filename, strerror(errno));
      return 0;
    }
  }    
  

  // Loop on all the devices
  for (device_it = 0; device_it < storage_to_rebuild->device_number;device_it++) {

    // Do not read the disk to rebuild
    if (device_it == device_to_rebuild) continue;

    // For spare and no spare
    for (spare_it = 0; spare_it < 2; spare_it++) {

      // Build path directory for this layout and this spare type        	
      sprintf(dir_path, "%s/%d/hdr_%u", storage_to_rebuild->root, device_it, spare_it);

      // Check that this directory already exists, otherwise it will be create
      if (access(dir_path, F_OK) == -1) continue;

      for (slice=0; slice < FID_STORAGE_SLICE_SIZE; slice++) {

        storage_build_hdr_path(slicepath, storage_to_rebuild->root, device_it, spare_it, slice);

        // Open this directory
        dir1 = opendir(slicepath);
        if (dir1 == NULL) continue;


        // Loop on header files in slice directory
        while ((file = readdir(dir1)) != NULL) {
          int i;
          
	  if (file->d_name[0] == '.') continue;

          // Read the file
          sprintf(filepath, "%s/%s",slicepath, file->d_name);

	  fd = open(filepath, ROZOFS_ST_NO_CREATE_FILE_FLAG, ROZOFS_ST_BINS_FILE_MODE);
	  if (fd < 0) continue;

          nb_read = pread(fd, &file_hdr, sizeof(file_hdr), 0);
	  close(fd);	    

          // What to do with such an error ?
	  if (nb_read != sizeof(file_hdr)) continue;

	  // When not in a relocation case, rewrite the file header on this device if it should
	  if (!relocate) {
            for (i=0; i < storage_to_rebuild->mapper_redundancy; i++) {
	          int dev;

              dev = storage_mapper_device(file_hdr.fid,i,storage_to_rebuild->mapper_modulo);

 	      if (dev == device_to_rebuild) {
		// Let's re-write the header file  	      
        	storage_build_hdr_path(filepath, storage_to_rebuild->root, device_to_rebuild, spare_it, slice);
        	ret = storage_write_header_file(NULL,dev,filepath,&file_hdr);
		if (ret != 0) {
	          severe("storage_write_header_file(%s) %s",filepath,strerror(errno))
		}	
		break;
	      } 
	    } 
	  }

          // Check whether this file has some chunk of data on the device to rebuild
	  for (chunk=0; chunk<ROZOFS_STORAGE_MAX_CHUNK_PER_FILE; chunk++) {
	  
	      if (file_hdr.device[chunk] == ROZOFS_EOF_CHUNK)  break;
	      
              if (file_hdr.device[chunk] != device_to_rebuild) continue;
	   
              /*
	      ** This file has a chunk on the device to rebuild
	      ** Check whether this FID is already set in the list
	      */
	      if (rb_hash_table_search_chunk(file_hdr.fid,chunk) == 0) {
	        rb_hash_table_insert_chunk(file_hdr.fid,chunk);	
	      }
	      else {
		continue;
	      }	      

	      /*
	      ** Layout not yet filled in st2rebuild. It is time
	      ** to update it
	      */
	      if (st2rebuild.layout == 0xFF) {
	        ret = rbs_write_st2rebuild(file_hdr.layout,cfgfd);
		if (ret != 0) goto out;
              }

	      memcpy(file_entry.fid,file_hdr.fid, sizeof (fid_t));
	      file_entry.layout      = file_hdr.layout;
	      file_entry.bsize       = file_hdr.bsize;
              file_entry.todo        = 1;     
	      file_entry.relocate    = relocate;
	      file_entry.block_start = chunk * ROZOFS_STORAGE_NB_BLOCK_PER_CHUNK(file_hdr.bsize);  
	      file_entry.block_end   = file_entry.block_start + ROZOFS_STORAGE_NB_BLOCK_PER_CHUNK(file_hdr.bsize) -1;  

              memcpy(file_entry.dist_set_current, file_hdr.dist_set_current, sizeof (sid_t) * ROZOFS_SAFE_MAX);	    

              ret = write(cfgfd[current_file_index],&file_entry,sizeof(file_entry)); 
	      if (ret != sizeof(file_entry)) {
	        severe("can not write file cid%d sid%d %d %s",cid,sid,current_file_index,strerror(errno));
	      }
	      current_file_index++;
	      if (current_file_index >= parallel) current_file_index = 0; 
	  }
	      
	} // End of loop in one slice 
	closedir(dir1);  
      } // End of slices
    }
  } 

out:
  for (idx=0; idx < parallel; idx++) {
    close(cfgfd[idx]);
  }  
  return 0;   
}
int rbs_build_job_lists(const char *export_host_list, int site, cid_t cid, sid_t sid,
        const char *root, uint32_t dev, uint32_t dev_mapper, uint32_t dev_red, int device, char * config_file, 
	fid_t fid2rebuild) {
    int status = -1;
    int ret;
    char * pExport_host = 0;
    int failed,available;
    char * dir;

    DEBUG_FUNCTION;

    rb_hash_table_initialize();

    // Initialize the storage to rebuild
    if (rbs_initialize(cid, sid, root, dev, dev_mapper, dev_red) != 0) {
        severe("can't init. storage to rebuild (cid:%u;sid:%u;path:%s)",
                cid, sid, root);
        goto out;
    }
    memset(&st2rebuild.counters,0,sizeof(st2rebuild.counters));
    strcpy(st2rebuild.export_hostname,export_host_list);
    strcpy(st2rebuild.config_file,config_file);
    st2rebuild.site = site;
    st2rebuild.layout = 0xFF;
    st2rebuild.device = rbs_device_number;

    // Get the list of storages for this cluster ID
    pExport_host = rbs_get_cluster_list(&rpcclt_export, export_host_list, 
                                        site, cid, &cluster_entries);
    if (pExport_host == NULL) {					
        severe("rbs_get_cluster_list failed (cid: %u) : %s", cid, strerror(errno));
        goto out;
    }

    // Check the list of cluster
    if (rbs_check_cluster_list(&cluster_entries, cid, sid) != 0)
        goto out;

    // Get connections for this given cluster
    rbs_init_cluster_cnts(&cluster_entries, cid, sid,&failed,&available);
    
    // Create a temporary directory to receive the list files 
    dir = get_rebuild_sid_directory_name(cid,sid);
    ret = mkdir(dir,ROZOFS_ST_BINS_FILE_MODE);
    if ((ret != 0)&&(errno!=EEXIST)) {
      severe("Can not create directory for cid %d sid %d : %s", cid, sid, strerror(errno));
      goto out;
    }   
	
    // Remove any old files that should exist in the process directory
    ret = rbs_empty_dir(dir);

    // One FID to rebuild
    if (device == -2) {
      uint32_t   bsize;
      uint8_t    layout; 
      ep_mattr_t attr;
      
      // Resolve this FID thanks to the exportd
      if (rbs_get_fid_attr(&rpcclt_export, pExport_host, fid2rebuild, &attr, &bsize, &layout) != 0)
      {
        if (errno == ENOENT) {
	  status = -2;
	  REBUILD_FAILED("Unknown FID");
	}
	else {
	  REBUILD_FAILED("Can not get attributes from export %s",strerror(errno));
	}
	goto out;
      }
      
      if (rbs_build_one_fid_list(cid, sid, layout, bsize, attr.sids, fid2rebuild) != 0)
        goto out;
      rb_fid_table_count = 1;	
      parallel           = 1; 
    }
    else if (device == -1) {
      // Build the list from the remote storages
      if (rbs_get_rb_entry_list_one_cluster(&cluster_entries, cid, sid, failed) != 0)
        goto out;  	 	 	 
    }
    else {
      // The device number is to big for theis storage
      if (device >= st2rebuild.storage.device_number) {
        REBUILD_FAILED("No such device number %d.",device);
	status = -2;	
	goto out;
      }
      // The storage has only on device, so this is a complete storage rebuild
      if (st2rebuild.storage.device_number == 1) {
	// Build the list from the remote storages
	if (rbs_get_rb_entry_list_one_cluster(&cluster_entries, cid, sid,failed) != 0)
          goto out;         
      }
      else {
	// Build the list from the available data on local disk
	if (rbs_build_device_missing_list_one_cluster(cid, sid, device) != 0)
          goto out;
      }		    		
    }
    
    // No file to rebuild
    if (rb_fid_table_count==0) {
      REBUILD_MSG("No file to rebuild");
      rbs_empty_dir (get_rebuild_sid_directory_name(cid,sid));
      unlink(get_rebuild_sid_directory_name(cid,sid));
    }
    else { 
      REBUILD_MSG("%llu files to rebuild by %d processes",
           (unsigned long long int)rb_fid_table_count,parallel);
    }	   
     
    status = 0;

out:    
    rb_hash_table_delete();    
    rbs_monitor[rbs_index].nb_files = rb_fid_table_count;
    // Free cluster(s) list
    rbs_release_cluster_list(&cluster_entries);
    return status;
}
/** Starts a thread for rebuild given storage(s)
 *
 * @param nb: Number of entries.
 * @param v: table of storages configurations to rebuild.
 */
static inline void rebuild_storage_thread(rbs_stor_config_t *stor_confs) {
  int    result;
  int    delay=1;
  int    cid, sid;

  rbs_monitor_update("initiated",0,0);


  while (run_loop < max_reloop) {

    run_loop++;

    /*
    ** When relooping, let some time for things to get repaired magicaly
    */
    if (run_loop != 1) {

      rbs_monitor_display();  
   
      if (max_reloop == -1) {
        REBUILD_MSG("Rebuild failed ! Attempt #%u will start in %d minutes", run_loop, delay);
      }
      else {
        REBUILD_MSG("Rebuild failed ! Attempt #%u/%u will start in %d minutes", run_loop, max_reloop, delay);      
      }	
      sleep(delay * 60);       
      if (delay < 60) delay = 2 *delay;
    }

    /*
    ** Let's process the clusters one after the other
    */
    for (rbs_index = 0; rbs_index < nb_rbs_entry; rbs_index++) {

      cid = stor_confs[rbs_index].cid;
      sid = stor_confs[rbs_index].sid;

      /* 
      ** Depending on the rebuild status
      */
      switch(stor_confs[rbs_index].status) {

	/*
	** The list of rebuilding jobs is not yet done for this sid
	*/
	case RBS_STATUS_BUILD_JOB_LIST:

          rbs_monitor_update("started",cid, sid);	    
          REBUILD_MSG("Start rebuild process (cid=%u;sid=%u).",cid, sid);

	  result = rbs_build_job_lists(stor_confs[rbs_index].export_hostname, 
	        		     storaged_geosite,
                		     cid, sid, stor_confs[rbs_index].root,
				     stor_confs[rbs_index].device.total,
				     stor_confs[rbs_index].device.mapper, 
				     stor_confs[rbs_index].device.redundancy, 
				     rbs_device_number,
				     storaged_config_file,
				     fid2rebuild);   
	  /*
	  ** Failure
	  */
          if (result == -1) {
            rbs_monitor_update("failed",cid, sid);
	    REBUILD_MSG("cid %d sid %d building list failed.", cid, sid);	      
	    continue; /* Process next sid */
	  }

	  /*
	  ** Abort
	  */	    
	  if (result < 0) {
            // ABort 
            rbs_monitor_update("abort",cid, sid);	      
            REBUILD_MSG("cid %d sid %d building list abort.",cid, sid);
	    stor_confs[rbs_index].status = RBS_STATUS_ABORTED;
	    continue;				
	  }


	/*
	** Try or retry to rebuild the job list
	*/ 
	case RBS_STATUS_PROCESSING_LIST:
        case RBS_STATUS_FAILED:

          rbs_monitor_update("running",cid, sid);
	  stor_confs[rbs_index].status = RBS_STATUS_PROCESSING_LIST;	      	    

	  result = 0;
	  if (rbs_monitor[rbs_index].nb_files != 0) {
	    result = rbs_do_list_rebuild(cid, sid);
	  }

	  /*
	  ** Failure
	  */
          if (result == -1) {
            rbs_monitor_update("failed",cid, sid);
	    REBUILD_MSG("cid %d sid %d Rebuild failed.", cid, sid);
	    stor_confs[rbs_index].status = RBS_STATUS_FAILED;
	    continue;	      
	  }

	  /*
	  ** Success 
	  */
          if (result == 0) {
            rbs_monitor_update("success",cid, sid);
	    REBUILD_MSG("cid %d sid %d Rebuild success.", cid, sid);
	    stor_confs[rbs_index].status = RBS_STATUS_SUCCESS;
	    /*
	    ** Remove cid/sid directory 
	    */
            rmdir(get_rebuild_sid_directory_name(cid,sid));
	    /*
	    ** Send reload signal. This will clear the error counters
	    */
	    send_reload_to_storio(cid);
	    continue;	      
	  }

	  /*
	  ** Abortion
	  */
          rbs_monitor_update("aborted",cid, sid);
	  REBUILD_MSG("cid %d sid %d Rebuild aborted.", cid, sid);
          stor_confs[rbs_index].status = RBS_STATUS_ABORTED;	    
	  continue;	      

	default:
	  continue;
      }
    }

    /*
    ** Check whether some reloop is to be done
    */
    for (rbs_index = 0; rbs_index < nb_rbs_entry; rbs_index++) {
      if ((stor_confs[rbs_index].status != RBS_STATUS_ABORTED)
      &&  (stor_confs[rbs_index].status != RBS_STATUS_SUCCESS)) break;
    }
    if (rbs_index == nb_rbs_entry) {
      /*
      ** Everything is finished 
      */
      rbs_monitor_update("completed",0,0);
      return;
    }

    rbs_monitor_update("failed",0,0);
  }  	    

  rbs_monitor_update("aborted",0,0);
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
static inline void rbs_process_initialize() {
    list_t *p = NULL;
    int found_cid_sid=0;
    int ret;

    rbs_stor_config_t rbs_stor_configs[STORAGES_MAX_BY_STORAGE_NODE] ;
    memset(&rbs_stor_configs, 0,
            STORAGES_MAX_BY_STORAGE_NODE * sizeof(rbs_stor_config_t));

    DEBUG_FUNCTION;
    

    // For each storage in configuration file

    list_for_each_forward(p, &storaged_config.storages) {

        storage_config_t *sc = list_entry(p, storage_config_t, list);
	
	/*
	** If a specific sid is to be rebuilt, skip the other
	*/
	if ((cid!=-1)&&(sid!=-1)) {
	  if ((cid != sc->cid) || (sid != sc->sid)) continue; 
	}
	found_cid_sid = 1;
	
	/*
	** If a specific device is to be rebuilt, checkt its number is valid
	*/
	if ((rbs_device_number >= 0)&&(rbs_device_number >= sc->device.total)) {
	  continue;
	} 

        // Copy the configuration for the storage to rebuild
        strncpy(rbs_stor_configs[nb_rbs_entry].export_hostname, rbs_export_hostname,
        ROZOFS_HOSTNAME_MAX);
        rbs_stor_configs[nb_rbs_entry].cid = sc->cid;
        rbs_stor_configs[nb_rbs_entry].sid = sc->sid;
        rbs_stor_configs[nb_rbs_entry].stor_idx = nb_rbs_entry;
	rbs_stor_configs[nb_rbs_entry].device.total      = sc->device.total;
	rbs_stor_configs[nb_rbs_entry].device.mapper     = sc->device.mapper;
	rbs_stor_configs[nb_rbs_entry].device.redundancy = sc->device.redundancy;
	rbs_stor_configs[nb_rbs_entry].status            = RBS_STATUS_BUILD_JOB_LIST;

        strncpy(rbs_stor_configs[nb_rbs_entry].root, sc->root, PATH_MAX);

        rbs_monitor[nb_rbs_entry].cid        = sc->cid;
        rbs_monitor[nb_rbs_entry].sid        = sc->sid;
        rbs_monitor[nb_rbs_entry].nb_files   = 0;	
        rbs_monitor[nb_rbs_entry].done_files = 0;
	rbs_monitor[nb_rbs_entry].layout     = 0xFF;
	
	nb_rbs_entry++;
    }

    if (nb_rbs_entry==0) {
      if (found_cid_sid == 0) {
        REBUILD_FAILED("No such cid/sid %d/%d.",cid,sid);          
      }
      else if (rbs_device_number >= 0) {
        REBUILD_FAILED("No such device number %d.",rbs_device_number); 
      }	 
      goto out;  
    }


    /*
    ** Create a temporary directory to receive the list files
    */  
    ret = mkdir(get_rebuild_directory_name(),ROZOFS_ST_BINS_FILE_MODE);
    if (ret != 0) {
      severe("Can not create directory %s : %s",get_rebuild_directory_name(), strerror(errno));
      goto out;
    }   

    /*
    ** Process to the rebuild
    */    
    rebuild_storage_thread(rbs_stor_configs);
    rbs_monitor_display();  

    /*
    ** Remove temporary directory
    */
    rmdir(get_rebuild_directory_name());

out:

    /*
    ** Purge excedent old rebuild result files
    */
    rbs_monitor_purge();
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
    printf("   -c, --config=config-file  \tSpecify config file to use (optional)\n");
    printf("                             \t(default: %s).\n",STORAGED_DEFAULT_CONFIG);
    printf("   -r, --rebuild=exportd-host\tlist of \'/\' separated host where exportd is running (mandatory)\n");
    printf("   -d, --device=device-number\tDevice number to rebuild.\n");
    printf("                             \tAll devices are rebuilt when omitted.\n");
    printf("   -s, --sid=<cid/sid>       \tCluster and storage identifier to rebuild.\n");
    printf("                             \tAll <cid/sid> are rebuilt when omitted.\n");
    printf("   -f, --fid=<FID>           \tSpecify one FID to rebuild. -s must also be set.\n");
    printf("   -p, --parallel            \tNumber of rebuild processes in parallel per cid/sid\n");
    printf("                             \t(default is %d, maximum is %d)\n",DEFAULT_PARALLEL_REBUILD_PER_SID,MAXIMUM_PARALLEL_REBUILD_PER_SID);   
    printf("   -g, --geosite             \tTo force site number in case of geo-replication\n");
    printf("   -R, --relocate             \tTo force site number in case of geo-replication\n");
    printf("   -l, --loop                 \tNumber of reloop in case of error (default infinite)\n");
    printf("\ne.g\n");
    printf("Rebuilding a whole storage node as fast as possible:\n");
    printf("storage_rebuild -r 192.168.0.201/192.168.0.202 -p %d\n\n",MAXIMUM_PARALLEL_REBUILD_PER_SID);
    printf("Rebuilding every devices of sid 2 of cluster 1:\n");
    printf("storage_rebuild -r 192.168.0.201/192.168.0.202 -s 1/2\n\n");
    printf("Rebuilding only device 3 of sid 2 of cluster 1:\n");
    printf("storage_rebuild -r 192.168.0.201/192.168.0.202 -s 1/2 -d 3\n\n");
    printf("Rebuilding by relocating device 3 of sid 2 of cluster 1 on other devices:\n");
    printf("storage_rebuild -r 192.168.0.201/192.168.0.202 -s 1/2 -d 3 -R\n\n");
    exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[]) {
    int c;
    int ret;
    
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
        { "relocate", no_argument, 0, 'R'},
        { "loop", required_argument, 0, 'l'},
        { 0, 0, 0, 0}
    };
    
    
    if (argc < 2) {
      usage();
    }  

    // Init of the timer configuration
    rozofs_tmr_init_configuration();
    storaged_hostname = NULL;
    
    storaged_geosite = rozofs_get_local_site();
    if (storaged_geosite == -1) {
      storaged_geosite = 0;
    }

    openlog("RBS", LOG_PID, LOG_DAEMON);

    while (1) {

        int option_index = 0;
        c = getopt_long(argc, argv, "hc:r:d:H:s:f:p:g:l:R", long_options, &option_index);

        if (c == -1)
            break;

        switch (c) {

            case 'h':
                usage();
                exit(EXIT_SUCCESS);
                break;
            case 'c':
                if (!realpath(optarg, storaged_config_file)) {
                    REBUILD_FAILED("No such configuration file %s.",optarg);
                    exit(EXIT_FAILURE);
                }
                break;
            case 'r':
                if (strncpy(rbs_export_hostname, optarg, ROZOFS_HOSTNAME_MAX)
                        == NULL) {
                    REBUILD_FAILED("Bad host name %s.", optarg);
                    exit(EXIT_FAILURE);
                }
                rbs_start_process = 1;
                break;
            case 's':
                {
		  int ret;
		  ret = sscanf(optarg,"%d/%d",&cid,&sid);
		  if (ret != 2) {
		    REBUILD_FAILED("-s option requires also cid/sid.\n");
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
		    REBUILD_FAILED("Bad FID format %s.", optarg);
                    exit(EXIT_FAILURE);
                  }
		  rbs_device_number = -2; // To tell one FID to rebuild 
                }
                break;
            case 'l':
	        {
		  int ret;
                  ret = sscanf(optarg,"%u", &max_reloop);
                  if (ret <= 0) { 
                      REBUILD_FAILED("Bad loop number %s.", optarg);
                      exit(EXIT_FAILURE);
                  }
		}
                break;									
            case 'd':
	        {
		  int ret;
                  ret = sscanf(optarg,"%d", &rbs_device_number);
                  if (ret <= 0) { 
                      REBUILD_FAILED("Bad device number %s.", optarg);
                      exit(EXIT_FAILURE);
                  }
		}
                break;			
            case 'g':
	        {
		  int ret;
                  ret = sscanf(optarg,"%d", &storaged_geosite);
                  if (ret <= 0) { 
                      REBUILD_FAILED("Bad site number %s.", optarg);
                      exit(EXIT_FAILURE);
                  }
		  if ((storaged_geosite!=0)&&(storaged_geosite!=1)) { 
                      REBUILD_FAILED("Site number must be within [0:1] instead of %s.", optarg);
                      exit(EXIT_FAILURE);
                  }
		}
                break;
	    case 'p':
	        {
		  int ret;

		  ret = sscanf(optarg,"%d", &parallel);
                  if (ret <= 0) { 
                      REBUILD_FAILED("Bad --parallel value %s.", optarg);
                      exit(EXIT_FAILURE);
                  }
		  if (parallel > MAXIMUM_PARALLEL_REBUILD_PER_SID) {
                      REBUILD_MSG("--parallel value is too big %d. Assume maximum parallel value of %d\n", parallel, MAXIMUM_PARALLEL_REBUILD_PER_SID);
		      parallel = MAXIMUM_PARALLEL_REBUILD_PER_SID;
                  }
		}
                break;
	    case 'R':
	        {
		  /* Relocation is required */
                  relocate = 1;
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
    
    {
      char * p = command;
      int i;
      
      for (i=0; i< argc; i++) p += sprintf(p, "%s ", argv[i]);
      info("%s",command);
    }
    
    /*
    ** Check parameter consistency
    */
    if (rbs_start_process == 0){
        REBUILD_FAILED("Missing manadtory option --rebuild");    
        exit(EXIT_FAILURE);      
    } 
    /*
    ** When FID is given, eid and cid/sid is mandatory
    */ 
    if (fid2rebuild_string) {
      if ((cid==-1)&&(sid==-1)) {
        REBUILD_FAILED("--fid option requires --sid option too.");
        exit(EXIT_FAILURE);      
      }
    }
    /*
    ** When relocate is set, cid/sid and device are mandatory 
    */
    if (relocate) {
      if ((cid==-1)&&(sid==-1)) {
        REBUILD_FAILED("--relocate option requires --sid option too.");
        exit(EXIT_FAILURE);      
      }
      if (rbs_device_number<0) {
        REBUILD_FAILED("--relocate option requires --device option too.");
        exit(EXIT_FAILURE);      
      }
    }

    // Initialize the list of storage config
    if (sconfig_initialize(&storaged_config) != 0) {
        REBUILD_FAILED("Can't initialize storaged config.");
        goto error;
    }
    // Read the configuration file
    if (cid == -1) {
      ret = sconfig_read(&storaged_config, storaged_config_file,0);
    }
    else {
      ret = sconfig_read(&storaged_config, storaged_config_file,cid);    
    }  
    if (ret < 0) {
        REBUILD_FAILED("Failed to parse storage configuration file %s.",storaged_config_file);
        goto error;
    }
    // Check the configuration
    if (sconfig_validate(&storaged_config) != 0) {
        REBUILD_FAILED("Inconsistent storage configuration file %s.",storaged_config_file);
        goto error;
    }
    // Check rebuild storage configuration if necessary
    if (rbs_check() != 0) goto error;


    // Initialization of the storage configuration
    if (storaged_initialize() != 0) {
        REBUILD_FAILED("Can't initialize storaged.");
        goto error;
    }
    
    // Start rebuild storage   
    rbs_process_initialize();
    on_stop();
    
    exit(0);
error:
    REBUILD_MSG("Can't start storage_rebuild. See logs for more details.");
    exit(EXIT_FAILURE);
}
