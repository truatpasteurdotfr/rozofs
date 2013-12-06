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
 
#ifndef STORIO_CACHE_H
#define STORIO_CACHE_H


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
#include "storio_bufcache.h"

/**
* Mode block cache constants
*/
#define STORIO_TSCACHE_LVL0_SZ_POWER_OF_2  6 
#define STORIO_TSCACHE_MAX_ENTRIES  (1024)

#define STORIO_TSCACHE_LVL0_SZ  (1 << STORIO_TSCACHE_LVL0_SZ_POWER_OF_2) 
#define STORIO_TSCACHE_LVL0_MASK  (STORIO_TSCACHE_LVL0_SZ-1)

/**
* state of the mode block cache
*/
#define STORIO_TSCACHE_ENABLE 1
#define STORIO_TSCACHE_DISABLE 0

/**
*  structure used for caching projection information in the storage cache
   the offset is given in projection units. Since rozofs works with 256K buffers
   and with a projection size of 8K, to cover 256K the system needs 32 projections
   whatever the projection size is (depends on the layout)
   the offset of the entry always starts on a 32 projections boundary.
*/
typedef union _storio_cache_off_bufidx_t
{
   uint64_t u64;
   struct
   {
       uint64_t off:42;  /**< offset in file: Max is 4 TeraBytes */
       uint64_t buf_idx:12;  /**< local buffer idx */
       uint64_t buf_idx_valid:1;  /**<assert to 1 when buffer idx is valid */
       uint64_t state:1;  /**< 0 free, 1: busy*/
       uint64_t filler:8;  /**< reserved*/
   }s;
} storio_cache_off_bufidx_t;

/**
* structure of a buffer that contains cache data (projection timestamp information:
*    off: offset within the file given in projection units
*/       
typedef struct _storio_cache_buffer_entry_t
{
  storio_cache_off_bufidx_t off_bufidx_st;
  uint64_t   owner_key;               /**< needed to check consistency with 8K cache buffer content  */
  uint32_t   empty_block_bitmap;      /**< bitmap of the blocks that are empty   */
  uint32_t   presence_block_bitmap;   /**< bitmap of the blocks that are present */
} storio_cache_buffer_entry_t;

/**
* search key for the fid
*/
typedef struct _storio_cache_key_t
{
  fid_t fid;  /**<  unique file id          */
} storio_cache_key_t;

/**
* structure that contains the pointers to n storio_cache_entry_t
*  object
*/
#define STORIO_CACHE_MX_BUF_PER_POOL  64  

typedef struct _storio_cache_pool_entry_t
{
    uint64_t bitmap;    /**< max entries : 64 */
   storio_cache_buffer_entry_t entry[STORIO_CACHE_MX_BUF_PER_POOL]; 
} storio_cache_pool_entry_t;

/**
*  structure associated with a file
*/
#define STORIO_CACHE_MX_POOL  128  

typedef struct _storio_cache_entry_t
{
  com_cache_entry_t   cache;   /** < common cache structure */
  storio_cache_key_t     key;
  storio_cache_pool_entry_t *pool[STORIO_CACHE_MX_POOL]; 
} storio_cache_entry_t;


typedef struct _storio_cache_stats
{
  uint64_t put_cache_hit;
  uint64_t put_cache_miss;
  uint64_t put_cache_coll;
  uint64_t put_cache_blocks;

  uint64_t get_cache_hit;
  uint64_t get_cache_miss;
  uint64_t get_cache_hit_bytes;
  uint64_t get_cache_req_blocks;

} storio_cache_stats_t;

/*
**______________________________________________________________________________

      FID LOOKUP SECTION
**______________________________________________________________________________
*/
extern com_cache_main_t  *storio_cache_cache_p; /**< pointer to the fid cache  */
extern uint64_t storio_cache_key;
extern storio_cache_stats_t  storio_cache_stats;
extern uint32_t storio_cache_enable_flag ;     /**< assert to 1 when the mode block cache is enable */

