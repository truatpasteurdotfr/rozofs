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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <rozofs/rozofs.h>
#include <rozofs/rozofs_srv.h>
#include <rozofs/common/list.h>
#include <rozofs/common/htable.h>

#define ROZOFS_MAX_DISK_THREADS  32

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
#define TIME_BETWEEN_2_RB_ATTEMPS 60



/*
** Structure used to monitor device errors
*/
typedef struct _storage_device_errors_t {
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



typedef enum _storage_device_status_e {
  storage_device_status_undeclared=0,
  storage_device_status_init,
  storage_device_status_is,
  storage_device_status_degraded,
  storage_device_status_relocating,
  storage_device_status_failed,
  storage_device_status_oos
} storage_device_status_e;

static inline char * storage_device_status2string(storage_device_status_e status) {
  switch(status) {
    case storage_device_status_undeclared: return "NONE";
    case storage_device_status_init:       return "INIT";
    case storage_device_status_is:         return "IS";
    case storage_device_status_degraded:    return "DEG";
    case storage_device_status_relocating: return "RELOC";
    case storage_device_status_failed:     return "FAILED";
    case storage_device_status_oos:        return "OOS";
    default:                               return "???";
  }
}

typedef struct _storage_device_info_t {
  storage_device_status_e    status;
  uint32_t                   padding;
  uint64_t                   free;
  uint64_t                   size;    
} storage_device_info_t;

typedef struct _storage_device_info_cache_t {
  time_t                 time;
  int                    nb_dev;
  storage_device_info_t  device[STORAGE_MAX_DEVICE_NB];
} storage_device_info_cache_t;

#define STORAGE_DEVICE_NO_ACTION      0
#define STORAGE_DEVICE_RESET_ERRORS   1
#define STORAGE_DEVICE_REINIT         2
typedef struct _storage_device_ctx_t {
  storage_device_status_e     status;
  uint64_t                    failure;
  uint8_t                     action;
} storage_device_ctx_t;


/** Directory used to store bins files for a specific storage ID*/
typedef struct storage {
    sid_t sid; ///< unique id of this storage for one cluster
    cid_t cid; //< unique id of cluster that owns this storage
    char root[FILENAME_MAX]; ///< absolute path.
   uint64_t  crc_error;   ///> CRC32C error counter
    uint32_t mapper_modulo; // Last device number that contains the fid to device mapping
    uint32_t device_number; // Number of devices to receive the data for this sid
    uint32_t mapper_redundancy; // Mapping file redundancy level
    int      selfHealing;
    char  *  export_hosts; /* For self healing purpose */
    storage_device_free_blocks_t device_free;    // available blocks on devices
    storage_device_errors_t      device_errors;  // To monitor errors on device
    storage_device_ctx_t         device_ctx[STORAGE_MAX_DEVICE_NB];  
    storage_device_info_cache_t *device_info_cache;             
} storage_t;

/**
 *  Header structure for one file bins
 */
 

//__Specific values in the chunk to device array
// Used in the interface When the FID is not inserted in the cache and so the
// header file will have to be read from disk.
#define ROZOFS_UNKNOWN_CHUNK  255
// Used in the device per chunk array when no device has been allocated 
// because the chunk is after the end of file
#define ROZOFS_EOF_CHUNK      254
// Used in the device per chunk array when no device has been allocated 
// because it is included in a whole of the file
#define ROZOFS_EMPTY_CHUNK    253
 
typedef struct rozofs_stor_bins_file_hdr_no_crc32 {
    uint8_t version; ///<  version of rozofs. (not used yet)
    uint8_t layout; ///< layout used for this file.
    uint8_t bsize;  ///< Block size as defined in enum ROZOFS_BSIZE_E
    fid_t   fid;
    sid_t   dist_set_current[ROZOFS_SAFE_MAX]; ///< currents sids of storage nodes target for this file.
    uint8_t device[ROZOFS_STORAGE_MAX_CHUNK_PER_FILE]; // Device number that hold the chunk of projection
} rozofs_stor_bins_file_hdr_no_crc32_t;

typedef struct rozofs_stor_bins_file_hdr {
    uint8_t version; ///<  version of rozofs. (not used yet)
    uint8_t layout; ///< layout used for this file.
    uint8_t bsize;  ///< Block size as defined in enum ROZOFS_BSIZE_E
    fid_t   fid;
    sid_t   dist_set_current[ROZOFS_SAFE_MAX]; ///< currents sids of storage nodes target for this file.
    uint8_t device[ROZOFS_STORAGE_MAX_CHUNK_PER_FILE]; // Device number that hold the chunk of projection
    uint32_t crc32; ///< CRC32 . Set to 0 by default when no CRC32 is computed
} rozofs_stor_bins_file_hdr_t;




typedef struct bins_file_rebuild {
    fid_t fid;
    uint8_t layout; ///< layout used for this file.
    uint8_t bsize; ///< Block size as defined in ROZOFS_BSIZE_E
    sid_t dist_set_current[ROZOFS_SAFE_MAX]; ///< currents sids of storage nodes target for this file.
    struct bins_file_rebuild *next;
} bins_file_rebuild_t;



/*
** Structures of the FID to rebuild file
*/
typedef struct  _rozofs_rbs_counters_t {
  uint32_t      done_files;
  uint64_t      written;
  uint64_t      written_spare; 
  uint64_t      read;
  uint64_t      read_spare; 
} ROZOFS_RBS_COUNTERS_T;

// File header
typedef struct _rozofs_rebuild_header_file_t {
  ROZOFS_RBS_COUNTERS_T counters;
  char          config_file[PATH_MAX];
  char          export_hostname[ROZOFS_HOSTNAME_MAX];
  int           site;
  storage_t     storage;
  uint8_t       layout;
  uint8_t       device;
} rozofs_rebuild_header_file_t;
 
typedef struct _rozofs_rebuild_entry_file_t {
    fid_t fid; ///< unique file identifier associated with the file
    uint8_t layout; ///< layout used for this file.
    uint8_t bsize;
    uint8_t todo:1;  
    uint8_t relocate:1;  
    sid_t dist_set_current[ROZOFS_SAFE_MAX]; ///< currents sids of storage nodes
    uint32_t  block_start; // Starting block to rebuild from 
    uint32_t  block_end;   // Last block to rebuild
} rozofs_rebuild_entry_file_t; 

#if 0
#define MYDBGTRACE(fmt,...) {\
    char MyString[256];\
    char trace_path[128];\
    char fid_string[37];\
    FILE * myfd;\
    uuid_unparse(fid, fid_string);\
    sprintf(MyString,"\n%s "fmt,fid_string,__VA_ARGS__);\
    sprintf(trace_path,"/tmp/trace_cid%d_sid%d",st->cid,st->sid);\
    myfd = fopen(trace_path, "a");\
    if (myfd != NULL) {\
      fwrite(MyString,strlen(MyString),1,myfd);\
      fclose(myfd);\
    }\
  }
