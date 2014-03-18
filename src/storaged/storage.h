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

#ifndef _STORAGE_H
#define _STORAGE_H

#include <stdint.h>
#include <limits.h>
#include <uuid/uuid.h>
#include <sys/param.h>
#include <unistd.h>

#include <rozofs/rozofs.h>
#include <rozofs/rozofs_srv.h>
#include <rozofs/common/list.h>
#include <rozofs/common/htable.h>

#define ROZOFS_MAX_DISK_THREADS  16

/* Storage config to be configured in cfg file */
#define STORAGE_MAX_DEVICE_NB   64
#define STORAGE_NB_DEVICE       6
#define STORAGE_NB_MAPPER       4
#define STORAGE_NB_MAPPER_RED   2


/** Maximum size in bytes for the header of file bins */
#define ROZOFS_ST_BINS_FILE_HDR_SIZE 8192

/** Default open flags to use for oprn without creation */
#define ROZOFS_ST_NO_CREATE_FILE_FLAG O_RDWR | O_NOATIME

/** Default open flags to use for open bins files */
#define ROZOFS_ST_BINS_FILE_FLAG O_RDWR | O_CREAT | O_NOATIME

/** Default mode to use for open bins files */
#define ROZOFS_ST_BINS_FILE_MODE S_IFREG | S_IRUSR | S_IWUSR
#define ROZOFS_ST_BINS_FILE_MODE_RW S_IFREG | S_IRUSR | S_IWUSR
#define ROZOFS_ST_BINS_FILE_MODE_RO S_IFREG | S_IRUSR

/** Default mode to use for create subdirectories */
#define ROZOFS_ST_DIR_MODE S_IRUSR | S_IWUSR | S_IXUSR

#define MAX_REBUILD_ENTRIES 60

#define STORIO_PID_FILE "storio"
#define TIME_BETWEEN_2_RB_ATTEMPS 30

/*
** Structure used to monitor device errors
*/
typedef struct _storage_device_errors_t {
  int      reset;   // a reset of the counters has been required
  int      active;  // active set of blocks
  uint32_t total[STORAGE_MAX_DEVICE_NB];
  uint32_t errors[2][STORAGE_MAX_DEVICE_NB];
} storage_device_errors_t;


/*
** Structure used to help allocating a device for a new file
*/
typedef struct _storage_device_free_blocks_t {
  int      active;  // active set of blocks
  uint64_t blocks[2][STORAGE_MAX_DEVICE_NB];
} storage_device_free_blocks_t;


/** Directory used to store bins files for a specific storage ID*/
typedef struct storage {
    sid_t sid; ///< unique id of this storage for one cluster
    cid_t cid; //< unique id of cluster that owns this storage
    char root[FILENAME_MAX]; ///< absolute path.
    uint32_t mapper_modulo; // Last device number that contains the fid to device mapping
    uint32_t device_number; // Number of devices to receive the data for this sid
    uint32_t mapper_redundancy; // Mapping file redundancy level
    storage_device_free_blocks_t device_free;    // available blocks on devices
    storage_device_errors_t      device_errors;  // To monitor errors on device
} storage_t;

/**
 *  Header structure for one file bins
 */
typedef struct rozofs_stor_bins_file_hdr {
    uint8_t layout; ///< layout used for this file.
    sid_t dist_set_current[ROZOFS_SAFE_MAX]; ///< currents sids of storage nodes target for this file.
    sid_t dist_set_next[ROZOFS_SAFE_MAX]; ///< next sids of storage nodes target for this. file (not used yet)
    uint8_t version; ///<  version of rozofs. (not used yet)
    uint32_t device_id; // Device number that hold the data
    fid_t fid;
} rozofs_stor_bins_file_hdr_t;




typedef struct bins_file_rebuild {
    fid_t fid;
    uint8_t layout; ///< layout used for this file.
    sid_t dist_set_current[ROZOFS_SAFE_MAX]; ///< currents sids of storage nodes target for this file.
    struct bins_file_rebuild *next;
} bins_file_rebuild_t;