/*
**______________________________________________________________________________
*/
/**
* allocate an entry for the mode block cache

  @param fid : fid associated with the file
  
  @retval <>NULL: pointer to the cache entry
  @retval NULL : out of memory
*/
com_cache_entry_t *storio_cache_alloc_entry(fid_t fid);


/*
**______________________________________________________________________________
*/
/**
* allocate a key for a buffer
  
  @retval key_value
*/
static inline uint64_t storio_cache_alloc_key_buf()
{
  storio_cache_key++;
  if (storio_cache_key == 0) storio_cache_key =1;
  return storio_cache_key;

}


/*
**______________________________________________________________________________
*/
/**
* release an entry of the fid cache

  @param p : pointer to the user cache entry 
  
*/
void storio_cache_release_entry(void *entry_p);

/*
**______________________________________________________________________________
*/
/**
* creation of the FID cache
 That API is intented to be called during the initialization of the module
 
 The max number of entries is given the STORIO_TSCACHE_MAX_ENTRIES constant
 and the size of the level 0 entry set is given by STORIO_TSCACHE_LVL0_SZ_POWER_OF_2 constant
 
  @param init_state : initial state of the cache 1: enable , 0 disable

 retval 0 on success
 retval < 0 on error
*/
 
uint32_t storio_cache_cache_init(uint32_t init_state);

/*
**______________________________________________________________________________
*/
/**
*  Build the key to perform a lookup with a <fid>

  @param Pkey: pointer to the resulting key
  @param fid: file fid
 
  @retval none
*/
static inline void storio_cache_fid_build_key(storio_cache_key_t *Pkey,unsigned char *fid) 
{
  memcpy(Pkey->fid,fid,sizeof(fid_t));  
}

/*
**______________________________________________________________________________
*/
/**
*  Insert a set a timestamp associated with projections

  @param fid : fid associated with the file
  @param bid: first projection index in the timestamp table 
  @param nblocks: number of projections to insert
  @param relative_ts_idx: bid relative idx in the timestamp table
  @param timestamp_tb_p : pointer to the data array that contains the timestamps associated with the projections
  
  
  @retval 0 on success
  @retval -1 on error
*/
int storio_cache_insert(fid_t fid,uint64_t bid,uint32_t nblocks,uint64_t *timestamp_tb_p,int relative_ts_idx);

/*
**______________________________________________________________________________
*/
/**
*  get the list of the timestamps associated with a set of projections

  @param fid : fid associated with the file
  @param off: index of the first projection
  @param len: number of projections
  @param src_bufp : pointer to the source timestamp buffer
  @param dst_bufp : pointer to the destination timestamp buffer
  
  @retval number of hits
*/
int storio_cache_get(fid_t fid,uint64_t off,uint32_t size,uint64_t *src_bufp,uint64_t *dst_bufp);


/*
**______________________________________________________________________________
*/
/**
*  remove a fid from the mode block cache

  @param fid : fid associated with the file
  
  @retval none
*/
void storio_cache_remove(fid_t fid);
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
int storio_cache_check(fid_t fid,uint64_t off,uint32_t size);
/*
**______________________________________________________________________________
*/
char *storio_cache_stats_display(char *buffer);

/*
**______________________________________________________________________________
*/
/**
* service for clearing the mode block cache statistics

  @param none
  retval none
*/
void storio_cache_stats_clear();


/*
**______________________________________________________________________________
*/
/**
* service to enable the mode block cache

  @param none
  retval none
*/
void storio_cache_enable();
/*
**______________________________________________________________________________
*/
/**
* service to disable the mode block cache

  @param none
  retval none
*/
void storio_cache_disable();

