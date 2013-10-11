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
 
 #ifndef ROZOFS_CACHE_H
#define ROZOFS_CACHE_H


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
typedef struct _rozofs_cache_buf_header_t
{
   uint64_t owner_key;   /**< key of the owner of the 8K cache block  */
   uint64_t len:14;      /**< effective length of the block           */
   uint64_t filler:50;  /**< for future usage                         */ 
} rozofs_cache_buf_header_t;
/**
*  max number of cache buffer pools: 256
*  the idx of a buffer pool is given by the hash of the fid and the file offset divided by 256K
*/
#define ROZOFS_CACHE_MAX_BUF_POOL_POWER_OF_2  8 
#define ROZOFS_CACHE_MAX_BUF_POOL  (1 << ROZOFS_CACHE_MAX_BUF_POOL_POWER_OF_2)  
/**
* max number of 256K buffers in a pool
*/
#define ROZOFS_CACHE_MAX_BUF_IN_A_POOL  256
/**
*  structure that defines a buffer pool: a cache entry reference it by the
*  local idx in the buffer pool
*/
typedef struct _rozofs_gcache_buffer_pool_t
{
  uint8_t *buffer_256K[ROZOFS_CACHE_MAX_BUF_IN_A_POOL];
} rozofs_gcache_buffer_pool_t;

/**
* structure that defines the table of pools
*/
typedef struct _rozofs_gcache_pool_t
{
  rozofs_gcache_buffer_pool_t *buffer_pool_p[ROZOFS_CACHE_MAX_BUF_POOL];
} rozofs_gcache_pool_t;

typedef struct _rozofs_gcache_stats_t
{
   uint64_t count_buf256K;  /**< number of 256K buffers allocated */
   uint64_t coll_buf256K;   /**< bumber of time a 256K bufer has taken from another entry */

} rozofs_gcache_stats_t;
/**
* size of a payload of a cache unit entry
*/
#define ROZOFS_CACHE_BSIZE 8192
/**
* number of unit entries per cache buffer
*/
#define ROZOFS_CACHE_BCOUNT 32

#define ROZOFS_CACHE_BUFFER_BSIZE (ROZOFS_CACHE_BCOUNT*ROZOFS_CACHE_BSIZE)