#define MYDBGTRACE_DEV(device,fmt,...) {\
    char MyString[512];\
    char MyDev[256];\
    char * ptChar= MyDev;\
    char trace_path[128];\
    char fid_string[37];\
    FILE * myfd;\
    int idx;\
    for (idx=0; idx<ROZOFS_STORAGE_MAX_CHUNK_PER_FILE;idx++) {\
      if (device[idx] == ROZOFS_UNKNOWN_CHUNK) {\
        ptChar += sprintf(ptChar,"/?/ ");\
        break;\
      }\
      if (device[idx] == ROZOFS_EOF_CHUNK) {\
        ptChar += sprintf(ptChar,"/ ");\
        break;\
      }\
      if (device[idx] == ROZOFS_EMPTY_CHUNK)\
        ptChar += sprintf(ptChar,"/E");\
      else\
        ptChar += sprintf(ptChar,"/%d",idx,device[idx]);\
    }\
    uuid_unparse(fid, fid_string);\
    sprintf(MyString,"\n%s %s"fmt,fid_string,MyDev,__VA_ARGS__);\
    sprintf(trace_path,"/tmp/trace_cid%d_sid%d",st->cid,st->sid);\
    myfd = fopen(trace_path, "a");\
    if (myfd != NULL) {\
      fwrite(MyString,strlen(MyString),1,myfd);\
      fclose(myfd);\
    }\
  }  
