/*
  Copyright (c) 2010 Fizians SAS. <http://www.fizians.com>
  This file is part of Rozofs.

  Rozofs is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  Rozofs is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
 */
 
 #ifndef ROZOFS_MODEBLOCK_CACHE_H
#define ROZOFS_MODEBLOCK_CACHE_H


#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/


#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <uuid/uuid.h>

#include <rozofs/rozofs.h>
#include <rozofs/common/log.h>
#include <rozofs/common/list.h>
#include <rozofs/common/xmalloc.h>
#include <rozofs/common/profile.h>
#include <rozofs/common/mattr.h>
#include <rozofs/core/com_cache.h>
/**
* Mode block cache constants
*/
#define ROZOFS_MBCACHE_LVL0_SZ_POWER_OF_2  6 
#define ROZOFS_MBCACHE_MAX_ENTRIES  (1024)

#define ROZOFS_MBCACHE_LVL0_SZ  (1 << ROZOFS_MBCACHE_LVL0_SZ_POWER_OF_2) 
#define ROZOFS_MBCACHE_LVL0_MASK  (ROZOFS_MBCACHE_LVL0_SZ-1)

/**
* state of the mode block cache
*/
#define ROZOFS_MBCACHE_ENABLE 1
#define ROZOFS_MBCACHE_DISABLE 0

/**
*
*/
typedef union _rozofs_mbcache_off_bufidx_t
{
   uint64_t u64;
   struct
   {
       uint64_t off:42;  /**< offset in file: Max is 4 TeraBytes */
       uint64_t buf_idx:8;  /**< local buffer idx */
       uint64_t buf_idx_valid:1;  /**<assert to 1 when buffer idx is valid */
       uint64_t state:1;  /**< 0 free, 1: busy*/
       uint64_t filler:12;  /**< reserved*/
   }s;
} rozofs_mbcache_off_bufidx_t;

/**
* structure of a buffer that contains cache data:
*    off: offset within the file
*    len: effective length of the block
*    state: 0: free /1:busy
*    pbuf : pointer to the memory array where data is stored
*/       
typedef struct _rozofs_mbcache_buffer_entry_t
{
  rozofs_mbcache_off_bufidx_t off_bufidx_st;
  uint64_t   owner_key;   /**< needed to check consistency with 8K cache buffer content  */
} rozofs_mbcache_buffer_entry_t;

#define ROZOFS_MBCACHE_BUF_SIZE (1024*256)     
/**
* search key for the fid
*/
typedef struct _rozofs_mbcache_key_t
{
  fid_t fid;  /**<  unique file id          */
} rozofs_mbcache_key_t;

/**
* structure that contains the pointers to n rozofs_mbcache_entry_t
*  object
*/
#define ROZOFS_MBCACHE_MX_BUF_PER_POOL  64  

typedef struct _rozofs_mbcache_pool_entry_t
{
    uint64_t bitmap;    /**< max entries : 64 */
   rozofs_mbcache_buffer_entry_t entry[ROZOFS_MBCACHE_MX_BUF_PER_POOL]; 
} rozofs_mbcache_pool_entry_t;

/**
*  structure associated with a file
*/
#define ROZOFS_MBCACHE_MX_POOL  128  

typedef struct _rozofs_mbcache_entry_t
{
  com_cache_entry_t   cache;   /** < common cache structure */
  rozofs_mbcache_key_t     key;
  rozofs_mbcache_pool_entry_t *pool[ROZOFS_MBCACHE_MX_POOL]; 
} rozofs_mbcache_entry_t;


typedef struct _rozofs_mbcache_stats
{
  uint64_t put_cache_hit;
  uint64_t put_cache_miss;
  uint64_t put_cache_coll;
  uint64_t put_cache_bytes;

  uint64_t get_cache_hit;
  uint64_t get_cache_miss;
  uint64_t get_cache_hit_bytes;
  uint64_t get_cache_req_bytes;

} rozofs_mbcache_stats_t;

