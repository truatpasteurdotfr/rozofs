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
#include "storio_bufcache.h"
#include <rozofs/core/uma_dbg_api.h>

/*
**______________________________________________________________________________
*/
/**
* Global datas
*/
storio_bufcache_pool_t *storio_bufcache_bufpool_p = NULL;  /**< pointer to the buffer  pool pointer array */
storio_bufcache_stats_t storio_bufcache_stats ;  /**< cache statistics*/
uint64_t storio_bufcache_max_buftimestamp;       /**< max configured number of 256K buffers  */

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

static char localBuf[4096];

void storio_bufcache_show_cache_stats(char * argv[], uint32_t tcpRef, void *bufRef) 
{
   char *pChar = localBuf;
   int reset = 0;
   storio_bufcache_stats_t *stat_p = &storio_bufcache_stats;

   if (argv[1] != NULL)
   {
     if (strcmp(argv[1],"flush")==0) reset = 1;
   }
   if (reset)
   {
     storio_bufcache_flush();
     uma_dbg_send(tcpRef, bufRef, TRUE, "Flush Done\n");    
     return;

   }
 
   pChar += sprintf(pChar,"%s:\n","Data Cache");
   pChar += sprintf(pChar,"level0_sz        : %d\n",STORIO_CACHE_MAX_BUF_POOL);
  // pChar += sprintf(pChar,"entries (max/cur): %u/%u\n\n",p->max,p->size);

   pChar +=sprintf(pChar,"%-28s :  %10llu\n","timestamp buffers max.",(long long unsigned int)storio_bufcache_max_buftimestamp);
   SHOW_STAT_GCACHE(count_,buf_timestamp );
   SHOW_STAT_GCACHE(coll_,buf_timestamp );

   uma_dbg_send(tcpRef, bufRef, TRUE, localBuf);    
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
storio_bufcache_buffer_pool_t *rozof_gcache_alloc_buffer_pool()
{
   storio_bufcache_buffer_pool_t *p;
   /*
   ** allocate the array for storing the buffer pools content
   */
   p = malloc(sizeof(storio_bufcache_pool_t));
   if (p == NULL)
   {
     return NULL;   
   }
   /**
   * clear all the pointers
   */
   memset(p,0,sizeof(storio_bufcache_buffer_pool_t));
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
uint8_t *rozof_gcache_alloc_timestamp_buffer()
{
   uint8_t *p;
   int i;
   /*
   ** allocate the array for storing the buffer pools content
   */
/*
   info("rozof_gcache_alloc_timestamp_buffer  size %d (%x)",
         (int)(STORIO_CACHE_BCOUNT*(ROZOFS_CACHE_BSIZE+sizeof(storio_bufcache_buf_header_t))),
         (unsigned int)(STORIO_CACHE_BCOUNT*(ROZOFS_CACHE_BSIZE+sizeof(storio_bufcache_buf_header_t))));
*/
   p = malloc(STORIO_CACHE_BCOUNT*sizeof(storio_bufcache_buf_header_t));
   if (p == NULL)
   {
     return NULL;  
   }
   /**
   * clear all the headers
   */
   storio_bufcache_buf_header_t *hdr_p = (storio_bufcache_buf_header_t*) p;
   for (i = 0; i< STORIO_CACHE_BCOUNT; i++,hdr_p++)
   {
     hdr_p->owner_key = 0;
     hdr_p->len = 0;
     hdr_p->filler = 0;   
   }
   /*
   ** updates stats
   */
   storio_bufcache_stats.count_buf_timestamp++;
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
uint8_t *storio_bufcache_alloc_buf_ts(fid_t fid,uint64_t off, int *local_buffer_idx_p)
{

   int idx_pool;
   uint32_t hash;
   int i;
   uint64_t off_key;
   
   off_key = off/STORIO_CACHE_BUFFER_BSIZE;
   /**
   * check the presence of the cache
   */
   if (storio_bufcache_bufpool_p == NULL) return NULL;
   /*
   ** find out on which pool we must allocate the entry
   */
   hash = storio_bufcache_pool_hash(&off_key,fid);
   
   idx_pool = hash%STORIO_CACHE_MAX_BUF_POOL;
/*
   info("FDL debug:---->  storio_bufcache_alloc_buf_ts  idx_pool %d off %llu",idx_pool,
        (unsigned long long int)off);
*/   
   if (storio_bufcache_bufpool_p->buffer_pool_p[idx_pool] == NULL)
   {
      /*
      ** the pool does not exist, so create it
      */
      storio_bufcache_bufpool_p->buffer_pool_p[idx_pool] = rozof_gcache_alloc_buffer_pool();
      if (storio_bufcache_bufpool_p->buffer_pool_p[idx_pool] == NULL)
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
   uint8_t *buffer_timestamp = NULL;
   int free_idx = -1;
   storio_bufcache_buffer_pool_t *pool_p = storio_bufcache_bufpool_p->buffer_pool_p[idx_pool];
   for (i = 0; i <STORIO_CACHE_MAX_BUF_IN_A_POOL; i++)
   { 
      if (pool_p->buffer_timestamp[i] == NULL) 
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
      buffer_timestamp = rozof_gcache_alloc_timestamp_buffer();
      pool_p->buffer_timestamp[i] = buffer_timestamp;   
      *local_buffer_idx_p = (uint8_t)i;

   }
   else
   {
     /*
     ** take one buffer that is inuse
     */
     i = ( hash>>16) & (STORIO_CACHE_MAX_BUF_IN_A_POOL -1);
     buffer_timestamp = pool_p->buffer_timestamp[i];
     *local_buffer_idx_p = (uint8_t)i;
     storio_bufcache_stats.coll_buf_timestamp++;

   }
   return buffer_timestamp;
}



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
                          uint32_t *empty_bitmap,uint32_t *presence_bitmap)
{
  storio_bufcache_buf_header_t *hdr_p = (storio_bufcache_buf_header_t*)buf_cache_ts_p;
  int block_count;
  int first_block;
  int i;
  int local_empty_bitmap = 0;
  int local_presence_bitmap = 0;
  
  first_block = off%STORIO_CACHE_BCOUNT;
  block_count = len;
  /*
  ** adjust the payload and the index of the first 8K block where data will be filled in
  */
  hdr_p +=first_block;
  
  if ((first_block + len ) > STORIO_CACHE_BCOUNT)
  {
     severe ("total length is greater than the buffer space %d (max is %d)",first_block + len,STORIO_CACHE_BCOUNT);
     return -1;
  }

  for(i = first_block; i < (first_block+block_count); i++,hdr_p++,src_p++)
  {
    /*
    ** skip the empty block, no need to register its timestamp
    */
    if (*src_p == 0) 
    {
      /*
      ** update the timestamp
      */
      local_empty_bitmap    |=(1<<i);
      local_presence_bitmap |=(1<<i);
      continue;
    }
    /**
    * register the key associated with the fid and the timestamp of the projection
    */
    hdr_p->owner_key = key;
    hdr_p->prj_timestamp = *src_p;      
    local_presence_bitmap |=(1<<i);
  }
  /*
  ** update the empty bitmap of the caller
  */
  *empty_bitmap    |= local_empty_bitmap;
  *presence_bitmap |= local_presence_bitmap;
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
void storio_bufcache_buf_ts_pool_flush(storio_bufcache_buffer_pool_t *pool)
{
  int i;
  
  /*
  ** check if the pool exists
  */
  if (pool == NULL) return;
  
  for (i = 0; i < STORIO_CACHE_MAX_BUF_IN_A_POOL; i ++)
  {
    if (pool->buffer_timestamp[i] == NULL) continue;
    /*
    ** release the associate dmemory
    */
    free(pool->buffer_timestamp[i] );
    storio_bufcache_stats.count_buf_timestamp-- ;

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
void storio_bufcache_flush()
{
  int i;
  
  /*
  ** check if the cache exists
  */
  if (storio_bufcache_bufpool_p == NULL) return;
  for (i = 0; i < STORIO_CACHE_MAX_BUF_POOL; i ++)
  {
    if (storio_bufcache_bufpool_p->buffer_pool_p[i] == NULL) continue;
    /*
    ** flush the 256K Buffer pool entry
    */
    storio_bufcache_buf_ts_pool_flush(storio_bufcache_bufpool_p->buffer_pool_p[i]);
    storio_bufcache_bufpool_p->buffer_pool_p[i] = NULL;  
    
  }
  storio_bufcache_stats.coll_buf_timestamp = 0;

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
int storio_bufcache_pool_init()
{
   
   /*
   ** allocate the array for storing the buffer pools content
   */
   storio_bufcache_bufpool_p = malloc(sizeof(storio_bufcache_pool_t));
   if (storio_bufcache_bufpool_p == NULL)
   {
     errno = ENOMEM;
     return -1;
   }
   /**
   * clear all the pointers
   */
   memset(storio_bufcache_bufpool_p,0,sizeof(storio_bufcache_pool_t));
   /*
   ** clear the stats array
   */
   memset(&storio_bufcache_stats,0,sizeof(storio_bufcache_stats_t));
   
   storio_bufcache_max_buftimestamp = ROZOFS_GCACHE_MAX_256K_BUF_COUNT_DEFAULT;
   
   return 0;
}


