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

#define _XOPEN_SOURCE 500
#define STORAGE_C

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <inttypes.h>
#include <glob.h>
#include <fnmatch.h>
#include <dirent.h>

#include <rozofs/rozofs.h>
#include <rozofs/common/log.h>
#include <rozofs/common/list.h>
#include <rozofs/common/xmalloc.h>
#include <rozofs/rozofs_srv.h>
//#include <rozofs/core/rozofs_optim.h>
#include "storio_cache.h"
#include "storio_bufcache.h"
#include "storio_device_mapping.h"
#include "storio_device_mapping.h"
#include "storio_crc32.h"

/*
** API to be called when an error occurs on a device
 *
 * @param st: the storage to be initialized.
 * @param device_nb: device number
 *
 */
int storage_error_on_device(storage_t * st, uint8_t device_nb) {

  if ((st == NULL) || (device_nb >= STORAGE_MAX_DEVICE_NB)) return 0;     
    
  int active = st->device_errors.active;
    
  // Since several threads can call this API at the same time
  // some count may be lost...
  st->device_errors.errors[active][device_nb]++;
  return st->device_errors.errors[active][device_nb];
}

/*
 ** Read a header/mapper file
    This function looks for a header file of the given FID on every
    device when it should reside on this storage.

  @param st    : storage we are looking on
  @param fid   : fid whose hader file we are looking for
  @param spare : whether this storage is spare for this FID
  @param hdr   : where to return the read header file
  
  @retval  STORAGE_READ_HDR_ERRORS     on failure
  @retval  STORAGE_READ_HDR_NOT_FOUND  when header file does not exist
  @retval  STORAGE_READ_HDR_OK         when header file has been read
  
*/
STORAGE_READ_HDR_RESULT_E storage_read_header_file(storage_t * st, fid_t fid, uint8_t spare, rozofs_stor_bins_file_hdr_t * hdr) {
  int  dev;
  int  hdrDevice;
  char path[FILENAME_MAX];
  int  storage_slice;
  int  fd;
  int  nb_read;
  int       device_result[STORAGE_MAX_DEVICE_NB];
  uint64_t  device_time[STORAGE_MAX_DEVICE_NB];
  uint64_t  device_id[STORAGE_MAX_DEVICE_NB];
  uint64_t  swap_time;
  int       swap_device;
  int       nb_devices=0;
  struct stat buf;
  int       idx;
  int       ret;
  
  memset(device_time,0,sizeof(device_time));
  memset(device_id,0,sizeof(device_id));
  memset(device_result,0,sizeof(device_result));
  
  /*
  ** Compute storage slice from FID
  */
  storage_slice = rozofs_storage_fid_slice(fid);    
 
  /*
  ** Search for the last updated file.
  ** It may occur that a file can not be written any more although 
  ** it can still be read, so we better read the lastest file writen
  ** on disk to be sure to get the correct information.
  */
  for (dev=0; dev < st->mapper_redundancy ; dev++) {

    /*
    ** Header file path
    */
    hdrDevice = storage_mapper_device(fid,dev,st->mapper_modulo);
    storage_build_hdr_path(path, st->root, hdrDevice, spare, storage_slice);
	            
    // Check that this directory already exists, otherwise it will be create
    if (storage_create_dir(path) < 0) {
      device_result[dev] = errno;		    
      storage_error_on_device(st,hdrDevice);
      continue;
    }   

    /* 
    ** Fullfill the path with the name of the mapping file
    */
    storage_complete_path_with_fid(fid,path);

    /*
    ** Get the file attributes
    */
    ret = stat(path,&buf);
    if (ret < 0) {
      device_result[dev] = errno;
      continue;
    }
    
    /*
    ** Insert in the table in the time order
    */
    for (idx=0; idx < nb_devices; idx++) {
      if (device_time[idx] < buf.st_mtime) continue;
      break;
    }
    nb_devices++;
    for (; idx < nb_devices; idx++) {  
     
      swap_time   = device_time[idx];
      swap_device = device_id[idx];

      device_time[idx] = buf.st_mtime;
      device_id[idx]   = hdrDevice;

      buf.st_mtime = swap_time;
      hdrDevice    = swap_device;  
    } 
  }
  
  
  /*
  ** Header files do not exist
  */
  if (nb_devices == 0) {
    for (dev=0; dev < st->mapper_redundancy ; dev++) {
      if (device_result[dev] == ENOENT) return STORAGE_READ_HDR_NOT_FOUND;  
    } 
    /*
    ** All devices have problems
    */
    return STORAGE_READ_HDR_ERRORS; 
  }
  

  /*
  ** Look for the mapping information in one of the redundant mapping devices
  ** which numbers are derived from the fid
  */
  for (dev=0; dev < nb_devices ; dev++) {

    /*
    ** Header file name
    */
    storage_build_hdr_path(path, st->root, device_id[dev], spare, storage_slice);
    storage_complete_path_with_fid(fid,path);

    // Open hdr file
    fd = open(path, ROZOFS_ST_NO_CREATE_FILE_FLAG, ROZOFS_ST_BINS_FILE_MODE);
    if (fd < 0) {
      device_result[dev] = errno;	
      continue;
    }
    
    nb_read = pread(fd, hdr, sizeof (*hdr), 0);
    close(fd);

    if (nb_read != sizeof (*hdr)) {
      device_result[dev] = EINVAL;	
      storage_error_on_device(st,hdrDevice);
      continue;
    }
    return STORAGE_READ_HDR_OK;	
  }  
  
  /*
  ** All devices have problems
  */
  return STORAGE_READ_HDR_ERRORS;
}

/*
 ** Write a header/mapper file on a device

  @param path : pointer to the bdirectory where to write the header file
  @param hdr : header to write in the file
  
  @retval 0 on sucess. -1 on failure
  
 */
int storage_write_header_file(storage_t * st,int dev, char * path, rozofs_stor_bins_file_hdr_t * hdr) {
  size_t                    nb_write;
  int                       fd;
  char                      my_path[FILENAME_MAX];
  
  /*
  ** Create directory when needed */
  if (storage_create_dir(path) < 0) {   
    storage_error_on_device(st,dev);
    return -1;
  }   
      
  strcpy(my_path,path); // Not to modify input path
  storage_complete_path_with_fid(hdr->fid, my_path);  

  // Open bins file
  fd = open(my_path, ROZOFS_ST_BINS_FILE_FLAG, ROZOFS_ST_BINS_FILE_MODE);
  if (fd < 0) {	
    storage_error_on_device(st,dev);    
    return -1;
  }      

  // Write the header for this bins file
  nb_write = pwrite(fd, hdr, sizeof (*hdr), 0);
  close(fd);

  if (nb_write != sizeof (*hdr)) {
    storage_error_on_device(st,dev);  
    return -1;
  }
  return 0;
}  
/*
 ** Write a header/mapper file on every device
    This function writes the header file of the given FID on every
    device where it should reside on this storage.    
    
  @param st    : storage we are looking on
  @param fid   : fid whose hader file we are looking for
  @param spare : whether this storage is spare for this FID
  @param hdr   : the content to be written in header file
  
  @retval The number of header file that have been written successfuly
  
 */
int storage_write_all_header_files(storage_t * st, fid_t fid, uint8_t spare, rozofs_stor_bins_file_hdr_t * hdr) {
  int                       dev;
  int                       hdrDevice;
  int                       storage_slice;
  char                      path[FILENAME_MAX];
  int                       result=0;
  
  storage_slice = rozofs_storage_fid_slice(fid);
  

  for (dev=0; dev < st->mapper_redundancy ; dev++) {

    hdrDevice = storage_mapper_device(fid,dev,st->mapper_modulo);
    storage_build_hdr_path(path, st->root, hdrDevice, spare, storage_slice);
                 
    if (storage_write_header_file(st,hdrDevice,path, hdr) == 0) {    
      //MYDBGTRACE("Header written on storage %d/%d device %d", st->cid, st->sid, hdrDevice);
      result++;
    }
  }  
  return result;
} 
#if 0
/*
 ** Write a header/mapper file on a device

  @param path : pointer to the bdirectory where to write the header file
  @param hdr : header to write in the file
  
  @retval 0 on sucess. -1 on failure
  
 */
