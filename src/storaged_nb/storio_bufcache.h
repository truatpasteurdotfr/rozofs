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
 
 #ifndef STORIO_BUFCACHE_H
#define STORIO_BUFCACHE_H


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
/**
* structure of the buffer cache
*/
typedef struct _storio_bufcache_buf_header_t
{
   uint64_t owner_key;   /**< key of the owner of the 8K cache block          */
//#warning len is not needed for the case of the storio
   uint64_t len:14;      /**< effective length of the rebuilt block           */
   uint64_t filler:50;  /**< for future usage                                 */ 
   uint64_t prj_timestamp;  /**< timestamp of the projection                  */
} storio_bufcache_buf_header_t;
/**
*  max number of cache buffer pools: 256
*  the idx of a buffer pool is given by the hash of the fid and the file offset divided by 256K
*/
#define STORIO_CACHE_MAX_BUF_POOL_POWER_OF_2  8 
#define STORIO_CACHE_MAX_BUF_POOL  (1 << STORIO_CACHE_MAX_BUF_POOL_POWER_OF_2)  
/**
* max number of 256K buffers in a pool
*/
#define STORIO_CACHE_MAX_BUF_IN_A_POOL  256
/**
*  structure that defines a buffer pool: a cache entry reference it by the
*  local idx in the buffer pool
*/
typedef struct _storio_bufcache_buffer_pool_t
{
  uint8_t *buffer_timestamp[STORIO_CACHE_MAX_BUF_IN_A_POOL];
} storio_bufcache_buffer_pool_t;

/**
* structure that defines the table of pools
*/
typedef struct _storio_bufcache_pool_t
{
  storio_bufcache_buffer_pool_t *buffer_pool_p[STORIO_CACHE_MAX_BUF_POOL];
} storio_bufcache_pool_t;

typedef struct _storio_bufcache_stats_t
{
   uint64_t count_buf_timestamp;  /**< number of 256K buffers allocated */
   uint64_t coll_buf_timestamp;   /**< number of time a 256K bufer has taken from another entry */

} storio_bufcache_stats_t;
/**
* size of a payload of a cache unit entry
*/
#define STORIO_CACHE_BSIZE 1
/**
* number of unit entries per timestamp cache buffer
*/
#define STORIO_CACHE_BCOUNT 32

#define STORIO_CACHE_BUFFER_BSIZE (STORIO_CACHE_BCOUNT*STORIO_CACHE_BSIZE)

#define  ROZOFS_GCACHE_MAX_256K_BUF_COUNT_DEFAULT  1024
/*
**______________________________________________________________________________
*/
/**
* Global datas
*/
extern storio_bufcache_pool_t *storio_bufcache_bufpool_p ;  /**< pointer to the buffer  pool pointer array */
extern storio_bufcache_stats_t storio_bufcache_stats ;  /**< cache statistics*/
extern uint64_t storio_bufcache_max_buf256K;       /**< max configured number of 256K buffers  */

/*
**______________________________________________________________________________
*/
/**
*  Init of the global cache array
   The goal of that function is to create the array for storing the buffer
   pool pointer
   
   @param none
   
   @retval 0 on success
   @retval -1 on error (see errno for detail)
*/
int storio_bufcache_pool_init();


/*
**______________________________________________________________________________
*/
/**
* 
   allocate a buffer pool array of 256K buffers
   
   @param none
   
   @retval <>NULL on success
   @retval NULL on error (out of memory)
*/
storio_bufcache_buffer_pool_t *rozof_gcache_alloc_buffer_pool();

/*
**______________________________________________________________________________
*/
/**
* 
   allocate a buffer within a buffer pool
   
   
   @retval <>NULL on success: points to the beginning of the 256K buffer 
   @retval NULL on error (out of memory)
*/
uint8_t *rozof_gcache_alloc_256K_buffer( );