/*
** Structures of the FID to rebuild file
*/

// File header
typedef struct _rozofs_rebuild_header_file_t {
  char          config_file[PATH_MAX];
  char          export_hostname[ROZOFS_HOSTNAME_MAX];
  storage_t     storage;
} rozofs_rebuild_header_file_t;
 
typedef struct _rozofs_rebuild_entry_file_t {
    fid_t fid; ///< unique file identifier associated with the file
    uint8_t layout; ///< layout used for this file.
    uint8_t todo:1;  
    uint8_t unlink:1;  
    sid_t dist_set_current[ROZOFS_SAFE_MAX]; ///< currents sids of storage nodes
} rozofs_rebuild_entry_file_t; 

/**
 *  Get the next storage 
 *
 *  @param st: NULL for getfirst, else getnext
 *  @return : the first or the next storage or NULL
 */
storage_t *storaged_next(storage_t * st);


/** API to be called when an error occurs on a device
 *
 * @param st: the storage to be initialized.
 * @param device_nb: device number
 *
 */
int storage_error_on_device(storage_t * st, int device_nb);

/** API to be called periodically to monitor errors on a period
 *
 * @param st: the storage to be initialized.
 *
 * @return a bitmask of the device having encountered an error
 */
static inline uint64_t storage_periodic_error_on_device_monitoring(storage_t * st) {
  int dev;
  uint64_t bitmask = 0;  
  int old_active = st->device_errors.active;
  int new_active = 1 - old_active;
  
  
  for (dev = 0; dev < STORAGE_MAX_DEVICE_NB; dev++) {   
    st->device_errors.errors[new_active][dev] = 0;    
  }  
  st->device_errors.active = new_active;
 
  
  for (dev = 0; dev < STORAGE_MAX_DEVICE_NB; dev++) {    
    st->device_errors.total[dev] = st->device_errors.total[dev] + st->device_errors.errors[old_active][dev];
    bitmask |= (1<<dev);
  }  
  return bitmask;
}
/** API to be called to reset error counters
 *
 *
 */
static inline void storage_device_mapping_reset_error_counters() {
  storage_t * st;    
  
  st = NULL;
  
  while ((st=storaged_next(st)) != NULL) {
    st->device_errors.reset = 1; 
  }
}




/** Transform a distribution set into a string
 *
 * @param st: the storage to be initialized.
 * @param layout: layout used for store this file.
 * @param dist_set: storages nodes used for store this file.
 * @param spare: indicator on the status of the projection.
 * @param path: the directory path.
 *
 * @return: the directory path
 */
static inline char * storage_dist_set_2_string(int layout, sid_t * dist_set, char * str) {
    char    * pt    = str;
    uint8_t   loop  = rozofs_get_rozofs_safe(layout) - 1;   
    int       i;
                 
    for (i = 0; i < loop; i++) pt += sprintf(pt, "%.3u-", dist_set[i]);
    pt += sprintf(pt, "%.3u/", dist_set[i]);
    
    return pt;
}

/** Compute the 
 *
 * @param fid: FID of the file
 * @param modulo: number of mapper devices
 * @param rank: rank of the mapper device within (0..modulo)
 *
 * @return: the device number to hold the mapper file of this FID/nb
 */
static inline int storage_mapper_device(fid_t fid, int rank, int modulo) {
  return (fid[2]+rank) % modulo;
} 

/*
 ** Write a header/mapper file on a device

  @param path : pointer to the bdirectory where to write the header file
  @param hdr : header to write in the file
  
  @retval 0 on sucess. -1 on failure
  
 */
int storage_write_header_file(storage_t * st,int device, char * path, rozofs_stor_bins_file_hdr_t * hdr);
 