int storage_rewrite_one_devices(storage_t * st,fid_t fid,int dev, char * path, uint8_t * device) {
  size_t                    nb_write;
  int                       fd;
  char                      my_path[FILENAME_MAX];
  rozofs_stor_bins_file_hdr_t hdr;
  
  /*
  ** Create directory when needed */
  if (storage_create_dir(path) < 0) {   
    storage_error_on_device(st,dev);
    return -1;
  }   
      
  strcpy(my_path,path); // Not to modify input path
  storage_complete_path_with_fid(fid, my_path);  

  // Open bins file
  fd = open(my_path, ROZOFS_ST_BINS_FILE_FLAG, ROZOFS_ST_BINS_FILE_MODE);
  if (fd < 0) {	
    storage_error_on_device(st,dev);    
    return -1;
  }      

  // Write the header for this bins file
  nb_write = pwrite(fd, device, sizeof (hdr.device), ((char*)&hdr.device-(char*)&hdr));
  close(fd);

  if (nb_write != sizeof (hdr.device)) {
    storage_error_on_device(st,dev);  
    return -1;
  }
  return 0;
}  
/*
 ** Write a header/mapper file on every device
    This function writes the header file of the given FID on every
    device where it should reside on this storage.    
    
  @param st    : storage we are looking on
  @param fid   : fid whose hader file we are looking for
  @param spare : whether this storage is spare for this FID
  @param hdr   : the content to be written in header file
  
  @retval The number of header file that have been written successfuly
  
 */
int storage_rewrite_devices(storage_t * st, fid_t fid, uint8_t spare, uint8_t * device) {
  int                       dev;
  int                       hdrDevice;
  int                       storage_slice;
  char                      path[FILENAME_MAX];
  int                       result=0;
  
  storage_slice = rozofs_storage_fid_slice(fid);

  for (dev=0; dev < st->mapper_redundancy ; dev++) {

    hdrDevice = storage_mapper_device(fid,dev,st->mapper_modulo);
    storage_build_hdr_path(path, st->root, hdrDevice, spare, storage_slice);
                 
    if (storage_rewrite_one_devices(st,fid,hdrDevice,path, device) == 0) {    
      //MYDBGTRACE("Header written on storage %d/%d device %d", st->cid, st->sid, hdrDevice);
      result++;
    }
  }  
  return result;
} 
#endif
/*
 ** Find out the directory absolute path where to write the projection file in.
    
  @param st        : storage we are looking on
  @param device_id : the device_id the file resides on or -1 when unknown
  @param chunk     : chunk number to write
  @param fid       : FID of the file to write
  @param layout    : layout to use
  @param dist_set  : the file sid distribution set
  @param spare     : whether this storage is spare for this FID
  @param path      : The returned absolute path
  @param version   : current header file format version

  @retval The number of header file that have been written successfuly
  
 */	  
char *storage_dev_map_distribution_write(storage_t * st, 
					 uint8_t * device,
					 uint8_t chunk,
					 uint32_t bsize, 
					 fid_t fid, 
					 uint8_t layout,
                                	 sid_t dist_set[ROZOFS_SAFE_MAX], 
					 uint8_t spare, 
					 char *path, 
					 int version) {
    int                       result;
    rozofs_stor_bins_file_hdr_t file_hdr;
    int                       storage_slice;
    STORAGE_READ_HDR_RESULT_E read_hdr_res;
    
    DEBUG_FUNCTION;
    
    /*
    ** A real device is given as input, so use it
    */
    if ((device[chunk] != ROZOFS_EOF_CHUNK)&&(device[chunk] != ROZOFS_EMPTY_CHUNK)&&(device[chunk] != ROZOFS_UNKNOWN_CHUNK)) {
       goto success;
    }   

    /*
    ** When no device id is given as input, let's read the header file 
    */    
    read_hdr_res = storage_read_header_file(st, fid, spare, &file_hdr);
      
    /*
    ** Error accessing all the devices
    */
    if (read_hdr_res == STORAGE_READ_HDR_ERRORS) {
      return NULL;
    }
    
    /*
    ** Header file has been read
    */
    if (read_hdr_res == STORAGE_READ_HDR_OK) {
         
      /*
      ** A device is already allocated for this chunk.
      */
      if ((file_hdr.device[chunk] != ROZOFS_EOF_CHUNK)&&(file_hdr.device[chunk] != ROZOFS_EMPTY_CHUNK)) {
         memcpy(device,file_hdr.device,ROZOFS_STORAGE_MAX_CHUNK_PER_FILE);
	 goto success;
      }   
      
      /*
      ** We are extending the file
      */
      if (file_hdr.device[chunk] == ROZOFS_EOF_CHUNK) {
        /*
	** All previous chunks that where said EOF must be said EMPTY
	*/
        int idx;
	for (idx=0; idx <= chunk; idx++) {
	  if (file_hdr.device[idx] == ROZOFS_EOF_CHUNK) {
	    file_hdr.device[idx] = ROZOFS_EMPTY_CHUNK;
	  }
	}
      } 
      
    }    
      
    /*
    ** Header file does not exist. This is a brand new file
    */    
    if (read_hdr_res == STORAGE_READ_HDR_NOT_FOUND) {
      int idx;
      /*
      ** Prepare file header
      */
      memcpy(file_hdr.dist_set_current, dist_set, ROZOFS_SAFE_MAX * sizeof (sid_t));
      file_hdr.layout = layout;
      file_hdr.bsize  = bsize;
      file_hdr.version = version;
      memcpy(file_hdr.fid, fid,sizeof(fid_t)); 
      for (idx=0; idx <= chunk; idx++) {
	file_hdr.device[idx] = ROZOFS_EMPTY_CHUNK;
      }
      for (;idx<ROZOFS_STORAGE_MAX_CHUNK_PER_FILE; idx++) {
	file_hdr.device[idx] = ROZOFS_EOF_CHUNK;      
      }        
    }
    
      
    /*
    ** Allocate a device for this newly written chunk
    */
    file_hdr.device[chunk] = storio_device_mapping_allocate_device(st); 
    memcpy(device,file_hdr.device,ROZOFS_STORAGE_MAX_CHUNK_PER_FILE);
    
    //MYDBGTRACE("%d/%d Allocate device %d for chunk %d", st->cid, st->sid, device[chunk], chunk);
     

    /*
    ** Write the header files on disk 
    */
    result = storage_write_all_header_files(st, fid, spare, &file_hdr);        
    /*
    ** Failure on every write operation
    */ 
    if (result == 0) {
      /*
      ** Header file was not existing, so let's remove it from every
      ** device. The inode may have been created although the file
      ** data can not be written
      */
      if (read_hdr_res == STORAGE_READ_HDR_NOT_FOUND) {
        storage_dev_map_distribution_remove(st, fid, spare);
      }
      return NULL;
    }       
    
success: 
    storage_slice = rozofs_storage_fid_slice(fid);
    storage_build_bins_path(path, st->root, device[chunk], spare, storage_slice);
    return path;            
}