/**______________________________________________________________________________
*/
/**
*  hash computation from parent fid and filename (directory name or link name)

  @param h : initial hahs value
  @param key1: filename or directory name
  @param key2: parent fid
  
  @retval hash value
*/
static inline uint32_t storio_bufcache_pool_hash(void *key1, void *key2) {

    unsigned char *d = (unsigned char *) key1;
    uint32_t h;

    h = 2166136261U;
    /*
     ** hash on off
     */
    for (d = key1; d !=key1 + 8; d++) {
        h = (h * 16777619)^ *d;

    }
    /*
     ** hash on fid
     */
    d = (unsigned char *) key2;
    for (d = key2; d != key2 + 16; d++) {
        h = (h * 16777619)^ *d;

    }
    return h;
}

/*
**______________________________________________________________________________
*/
/**
*  The purpose of that service is to return the pointer to the beginning
   of a 256K cache buffer
   
   @param fid: fid of the file
   @param off file offset
   @param local_buffer_idx : local buffer idx in the pool
   
   @retval <> NULL pointer to the beginning of the buffer
   @retval NULL no buffer
*/
static inline uint8_t *storio_bufcache_get_buf_ts_from_idx(fid_t fid,uint64_t off, int buf_idx)
{

   int idx_pool;
   uint32_t hash;
   uint64_t off_key;
   
   off_key = off/STORIO_CACHE_BUFFER_BSIZE;
   /**
   * check the presence of the cache
   */
   if (storio_bufcache_bufpool_p == NULL) return NULL;
   /*
   ** check if the buffer idx is in range
   */
   if (buf_idx >= STORIO_CACHE_MAX_BUF_IN_A_POOL)
   {
     /*
     ** out of range
     */
     return NULL;
   }
   hash = storio_bufcache_pool_hash(&off_key,fid);
   
   idx_pool = hash%STORIO_CACHE_MAX_BUF_POOL;
//   info("FDL debug:---->  storio_bufcache_get_buf_ts_from_idx  idx_pool %d off %llu",idx_pool,(long long unsigned int)off);
   
   if (storio_bufcache_bufpool_p->buffer_pool_p[idx_pool] == NULL)
   {
      /*
      ** the pool does not exist
      */
      return NULL;
   }
   /*
   ** OK now check the buf_idx
   */
   storio_bufcache_buffer_pool_t *pool_p = storio_bufcache_bufpool_p->buffer_pool_p[idx_pool];
   uint8_t *buf256K_p = pool_p->buffer_timestamp[buf_idx];  
   
   return buf256K_p;
}

/*
**______________________________________________________________________________
*/
/**
*  The purpose of that service is to allocate 256K buffer
   
   @param fid: fid of the file
   @param off file offset
   @param local_buffer_idx_p : address where the local_buffer_idx will be provided
   
   @retval <> NULL pointer to the beginning of the buffer,*local_buffer_idx_p contains the index 
   @retval NULL no buffer, *local_buffer_idx_p is not significant
*/
uint8_t *storio_bufcache_alloc_buf_ts(fid_t fid,uint64_t off, int *local_buffer_idx_p);

/*
**______________________________________________________________________________
*/
/**
*  Flush the global cache : the purpose of that service is to release all
   the 256K buffers and associated structures that have been allocated
  
   
   @param none
   
   @retval none
*/
void storio_bufcache_flush();


/*
**______________________________________________________________________________
*/
/**
*  The purpose of that service is to write projection timestamp in a cache buffer

   off might not be aligned on a 8K boundary, in that case the copy will take place only
   if the block already belong to the file, otherwise we skip it.
   
   @param buf_cache_ts_p: pointer to the head of the cache buffer that contains the timestamps of the blocks
   @param off: offset within the file to read ( index of the projection)
   @param src_p : pointer to the array that contains the timestamps to write (associated with the projections)
   @param len : number of projections 
   @param key : key associated with the file (fid)
   @param empty_bitmap : pointer to the current empty bitmap (updated on successfull execution)
   @param empty_bitmap : pointer to the current presence bitmap (updated on successfull execution)
   
   @retval nunber of byte written
   @retval < 0: error
*/
int storio_bufcache_write(uint8_t *buf_cache_ts_p,uint64_t off,uint64_t *src_p,int len,uint64_t key,
                          uint32_t *empty_bitmap,uint32_t *presence_bitmap);