#else  
#define MYDBGTRACE(fmt,...)
#define MYDBGTRACE_DEV(device,fmt,...)
#endif 

int storage_write_device_status(char * root, storage_device_info_t * info, int nbElement);
int storage_read_device_status(char * root, storage_device_info_t * info);

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
int storage_error_on_device(storage_t * st, uint8_t device_nb);

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
static inline char * trace_device(uint8_t * device, char * pChar) {
  int  idx;
      
  for (idx=0; idx<ROZOFS_STORAGE_MAX_CHUNK_PER_FILE;idx++) {
    if (device[idx] == ROZOFS_UNKNOWN_CHUNK) {
      pChar += sprintf(pChar,"?");
      break; 
    }   
    if (device[idx] == ROZOFS_EOF_CHUNK) break;
    if (device[idx] == ROZOFS_EMPTY_CHUNK) 
      pChar += sprintf(pChar,"/E");
    else
      pChar += sprintf(pChar,"/%d",device[idx]);
  }
  pChar += sprintf(pChar,"/");
  return pChar;
} 


/*
** FID storage slice computing
*/
#define FID_STORAGE_SLICE_SIZE 8
static inline unsigned int rozofs_storage_fid_slice(void * fid) {
#if FID_STORAGE_SLICE_SIZE == 1
  return 0;
#else  
  rozofs_inode_t *fake_inode = (rozofs_inode_t *) fid;
  return fake_inode->s.usr_id % FID_STORAGE_SLICE_SIZE;
} 
#endif
/*
** Build a path on storage disk
**
** @param path       where to write the path
** @param root_path  The cid/sid root path
** @param device     The device to access
** @param spare      Whether this is a path to a spare file
** @param slice      the storage slice number
*/
static inline void storage_build_hdr_path(char * path, 
                               char * root_path, 
			       uint32_t device, 
			       uint8_t spare, 
			       int     slice) {	       
#if FID_STORAGE_SLICE_SIZE == 1
   sprintf(path, "%s/%d/hdr_%u/", root_path, device, spare);  
#else 
    sprintf(path, "%s/%d/hdr_%u/%d/", root_path, device, spare, slice);  
#endif 
}     
/*
** Build a path on storage disk
**
** @param path       where to write the path
** @param root_path  The cid/sid root path
** @param device     The device to access
** @param spare      Whether this is a path to a spare file
** @param slice      the storage slice number
*/
static inline int storage_build_bins_path(char * path, 
                               char * root_path, 
			       uint32_t device, 
			       uint8_t spare, 
			       int     slice) {
#if FID_STORAGE_SLICE_SIZE == 1
   return sprintf(path, "%s/%d/bins_%u/", root_path, device, spare);  
#else 			       
   return sprintf(path, "%s/%d/bins_%u/%d/", root_path, device, spare, slice);  
#endif   
} 
/** Remove a chunk of data without modifying the header file 
 *
 * @param st: the storage where the data file resides
 * @param device: device where the data file resides
 * @param fid: the fid of the file 
 * @param spare: wheteher this is a spare file
 * @param chunk: The chunk number that has to be removed
 * @param errlog: whether an log is to be send on error
 */
int storage_rm_data_chunk(storage_t * st, uint8_t device, fid_t fid, uint8_t spare, uint8_t chunk, int errlog) ;

/** Restore a chunk of data as it was before the relocation attempt 
 *
 * @param st: the storage where the data file resides
 * @param device: device where the data file resides
 * @param fid: the fid of the file 
 * @param spare: wheteher this is a spare file
 * @param chunk: The chunk number that has to be removed
 * @param old_device: previous device to be restored
 */