/*
** 
  @param device_number: number of device handled by the sid
  @param mapper_modulo: number of device to hold mapping file
  @param mapper_redundancy: number of mapping file instance to write per FID
**  
*/
int storage_initialize(storage_t *st, 
                       cid_t cid, 
		       sid_t sid, 
		       const char *root, 
                       uint32_t device_number, 
		       uint32_t mapper_modulo, 
		       uint32_t mapper_redundancy,
		       int      selfHealing,
		       char   * export_hosts) {
    int status = -1;
    char path[FILENAME_MAX];
    struct stat s;
    int dev;

    DEBUG_FUNCTION;

    if (!realpath(root, st->root))
        goto out;
	
    if (mapper_modulo > device_number) {
      severe("mapper_modulo is %d > device_number %d",mapper_modulo,device_number)
      goto out;
    }	
		
    if (mapper_redundancy > mapper_modulo) {
      severe("mapper_redundancy is %d > mapper_modulo %d",mapper_redundancy,mapper_modulo)
      goto out;
    }	
    
    st->mapper_modulo     = mapper_modulo;
    st->device_number     = device_number; 
    st->mapper_redundancy = mapper_redundancy;
    st->selfHealing       = selfHealing; 
    st->export_hosts      = export_hosts;
    st->device_info_cache = NULL;
    
    st->device_free.active = 0;
    for (dev=0; dev<STORAGE_MAX_DEVICE_NB; dev++) {
      st->device_free.blocks[0][dev] = 20000;
      st->device_free.blocks[1][dev] = 20000;
    }

    /*
    ** Initialize device status
    */
    for (dev=0; dev<device_number; dev++) {
      st->device_ctx[dev].status = storage_device_status_init;
      st->device_ctx[dev].failure = 0;
    }

    memset(&st->device_errors , 0,sizeof(st->device_errors));        
	    
    // sanity checks
    if (stat(st->root, &s) != 0) {
        severe("can not stat %s",st->root);
        goto out;
    }

    if (!S_ISDIR(s.st_mode)) {
        errno = ENOTDIR;
        goto out;
    }		
        	
    for (dev=0; dev < st->device_number; dev++) {		
	

	// sanity checks
	sprintf(path,"%s/%d",st->root,dev);
	if (stat(path, &s) != 0) {
            severe("can not stat %s",path);
	    continue;
	}
	    
	if (!S_ISDIR(s.st_mode)) {
            severe("can not stat %s",path);
            errno = ENOTDIR;
            goto out;
	}

        /*
	** Build 2nd level directories
	*/

	
        sprintf(path, "%s/%d/hdr_0", st->root, dev);
        if (access(path, F_OK) != 0) {
          if (storage_create_dir(path) < 0) {
            severe("%s creation %s",path, strerror(errno));
          }
#if FID_STORAGE_SLICE_SIZE != 1
	  int slice;
          char slicepath[FILENAME_MAX];
	  for (slice = 0; slice < FID_STORAGE_SLICE_SIZE; slice++) {
	    sprintf(slicepath,"%s/%d",path,slice);
            if (storage_create_dir(slicepath) < 0) {
              severe("%s creation %s",slicepath, strerror(errno));
            }	    
	  }
#endif	  
	}  

        sprintf(path, "%s/%d/hdr_1", st->root, dev);
        if (access(path, F_OK) != 0) {
          if (storage_create_dir(path) < 0) {
            severe("%s creation %s",path, strerror(errno));
          }	
#if FID_STORAGE_SLICE_SIZE != 1
	  int slice;
          char slicepath[FILENAME_MAX];
	  for (slice = 0; slice < FID_STORAGE_SLICE_SIZE; slice++) {
	    sprintf(slicepath,"%s/%d",path,slice);
            if (storage_create_dir(slicepath) < 0) {
              severe("%s creation %s",slicepath, strerror(errno));
            }	    
	  }
#endif
	} 
	
        sprintf(path, "%s/%d/bins_0", st->root, dev);
        if (access(path, F_OK) != 0) {
          if (storage_create_dir(path) < 0) {
            severe("%s creation %s",path, strerror(errno));
          }	
#if FID_STORAGE_SLICE_SIZE != 1
	  int slice;
          char slicepath[FILENAME_MAX];
	  for (slice = 0; slice < FID_STORAGE_SLICE_SIZE; slice++) {
	    sprintf(slicepath,"%s/%d",path,slice);
            if (storage_create_dir(slicepath) < 0) {
              severe("%s creation %s",slicepath, strerror(errno));
            }	    
	  }
#endif
	} 
	
        sprintf(path, "%s/%d/bins_1", st->root, dev);
        if (access(path, F_OK) != 0) {
          if (storage_create_dir(path) < 0) {
            severe("%s creation %s",path, strerror(errno));
          }	
#if FID_STORAGE_SLICE_SIZE != 1
	  int slice;
          char slicepath[FILENAME_MAX];
	  for (slice = 0; slice < FID_STORAGE_SLICE_SIZE; slice++) {
	    sprintf(slicepath,"%s/%d",path,slice);
            if (storage_create_dir(slicepath) < 0) {
              severe("%s creation %s",slicepath, strerror(errno));
            }	    
	  }
#endif	  
	} 	
    }
    st->sid = sid;
    st->cid = cid;

    status = 0;
out:
    return status;
}

void storage_release(storage_t * st) {

    DEBUG_FUNCTION;

    st->sid = 0;
    st->cid = 0;
    st->root[0] = 0;
    if (st->device_info_cache != NULL) free(st->device_info_cache);
    st->device_info_cache = NULL;

}
static inline void storage_get_projection_size(uint8_t spare, 
                                               sid_t sid, 
					       uint8_t layout, 
					       uint32_t bsize,
					       sid_t * dist_set,
					       uint16_t * msg,
				    	       uint16_t * disk) { 
  int prj_id;
  int idx;
  int safe;
  int forward;
  char mylog[128];
  char * p = mylog;
    
  /* Size of a block in a message received from the client */  
  *msg = rozofs_get_max_psize_in_msg(layout,bsize);
  
  /*
  ** On a spare storage, we store the projections as received.
  ** That is one block takes the maximum projection block size.
  */
  if (spare) {
    *disk = *msg;
    return;
  }
    
  /*
  ** On a non spare storage, we store the projections on its exact size.
  */
  
  forward = rozofs_get_rozofs_forward(layout);
  safe    = rozofs_get_rozofs_safe(layout);

  /* Retrieve the current sid in the given distribution */
  for (prj_id=0; prj_id < safe; prj_id++) {
    if (sid == dist_set[prj_id]) break;
  }
  
  /* The sid is within the forward 1rst sids : this is what we expected */
  if (prj_id < forward) {
    *disk = rozofs_get_psizes_on_disk(layout,bsize,prj_id);
    return;
  }	  

  /* This is abnormal. The sid is not supposed to be spare */
  
  p += sprintf(p," safe %d ", safe);
  for (idx=0; idx < safe; idx++) {
    p += sprintf(p,"/%d", dist_set[idx]);
  }    
  p += sprintf(p," storage_get_projection_size spare %d sid %d",spare, sid);

  if (prj_id < safe) {
    /* spare should have been set to 1 !? */
    severe("%s",mylog);
    *disk = *msg;
    return;
  }
  
  /* !!! sid not in the distribution !!!! */
  fatal("%s",mylog);	
}  
static inline void storage_get_projid_size(uint8_t spare, 
                                           uint8_t prj_id, 
					   uint8_t layout,
					   uint32_t bsize,
					   uint16_t * msg,
				    	   uint16_t * disk) { 

  *msg = rozofs_get_max_psize_in_msg(layout,bsize);
  
  /*
  ** On a spare storage, we store the projections as received.
  ** That is one block takes the maximum projection block size.
  */
  if (spare) {
    *disk = *msg;
    return;
  }
    
  /*
  ** On a non spare storage, we store the projections on its exact size.
  */
  *disk = rozofs_get_psizes_on_disk(layout,bsize,prj_id);		
} 
uint64_t buf_ts_storage_write[STORIO_CACHE_BCOUNT];