/*
**______________________________________________________________________________
*/
/**
*  The purpose of that service is to read the timestamps associated with a set of projections
   
   @param buf_cache_ts_p: pointer to the head of the cache buffer
   @param off: offset of the first projection index
   @param src_p : pointer to the source buffer that contains the timestamp to search
   @param dst_p : pointer to the destination buffer where timestamp value are copied
   @param cur_empty_prj_bitmap : current bitmap that describes the empty projection
   @param cur_presence_prj_bitmap : bitmap of the projections for which we have a valid timestamp
   @param len : len to copy
   @param key : key associated with the file (fid)
   
   @retval <> NULL pointer to the beginning of the buffer,*local_buffer_idx_p contains the index 
   @retval NULL no buffer, *local_buffer_idx_p is not significant
*/
static inline int storio_bufcache_read(uint8_t *buf_cache_ts_p,uint64_t *src_p,uint64_t off,
                                   uint64_t *dst_p,int len,uint64_t key,
                                   uint32_t cur_empty_prj_bitmap,
                                   uint32_t cur_presence_prj_bitmap 
                                  )
{
  storio_bufcache_buf_header_t *hdr_p = (storio_bufcache_buf_header_t*)buf_cache_ts_p;
  int block_count;
  int i;
  int first_block;
  int nb_hits = 0;

  first_block = off%STORIO_CACHE_BCOUNT;
  hdr_p +=first_block;

  block_count = len;

  /*
  ** need to figure what are the blocks that belong to the requester
  */
  while(1)
  {
    for(i = first_block; i < (first_block+block_count); i++,hdr_p++)
    {
      /*
      ** check the presence of the timestamp of the projection
      */
      if ((cur_presence_prj_bitmap & (1 << i)) == 0) continue;
      /*
      ** check the case of the empty projection)
      */
      if ((cur_empty_prj_bitmap & (1 << i)) != 0)
      {
        if (src_p[i] == 0) nb_hits++;
        /*
        ** we have an empty block : indicate it by setting the timestamp to 0
        */
        dst_p[i- first_block] = 0;
        continue;
      }
      if (hdr_p->owner_key != key) continue;
      /*
      ** the timestamp is not null, check if there is a match
      */
      if (hdr_p->prj_timestamp == src_p[i]) nb_hits++;
      /*
      ** store the timestamp
      */
      dst_p[i-first_block] = hdr_p->prj_timestamp;
    }
    return nb_hits;
  }
}
/*
**______________________________________________________________________________
*/
/** cache stats display
*/
void storio_bufcache_show_cache_stats(char * argv[], uint32_t tcpRef, void *bufRef);

/*
**______________________________________________________________________________
*/
/**
* the goal of that function is to provide the first relative index to read and the number of holes
  in the bitmap
  
  @param presence bitmap
  @param hole_p:pointer to hole count (returned parameter)
  @param nblocks : number of blocks to read
  
  @retval relative index of the first block to read
*/
static inline uint32_t  storio_get_first_bid_index_and_hole_count(uint32_t presence_bitmap,uint8_t nblocks,int *hole_p)
{
  int first_idx_relative = 0;
  int hole = 0;
  *hole_p = 0;
  int i;
  
  for ( i = 0; i < nblocks; i++)
  {
    if ((presence_bitmap & (1<<i) ) != 0)
    {
      if (hole == 0)
      {
        first_idx_relative++;
      }
    }
    else
    {
      if (hole == 0)
      hole++;  
    } 
  }
  *hole_p = hole;
  return first_idx_relative;
}



#ifdef __cplusplus
}
#endif /*__cplusplus */



#endif