int storage_restore_chunk(storage_t * st, uint8_t * device,fid_t fid, uint8_t spare, 
                           uint8_t chunk, uint8_t old_device);
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
int storage_write_header_file(storage_t * st,int dev, char * path, rozofs_stor_bins_file_hdr_t * hdr);
/*
 ** Write all header/mapper files on a storage

  @param path : pointer to the bdirectory where to write the header file
  @param hdr : header to write in the file
  
  @retval The number of header file that have been written successfuly
  
 */
int storage_write_all_header_files(storage_t * st, fid_t fid, uint8_t spare, rozofs_stor_bins_file_hdr_t * hdr);
 

/** Get the directory path for a given [storage, layout, dist_set, spare]
 *
 * @param st: the storage to be initialized.
 * @param device_id: input current device id or -1 when unkown
 *                   output chossen device id or -1 on error
 * @param chunk: The chunk number that has to be mapped 
 * @param bsize: the block size as defined in ROZOFS_BSIZE_E 
 * @param fid: the fid of the file 
 * @param layout: layout used for store this file.
 * @param dist_set: storages nodes used for store this file.
 * @param spare: indicator on the status of the projection.
 * @param path: the directory path 
 * @param version: the version of the header file 
 *
 * @return: the directory path or NULL on case of error
 */

char *storage_dev_map_distribution_write( 
                                    storage_t * st, 
				    uint8_t * device_id,
				    uint8_t chunk,
				    uint32_t bsize, 
				    fid_t fid, 
				    uint8_t layout,
                                    sid_t dist_set[ROZOFS_SAFE_MAX], 
				    uint8_t spare, 
				    char *path, 
				    int version);
				    
char *storage_dev_map_distribution_read(  storage_t * st, 
					  uint8_t * device_id,
				           uint8_t chunk,					  
					  fid_t fid, 
					  uint8_t spare, 
					  char *path);	
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
 * @param selfHealing The delay in min before repairing a device
 *                    -1 when no self-healing
 * @param export_hosts The export hosts list for self healing purpose 
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
int storage_initialize(storage_t *st, cid_t cid, sid_t sid, const char *root,
                       uint32_t device_number, uint32_t mapper_modulo, uint32_t mapper_redundancy,
		       int selfHealing, char * export_hosts);

/** Release a storage
 *
 * @param st: the storage to be released.
 */
void storage_release(storage_t * st);

