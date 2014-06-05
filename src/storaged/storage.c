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


/*
** API to be called when an error occurs on a device
 *
 * @param st: the storage to be initialized.
 * @param device_nb: device number
 *
 */
int storage_error_on_device(storage_t * st, int device_nb) {

  if ((st == NULL) || (device_nb >= STORAGE_MAX_DEVICE_NB)) return 0;     
    
  int active = st->device_errors.active;
    
  // Since several threads can call this API at the same time
  // some count may be lost...
  st->device_errors.errors[active][device_nb]++;
  return st->device_errors.errors[active][device_nb];
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
      
  /* 
  ** Read the mapping file
  */
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



char *storage_dev_map_distribution_write(storage_t * st, 
					 int * device_id,
					 uint32_t bsize, 
					 fid_t fid, 
					 uint8_t layout,
                                	 sid_t dist_set[ROZOFS_SAFE_MAX], 
					 uint8_t spare, 
					 char *path, 
					 int version) {
    int                       dev;
    int                       hdrDevice;
    size_t                    nb_read;
    int                       fd;
    int                       result;
    rozofs_stor_bins_file_hdr_t file_hdr;
    int                       device_result[STORAGE_MAX_DEVICE_NB];
    int                       storage_slice;

    DEBUG_FUNCTION;
    
    /*
    ** Compute storage slice from FID
    */
    storage_slice = rozofs_storage_fid_slice(fid);
     
    memset(device_result,0,sizeof(device_result));    

    /*
    ** When no device id is given as input, let's search for the 
    ** device that store the data of this fid
    */
    while (*device_id == -1) {
    
      /*
      ** Look for the mapping information in one of the redundant mapping devices
      ** which numbers are derived from the fid
      */
      for (dev=0; dev < st->mapper_redundancy ; dev++) {

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

        // Open hdr file
        fd = open(path, ROZOFS_ST_NO_CREATE_FILE_FLAG, ROZOFS_ST_BINS_FILE_MODE);
        if (fd < 0) {
          device_result[dev] = errno;	
          continue;
        }
        nb_read = pread(fd, &file_hdr, sizeof(file_hdr), 0);
        if (nb_read != sizeof(file_hdr)) {
          device_result[dev] = EINVAL;	
          close(fd);
          storage_error_on_device(st,hdrDevice);
          continue;
        }
	
        close(fd);
	
        /* Wonderfull. We have found the device number in a mapping file */
        *device_id = file_hdr.device_id;
        device_result[dev] = 0;		
        break;	
      }	         
 
      break;
    }        

      
    /*
    **  Search the device of the fid and allocate one if none exist 
    */    
    
    if (*device_id != -1) {
       /* The fid exist on *device_id */         
       goto success;
    }   
   
    /*
    ** Device not found. Must allocate a device for this fid
    */
    
    for (dev=0; dev < st->mapper_redundancy ; dev++) {
      if (device_result[dev] != ENOENT) {
	    break;
      }	  
    } 
    /*
    ** There is a problem on at least one mapping device.
    ** Do not allocate a device on this sid !!!
    */
    if (dev != st->mapper_redundancy) {
      return NULL;
    }

    /*
    ** Every device has returned ENOENT
    ** allocate a device for the file
    */  
    *device_id = storio_device_mapping_allocate_device(st);

    result = 0;

    for (dev=0; dev < st->mapper_redundancy ; dev++) {
      int ret;

      hdrDevice = storage_mapper_device(fid,dev,st->mapper_modulo);
      storage_build_hdr_path(path, st->root, hdrDevice, spare, storage_slice);
                 
      // Prepare file header
      memcpy(file_hdr.dist_set_current, dist_set, ROZOFS_SAFE_MAX * sizeof (sid_t));
      memset(file_hdr.dist_set_next, 0, ROZOFS_SAFE_MAX * sizeof (sid_t));
      file_hdr.layout = layout;
      file_hdr.bsize  = bsize;
      file_hdr.version = version;
      file_hdr.device_id = *device_id;
      memcpy(file_hdr.fid, fid,sizeof(fid_t));  

      // Write the file
      ret = storage_write_header_file(st,hdrDevice,path,&file_hdr);
      if (ret == 0) result++;	
    }	         

    /*
    ** Failure on every write operation
    */ 
    if (result == 0) return NULL;
    
       
    
success: 
    storage_build_bins_path(path, st->root, *device_id, spare, storage_slice);
    return path;            
}
char *storage_dev_map_distribution_read(  storage_t * st, 
					  int * device_id,
					  fid_t fid, 
					  uint8_t spare, 
					  char *path) {
    int                       dev;
    int                       hdrDevice;
    size_t                    nb_read;
    int                       fd;
    rozofs_stor_bins_file_hdr_t file_hdr;
    int                       device_result[STORAGE_MAX_DEVICE_NB];
    int                       storage_slice;

    DEBUG_FUNCTION;
    
    /*
    ** Compute storage slice from FID
    */
    storage_slice = rozofs_storage_fid_slice(fid);
     
    memset(device_result,0,sizeof(device_result));    

    /*
    ** When no device id is given as input, let's search for the 
    ** device that store the data of this fid
    */
    while (*device_id == -1) {
    
      /*
      ** Look for the mapping information in one of the redundant mapping devices
      ** which numbers are derived from the fid
      */
      for (dev=0; dev < st->mapper_redundancy ; dev++) {

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

        // Open hdr file
        fd = open(path, ROZOFS_ST_NO_CREATE_FILE_FLAG, ROZOFS_ST_BINS_FILE_MODE);
        if (fd < 0) {
          device_result[dev] = errno;	
          continue;
        }
        nb_read = pread(fd, &file_hdr, sizeof(file_hdr), 0);
        if (nb_read != sizeof(file_hdr)) {
          device_result[dev] = EINVAL;	
          close(fd);
          storage_error_on_device(st,hdrDevice);
          continue;
        }
	
        close(fd);
	
        /* Wonderfull. We have found the device number in a mapping file */
        *device_id = file_hdr.device_id;
        device_result[dev] = 0;		
        break;	
      }	         
 
      break;
    }        


    /* No device found */
    if (*device_id == -1) {
      /* The FID does not exit !!! */
      return NULL;
    }   
 
    storage_build_bins_path(path, st->root, *device_id, spare, storage_slice);
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
		       uint32_t mapper_redundancy) {
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
    
    st->device_free.active = 0;
    for (dev=0; dev<STORAGE_MAX_DEVICE_NB; dev++) {
      st->device_free.blocks[0][dev] = 20000;
      st->device_free.blocks[1][dev] = 20000;
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
  *msg = rozofs_get_max_psize(layout,bsize) * sizeof (bin_t) 
       + sizeof (rozofs_stor_bins_hdr_t) 
       + sizeof(rozofs_stor_bins_footer_t);
  
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
    *disk = rozofs_get_psizes(layout,bsize,prj_id) * sizeof (bin_t) 
          + sizeof (rozofs_stor_bins_hdr_t) 
          + sizeof(rozofs_stor_bins_footer_t);
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

  *msg = rozofs_get_max_psize(layout,bsize) * sizeof (bin_t) 
       + sizeof (rozofs_stor_bins_hdr_t) 
       + sizeof(rozofs_stor_bins_footer_t);
  
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
  *disk = rozofs_get_psizes(layout,bsize,prj_id) * sizeof (bin_t) 
        + sizeof (rozofs_stor_bins_hdr_t) 
        + sizeof(rozofs_stor_bins_footer_t);		
} 
uint64_t buf_ts_storage_write[STORIO_CACHE_BCOUNT];

int storage_write(storage_t * st, int * device_id, uint8_t layout, uint32_t bsize, sid_t * dist_set,
        uint8_t spare, fid_t fid, bid_t bid, uint32_t nb_proj, uint8_t version,
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

open:    
    // If the device id is given as input, that proves that the file
    // has been existing with that name on this device sometimes ago. 
    if (*device_id != -1) {
      device_id_is_given = 1;
      open_flags = ROZOFS_ST_NO_CREATE_FILE_FLAG;
    }
    // The file location is not known. It may not exist and should be created 
    else {
      device_id_is_given = 0;
      open_flags = ROZOFS_ST_BINS_FILE_FLAG;
    }        
 
    // Build the full path of directory that contains the bins file
    if (storage_dev_map_distribution_write(st, device_id, bsize, 
                                          fid, layout, dist_set, 
					  spare, path, 0) == NULL) {
      severe("storage_write storage_dev_map_distribution");
      goto out;      
    }  
    
    // Check that this directory already exists, otherwise it must be created
    if (storage_create_dir(path) < 0) {
      storage_error_on_device(st,*device_id);
      goto out;
    }

    // Build the path of bins file
    storage_complete_path_with_fid(fid, path);


    // Open bins file
    fd = open(path, open_flags, ROZOFS_ST_BINS_FILE_MODE);
    if (fd < 0) {
    
        // Something definitively wrong on device
        if (errno != ENOENT) {
	  storage_error_on_device(st,*device_id); 
	  goto out;
	}
	
        // If device id was not given as input, the file path has been deduced from 
	// the header files or should have been allocated. This is an error !!!
	if (device_id_is_given == 0) {
	  storage_error_on_device(st,*device_id); 
	  goto out;
	}
	
	// The device id was given as input so the file did exist some day,
	// but the file may have been deleted without storio being aware of it.
	// Let's try to find the file again without using the given device id.
	*device_id = -1;
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

    /*
    ** Writting the projection as received directly on disk
    */
    if (rozofs_msg_psize == rozofs_disk_psize) {
      nb_write = pwrite(fd, bins, length_to_write, bins_file_offset);
    }

    /*
    ** Writting the projections on a different size on disk
    */
    else {
      struct iovec       vector[ROZOFS_MAX_BLOCK_PER_MSG]; 
      int                i;
      char *             pMsg;
      
      if (nb_proj > ROZOFS_MAX_BLOCK_PER_MSG) {  
        severe("storage_write more blocks than possible %d vs max %d",
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
      nb_write = pwritev(fd, vector, nb_proj, bins_file_offset);      
    } 

    if (nb_write != length_to_write) {
	storage_error_on_device(st,*device_id);
	// A fault probably localized to this FID is detected   
	*is_fid_faulty = 1;  
        severe("pwrite failed: %s", strerror(errno));
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

uint64_t buf_ts_storage_before_read[STORIO_CACHE_BCOUNT];
uint64_t buf_ts_storage_after_read[STORIO_CACHE_BCOUNT];
uint64_t buf_ts_storcli_read[STORIO_CACHE_BCOUNT];
char storage_bufall[4096];
uint8_t storage_read_optim[4096];

int storage_read(storage_t * st, int * device_id, uint8_t layout, uint32_t bsize, sid_t * dist_set,
        uint8_t spare, fid_t fid, bid_t bid, uint32_t nb_proj,
        bin_t * bins, size_t * len_read, uint64_t *file_size,int * is_fid_faulty) {

    int status = -1;
    char path[FILENAME_MAX];
    int fd = -1;
    size_t nb_read = 0;
    size_t length_to_read = 0;
    off_t bins_file_offset = 0;
    uint16_t rozofs_msg_psize;
    uint16_t rozofs_disk_psize;
    struct stat sb;
    int    device_id_is_given = 0;

    // No specific fault on this FID detected
    *is_fid_faulty = 0;  
    
    // If the device id is given as input, that proves that the file
    // has been existing with that name on this device sometimes ago. 
    if (*device_id != -1) device_id_is_given = 1;    

open:

    // Build the full path of the directory that contains the bins file
    // If device id is not given, it will look into the header files
    // to find out the device on which the file should stand.
    if (storage_dev_map_distribution_read(st, device_id, 
                                     fid, spare,path) == NULL) {
      // The file does not exist
      errno = ENOENT;
      goto out;      
    }   
    
    // Build the path of bins file
    storage_complete_path_with_fid(fid, path);

    // Open bins file
    fd = open(path, ROZOFS_ST_NO_CREATE_FILE_FLAG, ROZOFS_ST_BINS_FILE_MODE_RO);
    if (fd < 0) {
    
        // Something definitively wrong on device
        if (errno != ENOENT) {
	  storage_error_on_device(st,*device_id); 
	  goto out;
	}
	
        // If device id was not given as input, the file path has been deduced from 
	// the header files and so should exist. This is an error !!!
	if (device_id_is_given == 0) {
	  storage_error_on_device(st,*device_id); 
	  goto out;
	}
	
	// The device id was given as input so the file did exist some day,
	// but the file may have been deleted without storio being aware of it.
	// Let's try to find the file again without using the given device id.
	device_id_is_given = 0;
	*device_id = -1;
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
    length_to_read   = nb_proj * rozofs_disk_psize;

    
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
      struct iovec vector[ROZOFS_MAX_BLOCK_PER_MSG]; 
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
	storage_error_on_device(st,*device_id);  
	// A fault probably localized to this FID is detected   
	*is_fid_faulty = 1;   		
        goto out;
    }

    // Check the length read
    if ((nb_read % rozofs_disk_psize) != 0) {
        char fid_str[37];
        uuid_unparse(fid, fid_str);
        severe("storage_read failed (FID: %s): read inconsistent length",
                fid_str);
        errno = EIO;
	// A fault probably localized to this FID is detected   
	*is_fid_faulty = 1;  
        goto out;
    }

    // Update the length read
    *len_read = (nb_read/rozofs_disk_psize)*rozofs_msg_psize;


    // Stat file for return the size of bins file after the read operation
    if (fstat(fd, &sb) == -1) {
        severe("fstat failed: %s", strerror(errno));
        goto out;
    }

    *file_size = sb.st_size;

    // Read is successful
    status = 0;

out:
    if (fd != -1) close(fd);
    return status;
}

int storage_truncate(storage_t * st, int * device_id, uint8_t layout, uint32_t bsize, sid_t * dist_set,
        uint8_t spare, fid_t fid, tid_t proj_id,bid_t bid,uint8_t version,uint16_t last_seg,uint64_t last_timestamp,
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

    // No specific fault on this FID detected
    *is_fid_faulty = 0;  
        
    /*
    ** If a device is given, the file is known so do not create it
    */
    if (*device_id == -1) open_flags = ROZOFS_ST_BINS_FILE_FLAG;
    else                  open_flags = ROZOFS_ST_NO_CREATE_FILE_FLAG;
  
    
    // Build the full path of directory that contains the bins file
    if (storage_dev_map_distribution_read(st, device_id, fid,spare, path) == NULL) {
      severe("storage_truncate storage_dev_map_distribution");
      goto out;      
    }   

    // Check that this directory already exists, otherwise it will be create
    if (storage_create_dir(path) < 0) {
      storage_error_on_device(st,*device_id);
      goto out;
    }   

    // Build the path of bins file
    storage_complete_path_with_fid(fid, path);


    // Open bins file
    fd = open(path, open_flags, ROZOFS_ST_BINS_FILE_MODE);
    if (fd < 0) {
	storage_error_on_device(st,*device_id);  				    
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
	nb_write = pwrite(fd, data, length_to_write, bins_file_offset);
	if (nb_write != length_to_write) {
            status = -1;
            severe("pwrite failed on last segment: %s", strerror(errno));
	    storage_error_on_device(st,*device_id); 
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
	    storage_error_on_device(st,*device_id); 
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
	    storage_error_on_device(st,*device_id);  				    
	    // A fault probably localized to this FID is detected   
	    *is_fid_faulty = 1;  
            goto out;
        }   	  
      }
    }  
    status = 0;
out:

    if (fd != -1) close(fd);
    return status;
}

int storage_rm_file(storage_t * st, fid_t fid) {
    int status = -1;
    uint8_t spare = 0;
    char path[FILENAME_MAX];
    int device_id = -1;

    DEBUG_FUNCTION;

    // For spare and no spare
    for (spare = 0; spare < 2; spare++) {

        // Build the full path of directory that contains the bins file
        if (storage_dev_map_distribution_read(st, &device_id, fid, spare, path) == NULL) {
	  continue;
        }					 

        // Build the path of bins file
        storage_complete_path_with_fid(fid, path);

        // Check that this file exists
        if (access(path, F_OK) == -1)
            continue;

        if (unlink(path) == -1) {
            if (errno != ENOENT) {
                severe("storage_rm_file failed: unlink file %s failed: %s",
                        path, strerror(errno));
                goto out;
            }
        } 
	
        // It's not possible for one storage to store one bins file
        // in directories spare and no spare.
        status = 0;
        storage_dev_map_distribution_remove(st, fid, spare);
        goto out;
    }
    status = 0;
out:
    return status;
}

int storage_stat(storage_t * st, sstat_t * sstat) {
    int status = -1;
    struct statfs sfs;
    DEBUG_FUNCTION;

    if (statfs(st->root, &sfs) == -1)
        goto out;

    /*
    ** Privileged process can use the whole free space
    */
    if (getuid() == 0) {
      sstat->free = (uint64_t) sfs.f_bfree * (uint64_t) sfs.f_bsize;
    }
    /*
    ** non privileged process can not use root reserved space
    */
    else {
      sstat->free = (uint64_t) sfs.f_bavail * (uint64_t) sfs.f_bsize;
    }
    sstat->size = (uint64_t) sfs.f_blocks * (uint64_t) sfs.f_bsize;
    status = 0;
out:
    return status;
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

    // Readdir first time
    ep = readdir(dp);

    // Step to the cookie index
    if (*cookie != 0) {
      seekdir(dp, *cookie);
    }

    // The current nb. of bins files in the list
    i = *current_files_nb;

    // Readdir the next entries
    while (ep && i < MAX_REBUILD_ENTRIES) {
    
        if ((strcmp(ep->d_name,".") != 0) && (strcmp(ep->d_name,"..") != 0)) {      

            // Read the file
            storage_build_hdr_path(path, st->root, device_id, spare, slice);
            strcat(path,ep->d_name);

	    fd = open(path, ROZOFS_ST_NO_CREATE_FILE_FLAG, ROZOFS_ST_BINS_FILE_MODE);
	    if (fd < 0) continue;

            nb_read = pread(fd, &file_hdr, sizeof(file_hdr), 0);
	    close(fd);	    

            // What to do with such an error ?
	    if (nb_read != sizeof(file_hdr)) continue;
	    
	    // Check the requested sid is in the distribution
	    safe = rozofs_get_rozofs_safe(file_hdr.layout);
	    for (sid_idx=0; sid_idx<safe; sid_idx++) {
	      if (file_hdr.dist_set_current[sid_idx] == sid) break;
	    }
	    if (sid_idx == safe) continue;
	    

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
        ep = readdir(dp);
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