int storage_relocate_chunk(storage_t * st, uint8_t * device,fid_t fid, uint8_t spare, 
                           uint8_t chunk, uint8_t * old_device) {
    STORAGE_READ_HDR_RESULT_E      read_hdr_res;  
    rozofs_stor_bins_file_hdr_t    file_hdr;
    int                            result;

    /*
    ** Let's read the header file 
    */    
    read_hdr_res = storage_read_header_file(st, fid, spare, &file_hdr);

    /*
    ** Error accessing all the devices
    */
    if (read_hdr_res == STORAGE_READ_HDR_ERRORS) {
      severe("storage_relocate_chunk");
      return -1;
    }

    /*
    ** Header file does not exist! This is a brand new file
    */    
    if (read_hdr_res == STORAGE_READ_HDR_NOT_FOUND) { 
      memset(device,ROZOFS_UNKNOWN_CHUNK,ROZOFS_STORAGE_MAX_CHUNK_PER_FILE);
      *old_device = ROZOFS_EMPTY_CHUNK;
      return 0;
    }

    /*
    ** Header file has been read
    */
    
    /* Save the previous chunk location and then release it */
    *old_device = file_hdr.device[chunk];
    
    /* Last chunk ? */
    if (chunk == (ROZOFS_STORAGE_MAX_CHUNK_PER_FILE-1)) {
      file_hdr.device[chunk] = ROZOFS_EOF_CHUNK;
    }
    /* End of file ? */
    else if (file_hdr.device[chunk+1] == ROZOFS_EOF_CHUNK) {
      int idx;
      file_hdr.device[chunk] = ROZOFS_EOF_CHUNK;
      idx = chunk-1;
      /* Previous empty chunk is now end of file */
      while (idx>=0) {
        if (file_hdr.device[idx] != ROZOFS_EMPTY_CHUNK) break;
	file_hdr.device[idx] = ROZOFS_EOF_CHUNK;
	idx--;
      }
    }
    /* Inside the file */
    else {
      file_hdr.device[chunk] = ROZOFS_EMPTY_CHUNK;
    }  
    memcpy(device,file_hdr.device,ROZOFS_STORAGE_MAX_CHUNK_PER_FILE);

    /* 
    ** Rewrite file header on disk
    */   
    result = storage_write_all_header_files(st, fid, spare, &file_hdr);        
    /*
    ** Failure on every write operation
    */ 
    if (result == 0) return -1;   
    return 0;
}
int storage_rm_data_chunk(storage_t * st, uint8_t device, fid_t fid, uint8_t spare, uint8_t chunk, int errlog) {
  char path[FILENAME_MAX];
  int  ret;

  uint32_t storage_slice = rozofs_storage_fid_slice(fid);
  storage_build_bins_path(path, st->root, device, spare, storage_slice);
  storage_complete_path_with_fid(fid, path);
  storage_complete_path_with_chunk(chunk,path);

  ret = unlink(path);   
  if ((ret < 0) && (errno != ENOENT) && (errlog)) {
    severe("storage_rm_data_chunk(%s) %s", path, strerror(errno));
  }
  return ret;  
} 
int storage_restore_chunk(storage_t * st, uint8_t * device,fid_t fid, uint8_t spare, 
                           uint8_t chunk, uint8_t old_device) {
    STORAGE_READ_HDR_RESULT_E      read_hdr_res;  
    rozofs_stor_bins_file_hdr_t    file_hdr;
    int                            result;       
   
    /*
    ** Let's read the header file 
    */    
    read_hdr_res = storage_read_header_file(st, fid, spare, &file_hdr);

    /*
    ** Error accessing all the devices
    */
    if (read_hdr_res == STORAGE_READ_HDR_ERRORS) {
      severe("storage_relocate_chunk");
      return -1;
    }

    /*
    ** Header file does not exist! This is a brand new file
    */    
    if (read_hdr_res == STORAGE_READ_HDR_NOT_FOUND) { 
      memset(device,ROZOFS_UNKNOWN_CHUNK,ROZOFS_STORAGE_MAX_CHUNK_PER_FILE);
      return 0;
    }

    /*
    ** Header file has been read. 
    */
       
    
    /*
    ** Remove new data file which rebuild has failed 
    */
    storage_rm_data_chunk(st, file_hdr.device[chunk], fid, spare, chunk,0/* No errlog*/);        
    
    /*
    ** Restore device in header file
    */
    file_hdr.device[chunk] = old_device;
    if (old_device==ROZOFS_EOF_CHUNK) {
      /* not the last chunk */
      if ((chunk != (ROZOFS_STORAGE_MAX_CHUNK_PER_FILE-1))
      &&  (file_hdr.device[chunk+1] != ROZOFS_EOF_CHUNK)) {
	file_hdr.device[chunk] = ROZOFS_EMPTY_CHUNK;
      }
    }
    else if (old_device==ROZOFS_EMPTY_CHUNK) {  
      /* Last chunk */
      if ((chunk == (ROZOFS_STORAGE_MAX_CHUNK_PER_FILE-1))
      ||  (file_hdr.device[chunk+1] != ROZOFS_EOF_CHUNK)) {
	file_hdr.device[chunk] = ROZOFS_EOF_CHUNK;
      }   
    }
    if (file_hdr.device[chunk] == ROZOFS_EOF_CHUNK) {
      int idx = chunk-1;
      /* Previous empty chunk is now end of file */
      while (idx>=0) {
        if (file_hdr.device[idx] != ROZOFS_EMPTY_CHUNK) break;
	file_hdr.device[idx] = ROZOFS_EOF_CHUNK;
	idx--;
      }      
    }     
    memcpy(device,file_hdr.device,ROZOFS_STORAGE_MAX_CHUNK_PER_FILE);

    /* 
    ** Rewrite file header on disk
    */   
    result = storage_write_all_header_files(st, fid, spare, &file_hdr);        
    /*
    ** Failure on every write operation
    */ 
    if (result == 0) return -1;   
    return 0;
}
int storage_write_chunk(storage_t * st, uint8_t * device, uint8_t layout, uint32_t bsize, sid_t * dist_set,
        uint8_t spare, fid_t fid, uint8_t chunk, bid_t bid, uint32_t nb_proj, uint8_t version,
        uint64_t *file_size, const bin_t * bins, int * is_fid_faulty) {
    int status = -1;
    char path[FILENAME_MAX];
    int fd = -1;
    size_t nb_write = 0;
    size_t length_to_write = 0;
    off_t bins_file_offset = 0;
    uint16_t rozofs_msg_psize;
    uint16_t rozofs_disk_psize;
    struct stat sb;
    int open_flags;
    int    device_id_is_given;

    // No specific fault on this FID detected
    *is_fid_faulty = 0; 

    MYDBGTRACE_DEV(device,"%d/%d Write chunk %d : ", st->cid, st->sid, chunk);
   
open:    
    // If the device id is given as input, that proves that the file
    // has been existing with that name on this device sometimes ago. 
    if ((device[chunk] != ROZOFS_EOF_CHUNK)&&(device[chunk] != ROZOFS_EMPTY_CHUNK)&&(device[chunk] != ROZOFS_UNKNOWN_CHUNK)) {
      device_id_is_given = 1;
      open_flags = ROZOFS_ST_NO_CREATE_FILE_FLAG;
    }
    // The file location is not known. It may not exist and should be created 
    else {
      device_id_is_given = 0;
      open_flags = ROZOFS_ST_BINS_FILE_FLAG;
    }        
 
    // Build the full path of directory that contains the bins file
    if (storage_dev_map_distribution_write(st, device, chunk, bsize, 
                                          fid, layout, dist_set, 
					  spare, path, 0) == NULL) {
      goto out;      
    }  
    
    // Check that this directory already exists, otherwise it must be created
    if (storage_create_dir(path) < 0) {
      storage_error_on_device(st,device[chunk]);
      goto out;
    }

    // Build the path of bins file
    storage_complete_path_with_fid(fid, path);
    storage_complete_path_with_chunk(chunk,path);

    // Open bins file
    fd = open(path, open_flags, ROZOFS_ST_BINS_FILE_MODE);
    if (fd < 0) {
    
        // Something definitively wrong on device
        if (errno != ENOENT) {
	  storage_error_on_device(st,device[chunk]); 
	  goto out;
	}
	
        // If device id was not given as input, the file path has been deduced from 
	// the header files or should have been allocated. This is a definitive error !!!
	if (device_id_is_given == 0) {
	  storage_error_on_device(st,device[chunk]); 
	  goto out;
	}
	
	// The device id was given as input so the file did exist some day,
	// but the file may have been deleted without storio being aware of it.
	// Let's try to find the file again without using the given device id.
	device[chunk] = ROZOFS_EOF_CHUNK;
	goto open;    
    }

    /*
    ** Retrieve the projection size in the message
    ** and the projection size on disk 
    */
    storage_get_projection_size(spare, st->sid, layout, bsize, dist_set,
                                &rozofs_msg_psize, &rozofs_disk_psize); 
	       
    // Compute the offset and length to write
    
    bins_file_offset = bid * rozofs_disk_psize;
    length_to_write  = nb_proj * rozofs_disk_psize;

    //MYDBGTRACE("write %s bid %d nb %d",path,bid,nb_proj);

    /*
    ** Writting the projection as received directly on disk
    */
    if (rozofs_msg_psize == rozofs_disk_psize) {
    
      /*
      ** generate the crc32c for each projection block
      */
      storio_gen_crc32((char*)bins,nb_proj,rozofs_disk_psize);

      errno = 0;
      nb_write = pwrite(fd, bins, length_to_write, bins_file_offset);
    }

    /*
    ** Writing the projections on a different size on disk
    */
    else {
      struct iovec       vector[ROZOFS_MAX_BLOCK_PER_MSG+1]; 
      int                i;
      char *             pMsg;
      
      if (nb_proj > (ROZOFS_MAX_BLOCK_PER_MSG+1)) {  
        severe("storage_write more blocks than possible %d vs max %d",
	        nb_proj,ROZOFS_MAX_BLOCK_PER_MSG+1);
        errno = ESPIPE;	
        goto out;
      }
      pMsg  = (char *) bins;
      for (i=0; i< nb_proj; i++) {
        vector[i].iov_base = pMsg;
        vector[i].iov_len  = rozofs_disk_psize;
	pMsg += rozofs_msg_psize;
      }
      
      /*
      ** generate the crc32c for each projection block
      */
      storio_gen_crc32_vect(vector,nb_proj,rozofs_disk_psize);
      
      errno = 0;      
      nb_write = pwritev(fd, vector, nb_proj, bins_file_offset);      
    } 

    if (nb_write != length_to_write) {
        
	/*
	** Only few bytes written since no space left on device 
	*/
        if ((errno==0)||(errno==ENOSPC)) {
	  errno = ENOSPC;
	  goto out;
        }
	
	storage_error_on_device(st,device[chunk]);
	// A fault probably localized to this FID is detected   
	*is_fid_faulty = 1;  
        severe("pwrite(%s) size %llu expecting %llu offset %llu : %s",
	        path, (unsigned long long)nb_write,
	        (unsigned long long)length_to_write, 
		(unsigned long long)bins_file_offset, 
		strerror(errno));
        goto out;
    }
    /**
    * insert in the fid cache the written section
    */
//    storage_build_ts_table_from_prj_header((char*)bins,nb_proj,rozofs_max_psize,buf_ts_storage_write);
//    storio_cache_insert(fid,bid,nb_proj,buf_ts_storage_write,0);
    
    // Stat file for return the size of bins file after the write operation
    if (fstat(fd, &sb) == -1) {
        severe("fstat failed: %s", strerror(errno));
        goto out;
    }

    *file_size = sb.st_size;


    // Write is successful
    status = nb_proj * rozofs_msg_psize;

out:
    if (fd != -1) close(fd);
    return status;
}
int storage_write_repair_chunk(storage_t * st, uint8_t * device, uint8_t layout, uint32_t bsize, sid_t * dist_set,
        uint8_t spare, fid_t fid, uint8_t chunk, bid_t bid, uint32_t nb_proj, uint64_t bitmap, uint8_t version,
        uint64_t *file_size, const bin_t * bins, int * is_fid_faulty) {
    int status = -1;
    char path[FILENAME_MAX];
    int fd = -1;
    size_t nb_write = 0;
    off_t bins_file_offset = 0;
    uint16_t rozofs_msg_psize;
    uint16_t rozofs_disk_psize;
    struct stat sb;
    int open_flags;
    int    device_id_is_given;

    // No specific fault on this FID detected
    *is_fid_faulty = 0; 

    MYDBGTRACE_DEV(device,"%d/%d repair chunk %d : ", st->cid, st->sid, chunk);
   
open:    
    // If the device id is given as input, that proves that the file
    // has been existing with that name on this device sometimes ago. 
    if ((device[chunk] != ROZOFS_EOF_CHUNK)&&(device[chunk] != ROZOFS_EMPTY_CHUNK)&&(device[chunk] != ROZOFS_UNKNOWN_CHUNK)) {
      device_id_is_given = 1;
      open_flags = ROZOFS_ST_NO_CREATE_FILE_FLAG;
    }
    // The file location is not known. It may not exist and should be created 
    else {
      device_id_is_given = 0;
      open_flags = ROZOFS_ST_BINS_FILE_FLAG;
    }        
 
    // Build the full path of directory that contains the bins file
    if (storage_dev_map_distribution_write(st, device, chunk, bsize, 
                                          fid, layout, dist_set, 
					  spare, path, 0) == NULL) {
      goto out;      
    }  
    
    // Check that this directory already exists, otherwise it must be created
    if (storage_create_dir(path) < 0) {
      storage_error_on_device(st,device[chunk]);
      goto out;
    }

    // Build the path of bins file
    storage_complete_path_with_fid(fid, path);
    storage_complete_path_with_chunk(chunk,path);

    // Open bins file
    fd = open(path, open_flags, ROZOFS_ST_BINS_FILE_MODE);
    if (fd < 0) {
    
        // Something definitively wrong on device
        if (errno != ENOENT) {
	  storage_error_on_device(st,device[chunk]); 
	  goto out;
	}
	
        // If device id was not given as input, the file path has been deduced from 
	// the header files or should have been allocated. This is a definitive error !!!
	if (device_id_is_given == 0) {
	  storage_error_on_device(st,device[chunk]); 
	  goto out;
	}
	
	// The device id was given as input so the file did exist some day,
	// but the file may have been deleted without storio being aware of it.
	// Let's try to find the file again without using the given device id.
	device[chunk] = ROZOFS_EOF_CHUNK;
	goto open;    
    }

    /*
    ** Retrieve the projection size in the message
    ** and the projection size on disk 
    */
    storage_get_projection_size(spare, st->sid, layout, bsize, dist_set,
                                &rozofs_msg_psize, &rozofs_disk_psize); 
	       
    char *data_p = (char *)bins;
    int block_idx = 0;
    int block_count = 0;
    int error = 0;           
    for (block_idx = 0; block_idx < nb_proj; block_idx++)
    {
       if ((bitmap & (1 << block_idx)) == 0) continue;
       /*
       ** generate the crc32c for each projection block
       */
       storio_gen_crc32((char*)data_p,1,rozofs_disk_psize);
       /* 
       **  write the projection on disk
       */
       bins_file_offset = (bid+block_idx) * rozofs_disk_psize;
       nb_write = pwrite(fd, data_p, rozofs_disk_psize, bins_file_offset);
       if (nb_write != rozofs_disk_psize) {
           severe("pwrite failed: %s", strerror(errno));
	   error +=1;
       }
       /*
       ** update the data pointer for the next write
       */
       data_p+=rozofs_msg_psize;
       block_count += rozofs_msg_psize;
    }
    if (error != 0) goto out;
    

    // Stat file for return the size of bins file after the write operation
    if (fstat(fd, &sb) == -1) {
        severe("fstat failed: %s", strerror(errno));
        goto out;
    }
    *file_size = sb.st_size;


    // Write is successful
    status = block_count;

out:
    if (fd != -1) close(fd);
    return status;
}