/** Write nb_proj projections
 *
 * @param st: the storage to use.
 * @param device: Array of device allocated for the 128 chunks
 * @param layout: layout used for store this file.
 * @param bsize: Block size from enum ROZOFS_BSIZE_E
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

int storage_write_chunk(storage_t * st, uint8_t * device_id, uint8_t layout, uint32_t bsize, sid_t * dist_set,
        uint8_t spare, fid_t fid, uint8_t chunk, bid_t bid, uint32_t nb_proj, uint8_t version,
        uint64_t *file_size, const bin_t * bins, int * is_fid_faulty);
	 
static inline int storage_write(storage_t * st, uint8_t * device, uint8_t layout, uint32_t bsize, sid_t * dist_set,
        uint8_t spare, fid_t fid, bid_t input_bid, uint32_t input_nb_proj, uint8_t version,
        uint64_t *file_size, const bin_t * bins, int * is_fid_faulty) {
    int ret1,ret2;
    bid_t      bid;
    uint32_t   nb_proj;
    char     * pBins;
    
    MYDBGTRACE_DEV(device,"write bid %d nb %d",input_bid,input_nb_proj);
    int block_per_chunk         = ROZOFS_STORAGE_NB_BLOCK_PER_CHUNK(bsize);
    int chunk                   = input_bid/block_per_chunk;

    
    if (chunk>=ROZOFS_STORAGE_MAX_CHUNK_PER_FILE) { 
      errno = EFBIG;
      return -1;
    }  
    
    bid = input_bid - (chunk * block_per_chunk);
           
    if ((bid+input_nb_proj) <= block_per_chunk){ 
      /*
      ** Every block can be written in one time in the same chunk
      */
      ret1 = storage_write_chunk(st, device, layout, bsize, dist_set,
        			 spare, fid, chunk, bid, input_nb_proj, version,
        			 file_size, bins, is_fid_faulty);
      if (ret1 == -1) {
        MYDBGTRACE("write errno %s",strerror(errno));			     
      }
      return ret1;	 
    }  

    /* 
    ** We have to write two chunks
    */ 
    
    if ((chunk+1)>=ROZOFS_STORAGE_MAX_CHUNK_PER_FILE) { 
      errno = EFBIG;
      return -1;
    }        
    
    // 1rst chunk
    nb_proj = block_per_chunk-bid;
    pBins = (char *) bins;    
    ret1 = storage_write_chunk(st, device, layout, bsize, dist_set,
        		      spare, fid, chunk, bid, nb_proj, version,
        		      file_size, (bin_t*)pBins, is_fid_faulty); 
    if (ret1 == -1) {
      MYDBGTRACE("write errno %s",strerror(errno));
      return -1;			     
    }
	    
      
    // 2nd chunk
    chunk++;         
    bid     = 0;
    nb_proj = input_nb_proj - nb_proj;
    pBins += ret1;  
    ret2 = storage_write_chunk(st, device, layout, bsize, dist_set,
        		      spare, fid, chunk, bid, nb_proj, version,
        		      file_size, (bin_t*)pBins, is_fid_faulty); 
    if (ret2 == -1) {
      MYDBGTRACE("write errno %s",strerror(errno));
      return -1;			     
    }
    
    return ret2+ret1;
}
/** Write nb_proj projections
 *
 * @param st: the storage to use.
 * @param device: Array of device allocated for the 128 chunks
 * @param layout: layout used for store this file.
 * @param bsize: Block size from enum ROZOFS_BSIZE_E
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

int storage_write_repair_chunk(storage_t * st, uint8_t * device, uint8_t layout, uint32_t bsize, sid_t * dist_set,
        uint8_t spare, fid_t fid, uint8_t chunk, bid_t bid, uint32_t nb_proj, uint64_t bitmap, uint8_t version,
        uint64_t *file_size, const bin_t * bins, int * is_fid_faulty);

static inline int storage_write_repair(storage_t * st, uint8_t * device, uint8_t layout, uint32_t bsize, sid_t * dist_set,
        uint8_t spare, fid_t fid, bid_t input_bid, uint32_t input_nb_proj, uint64_t bitmap, uint8_t version,
        uint64_t *file_size, const bin_t * bins, int * is_fid_faulty) {
    int ret1,ret2;
    bid_t      bid;
    uint32_t   nb_proj;
    char     * pBins;
    
    int block_per_chunk         = ROZOFS_STORAGE_NB_BLOCK_PER_CHUNK(bsize);
    int chunk                   = input_bid/block_per_chunk;

    
    if (chunk>=ROZOFS_STORAGE_MAX_CHUNK_PER_FILE) { 
      errno = EFBIG;
      return -1;
    }  
    
    bid = input_bid - (chunk * block_per_chunk);
           
    if ((bid+input_nb_proj) <= block_per_chunk){ 
      /*
      ** Every block can be written in one time in the same chunk
      */
      ret1 = storage_write_repair_chunk(st, device, layout, bsize, dist_set,
        			 spare, fid, chunk, bid, input_nb_proj,bitmap, version,
        			 file_size, bins, is_fid_faulty);
      if (ret1 == -1) {
        MYDBGTRACE("write errno %s",strerror(errno));			     
      }
      return ret1;	 
    }  

    /* 
    ** We have to write two chunks
    */ 
    
    if ((chunk+1)>=ROZOFS_STORAGE_MAX_CHUNK_PER_FILE) { 
      errno = EFBIG;
      return -1;
    }        
    
    // 1rst chunk
    nb_proj = block_per_chunk-bid;
    pBins = (char *) bins;    
    ret1 = storage_write_repair_chunk(st, device, layout, bsize, dist_set,
        			 spare, fid, chunk, bid, nb_proj, bitmap, version,
        			 file_size, (bin_t*)pBins, is_fid_faulty);    
    if (ret1 == -1) {
      MYDBGTRACE("write errno %s",strerror(errno));
      return -1;			     
    }
	    
      
    // 2nd chunk
    chunk++;         
    bid     = 0;
    nb_proj = input_nb_proj - nb_proj;
    bitmap = bitmap>>nb_proj;
    pBins += ret1;  
    ret2 = storage_write_repair_chunk(st, device, layout, bsize, dist_set,
        			 spare, fid, chunk, bid, nb_proj, bitmap, version,
        			 file_size, (bin_t*)pBins, is_fid_faulty);    
    if (ret2 == -1) {
      MYDBGTRACE("write errno %s",strerror(errno));
      return -1;			     
    }
    
    return ret2+ret1;
}