/** Get the directory path for a given [storage, layout, dist_set, spare]
 *
 * @param st: the storage to be initialized.
 * @param layout: layout used for store this file.
 * @param dist_set: storages nodes used for store this file.
 * @param spare: indicator on the status of the projection.
 * @param path: the directory path.
 *
 * @return: the directory path
 */
char *storage_map_distribution(storage_t * st, uint8_t layout,
        sid_t dist_set[ROZOFS_SAFE_MAX], uint8_t spare, char *path);
	
	
/** Get the directory path for a given [storage, layout, dist_set, spare]
 *
 * @param read_operation: from DEVICE_MAP_OPERATION_E
 * @param st: the storage to be initialized.
 * @param device_id: input current device id or -1 when unkown
 *                   output chossen device id or -1 on error
 * @param fid: the fid of the file 
 * @param layout: layout used for store this file.
 * @param dist_set: storages nodes used for store this file.
 * @param spare: indicator on the status of the projection.
 * @param path: the directory path 
 * @param version: the version of the header file 
 *
 * @return: the directory path or NULL on case of error
 */
typedef enum device_map_operation_e {
  DEVICE_MAP_SEARCH_CREATE = 0,
  DEVICE_MAP_SEARCH_ONLY,
} DEVICE_MAP_OPERATION_E;

char *storage_dev_map_distribution(DEVICE_MAP_OPERATION_E read_operation, 
                                    storage_t * st, 
				    int * device_id, 
				    fid_t fid, 
				    uint8_t layout,
                                    sid_t dist_set[ROZOFS_SAFE_MAX], 
				    uint8_t spare, 
				    char *path, 
				    int version);
	
/** Add the fid bins file to a given path
 *
 * @param fid: unique file id.
 * @param path: the directory path.
 *
 * @return: the directory path
 */
char *storage_map_projection(fid_t fid, char *path);

/** Initialize a storage
 *
 * @param st: the storage to be initialized.
 * @param cid: unique id of cluster that owns this storage.
 * @param sid: the unique id for this storage.
 * @param root: the absolute path.
 * @param device_number: number of device for data storage
 * @param mapper_modulo: number of device for device mapping
 * @param mapper_redundancy: number of mapping device
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
int storage_initialize(storage_t *st, cid_t cid, sid_t sid, const char *root,
                       uint32_t device_number, uint32_t mapper_modulo, uint32_t mapper_redundancy);

/** Release a storage
 *
 * @param st: the storage to be released.
 */
void storage_release(storage_t * st);