#define  ROZOFS_GCACHE_MAX_256K_BUF_COUNT_DEFAULT  1024
/*
**______________________________________________________________________________
*/
/**
* Global datas
*/
extern rozofs_gcache_pool_t *rozofs_gcache_bufpool_p ;  /**< pointer to the buffer  pool pointer array */
extern rozofs_gcache_stats_t rozofs_gcache_stats ;  /**< cache statistics*/
extern uint64_t rozofs_gcache_max_buf256K;       /**< max configured number of 256K buffers  */

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
int rozofs_gcache_pool_init();


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
rozofs_gcache_buffer_pool_t *rozof_gcache_alloc_buffer_pool();

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
static inline uint32_t rozofs_cache_pool_hash(void *key1, void *key2) {

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
static inline uint8_t *rozofs_cache_get_buf256K_from_idx(fid_t fid,uint64_t off, int buf_idx)
{

   int idx_pool;
   uint32_t hash;
   uint64_t off_key;
   
   off_key = off/ROZOFS_CACHE_BUFFER_BSIZE;
   /**
   * check the presence of the cache
   */
   if (rozofs_gcache_bufpool_p == NULL) return NULL;
   /*
   ** check if the buffer idx is in range
   */
   if (buf_idx >= ROZOFS_CACHE_MAX_BUF_IN_A_POOL)
   {
     /*
     ** out of range
     */
     return NULL;
   }
   hash = rozofs_cache_pool_hash(&off_key,fid);
   
   idx_pool = hash%ROZOFS_CACHE_MAX_BUF_POOL;
//   info("FDL debug:---->  rozofs_cache_get_buf256K_from_idx  idx_pool %d off %llu",idx_pool,(long long unsigned int)off);
   
   if (rozofs_gcache_bufpool_p->buffer_pool_p[idx_pool] == NULL)
   {
      /*
      ** the pool does not exist
      */
      return NULL;
   }
   /*
   ** OK now check the buf_idx
   */
   rozofs_gcache_buffer_pool_t *pool_p = rozofs_gcache_bufpool_p->buffer_pool_p[idx_pool];
   uint8_t *buf256K_p = pool_p->buffer_256K[buf_idx];  
   
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
uint8_t *rozofs_cache_alloc_buf256K(fid_t fid,uint64_t off, int *local_buffer_idx_p);

/*
**______________________________________________________________________________
*/
/**
*  Flush the global cache : the purpose of that service is to release all
   the 256K buffers and associated structures that have been allocated
  
   
   @param none
   
   @retval none
*/
void rozofs_gcache_flush();

/*
**______________________________________________________________________________
*/
/**
*  The purpose of that service is to write data in a 256K buffer

   off might not be aligned on a 8K boundary, in that case the copy will take place only
   if the block already belong to the file, otherwise we skip it.
   
   @param buf_cache256K_p: pointer to the head of the cache buffer
   @param off: offset within the file to read
   @param src_p : pointer to the source data to copy
   @param len : len to copy
   @param key : key associated with the file (fid)
   
   @retval nunber of byte written
   @retval < 0: error
*/
int rozos_cache_write_not_aligned(uint8_t *buf_cache256K_p,uint64_t off,uint8_t *src_p,int len,uint64_t key);

/*
**______________________________________________________________________________
*/
/**
*  The purpose of that service is to write data in a 256K buffer
   
   @param buf_cache256K_p: pointer to the head of the cache buffer
   @param off: offset within the file to read
   @param src_p : pointer to the source data to copy
   @param len : len to copy
   @param key : key associated with the file (fid)
   
   @retval nunber of byte written
   @retval < 0: error
*/
int rozos_cache_write(uint8_t *buf_cache256K_p,uint64_t off,uint8_t *src_p,int len,uint64_t key);
#if 0
{
  rozofs_cache_buf_header_t *hdr_p = (rozofs_cache_buf_header_t*)buf_cache256K_p;
  uint8_t *payload_p   = (uint8_t*) (hdr_p+ROZOFS_CACHE_BCOUNT);
  int relative_offset  = off%ROZOFS_CACHE_BUFFER_BSIZE;
  int block_count      = len/ROZOFS_CACHE_BSIZE;
  int last_len         = len%ROZOFS_CACHE_BSIZE;
  int block_len        = ROZOFS_CACHE_BSIZE;
  int first_block_len;
  int i;
  /*
  ** adjust the payload and the index of the first 8K block where data will be filled in
  */
  payload_p += relative_offset;
  first_block_len = relative_offset/block_len;
  hdr_p +=first_block_len;
  
  if ((relative_offset + len ) > ROZOFS_CACHE_BUFFER_BSIZE)
  {
     severe ("total length is greater than the buffer space %d (max is %d)",relative_offset + len,ROZOFS_CACHE_BUFFER_BSIZE);
     return -1;
  }
  for(i = first_block_len; i < (first_block_len+block_count); i++,hdr_p++)
  {
    hdr_p->owner_key = key;
    hdr_p->len = block_len;  
  }
  if (last_len != 0) 
  {
    hdr_p->owner_key = key;
    hdr_p->len = last_len;    
  }
  /*
  ** copy the data in the payload
  */
  memcpy(payload_p,src_p,len);
  return len;

}
#endif
/*
**______________________________________________________________________________
*/
/**
*  The purpose of that service is to write data in a 256K buffer
   
   @param buf_cache256K_p: pointer to the head of the cache buffer
   @param off: offset within the file to read
   @param dst_p : pointer to the destination buffer 
   @param len : len to copy
   @param key : key associated with the file (fid)
   
   @retval <> NULL pointer to the beginning of the buffer,*local_buffer_idx_p contains the index 
   @retval NULL no buffer, *local_buffer_idx_p is not significant
*/
static inline int rozos_cache_read(uint8_t *buf_cache256K_p,uint64_t off,uint8_t *dst_p,int len,uint64_t key)
{
  rozofs_cache_buf_header_t *hdr_p = (rozofs_cache_buf_header_t*)buf_cache256K_p;
  int block_count;
  int last_len;
  int len_adjusted;
  uint8_t *payload_p  = (uint8_t*) (hdr_p+ROZOFS_CACHE_BCOUNT);  
  int relative_offset = off%ROZOFS_CACHE_BUFFER_BSIZE;  
  int i;
  int eof = 0;
  int len_read = 0;
  int first_block_len;

  /*
  ** adjust the payload and the index of the first 8K block where data are supposed to be 
  */  
  payload_p += relative_offset;
  first_block_len = relative_offset/ROZOFS_CACHE_BSIZE;
  hdr_p +=first_block_len;

  /*
  ** adjust the length to read
  */
  if ((relative_offset + len ) > ROZOFS_CACHE_BUFFER_BSIZE)
  {
    len_adjusted = ROZOFS_CACHE_BUFFER_BSIZE - relative_offset;
  }
  else
  {
    len_adjusted = len;  
  }
  block_count = len_adjusted/ROZOFS_CACHE_BSIZE;
  last_len    = len%ROZOFS_CACHE_BSIZE;

  /*
  ** need to figure what are the 8K block that belongs to the requester
  */
  while(1)
  {
    for(i = first_block_len; i < (first_block_len+block_count); i++,hdr_p++)
    {
      if (hdr_p->owner_key == key) 
      {
        len_read += hdr_p->len;
        if (hdr_p->len != ROZOFS_CACHE_BSIZE) 
        {
          eof = 1;
          break;
        }
        continue;
      }
      break;
    }
    /*
    ** check the case of eof: because in that case it does not make
    ** sense to address the case of the last_len
    */
    if (eof) break;
    /*
    ** check if there is at least a block that belongs to the file
    */
    if (i ==  first_block_len)
    {
      /*
      ** no block
      */
      return -1;
    }
    /*
    ** check if all of them belongs to the requester, otherwise we leeave here
    */
    if ( i == (first_block_len+block_count))
    {
      /*
      ** check the last
      */
      if (last_len != 0) 
      {
        if (hdr_p->owner_key == key) len_read+= last_len;
        len_read += last_len;    
      }
    }
    break;
  }
  /*
  ** ok now check the length to read
  */
  /*
  ** copy the data in the payload
  */
  memcpy(dst_p,payload_p,len_read);
  return len_read;

}
/*
**______________________________________________________________________________
*/
/** cache stats display
*/
void rozofs_gcache_show_cache_stats(char * argv[], uint32_t tcpRef, void *bufRef);


#ifdef __cplusplus
}
#endif /*__cplusplus */




#endif