uint64_t buf_ts_storage_before_read[STORIO_CACHE_BCOUNT];
uint64_t buf_ts_storage_after_read[STORIO_CACHE_BCOUNT];
uint64_t buf_ts_storcli_read[STORIO_CACHE_BCOUNT];
char storage_bufall[4096];
uint8_t storage_read_optim[4096];

int storage_read_chunk(storage_t * st, uint8_t * device, uint8_t layout, uint32_t bsize, sid_t * dist_set,
        uint8_t spare, fid_t fid, uint8_t chunk, bid_t bid, uint32_t nb_proj,
        bin_t * bins, size_t * len_read, uint64_t *file_size,int * is_fid_faulty) {

    int status = -1;
    char path[FILENAME_MAX];
    int fd = -1;
    size_t nb_read = 0;
    size_t length_to_read = 0;
    off_t bins_file_offset = 0;
    uint16_t rozofs_msg_psize;
    uint16_t rozofs_disk_psize;
    int    device_id_is_given = 1;
    int                       storage_slice;
    struct iovec vector[ROZOFS_MAX_BLOCK_PER_MSG]; 


    MYDBGTRACE_DEV(device,"%d/%d Read chunk %d : ", st->cid, st->sid, chunk);

    // No specific fault on this FID detected
    *is_fid_faulty = 0;  
    path[0]=0;
    
    /*
    ** When device array is not given, one has to read the header file on disk
    */
    if (device[0] == ROZOFS_UNKNOWN_CHUNK) {
      device_id_is_given = 0;    
    }

    /*
    ** Retrieve the projection size in the message 
    ** and the projection size on disk
    */
    storage_get_projection_size(spare, st->sid, layout, bsize, dist_set,
                                &rozofs_msg_psize, &rozofs_disk_psize); 


retry:

    /*
    ** Let's read the header file from disk
    */
    if (!device_id_is_given) {
      rozofs_stor_bins_file_hdr_t file_hdr;
      STORAGE_READ_HDR_RESULT_E read_hdr_res;  
      
      read_hdr_res = storage_read_header_file(st, fid, spare, &file_hdr);

      /*
      ** Header files are unreadable
      */
      if (read_hdr_res == STORAGE_READ_HDR_ERRORS) {
	*is_fid_faulty = 1; 
	errno = EIO;
	goto out;
      }
      
      /*
      ** Header files do not exist
      */      
      if (read_hdr_res == STORAGE_READ_HDR_NOT_FOUND) {
        errno = ENOENT;
        goto out;  
      } 

      /* 
      ** The header file has been read
      */
      memcpy(device,file_hdr.device,ROZOFS_STORAGE_MAX_CHUNK_PER_FILE);
    } 
    
         
    /*
    ** We are trying to read after the end of file
    */				     
    if (device[chunk] == ROZOFS_EOF_CHUNK) {
      *len_read = 0;
      status = nb_proj * rozofs_msg_psize;
      goto out;
    }

    /*
    ** We are trying to read inside a whole. Return 0 on the requested size
    */      
    if(device[chunk] == ROZOFS_EMPTY_CHUNK) {
      *len_read = nb_proj * rozofs_msg_psize;
      memset(bins,0,* len_read);
      status = *len_read;
      goto out;
    }
    
  
    storage_slice = rozofs_storage_fid_slice(fid);
    storage_build_bins_path(path, st->root, device[chunk], spare, storage_slice);
    storage_complete_path_with_fid(fid, path);
    storage_complete_path_with_chunk(chunk,path);

    // Open bins file
    fd = open(path, ROZOFS_ST_NO_CREATE_FILE_FLAG, ROZOFS_ST_BINS_FILE_MODE_RO);
    if (fd < 0) {
    
        // Something definitively wrong on device
        if (errno != ENOENT) {
	  storage_error_on_device(st,device[chunk]); 
	  goto out;
	}
	
        // If device id was not given as input, the file path has been deduced from 
	// the header files and so should exist. This is an error !!!
	if (device_id_is_given == 0) {
	  errno = EIO; // Data file is missing !!!
	  *is_fid_faulty = 1;
	  storage_error_on_device(st,device[chunk]); 
	  goto out;
	}
	
	// The device id was given as input so the file did exist some day,
	// but the file may have been deleted without storio being aware of it.
	// Let's try to find the file again without using the given device id.
	device_id_is_given = 0;
	goto retry ;
    }	

	       
    // Compute the offset and length to write
    
    bins_file_offset = bid * rozofs_disk_psize;
    length_to_read   = nb_proj * rozofs_disk_psize;

    //MYDBGTRACE("read %s bid %d nb %d",path,bid,nb_proj);
    
    /*
    ** Reading the projection directly as they will be sent in message
    */
    if (rozofs_msg_psize == rozofs_disk_psize) {    
      // Read nb_proj * (projection + header)
      nb_read = pread(fd, bins, length_to_read, bins_file_offset);       
    }
    /*
    ** Projections are smaller on disk than in message
    */
    else {
      int          i;
      char *       pMsg;
      
      if (nb_proj > ROZOFS_MAX_BLOCK_PER_MSG) {  
        severe("storage_read more blocks than possible %d vs max %d",
	        nb_proj,ROZOFS_MAX_BLOCK_PER_MSG);
        errno = ESPIPE;			
        goto out;
      }
      pMsg  = (char *) bins;
      for (i=0; i< nb_proj; i++) {
        vector[i].iov_base = pMsg;
        vector[i].iov_len  = rozofs_disk_psize;
	pMsg += rozofs_msg_psize;
      }
      nb_read = preadv(fd, vector, nb_proj, bins_file_offset);      
    } 
    
    // Check error
    if (nb_read == -1) {
        severe("pread failed: %s", strerror(errno));
	storage_error_on_device(st,device[chunk]);  
	// A fault probably localized to this FID is detected   
	*is_fid_faulty = 1;   		
        goto out;
    }


    // Check the length read
    if ((nb_read % rozofs_disk_psize) != 0) {
        char fid_str[37];
        uuid_unparse(fid, fid_str);
        severe("storage_read failed (FID: %s layout %d bsize %d chunk %d bid %d): read inconsistent length %d not modulo of %d",
	       fid_str,layout,bsize,chunk, (int) bid,(int)nb_read,rozofs_disk_psize);
	nb_read = (nb_read / rozofs_disk_psize) * rozofs_disk_psize;
    }

    int nb_proj_effective;
    nb_proj_effective = nb_read /rozofs_disk_psize ;

    /*
    ** check the crc32c for each projection block
    */
    if (rozofs_msg_psize == rozofs_disk_psize) {        
      storio_check_crc32((char*)bins,
                         nb_proj_effective,
                	 rozofs_disk_psize,
			 &st->crc_error);
    }
    else {
      storio_check_crc32_vect(vector,
                         nb_proj_effective,
                	 rozofs_disk_psize,
			 &st->crc_error);      
    }


    // Update the length read
    *len_read = (nb_read/rozofs_disk_psize)*rozofs_msg_psize;

    *file_size = 0;

    // Read is successful
    status = nb_proj * rozofs_msg_psize;

out:
    if (fd != -1) close(fd);
    return status;
}