/** Read nb_proj projections
 *
 * @param st: the storage to use.
 * @param device: Array of device allocated for the 128 chunks
 * @param layout: layout used by this file.
 * @param bsize: Block size from enum ROZOFS_BSIZE_E
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
int storage_read_chunk(storage_t * st, uint8_t * device, uint8_t layout, uint32_t bsize, sid_t * dist_set,
        uint8_t spare, fid_t fid, uint8_t chunk, bid_t bid, uint32_t nb_proj,
        bin_t * bins, size_t * len_read, uint64_t *file_size,int * is_fid_faulty) ;
static inline int storage_read(storage_t * st, uint8_t * device, uint8_t layout, uint32_t bsize, sid_t * dist_set,
        uint8_t spare, fid_t fid, bid_t input_bid, uint32_t input_nb_proj,
        bin_t * bins, size_t * len_read, uint64_t *file_size,int * is_fid_faulty) {
    bid_t    bid;
    uint32_t nb_proj;
    char * pBins = (char *) bins;
    size_t len_read1,len_read2;
    int    ret1,ret2;

    MYDBGTRACE_DEV(device,"read bid %d nb %d",input_bid,input_nb_proj);

    *len_read = len_read1 = len_read2 = 0;       
    int block_per_chunk         = ROZOFS_STORAGE_NB_BLOCK_PER_CHUNK(bsize);
    int chunk                   = input_bid/block_per_chunk;
    
    if (chunk>=ROZOFS_STORAGE_MAX_CHUNK_PER_FILE) {  
      return 0;
    }        
    
    bid = input_bid - (chunk * block_per_chunk);
    if ((bid+input_nb_proj) <= block_per_chunk){ 
      /*
      ** All the blocks can be read in one time in the same chunk
      */
      ret1 = storage_read_chunk(st, device, layout, bsize, dist_set,
        			spare, fid, chunk, bid, input_nb_proj,
        			bins, len_read, file_size,is_fid_faulty);
      if (ret1 == -1) {
        MYDBGTRACE("read error len %d errno %s",*len_read,strerror(errno));			     
        return -1;
      }
      /*
      ** When this chunk is not the last one and we have read less than requested
      ** one has to pad with 0 the missing data (whole in file)
      */
      if (*len_read < ret1) {
        chunk++;
        if (chunk<ROZOFS_STORAGE_MAX_CHUNK_PER_FILE) {          
          if (device[chunk] != ROZOFS_EOF_CHUNK) {
	    pBins += *len_read;
	    memset(pBins,0,ret1-*len_read);
	    *len_read = ret1;
	  }  
        }
      }	      
      MYDBGTRACE("read success len %d",*len_read);			           	
      return 0;				  
    }  


    /* 
    ** We have to read from two chunks
    */   
    
    /*
    *_____ 1rst chunk
    */
    nb_proj = (block_per_chunk-bid);

    ret1 = storage_read_chunk(st, device, layout, bsize, dist_set,
        		      spare, fid, chunk, bid, nb_proj,
        		      (bin_t*) pBins, &len_read1, file_size,is_fid_faulty);
    if (ret1 == -1) {
      MYDBGTRACE("read error len %d errno %s",*len_read,strerror(errno));			     		    
      return -1;
    }

    /*
    ** Is there a next chunk ?
    */
    chunk++;
    if (chunk>=ROZOFS_STORAGE_MAX_CHUNK_PER_FILE) {
      *len_read = len_read1;
      MYDBGTRACE("read success len %d",*len_read);			           	
      return 0;	
    }
    
    if (device[chunk] == ROZOFS_EOF_CHUNK) {
      *len_read = len_read1;
      MYDBGTRACE("read success len %d",*len_read);			           	
      return 0;	
    }
    
        
    /*
    ** When this chunk is not the last one and we have read less than requested
    ** one has to pad with 0 the missing data (whole in file)
    */
    if (len_read1 < ret1) {
      pBins += len_read1;
      memset(pBins,0,ret1-len_read1);
      len_read1 = ret1;
    }
          
    /*
    *_____ 2nd chunk
    */
    bid = 0;
    nb_proj = input_nb_proj - nb_proj;  
    pBins = (char *) bins;
    pBins += ret1;  
    ret2 = storage_read_chunk(st, device, layout, bsize, dist_set,
        		      spare, fid, chunk, bid, nb_proj,
        		      (bin_t*) pBins, &len_read2, file_size,is_fid_faulty);
    if (ret2 == -1) {
      MYDBGTRACE("read error len %d errno %s",*len_read,strerror(errno));			     		    
      return -1;
    }     

    /*
    ** Is there a next chunk ?
    */
    chunk++;
    if (chunk>=ROZOFS_STORAGE_MAX_CHUNK_PER_FILE) {
      *len_read = len_read1 + len_read2;
      MYDBGTRACE("read success len %d",*len_read);			           	
      return 0;	
    }
    
    if (device[chunk] == ROZOFS_EOF_CHUNK) {
      *len_read = len_read1 + len_read2;
      MYDBGTRACE("read success len %d",*len_read);			           	
      return 0;	
    }
            
    /*
    ** When this chunk is not the last one and we have read less than requested
    ** one has to pad with 0 the missing data (whole in file)
    */
    if (len_read2 < ret2) {
      pBins += len_read2;
      memset(pBins,0,ret2-len_read2);
      len_read2 = ret2;
    }    
    *len_read = len_read1 + len_read2;
    MYDBGTRACE("read success len %d",*len_read);			           	
    return 0;

}
/** Relocate a chunk on a new device in a process of rebuild. 
 *  This just consist in changing in the file distribution the 
 *  chunk to empty, but not removing the data in order to be 
 *  able to restore it later when the rebuild fails...
 * 
 * @param st: the storage to use.
 * @param device: Array of device allocated for the 128 chunks
 * @param fid: unique file id.
 * @param spare: indicator on the status of the projection.
 * @param chunk: the chunk that is to be rebuilt with relocate
 * @param old_device: to return the old device value 
 * 
 * @return: 0 on success -1 otherwise (errno is set)
 */