/*
**______________________________________________________________________________
*/
/**
*  The goal of that function is to return the index of the first 
   block to read and the number of blocks to read base a 2 timestamps table
   That API can also be used for preparing the timestamp table for cache insertion

  @param off: index of the first projection
  @param len: number of projections
  @param src_bufp : pointer to the source timestamp buffer
  @param dst_bufp : pointer to the destination timestamp buffer
  
  @retval number of blocks that do not match
*/
static inline int storio_get_block_idx_and_len(uint64_t off,uint32_t size,uint64_t *src_bufp,uint64_t *dst_bufp,uint64_t *off_return)
{
  uint64_t relative_idx = 0;
  uint64_t inval_ts = 0;
  int last_idx = 0;
  int i;
  
  
  inval_ts -=1;
  
  for( i = 0; i < size; i++)
  {
    if (dst_bufp[i] == inval_ts) 
    {
      /*
      ** the block is invalid and must be read
      */
      break;    
    }  
    if (dst_bufp[i] == src_bufp[i]) continue;
    /*
    ** the block is invalid and must be read
    */
    break;        

  }
  relative_idx = i;
  *off_return = i+off;
  if (i == size)
  {
    return 0;
  }
  last_idx = i;
  i++;
  /*
  ** OK we have the start, need to figure out about the length: search
  ** for consecutive holes
  */
  for(; i < size; i++)
  {
    if ((dst_bufp[i] == inval_ts) || (dst_bufp[i] != src_bufp[i])) 
    {
      /*
      ** the block is invalid and must be read
      */
      last_idx = i;
      continue;
    }
  }
  return (last_idx+1) - relative_idx;

 }
 
 
 /*
**______________________________________________________________________________
*/
/**
*  The goal of that function is to return the index of the first 
   block to read and the number of blocks to read base a 2 timestamps table
   That API can also be used for preparing the timestamp table for cache insertion

  @param req_ts_tb_p : pointer to the source timestamp buffer
  @param res_ts_tb_p : pointer to the destination timestamp buffer
  
*/
static inline void storio_build_presence_and_empty_bitmaps(uint64_t *req_ts_tb_p,uint64_t *res_ts_tb_p,
                                                         uint32_t *presence_bitmap_p,uint32_t *empty_bitmap_p)
{
  int i;  
  uint64_t inval_ts = 0;
  inval_ts -=1;
  *presence_bitmap_p = 0;
  *empty_bitmap_p    = 0;

  
  for( i = 0; i < STORIO_CACHE_BCOUNT; i++)
  {
    if (res_ts_tb_p[i] == inval_ts) 
    {
      /*
      ** the block is invalid and must be read
      */
      break;    
    } 
    /*
    **  check if it is an empty block
    */
    if (res_ts_tb_p[i] == 0) 
    {
      *presence_bitmap_p |= (1<<i);
      *empty_bitmap_p    |= (1<<i);
      continue;
    }
    if (req_ts_tb_p[i] == res_ts_tb_p[i]) continue;
    /*
    ** no match the projection must be returned
    */
    *presence_bitmap_p |= (1<<i);
  }
}


#if 0
 /*
**______________________________________________________________________________
*/
/**
  Concatenate a bins buffer according the information found in the empty and presence bitmaps

  @param presence_bitmap_p : presence bitmap
  @param res_ts_tb_p : pointer to the destination timestamp buffer
  
*/
static inline void storio_concatenate(uint32_t presence_bitmap_p,uint32_t empty_bitmap_p,
                                                           char *bins, int bins_idx,int bins_count)
{
  int i;  
  rozofs_stor_bins_hdr_t *prj_hdr;
  char *bins_src = bins;
  char *bins_dst = bins;
  
  int bins_size = prj_size * sizeof (bin_t) + sizeof (rozofs_stor_bins_hdr_t);

  
  for( i = bins_idx; i < (bins_idx+bins_count); i++,bins_src+= bins_size)
  {
    if ((presence_bitmap_p & (1 << i))  == 0) continue;
    if ((empty_bitmap_p & (1 << i))  != 0) continue;    
    /*
    ** the block must be returned: check if src and dst are the same because
    ** in that caase there is nothing to
    */
    if (bins_src != bins_dst) 
    {
      memcpy(bins_dst,bins_src,bins_size);
    }
    bins_dst += bins_size;
  }
}
#endif

#ifdef __cplusplus
}
#endif /*__cplusplus */

#endif