/** Write nb_proj projections
 *
 * @param st: the storage to use.
 * @param device_id: device_id holding FID or -1 when unknown
 * @param layout: layout used for store this file.
 * @param dist_set: storages nodes used for store this file.
 * @param spare: indicator on the status of the projection.
 * @param fid: unique file id.
 * @param bid: first block idx (offset).
 * @param nb_proj: nb of projections to write.
 * @param version: version of rozofs used by the client. (not used yet)
 * @param *file_size: size of file after the write operation.
 * @param *bins: bins to store.
 * @param *is_fid_faulty: returns whether a fault is localized in the file
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
int storage_write(storage_t * st, int * device_id, uint8_t layout, sid_t * dist_set,
        uint8_t spare, fid_t fid, bid_t bid, uint32_t nb_proj, uint8_t version,
        uint64_t *file_size, const bin_t * bins, int * is_fid_faulty);

/** Read nb_proj projections
 *
 * @param st: the storage to use.
 * @param device_id: device_id holding FID or -1 when unknown
 * @param layout: layout used by this file.
 * @param dist_set: storages nodes used for store this file.
 * @param spare: indicator on the status of the projection.
 * @param fid: unique file id.
 * @param bid: first block idx (offset).
 * @param nb_proj: nb of projections to read.
 * @param *bins: bins to store.
 * @param *len_read: the length read.
 * @param *file_size: size of file after the read operation.
 * @param *is_fid_faulty: returns whether a fault is localized in the file
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
int storage_read(storage_t * st, int * device_id, uint8_t layout, sid_t * dist_set,
        uint8_t spare, fid_t fid, bid_t bid, uint32_t nb_proj,
        bin_t * bins, size_t * len_read, uint64_t *file_size,int * is_fid_faulty);

/** Truncate a bins file (not used yet)
 *
 * @param st: the storage to use.
 * @param device_id: device_id holding FID or -1 when unknown
 * @param layout: layout used by this file.
 * @param dist_set: storages nodes used for store this file.
 * @param spare: indicator on the status of the projection.
 * @param fid: unique file id.
 * @param proj_id: the projection id.
 * @param bid: first block idx (offset).
 * @param last_seg: length of the last segment if not modulo prj. size
 * @param last_timestamp: timestamp to associate with the last_seg
 * @param len: the len to writen in the last segment
 * @param data: the data of the last segment to write
 * @param is_fid_faulty: returns whether a fault is localized in the file
 
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
int storage_truncate(storage_t * st, int * device_id, uint8_t layout, sid_t * dist_set,
        uint8_t spare, fid_t fid, tid_t proj_id,bid_t bid,uint8_t version,
         uint16_t last_seg,uint64_t last_timestamp,u_int len, char * data,int * is_fid_faulty);

/** Remove a bins file
 *
 * @param st: the storage to use.
 * @param layout: layout used by this file.
 * @param dist_set: storages nodes used for store this file.
 * @param fid: unique file id.
 * @param version: version of rozofs used by the client. (not used yet)
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
int storage_rm_file(storage_t * st, uint8_t layout, sid_t * dist_set,
        fid_t fid);

/** Stat a storage
 *
 * @param st: the storage to use.
 * @param sstat: structure to use for store stats about this storage.
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
int storage_stat(storage_t * st, sstat_t * sstat);


int storage_list_bins_files_to_rebuild(storage_t * st, sid_t sid,  uint8_t * device_id,
        uint8_t * layout, sid_t *dist_set, uint8_t * spare, uint64_t * cookie,
        bins_file_rebuild_t ** children, uint8_t * eof);

/*
 ** Build the path for the projection file
  @param fid: unique file identifier
  @param path : pointer to the buffer where reuslting path will be stored
  
  @retval pointer to the beginning of the path
  
 */
static inline char *storage_map_projection_hdr(fid_t fid, char *path) {
    char str[37];

    uuid_unparse(fid, str);
    strcat(path, str);
    sprintf(str, ".hdr");
    strcat(path, str);
    return path;
}
/** Remove header files from disk
 *
 * @param st: the storage to use.
 * @param fid: FID of the file
 * @param layout: layout of the file
 * @param dist_set: distribution set of the file
 * @param spare: whether this is a spare sid
*
 * @return: 0 on success -1 otherwise (errno is set)
 */	
void static inline storage_dev_map_distribution_remove(storage_t * st, fid_t fid, uint8_t layout,
                                          sid_t dist_set[ROZOFS_SAFE_MAX], uint8_t spare) {
    char                      dist_set_string[FILENAME_MAX];
    char                      path[FILENAME_MAX];
    int                       dev;
    int                       hdrDevice;

    DEBUG_FUNCTION;

    /*
    ** Pre-format distribution string
    */
    storage_dist_set_2_string(layout, dist_set, dist_set_string);
  

   /*
   ** Loop on the reduncant devices that should hold a copy of the mapping file
   */
   for (dev=0; dev < st->mapper_redundancy ; dev++) {

       hdrDevice = storage_mapper_device(fid,dev,st->mapper_modulo);	
       sprintf(path, "%s/%d/layout_%u/spare_%u/%s", st->root, hdrDevice, layout, spare, dist_set_string);            
       storage_map_projection_hdr(fid,path);

       // Check that the file exists
       if (access(path, F_OK) == -1) continue;

       // The file exist, let's remove it
       if (unlink(path) < 0) {
	   severe("unlink %s - %s", path, strerror(errno));
       }
   }
}
#endif

