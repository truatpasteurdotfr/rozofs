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
#include "rozofs_cache.h"
#include <rozofs/core/uma_dbg_api.h>

/*
**______________________________________________________________________________
*/
/**
* Global datas
*/
rozofs_gcache_pool_t *rozofs_gcache_bufpool_p = NULL;  /**< pointer to the buffer  pool pointer array */
rozofs_gcache_stats_t rozofs_gcache_stats ;  /**< cache statistics*/
uint64_t rozofs_gcache_max_buf256K;       /**< max configured number of 256K buffers  */

/*
**______________________________________________________________________________
*/
/**
*  API for displaying the statistics of a cache

  @param buffer: buffer where to format the output
  @param p : pointer to the cache structure
  @param cache_name: name of the cache
  
  @retval none
*/
#define SHOW_STAT_GCACHE(prefix,probe) pChar += sprintf(pChar,"%-28s :  %10llu\n","  "#probe ,(long long unsigned int) stat_p->prefix ## probe);

void rozofs_gcache_show_cache_stats(char * argv[], uint32_t tcpRef, void *bufRef) 
{
   char *pChar = uma_dbg_get_buffer();
   int reset = 0;
   rozofs_gcache_stats_t *stat_p = &rozofs_gcache_stats;

   if (argv[1] != NULL)
   {
     if (strcmp(argv[1],"flush")==0) reset = 1;
   }
   if (reset)
   {
     rozofs_gcache_flush();
     uma_dbg_send(tcpRef, bufRef, TRUE, "Flush Done\n");    
     return;

   }
 
   pChar += sprintf(pChar,"%s:\n","Data Cache");
   pChar += sprintf(pChar,"level0_sz        : %d\n",ROZOFS_CACHE_MAX_BUF_POOL);
  // pChar += sprintf(pChar,"entries (max/cur): %u/%u\n\n",p->max,p->size);

   pChar +=sprintf(pChar,"%-28s :  %10llu\n","256K buffers max.",(long long unsigned int)rozofs_gcache_max_buf256K);
   SHOW_STAT_GCACHE(count_,buf256K );
   SHOW_STAT_GCACHE(coll_,buf256K );

   uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());    
}

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
rozofs_gcache_buffer_pool_t *rozof_gcache_alloc_buffer_pool()
{
   rozofs_gcache_buffer_pool_t *p;
   /*
   ** allocate the array for storing the buffer pools content
   */
   p = malloc(sizeof(rozofs_gcache_pool_t));
   if (p == NULL)
   {
     return NULL;   
   }
   /**
   * clear all the pointers
   */
   memset(p,0,sizeof(rozofs_gcache_buffer_pool_t));
   return p;
}