int storage_truncate(storage_t * st, uint8_t * device, uint8_t layout, uint32_t bsize, sid_t * dist_set,
        uint8_t spare, fid_t fid, tid_t proj_id,bid_t input_bid,uint8_t version,uint16_t last_seg,uint64_t last_timestamp,
	u_int length_to_write, char * data, int * is_fid_faulty) {
    int status = -1;
    char path[FILENAME_MAX];
    int fd = -1;
    off_t bins_file_offset = 0;
    uint16_t rozofs_msg_psize;
    uint16_t rozofs_disk_psize;
    bid_t bid_truncate;
    size_t nb_write = 0;
    int open_flags;
    int block_per_chunk         = ROZOFS_STORAGE_NB_BLOCK_PER_CHUNK(bsize);
    int chunk                   = input_bid/block_per_chunk;
    int result;
    bid_t bid = input_bid - (chunk * block_per_chunk);
    STORAGE_READ_HDR_RESULT_E read_hdr_res;
    int chunk_idx;
    rozofs_stor_bins_file_hdr_t file_hdr;
    int                         rewrite_file_hdr = 0;

    if (chunk>=ROZOFS_STORAGE_MAX_CHUNK_PER_FILE) { 
      errno = EFBIG;
      return -1;
    } 
    
    // No specific fault on this FID detected
    *is_fid_faulty = 0;  

    /*
    ** When no device id is given as input, let's read the header file 
    */      
    if (device[0] == ROZOFS_UNKNOWN_CHUNK) {
   
      read_hdr_res = storage_read_header_file(st, fid, spare, &file_hdr);

      /*
      ** File is unreadable
      */
      if (read_hdr_res == STORAGE_READ_HDR_ERRORS) {
	return -1;
      }

      /*
      ** This is a file creation
      */    
      if (read_hdr_res == STORAGE_READ_HDR_NOT_FOUND) {
	MYDBGTRACE("%s","Truncate create");
	rewrite_file_hdr = 1;
	/*
	** Prepare file header
	*/
	memcpy(file_hdr.dist_set_current, dist_set, ROZOFS_SAFE_MAX * sizeof (sid_t));
	file_hdr.layout = layout;
	file_hdr.bsize  = bsize;
	file_hdr.version = 0;
	memcpy(file_hdr.fid, fid,sizeof(fid_t)); 
	memset(file_hdr.device,ROZOFS_EMPTY_CHUNK,ROZOFS_STORAGE_MAX_CHUNK_PER_FILE);
      }
    }
    /*
    ** distribution upon the devices is given as input
    */
    else {
      /*
      ** Prepare file header from input information
      */
      memcpy(file_hdr.dist_set_current, dist_set, ROZOFS_SAFE_MAX * sizeof (sid_t));
      file_hdr.layout = layout;
      file_hdr.bsize  = bsize;
      file_hdr.version = 0;
      memcpy(file_hdr.fid, fid,sizeof(fid_t)); 
      memcpy(file_hdr.device, device,ROZOFS_STORAGE_MAX_CHUNK_PER_FILE); 
    }
   
    /*
    ** Set previous chunks to empty when they where EOF
    */
    for (chunk_idx=0; chunk_idx<chunk; chunk_idx++) {
      if (file_hdr.device[chunk_idx] == ROZOFS_EOF_CHUNK) {
        rewrite_file_hdr = 1;
        file_hdr.device[chunk_idx] = ROZOFS_EMPTY_CHUNK;
      }
    }
     
    /*
    ** We may allocate a device for the current truncated chunk
    */ 
    if ((file_hdr.device[chunk] == ROZOFS_EOF_CHUNK)||(file_hdr.device[chunk] == ROZOFS_EMPTY_CHUNK)) {
      rewrite_file_hdr = 1;    
      file_hdr.device[chunk] = storio_device_mapping_allocate_device(st);
      open_flags = ROZOFS_ST_BINS_FILE_FLAG; 
    }
    else {
      open_flags = ROZOFS_ST_NO_CREATE_FILE_FLAG;
    }
    
    if (storage_dev_map_distribution_write(st, file_hdr.device, chunk, bsize, 
                                          fid, layout, dist_set, 
					  spare, path, 0) == NULL) {
      goto out;      
    }   

    // Check that this directory already exists, otherwise it will be create
    if (storage_create_dir(path) < 0) {
      storage_error_on_device(st,file_hdr.device[chunk]);
      goto out;
    }   

    // Build the path of bins file
    storage_complete_path_with_fid(fid, path);
    storage_complete_path_with_chunk(chunk,path);


    // Open bins file
    fd = open(path, open_flags, ROZOFS_ST_BINS_FILE_MODE);
    if (fd < 0) {
	storage_error_on_device(st,file_hdr.device[chunk]);  				    
        severe("open failed (%s) : %s", path, strerror(errno));
        goto out;
    }


    /*
    ** Retrieve the projection size in the message 
    ** and the projection size on disk
    */
    storage_get_projid_size(spare, proj_id, layout, bsize,
                            &rozofs_msg_psize, &rozofs_disk_psize); 
	       
     // Compute the offset from the truncate
    bid_truncate = bid;
    if (last_seg!= 0) bid_truncate+=1;
    bins_file_offset = bid_truncate * rozofs_disk_psize;
    status = ftruncate(fd, bins_file_offset);
    if (status < 0) goto out;
    
    /*
    ** When the truncate occurs in the middle of a block, it is either
    ** a shortening of the block or a an extension of the file.
    ** When extending the file only the header of the block is written 
    ** to reflect the new size. 
    ** In case of a shortening the whole block to write is given in the
    ** request
    */
    if (last_seg!= 0) {
	
      bins_file_offset = bid * rozofs_disk_psize;

      /*
      ** Rewrite the whole given data block 
      */
      if (length_to_write!= 0)
      {

        length_to_write = rozofs_disk_psize;
	
	/*
	** generate the crc32c for each projection block
	*/
	storio_gen_crc32(data,1,rozofs_disk_psize);	
	
	nb_write = pwrite(fd, data, length_to_write, bins_file_offset);
	if (nb_write != length_to_write) {
            status = -1;
            severe("pwrite failed on last segment: %s", strerror(errno));
	    storage_error_on_device(st,file_hdr.device[chunk]); 
	    // A fault probably localized to this FID is detected   
	    *is_fid_faulty = 1;  	     				    
            goto out;
	}
      
      }
      else {
      
        // Write the block header
        rozofs_stor_bins_hdr_t bins_hdr;  
	bins_hdr.s.timestamp        = last_timestamp;
	bins_hdr.s.effective_length = last_seg;
	bins_hdr.s.projection_id    = proj_id;
	bins_hdr.s.version          = version;

	nb_write = pwrite(fd, &bins_hdr, sizeof(bins_hdr), bins_file_offset);
	if (nb_write != sizeof(bins_hdr)) {
            severe("pwrite failed on last segment header : %s", strerror(errno));
	    storage_error_on_device(st,file_hdr.device[chunk]); 
	    // A fault probably localized to this FID is detected   
	    *is_fid_faulty = 1;  	     				    
            goto out;
        }   
        
        // Write the block footer
	bins_file_offset += (sizeof(rozofs_stor_bins_hdr_t) 
	        + rozofs_get_psizes(layout,bsize,proj_id) * sizeof (bin_t));
	nb_write = pwrite(fd, &last_timestamp, sizeof(last_timestamp), bins_file_offset);
	if (nb_write != sizeof(last_timestamp)) {
            severe("pwrite failed on last segment footer : %s", strerror(errno));
	    storage_error_on_device(st,file_hdr.device[chunk]);  				    
	    // A fault probably localized to this FID is detected   
	    *is_fid_faulty = 1;  
            goto out;
        }   	  
      }
    } 
    

    /*
    ** Remove the extra chunks
    */
    for (chunk_idx=(chunk+1); chunk_idx<ROZOFS_STORAGE_MAX_CHUNK_PER_FILE; chunk_idx++) {

      if (file_hdr.device[chunk_idx] == ROZOFS_EOF_CHUNK) {
        continue;
      }
      
      if (file_hdr.device[chunk_idx] == ROZOFS_EMPTY_CHUNK) {
        rewrite_file_hdr = 1;      
        file_hdr.device[chunk_idx] = ROZOFS_EOF_CHUNK;
	continue;
      }
      
      storage_rm_data_chunk(st, file_hdr.device[chunk_idx], fid, spare, chunk_idx,1/*errlog*/);
      rewrite_file_hdr = 1;            
      file_hdr.device[chunk_idx] = ROZOFS_EOF_CHUNK;
    } 
    
    /* 
    ** Rewrite file header on disk
    */   
    if (rewrite_file_hdr) {
      MYDBGTRACE("%s","truncate rewrite file header");
      result = storage_write_all_header_files(st, fid, spare, &file_hdr);        
      /*
      ** Failure on every write operation
      */ 
      if (result == 0) goto out;
    }
    memcpy(device,file_hdr.device,ROZOFS_STORAGE_MAX_CHUNK_PER_FILE);
       
    status = 0;
out:

    if (fd != -1) close(fd);
    return status;
}