int storage_relocate_chunk(storage_t * st, uint8_t * device,fid_t fid, uint8_t spare, 
                           uint8_t chunk, uint8_t * old_device);
			   
/** Truncate a bins file (not used yet)
 *
 * @param st: the storage to use.
 * @param device: Array of device allocated for the 128 chunks
 * @param layout: layout used by this file.
 * @param bsize: Block size from enum ROZOFS_BSIZE_E
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
int storage_truncate(storage_t * st, uint8_t * device, uint8_t layout, uint32_t bsize, sid_t * dist_set,
        uint8_t spare, fid_t fid, tid_t proj_id,bid_t bid,uint8_t version,
         uint16_t last_seg,uint64_t last_timestamp,u_int len, char * data,int * is_fid_faulty);

/** Remove a bins file
 *
 * @param st: the storage to use.
 * @param fid: unique file id.
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
int storage_rm_file(storage_t * st, fid_t fid);

/** Stat a storage
 *
 * @param st: the storage to use.
 * @param sstat: structure to use for store stats about this storage.
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
int storage_stat(storage_t * st, sstat_t * sstat);


int storage_list_bins_files_to_rebuild(storage_t * st, sid_t sid,  uint8_t * device_id,
        uint8_t * spare, uint16_t * slice, uint64_t * cookie,
        bins_file_rebuild_t ** children, uint8_t * eof);

/*
 ** Build the path for the projection file
  @param fid: unique file identifier
  @param path : pointer to the buffer where resulting path will be stored
  
  @retval pointer to the beginning of the path
  
 */