/*
**______________________________________________________________________________
*/
/**
* 
   allocate a buffer within a buffer pool
   
   
   @retval <>NULL on success: points to the beginning of the 256K buffer 
   @retval NULL on error (out of memory)
*/
uint8_t *rozof_gcache_alloc_256K_buffer()
{
   uint8_t *p;
   int i;
   /*
   ** allocate the array for storing the buffer pools content
   */
/*
   info("rozof_gcache_alloc_256K_buffer  size %d (%x)",
         (int)(ROZOFS_CACHE_BCOUNT*(ROZOFS_CACHE_BSIZE+sizeof(rozofs_cache_buf_header_t))),
         (unsigned int)(ROZOFS_CACHE_BCOUNT*(ROZOFS_CACHE_BSIZE+sizeof(rozofs_cache_buf_header_t))));
*/
   p = malloc(ROZOFS_CACHE_BCOUNT*(ROZOFS_CACHE_BSIZE+sizeof(rozofs_cache_buf_header_t)));
   if (p == NULL)
   {
     return NULL;  
   }
   /**
   * clear all the headers
   */
   rozofs_cache_buf_header_t *hdr_p = (rozofs_cache_buf_header_t*) p;
   for (i = 0; i< ROZOFS_CACHE_BCOUNT; i++,hdr_p++)
   {
     hdr_p->owner_key = 0;
     hdr_p->len = 0;
     hdr_p->filler = 0;   
   }
   /*
   ** updates stats
   */
   rozofs_gcache_stats.count_buf256K++;
   return p;
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
uint8_t *rozofs_cache_alloc_buf256K(fid_t fid,uint64_t off, int *local_buffer_idx_p)
{

   int idx_pool;
   uint32_t hash;
   int i;
   uint64_t off_key;
   
   off_key = off/ROZOFS_CACHE_BUFFER_BSIZE;
   /**
   * check the presence of the cache
   */
   if (rozofs_gcache_bufpool_p == NULL) return NULL;
   /*
   ** find out on which pool we must allocate the entry
   */
   hash = rozofs_cache_pool_hash(&off_key,fid);
   
   idx_pool = hash%ROZOFS_CACHE_MAX_BUF_POOL;
/*
   info("FDL debug:---->  rozofs_cache_alloc_buf256K  idx_pool %d off %llu",idx_pool,
        (unsigned long long int)off);
*/   
   if (rozofs_gcache_bufpool_p->buffer_pool_p[idx_pool] == NULL)
   {
      /*
      ** the pool does not exist, so create it
      */
      rozofs_gcache_bufpool_p->buffer_pool_p[idx_pool] = rozof_gcache_alloc_buffer_pool();
      if (rozofs_gcache_bufpool_p->buffer_pool_p[idx_pool] == NULL)
      {
        /*
        ** out of memory
        */
        return NULL;
      }
   }
   /*
   ** OK now check find out a free entry
   */
   uint8_t *buffer_256K = NULL;
   int free_idx = -1;
   rozofs_gcache_buffer_pool_t *pool_p = rozofs_gcache_bufpool_p->buffer_pool_p[idx_pool];
   for (i = 0; i <ROZOFS_CACHE_MAX_BUF_IN_A_POOL; i++)
   { 
      if (pool_p->buffer_256K[i] == NULL) 
      {
        free_idx = i;
        break;      
      }  
   }
   /*
   ** check for free idx
   */
   if (free_idx != -1)
   {
      /*
      ** Ok, let's allocate a new buffer
      */
      buffer_256K = rozof_gcache_alloc_256K_buffer();
      pool_p->buffer_256K[i] = buffer_256K;   
      *local_buffer_idx_p = (uint8_t)i;

   }
   else
   {
     /*
     ** take one buffer that is inuse
     */
     i = ( hash>>16) & (ROZOFS_CACHE_MAX_BUF_IN_A_POOL -1);
     buffer_256K = pool_p->buffer_256K[i];
     *local_buffer_idx_p = (uint8_t)i;
     rozofs_gcache_stats.coll_buf256K++;

   }
   return buffer_256K;
}


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
int rozos_cache_write(uint8_t *buf_cache256K_p,uint64_t off,uint8_t *src_p,int len,uint64_t key)
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
int rozos_cache_write_not_aligned(uint8_t *buf_cache256K_p,uint64_t off,uint8_t *src_p,int len,uint64_t key)
{
  rozofs_cache_buf_header_t *hdr_p = (rozofs_cache_buf_header_t*)buf_cache256K_p;
  uint8_t *payload_p   = (uint8_t*) (hdr_p+ROZOFS_CACHE_BCOUNT);
  int relative_offset  = off%ROZOFS_CACHE_BUFFER_BSIZE;
  int relative_8K_block_offset  = relative_offset%ROZOFS_CACHE_BSIZE;
  int block_count;
  int last_len;
  int block_len        = ROZOFS_CACHE_BSIZE;
  int first_block;
  int i;
  int effective_length;
  
  /*
  ** we need effective length because it might be possible to skip the first 8K buffer if
  ** the offset does not start on a 8K boundary and when the first 8K block does not belong
  ** to the file
  */
  effective_length = len;
  /*
  ** now since the length has been adjusted compute the number of blocks and the remaining
  ** length for the last block
  */
  block_count = (len-relative_8K_block_offset)/ROZOFS_CACHE_BSIZE;
  last_len    = (len-relative_8K_block_offset)%ROZOFS_CACHE_BSIZE;
  /*
  ** adjust the payload and the index of the first 8K block where data will be filled in
  */
  payload_p += relative_offset;
  first_block = relative_offset/block_len;
  hdr_p +=first_block;
  
  if ((relative_offset + len ) > ROZOFS_CACHE_BUFFER_BSIZE)
  {
     severe ("total length is greater than the buffer space %d (max is %d)",relative_offset + len,ROZOFS_CACHE_BUFFER_BSIZE);
     return -1;
  }
  /*
  ** check if the off starts on a 8K block boundary because in that case we need to 
  ** check if the 8K array belongs to the file
  */
  if (relative_8K_block_offset != 0)
  {
    if (hdr_p->owner_key != key)
    {
      /*
      ** the 8K block belongs to another file, so we cannot take that block
      ** we need to adjust the length and the offset in the source and destination buffer
      */
      payload_p += relative_8K_block_offset;
      effective_length -= relative_8K_block_offset;
      src_p += relative_8K_block_offset;
      i = first_block+1;
      hdr_p++;
    }
    else
    {
     i = first_block;
    }      
  }
  for(; i < (first_block+block_count); i++,hdr_p++)
  {
    hdr_p->owner_key = key;
    hdr_p->len = block_len;  
  }
  if (last_len != 0) 
  {
    /*
    ** check if the file is the owner of the 8K block, since in that case we do not need to update
    ** the length excepted if the length is less than last_len
    */
    if (hdr_p->owner_key == key)
    {
      if (hdr_p->len < last_len) hdr_p->len = last_len;
    }
    else
    {
      hdr_p->owner_key = key;
      hdr_p->len = last_len; 
    }   
  }
  /*
  ** copy the data in the payload
  */
  memcpy(payload_p,src_p,effective_length);
  return len;

}

/*
**______________________________________________________________________________
*/
/**
*  Flush a 256K buffers pool
  
   
   @param pool : pointer to the 256K buffers pool
   
   @retval none
*/
void rozofs_gcache_buf256K_pool_flush(rozofs_gcache_buffer_pool_t *pool)
{
  int i;
  
  /*
  ** check if the pool exists
  */
  if (pool == NULL) return;
  
  for (i = 0; i < ROZOFS_CACHE_MAX_BUF_IN_A_POOL; i ++)
  {
    if (pool->buffer_256K[i] == NULL) continue;
    /*
    ** release the associate dmemory
    */
    free(pool->buffer_256K[i] );
    rozofs_gcache_stats.count_buf256K-- ;

  }
  /*
  ** release the pool
  */
  free(pool);
}

/*
**______________________________________________________________________________
*/
/**
*  Flush the global cache : the purpose of that service is to release all
   the 256K buffers and associated structures that have been allocated
  
   
   @param none
   
   @retval none
*/
void rozofs_gcache_flush()
{
  int i;
  
  /*
  ** check if the cache exists
  */
  if (rozofs_gcache_bufpool_p == NULL) return;
  for (i = 0; i < ROZOFS_CACHE_MAX_BUF_POOL; i ++)
  {
    if (rozofs_gcache_bufpool_p->buffer_pool_p[i] == NULL) continue;
    /*
    ** flush the 256K Buffer pool entry
    */
    rozofs_gcache_buf256K_pool_flush(rozofs_gcache_bufpool_p->buffer_pool_p[i]);
    rozofs_gcache_bufpool_p->buffer_pool_p[i] = NULL;  
    
  }
  rozofs_gcache_stats.coll_buf256K = 0;

}
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
int rozofs_gcache_pool_init()
{
   
   /*
   ** allocate the array for storing the buffer pools content
   */
   rozofs_gcache_bufpool_p = malloc(sizeof(rozofs_gcache_pool_t));
   if (rozofs_gcache_bufpool_p == NULL)
   {
     errno = ENOMEM;
     return -1;
   }
   /**
   * clear all the pointers
   */
   memset(rozofs_gcache_bufpool_p,0,sizeof(rozofs_gcache_pool_t));
   /*
   ** clear the stats array
   */
   memset(&rozofs_gcache_stats,0,sizeof(rozofs_gcache_stats_t));
   
   rozofs_gcache_max_buf256K = ROZOFS_GCACHE_MAX_256K_BUF_COUNT_DEFAULT;
   
   return 0;
}