int storage_rm_chunk(storage_t * st, uint8_t * device, 
                     uint8_t layout, uint8_t bsize, uint8_t spare, 
		     sid_t * dist_set, fid_t fid, 
		     uint8_t chunk, int * is_fid_faulty) {
    rozofs_stor_bins_file_hdr_t file_hdr;

    MYDBGTRACE_DEV(device,"%d/%d rm chunk %d : ", st->cid, st->sid, chunk);

    // No specific fault on this FID detected
    *is_fid_faulty = 0;  
    
    /*
    ** When device array is not given, one has to read the header file on disk
    */
    if (device[0] == ROZOFS_UNKNOWN_CHUNK) {
      STORAGE_READ_HDR_RESULT_E read_hdr_res;  
      
      read_hdr_res = storage_read_header_file(st, fid, spare, &file_hdr);

      /*
      ** Header files are unreadable
      */
      if (read_hdr_res == STORAGE_READ_HDR_ERRORS) {
	*is_fid_faulty = 1;
	errno = EIO; 
	return -1;
      }
      
      /*
      ** Header files do not exist
      */      
      if (read_hdr_res == STORAGE_READ_HDR_NOT_FOUND) {
        return 0;  
      } 

      /* 
      ** The header file has been read
      */
      memcpy(device,file_hdr.device,ROZOFS_STORAGE_MAX_CHUNK_PER_FILE);
    } 
    else {
      file_hdr.layout = layout;
      file_hdr.bsize  = bsize;  ///< Block size as defined in enum ROZOFS_BSIZE_E
      memcpy(file_hdr.fid, fid, sizeof(fid_t));
      memcpy(file_hdr.dist_set_current, dist_set, sizeof(sid_t)*ROZOFS_SAFE_MAX);
    }
    
         
    /*
    ** We are trying to read after the end of file
    */				     
    if (device[chunk] == ROZOFS_EOF_CHUNK) {
      return 0;
    }

    /*
    ** This chunk is a whole
    */      
    if(device[chunk] == ROZOFS_EMPTY_CHUNK) {
      return 0;
    }
    
    /*
    ** Remove data chunk
    */
    storage_rm_data_chunk(st, device[chunk], fid, spare, chunk, 0 /* No errlog*/) ;
    
    // Last chunk
    if ((chunk+1) >= ROZOFS_STORAGE_MAX_CHUNK_PER_FILE) {
      device[chunk] = ROZOFS_EOF_CHUNK;
    }
    // Next chunk is end of file
    else if (device[chunk+1] == ROZOFS_EOF_CHUNK) {  
      device[chunk] = ROZOFS_EOF_CHUNK;
    }
    // Next chunk is not end of file
    else {
      device[chunk] = ROZOFS_EMPTY_CHUNK;
    }
    
    /*
    ** Chunk is now EOF. Are the previous chunks empty ?
    */ 
    while (device[chunk] == ROZOFS_EOF_CHUNK) {
      /*
      ** The file is totaly empty
      */
      if (chunk == 0) {
        storage_dev_map_distribution_remove(st, fid, spare);
	return 0;
      }
      
      chunk--;
      if (device[chunk] == ROZOFS_EMPTY_CHUNK) {
        device[chunk] = ROZOFS_EOF_CHUNK;
      }
    }
    
    /*
    ** Re-write distibution
    */
    memcpy(file_hdr.device,device,sizeof(file_hdr.device));
    storage_write_all_header_files(st, fid, spare, &file_hdr);        
    return 0;
}
int storage_rm_file(storage_t * st, fid_t fid) {
    uint8_t spare = 0;
    STORAGE_READ_HDR_RESULT_E read_hdr_res;
    int chunk;
    rozofs_stor_bins_file_hdr_t file_hdr;


    // For spare and no spare
    for (spare = 0; spare < 2; spare++) {

      /*
      ** When no device id is given as input, let's read the header file 
      */      
      read_hdr_res = storage_read_header_file(st, fid, spare, &file_hdr);

      /*
      ** File does not exist or is unreadable
      */
      if (read_hdr_res != STORAGE_READ_HDR_OK) {
	continue;
      }
      
      for (chunk=0; chunk<ROZOFS_STORAGE_MAX_CHUNK_PER_FILE; chunk++) {
      
        if (file_hdr.device[chunk] == ROZOFS_EOF_CHUNK) {
	  break;
	}
	
	if (file_hdr.device[chunk] == ROZOFS_EMPTY_CHUNK) {
	  continue;
	}

	/*
	** Remove data chunk
	*/
	storage_rm_data_chunk(st, file_hdr.device[chunk], fid, spare, chunk, 0 /* No errlog*/);
      }

      // It's not possible for one storage to store one bins file
      // in directories spare and no spare.
      storage_dev_map_distribution_remove(st, fid, spare);
      return 0;
               
    }
    return 0;
} 
int storage_write_device_status(char * root, storage_device_info_t * info, int nbElement) {
    char          path[FILENAME_MAX];
    FILE *        fd=NULL; 

    sprintf(path,"%s/status",root);
    fd = fopen(path,"w");
    if (fd != NULL) {
      fwrite(info,sizeof(storage_device_info_t),nbElement,fd);
      fclose(fd);
    }  
    return 0;
}
int storage_read_device_status(char * root, storage_device_info_t * info) {
    char          path[FILENAME_MAX];
    FILE *        fd=NULL; 
    int           read=-1;

    sprintf(path,"%s/status",root);
    fd = fopen(path,"r");
    if (fd != NULL) {
      read = fread(info,sizeof(storage_device_info_t),STORAGE_MAX_DEVICE_NB,fd);
      fclose(fd);
    }  
    return read;
}