static inline void storage_complete_path_with_fid(fid_t fid, char *path) {
    int len = strlen(path);
    uuid_unparse(fid, &path[len]);
}
/*
 ** Add/replace a chunk number at end of the path
    
    When the path string finishes with "-xxx" replace "xxx" 
    with the given chunk value else add "-xxx" at the end of the string
 
  @param chunk: chunk number
  @param path : pointer to the buffer where resulting path will be stored
  
  @retval pointer to the beginning of the path
  
 */
static inline void storage_complete_path_with_chunk(int chunk, char *path) {
    int len = strlen(path);
    char * pt;
    
    pt = path+len-4;
    if (*pt != '-') pt += 4;
    sprintf(pt,"-%3.3d", chunk);
}
/*
 ** Create a directory if it does not yet exist
  @param path : path toward the directory
  
  @retval 0 on success
  
 */
static inline int storage_create_dir(char *path) {

  /* Directory exist */
  if (access(path, F_OK) == 0) return 0;
  
  /* Unhandled error on directory access */
  if (errno != ENOENT) return -1;
  
  /* The directory doesn't exist, let's create it */
  if (mkdir(path, ROZOFS_ST_DIR_MODE) == 0) return 0;
  
  /* Someone else has just created the directory */ 
  if (errno == EEXIST) return 0;		

  /* Unhandled error on directory creation */
  return -1;
}  
/** Remove header files from disk
 *
 * @param st: the storage to use.
 * @param fid: FID of the file
 * @param dist_set: distribution set of the file
 * @param spare: whether this is a spare sid
*
 * @return: 0 on success -1 otherwise (errno is set)
 */	
void static inline storage_dev_map_distribution_remove(storage_t * st, fid_t fid, uint8_t spare) {
    char                      path[FILENAME_MAX];
    int                       dev;
    int                       hdrDevice;

    DEBUG_FUNCTION;
 
    /*
    ** Compute storage slice from FID
    */
    int storage_slice = rozofs_storage_fid_slice(fid);

   /*
   ** Loop on the reduncant devices that should hold a copy of the mapping file
   */
   for (dev=0; dev < st->mapper_redundancy ; dev++) {

       hdrDevice = storage_mapper_device(fid,dev,st->mapper_modulo);	
       storage_build_hdr_path(path, st->root, hdrDevice, spare, storage_slice);

       storage_complete_path_with_fid(fid,path);

       // Check that the file exists
       if (access(path, F_OK) == -1) continue;

       // The file exist, let's remove it
       unlink(path);
   }
}
/*
 ** Read a header/mapper file

  @param path : pointer to the bdirectory where to write the header file
  @param hdr : header to write in the file
  
  @retval  STORAGE_READ_HDR_ERRORS     on failure
  @retval  STORAGE_READ_HDR_NOT_FOUND  when header file does not exist
  @retval  STORAGE_READ_HDR_OK         when header file has been read
  
 */
typedef enum { 
  STORAGE_READ_HDR_OK,
  STORAGE_READ_HDR_NOT_FOUND,
  STORAGE_READ_HDR_ERRORS    
} STORAGE_READ_HDR_RESULT_E;

STORAGE_READ_HDR_RESULT_E storage_read_header_file(storage_t * st, fid_t fid, uint8_t spare, rozofs_stor_bins_file_hdr_t * hdr);

int storage_rm_chunk(storage_t * st, uint8_t * device, 
                     uint8_t layout, uint8_t bsize, uint8_t spare, 
		     sid_t * dist_set, fid_t fid, 
		     uint8_t chunk, int * is_fid_faulty);
#endif