/*
**______________________________________________________________________________

      FID LOOKUP SECTION
**______________________________________________________________________________
*/
extern com_cache_main_t  *rozofs_mbcache_cache_p; /**< pointer to the fid cache  */
extern uint64_t rozofs_mbcache_key;
extern rozofs_mbcache_stats_t  rozofs_mbcache_stats;
extern uint32_t rozofs_mbcache_enable_flag ;     /**< assert to 1 when the mode block cache is enable */

/*
**______________________________________________________________________________
*/
/**
* allocate an entry for the mode block cache

  @param fid : fid associated with the file
  
  @retval <>NULL: pointer to the cache entry
  @retval NULL : out of memory
*/
com_cache_entry_t *rozofs_mbcache_alloc_entry(fid_t fid);


/*
**______________________________________________________________________________
*/
/**
* allocate a key for a buffer
  
  @retval key_value
*/
static inline uint64_t rozofs_mbcache_alloc_key_buf()
{
  rozofs_mbcache_key++;
  if (rozofs_mbcache_key == 0) rozofs_mbcache_key =1;
  return rozofs_mbcache_key;

}


/*
**______________________________________________________________________________
*/
/**
* release an entry of the fid cache

  @param p : pointer to the user cache entry 
  
*/
void rozofs_mbcache_release_entry(void *entry_p);

/*
**______________________________________________________________________________
*/
/**
* creation of the FID cache
 That API is intented to be called during the initialization of the module
 
 The max number of entries is given the ROZOFS_MBCACHE_MAX_ENTRIES constant
 and the size of the level 0 entry set is given by ROZOFS_MBCACHE_LVL0_SZ_POWER_OF_2 constant
 
  @param init_state : initial state of the cache 1: enable , 0 disable

 retval 0 on success
 retval < 0 on error
*/
 
uint32_t rozofs_mbcache_cache_init(uint32_t init_state);

/*
**______________________________________________________________________________
*/
/**
*  Build the key to perform a lookup with a <fid>

  @param Pkey: pointer to the resulting key
  @param fid: file fid
 
  @retval none
*/
static inline void rozofs_mbcache_fid_build_key(rozofs_mbcache_key_t *Pkey,unsigned char *fid) 
{
  memcpy(Pkey->fid,fid,sizeof(fid_t));  
}

/*
**______________________________________________________________________________
*/
/**
*  Insert a data block in the mode block cache

  @param off: offset within the file
  @param len: length to insert
  @param src_bufp : pointer to the data array
  @param fid : fid associated with the file
  
  @retval 0 on success
  @retval -1 on error
*/
int rozofs_mbcache_insert(fid_t fid,uint64_t off,uint32_t size,uint8_t *src_bufp);

/*
**______________________________________________________________________________
*/
/**
*  get a data block from the mode block cache

  @param off: offset within the file
  @param len: length to insert
  @param src_bufp : pointer to the data array
  @param fid : fid associated with the file
  
  @retval >= effective read length
  @retval -1 on error
*/
int rozofs_mbcache_get(fid_t fid,uint64_t off,uint32_t size,uint8_t *dst_bufp);

/*
**______________________________________________________________________________
*/
/**
*  check the presence of a data block in the cache

  @param off: offset within the file
  @param len: length to insert
  @param fid : fid associated with the file
  
  @retval 0 indicates the presence (but it does indicates that the 256 buffer is owned by the requester
  @retval -1 on error
*/
int rozofs_mbcache_check(fid_t fid,uint64_t off,uint32_t size);
/*
**______________________________________________________________________________
*/
char *rozofs_mbcache_stats_display(char *buffer);

/*
**______________________________________________________________________________
*/
/**
* service for clearing the mode block cache statistics

  @param none
  retval none
*/
void rozofs_mbcache_stats_clear();


/*
**______________________________________________________________________________
*/
/**
* service to enable the mode block cache

  @param none
  retval none
*/
void rozofs_mbcache_enable();
/*
**______________________________________________________________________________
*/
/**
* service to disable the mode block cache

  @param none
  retval none
*/
void rozofs_mbcache_disable();

#ifdef __cplusplus
}
#endif /*__cplusplus */

#endif