bins_file_rebuild_t ** storage_list_bins_file(storage_t * st, sid_t sid, uint8_t device_id, 
                                              uint8_t spare, uint16_t slice, uint64_t * cookie,
        				      bins_file_rebuild_t ** children, uint8_t * eof,
        				      uint64_t * current_files_nb) {
    int i = 0;
    char path[FILENAME_MAX];
    DIR *dp = NULL;
    struct dirent *ep = NULL;
    bins_file_rebuild_t **iterator = children;
    rozofs_stor_bins_file_hdr_t file_hdr;
    int                         fd;
    int                         nb_read;
    int                         sid_idx;
    int                         safe;

    DEBUG_FUNCTION;
        
    /*
    ** Build the directory path
    */
    storage_build_hdr_path(path, st->root, device_id, spare, slice);
    
     
    // Open directory
    if (!(dp = opendir(path)))
        goto out;

    // Step to the cookie index
    if (*cookie != 0) {
      seekdir(dp, *cookie);
    }

    // Readdir first time
    ep = readdir(dp);


    // The current nb. of bins files in the list
    i = *current_files_nb;

    // Readdir the next entries
    while (ep && i < MAX_REBUILD_ENTRIES) {
    
        if ((strcmp(ep->d_name,".") != 0) && (strcmp(ep->d_name,"..") != 0)) {      

            // Read the file
            storage_build_hdr_path(path, st->root, device_id, spare, slice);
            strcat(path,ep->d_name);

	    fd = open(path, ROZOFS_ST_NO_CREATE_FILE_FLAG, ROZOFS_ST_BINS_FILE_MODE);
	    if (fd < 0) {
	       severe("open(%s) %s", path, strerror(errno));
               // Readdir for next entry
               ep = readdir(dp);	       
	       continue;
            }
            nb_read = pread(fd, &file_hdr, sizeof(file_hdr), 0);
	    close(fd);	    

            // What to do with such an error ?
	    if (nb_read != sizeof(file_hdr)) {
	       severe("nb_read %d vs %d %s", nb_read, (int) sizeof(file_hdr), path);
               // Readdir for next entry
               ep = readdir(dp);     
	       continue;
            }
	    // Check the requested sid is in the distribution
	    safe = rozofs_get_rozofs_safe(file_hdr.layout);
	    for (sid_idx=0; sid_idx<safe; sid_idx++) {
	      if (file_hdr.dist_set_current[sid_idx] == sid) break;
	    }
	    if (sid_idx == safe) {
               // Readdir for next entry
               ep = readdir(dp);	       
	       continue;
            }	    

            // Alloc a new bins_file_rebuild_t
            *iterator = xmalloc(sizeof (bins_file_rebuild_t)); // XXX FREE ?
            // Copy FID
            uuid_parse(ep->d_name, (*iterator)->fid);
            // Copy current dist_set
            memcpy((*iterator)->dist_set_current, file_hdr.dist_set_current,
                    sizeof (sid_t) * ROZOFS_SAFE_MAX);
            // Copy layout
            (*iterator)->layout = file_hdr.layout;
            (*iterator)->bsize = file_hdr.bsize;

            // Go to next entry
            iterator = &(*iterator)->next;

            // Increment the current nb. of bins files in the list
            i++;
        }
	
        // Readdir for next entry
        if (i < MAX_REBUILD_ENTRIES) {
          ep = readdir(dp);
        }  
    }

    // Update current nb. of bins files in the list
    *current_files_nb = i;

    if (ep) {
        // It's not the EOF
        *eof = 0;
	// Save where we are
        *cookie = telldir(dp);
    } else {
        *eof = 1;
    }

    // Close directory
    if (closedir(dp) == -1)
        goto out;

    *iterator = NULL;
out:
    return iterator;
}

int storage_list_bins_files_to_rebuild(storage_t * st, sid_t sid, uint8_t * device_id,
        uint8_t * spare, uint16_t * slice, uint64_t * cookie,
        bins_file_rebuild_t ** children, uint8_t * eof) {

    int status = -1;
    uint8_t spare_it = 0;
    uint64_t current_files_nb = 0;
    bins_file_rebuild_t **iterator = NULL;
    uint8_t device_it = 0;
    uint16_t slice_it = 0;

    DEBUG_FUNCTION;

    // Use iterator
    iterator = children;

    device_it = *device_id;
    spare_it  = *spare;
    slice_it  = *slice;
    
    // Loop on all the devices
    for (; device_it < st->device_number;device_it++,spare_it=0) {

        // For spare and no spare
        for (; spare_it < 2; spare_it++,slice_it=0) {
	
            // For slice
            for (; slice_it < FID_STORAGE_SLICE_SIZE; slice_it++) {

        	// Build path directory for this layout and this spare type
        	char path[FILENAME_MAX];
        	storage_build_hdr_path(path, st->root, device_it, spare_it, slice_it);

        	// Go to this directory
        	if (chdir(path) != 0)
                    continue;

                // List the bins files for this specific directory
                if ((iterator = storage_list_bins_file(st, sid, device_it, spare_it, slice_it, 
		                                       cookie, iterator, eof,
                                                       &current_files_nb)) == NULL) {
                    severe("storage_list_bins_file failed: %s\n",
                            strerror(errno));
                    continue;
                }
		

                // Check if EOF
                if (0 == *eof) {
                    status = 0;
		            *device_id = device_it;
                    *spare = spare_it;
                    *slice = slice_it;
                    goto out;
                } else {
                    *cookie = 0;
                }
            }
	    }    
    }
    *eof = 1;
    status = 0;

out:

    return status;
}
