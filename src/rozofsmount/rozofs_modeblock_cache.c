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

#include "rozofs_modeblock_cache.h"
#include "rozofs_cache.h"

/*
**______________________________________________________________________________

      Global data
**______________________________________________________________________________
*/
com_cache_main_t  *rozofs_mbcache_cache_p= NULL; /**< pointer to the fid cache  */
uint32_t rozofs_mbcache_enable_flag = 0;     /**< assert to 1 when the mode block cache is enable */
uint64_t rozofs_mbcache_key;
rozofs_mbcache_stats_t  rozofs_mbcache_stats;
/*
**______________________________________________________________________________
*/
/**
* allocate an entry for the mode block cache

  @param fid : fid associated with the file
  
  @retval <>NULL: pointer to the cache entry
  @retval NULL : out of memory
*/
com_cache_entry_t *rozofs_mbcache_alloc_entry(fid_t fid)
{
  rozofs_mbcache_entry_t  *p;
  rozofs_mbcache_key_t    *key_p;
  /*
  ** allocate an entry for the context
  */
  p = malloc(sizeof(rozofs_mbcache_entry_t));
  if (p == NULL)
  {
    return NULL;
  }
  /*
  ** clear the structure
  */
  memset(p,0,sizeof(rozofs_mbcache_entry_t));
  p->cache.usr_entry_p = p;
  p->cache.usr_key_p   = &p->key;
  list_init(&p->cache.global_lru_link);
  list_init(&p->cache.bucket_lru_link);
  p->cache.dirty_bucket_counter = 0;
  p->cache.dirty_main_counter = 0;
  /*
  ** copy the informationn of the key
  */
  key_p = &p->key;
  memcpy(key_p->fid,fid,sizeof(fid_t));
#if 0  
  if (fid != NULL)
  {
    /*
    ** copy the child fid
    */
    memcpy(p->fid,fid,sizeof(fid_t));
  }
#endif
  return &p->cache;
}

/*
**______________________________________________________________________________
*/
/**
* release an entry of the fid cache

  @param p : pointer to the user cache entry 
  
*/
void rozofs_mbcache_release_entry(void *entry_p)
{
  rozofs_mbcache_entry_t  *p = (rozofs_mbcache_entry_t*) entry_p;
  rozofs_mbcache_pool_entry_t *pool_p;
  int i;

  /*
  ** release the array used for storing the name
  */
  list_remove(&p->cache.global_lru_link);
  list_remove(&p->cache.bucket_lru_link);
  /*
  ** release all the allocated pointers (pools)
  */
  for (i = 0; i < ROZOFS_MBCACHE_MX_POOL; i++)
  {
    pool_p = p->pool[i];
    if (pool_p == NULL) continue;
    free(pool_p);
  }
  free(p);
}
/*
**______________________________________________________________________________
*/
/**
*  hash computation from parent fid and filename (directory name or link name)

  @param h : initial hash value
  @param key2:  fid
  
  @retval hash value
*/
static inline uint32_t uuid_hash_fnv(uint32_t h,void *key) {

    unsigned char *d;

    if (h == 0) h = 2166136261U;
 
    /*
     ** hash on fid
     */
    d = (unsigned char *) key;
    for (d = key; d != key + 16; d++) {
        h = (h * 16777619)^ *d;

    }
    return h;
}

/*
**______________________________________________________________________________
*/
/**
* fid entry hash compute 

  @param p : pointer to the user cache entry 
  
  @retval hash value
  
*/
uint32_t rozofs_mbcache_hash_compute(void *usr_key)
{
  rozofs_mbcache_key_t  *key_p = (rozofs_mbcache_key_t*) usr_key;
  uint32_t hash;
  
   hash = uuid_hash_fnv(0, key_p->fid);
   return hash;
}



/*
**______________________________________________________________________________
*/
/**
* fid entry hash compute 

  @param key1 : pointer to the key associated with the entry from cache 
  
  @retval 0 on match
  @retval <> 0  no match  
*/

uint32_t rozofs_mbcache_exact_match(void *key1, void *key2)
{
  rozofs_mbcache_key_t  *key1_p = (rozofs_mbcache_key_t*) key1;
  rozofs_mbcache_key_t  *key2_p = (rozofs_mbcache_key_t*) key2;
  

  if (uuid_compare(key1_p->fid, key2_p->fid) != 0)  
  {
    return 1;
  }
  /*
  ** Match !!
  */
  return 0;
}

/*
**______________________________________________________________________________
*/
/**
* allocate a pool of cache enties
  @param none
  
  @retval <> NULL pointer to the pool
  @retval NULL out of memory
*/
rozofs_mbcache_pool_entry_t *rozofs_mbcache_pool_alloc()
{
  rozofs_mbcache_pool_entry_t *p;
  
  p = malloc(sizeof(rozofs_mbcache_pool_entry_t));
  if (p==NULL) return NULL;
  memset(p,0,sizeof(rozofs_mbcache_pool_entry_t));
  return p;
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
int rozofs_mbcache_insert(fid_t fid,uint64_t off,uint32_t size,uint8_t *src_bufp)
{
    rozofs_mbcache_key_t key;
    rozofs_mbcache_entry_t *cache_entry_p;
    rozofs_mbcache_buffer_entry_t *cache_data_p;
    rozofs_mbcache_buffer_entry_t *cache_data_free_p = NULL;
    com_cache_entry_t  *com_cache_entry_p;
    rozofs_mbcache_pool_entry_t *pool_p;
    int i;
    uint8_t *buf_cache256K_p = NULL;
    int cur_len;
    uint64_t cur_off;
    uint64_t off_cache;
    uint8_t *cur_buf_p;
    int pool_idx;
    int local_buffer_idx;
    uint64_t key_buf;
    
    cur_len = size;
    cur_off = off;
    cur_buf_p = src_bufp;

    /*
    ** check if the cache is enabled
    */    
    if (rozofs_mbcache_enable_flag != ROZOFS_MBCACHE_ENABLE) return -1;
        
    if (size == 0) return -1;

    key_buf = rozofs_mbcache_alloc_key_buf();

    if (rozofs_mbcache_cache_p == NULL) return -1;
    /*
    ** OK, the fid is handled by this server, so attempt to get the child fid
    ** from the cache
    */
    rozofs_mbcache_fid_build_key(&key,(unsigned char *)fid);
   /*
   ** Perform a lookup on the mode block cache
   */
   cache_entry_p = com_cache_bucket_search_entry(rozofs_mbcache_cache_p,&key); 
   if (cache_entry_p == NULL)
   {
      /*
      ** allocate an entry
      */
      com_cache_entry_p = rozofs_mbcache_alloc_entry(fid); 
      if (com_cache_entry_p == NULL) return -1;
      if (com_cache_bucket_insert_entry(rozofs_mbcache_cache_p, com_cache_entry_p) < 0)
      {
         severe("error on fid insertion"); 
         rozofs_mbcache_release_entry(com_cache_entry_p->usr_entry_p);
         return -1;
      }
      cache_entry_p = com_cache_entry_p->usr_entry_p;         
   }
   while (cur_len > 0)
   {
     /*
     **  Get the index to the array where data will be stored:
     **  compute off_cache from cur_off in order to have a off_cache
     **  that is always aligned on a 256K boundary
     */
     off_cache = cur_off/ROZOFS_CACHE_BUFFER_BSIZE;
     pool_idx = off_cache%(ROZOFS_MBCACHE_MX_POOL);
     off_cache = off_cache*ROZOFS_CACHE_BUFFER_BSIZE;
     /*
     ** check if there is an available pool
     */
     if (cache_entry_p->pool[pool_idx] == NULL)
     {
        /*
        ** need to allocated a pool
        */
        cache_entry_p->pool[pool_idx] = rozofs_mbcache_pool_alloc();   
     }
     pool_p = cache_entry_p->pool[pool_idx];
     if (pool_p == NULL)
     {
       /*
       ** out of memory-> do not cache
       */
       return -1;
     }
     /*
     ** OK now search for the array where we can fill the data
     */
     cache_data_p = pool_p->entry;
     cache_data_free_p = NULL;
     int found = 0;
     /*
     ** search for a free entry or an exact match
     */
     for (i = 0; i < ROZOFS_MBCACHE_MX_BUF_PER_POOL ; i++,cache_data_p++) 
     {
       if (cache_data_p->off_bufidx_st.s.state == 0)
       {
         if (cache_data_free_p!= NULL) continue;
         cache_data_free_p = cache_data_p;
         continue;
       }
       if (cache_data_p->off_bufidx_st.s.off == off_cache) 
       {
         cache_data_free_p = cache_data_p;
         found = 1;
         break;     
       }
     }
     /*
     ** check if one entry has been found (empty or not empty: for not empty, it 
     ** corresponds to an exact match
     */
     if (cache_data_free_p == NULL)
     {
       /*
       ** no free entry so take one that is already in use
       */
       rozofs_mbcache_stats.put_cache_coll++;
       i = (cur_off/ROZOFS_MBCACHE_BUF_SIZE)%ROZOFS_MBCACHE_MX_BUF_PER_POOL;
       cache_data_p =  &pool_p->entry[i];   
       cache_data_p->off_bufidx_st.s.off   = off_cache;
       cache_data_p->off_bufidx_st.s.state = 1;
       cache_data_p->owner_key = key_buf;     

     }
     else
     {
       cache_data_p = cache_data_free_p;
       if (found  == 0)
       {
         /*
         ** this is a new entry
         */
         rozofs_mbcache_stats.put_cache_miss++;
         cache_data_p->off_bufidx_st.s.off   = off_cache;
         cache_data_p->off_bufidx_st.s.state = 1;
         cache_data_p->owner_key = key_buf;                  
       }
       else
       {
         rozofs_mbcache_stats.put_cache_hit++;       
       }
     }
     /*
     ** check if we have a valid buffer for storing the data
     */
     if (cache_data_p->off_bufidx_st.s.buf_idx_valid == 0) 
     {
       /*
       ** need to allocate one
       */
       buf_cache256K_p = rozofs_cache_alloc_buf256K(fid,cur_off,&local_buffer_idx);
       if (buf_cache256K_p != NULL) 
       {
        cache_data_p->off_bufidx_st.s.buf_idx_valid = 1;
        cache_data_p->off_bufidx_st.s.buf_idx = local_buffer_idx;
       }
     }
     else
     {   
       buf_cache256K_p = rozofs_cache_get_buf256K_from_idx(fid,cur_off,cache_data_p->off_bufidx_st.s.buf_idx);   
     }
     if (buf_cache256K_p == NULL) 
     {
       /*
       ** that case can happen after a flush of the global cache: all the 256K buffer have been release so
       ** we need to reallocate them.
       */
       buf_cache256K_p = rozofs_cache_alloc_buf256K(fid,cur_off,&local_buffer_idx);
       if (buf_cache256K_p == NULL) return -1;
       /*
       ** update the entry with the new buffer reference
       */
       cache_data_p->off_bufidx_st.s.buf_idx_valid = 1;
       cache_data_p->off_bufidx_st.s.buf_idx = local_buffer_idx;     
     }
     /*
     ** OK, let's fill the data in the buffer: caution need to adjust the
     ** offset of the source buffer as well as the length to fill since the buffer to copy can
     ** span on two different cache entries.
     */
     int skip;
     int len2copy;
     skip = cur_off-off_cache;
     if ((skip+cur_len)> ROZOFS_MBCACHE_BUF_SIZE)
     {
       len2copy = ROZOFS_MBCACHE_BUF_SIZE - skip;
     }
     else len2copy =cur_len;
     /*
     ** address the case of the not aligned write: that occurs when the insertion is done for a write
     */
     if ((skip % ROZOFS_CACHE_BSIZE) == 0)
     {
       rozos_cache_write(buf_cache256K_p,cur_off,cur_buf_p,len2copy,cache_data_p->owner_key);
     }
     else
     {
       rozos_cache_write_not_aligned(buf_cache256K_p,cur_off,cur_buf_p,len2copy,cache_data_p->owner_key);     
     }
     /*
     ** update the pointers
     */
     rozofs_mbcache_stats.put_cache_bytes+= len2copy ;
     cur_buf_p +=len2copy;
     cur_len -= len2copy;
     cur_off += len2copy;
     
   }
   return 0;
   
}


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
int rozofs_mbcache_get(fid_t fid,uint64_t off,uint32_t size,uint8_t *dst_bufp)
{
    rozofs_mbcache_key_t key;
    rozofs_mbcache_entry_t *cache_entry_p;
    rozofs_mbcache_buffer_entry_t *cache_data_p;
    rozofs_mbcache_buffer_entry_t *cache_data_free_p = NULL;
    rozofs_mbcache_pool_entry_t *pool_p;
    int i;
    uint8_t *buf_cache256K_p = NULL;
    int cur_len;
    uint64_t cur_off;
    uint64_t off_cache;
    uint8_t *cur_buf_p;
    int pool_idx;
    int total_read_len = 0;
    
    cur_len = size;
    cur_off = off;
    cur_buf_p = dst_bufp;

    /*
    ** check if the cache is enabled
    */    
    if (rozofs_mbcache_enable_flag != ROZOFS_MBCACHE_ENABLE) return -1;
        
    if (size == 0) return -1;


    if (rozofs_mbcache_cache_p == NULL) return -1;
    
    rozofs_mbcache_stats.get_cache_req_bytes+=size;

    /*
    ** OK, the fid is handled by this server, so attempt to get the child fid
    ** from the cache
    */
    rozofs_mbcache_fid_build_key(&key,(unsigned char *)fid);
   /*
   ** Perform a lookup on the mode block cache
   */
   cache_entry_p = com_cache_bucket_search_entry(rozofs_mbcache_cache_p,&key); 
   if (cache_entry_p == NULL)
   {
      /*
      ** entry not found
      */
      rozofs_mbcache_stats.get_cache_miss++;
      return -1;  
   }
   while (cur_len > 0)
   {
     /*
     ** Get the index to the array where data will be stored
     */
     off_cache = cur_off/ROZOFS_CACHE_BUFFER_BSIZE;
     pool_idx = off_cache%(ROZOFS_MBCACHE_MX_POOL);
     off_cache = off_cache*ROZOFS_CACHE_BUFFER_BSIZE;
     /*
     ** check if there is an available pool
     */
     if (cache_entry_p->pool[pool_idx] == NULL)
     {
        /*
        ** nothing for that offset
        */
        rozofs_mbcache_stats.get_cache_miss++;
        return -1;   
     }
     pool_p = cache_entry_p->pool[pool_idx];

     /*
     ** OK now search for the array where we can fill the data
     */
     cache_data_p = pool_p->entry;
     cache_data_free_p = NULL;
     /*
     ** search for  an exact match
     */
     for (i = 0; i < ROZOFS_MBCACHE_MX_BUF_PER_POOL ; i++,cache_data_p++) 
     {
       if (cache_data_p->off_bufidx_st.s.state == 0)
       {
         continue;
       }
       if (cache_data_p->off_bufidx_st.s.off == off_cache) 
       {
         cache_data_free_p = cache_data_p;
         break;     
       }
     }
     /*
     ** check if one entry has been found (empty or not empty: for not empty, it 
     ** corresponds to an exact match
     */
     if (cache_data_free_p == NULL)
     {
       /*
       ** no entry
       */
       rozofs_mbcache_stats.get_cache_miss++;
       return -1;
     }

     /*
     ** check if we have a valid buffer for storing the data
     */
     if (cache_data_p->off_bufidx_st.s.buf_idx_valid == 0) 
     {
       /*
       ** no buffer !!
       */
       rozofs_mbcache_stats.get_cache_miss++;
       return -1;
     }
     /*
     ** get the pointer to the 256K buffer
     */
     buf_cache256K_p = rozofs_cache_get_buf256K_from_idx(fid,cur_off,cache_data_p->off_bufidx_st.s.buf_idx);   
     
     if (buf_cache256K_p == NULL) 
     {
       /*
       ** that case can happen after a flush of the global cache where all the 256K buffers have been released
       ** in that case we invalidate the enty in terms a buffer validity
       */
       cache_data_p->off_bufidx_st.s.buf_idx_valid = 0;
       rozofs_mbcache_stats.get_cache_miss++;
       return -1;
     }
     /*
     ** OK, let's fill the data in the buffer: caution need to adjust the
     ** offset of the source buffer as well as the length to fill since the buffer to copy can
     ** span on two different cache entries.
     */
     int skip;
     int len2copy;
     skip = cur_off-off_cache;
     if ((skip+cur_len)> ROZOFS_MBCACHE_BUF_SIZE)
     {
       len2copy = ROZOFS_MBCACHE_BUF_SIZE -skip;
     }
     else len2copy =cur_len;
     int read_len;

     read_len = rozos_cache_read(buf_cache256K_p,cur_off,cur_buf_p,len2copy,cache_data_p->owner_key);
     if (read_len < 0)
     {
       rozofs_mbcache_stats.get_cache_miss++;
       return -1;
     }
     /*
     ** update the total read length
     */
     rozofs_mbcache_stats.get_cache_hit++;
     total_read_len += read_len;
     if (read_len != len2copy)
     {
       /*
       ** we are done
       */
       break;
     }
     /*
     ** update the pointers
     */
     cur_buf_p += len2copy;
     cur_len -= len2copy;
     cur_off += len2copy;
     
   }
   rozofs_mbcache_stats.get_cache_hit_bytes+=total_read_len;
   return total_read_len;
   
}


/*
**______________________________________________________________________________
*/
/**
*  remove a fid from the mode block cache

  @param fid : fid associated with the file
  
  @retval none
*/
void rozofs_mbcache_remove(fid_t fid)
{
    rozofs_mbcache_key_t key;
    
    /*
    ** check if the cache is enabled
    */    
    if (rozofs_mbcache_enable_flag != ROZOFS_MBCACHE_ENABLE) return;        

    if (rozofs_mbcache_cache_p == NULL) return ;
    
    /*
    ** OK, the fid is handled by this server, so attempt to get the child fid
    ** from the cache
    */
    rozofs_mbcache_fid_build_key(&key,(unsigned char *)fid);
   /*
   ** remove the entry from the cache
   */
   com_cache_bucket_remove_entry(rozofs_mbcache_cache_p,&key); 

}


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
int rozofs_mbcache_check(fid_t fid,uint64_t off,uint32_t size)
{
    rozofs_mbcache_key_t key;
    rozofs_mbcache_entry_t *cache_entry_p;
    rozofs_mbcache_buffer_entry_t *cache_data_p;
    rozofs_mbcache_buffer_entry_t *cache_data_free_p = NULL;
    rozofs_mbcache_pool_entry_t *pool_p;
    int i;
    uint8_t *buf_cache256K_p = NULL;
    int cur_len;
    uint64_t cur_off;
    uint64_t off_cache;
    int pool_idx;
    
    cur_len = size;
    cur_off = off;
    
    /*
    ** check if the cache is enabled
    */    
    if (rozofs_mbcache_enable_flag != ROZOFS_MBCACHE_ENABLE) return -1;
    
    if (size == 0) return -1;


    if (rozofs_mbcache_cache_p == NULL) return -1;    
    /*
    ** OK, the fid is handled by this server, so attempt to get the child fid
    ** from the cache
    */
    rozofs_mbcache_fid_build_key(&key,(unsigned char *)fid);
   /*
   ** Perform a lookup on the mode block cache
   */
   cache_entry_p = com_cache_bucket_search_entry(rozofs_mbcache_cache_p,&key); 
   if (cache_entry_p == NULL)
   {
      /*
      ** entry not found
      */
      return -1;  
   }
   while (cur_len > 0)
   {
     /*
     ** Get the index to the array where data will be stored
     */
     off_cache = cur_off/ROZOFS_CACHE_BUFFER_BSIZE;
     pool_idx = off_cache%(ROZOFS_MBCACHE_MX_POOL);
     off_cache = off_cache*ROZOFS_CACHE_BUFFER_BSIZE;
     /*
     ** check if there is an available pool
     */
     if (cache_entry_p->pool[pool_idx] == NULL)
     {
        /*
        ** nothing for that offset
        */
        return -1;   
     }
     pool_p = cache_entry_p->pool[pool_idx];

     /*
     ** OK now search for the array where we can fill the data
     */
     cache_data_p = pool_p->entry;
     /*
     ** search for  an exact match
     */
     for (i = 0; i < ROZOFS_MBCACHE_MX_BUF_PER_POOL ; i++,cache_data_p++) 
     {
       if (cache_data_p->off_bufidx_st.s.state == 0)
       {
         continue;
       }
       if (cache_data_p->off_bufidx_st.s.off == off_cache) 
       {
         cache_data_free_p = cache_data_p;
         break;     
       }
     }
     /*
     ** check if one entry has been found (empty or not empty: for not empty, it 
     ** corresponds to an exact match
     */
     if (cache_data_free_p == NULL)
     {
       /*
       ** no entry
       */
       return -1;
     }

     /*
     ** check if we have a valid buffer for storing the data
     */
     if (cache_data_p->off_bufidx_st.s.buf_idx_valid == 0) 
     {
       /*
       ** no buffer !!
       */
       return -1;
     }
     /*
     ** get the pointer to the 256K buffer
     */
     buf_cache256K_p = rozofs_cache_get_buf256K_from_idx(fid,cur_off,cache_data_p->off_bufidx_st.s.buf_idx);   
     
     if (buf_cache256K_p == NULL) 
     {
       return -1;
     }
     break;     
   }
   return 0;
   
}


/*
**______________________________________________________________________________
*/

#define SHOW_STAT_MBCACHE(probe) pChar += sprintf(pChar,"%-28s :  %10llu\n","  "#probe ,(long long unsigned int) stat_p->probe);


char *rozofs_mbcache_stats_display(char *buffer)
{
 char *pChar = buffer;
 rozofs_mbcache_stats_t *stat_p =&rozofs_mbcache_stats;
 SHOW_STAT_MBCACHE(put_cache_hit);
 SHOW_STAT_MBCACHE(put_cache_coll);
 SHOW_STAT_MBCACHE(put_cache_miss);
 SHOW_STAT_MBCACHE(put_cache_bytes);


 SHOW_STAT_MBCACHE(get_cache_miss);
 SHOW_STAT_MBCACHE(get_cache_hit);
 SHOW_STAT_MBCACHE(get_cache_req_bytes);
 SHOW_STAT_MBCACHE(get_cache_hit_bytes);
 pChar += sprintf(pChar,"\n\n");
 return pChar;
}
/*
**______________________________________________________________________________
*/
/**
* service for clearing the mode block cache statistics

  @param none
  retval none
*/
void rozofs_mbcache_stats_clear()
{
  memset(&rozofs_mbcache_stats,0,sizeof(rozofs_mbcache_stats));
}

/*
**______________________________________________________________________________
*/
/**
* service to enable the mode block cache

  @param none
  retval none
*/
void rozofs_mbcache_enable()
{
  if (rozofs_mbcache_enable_flag !=  ROZOFS_MBCACHE_ENABLE)
  {
    rozofs_mbcache_enable_flag = ROZOFS_MBCACHE_ENABLE;
    /*
    ** request for flushing the 256K buffer cache pool
    */
    rozofs_gcache_flush();  
  }
}

/*
**______________________________________________________________________________
*/
/**
* service to disable the mode block cache

  @param none
  retval none
*/
void rozofs_mbcache_disable()
{
  if (rozofs_mbcache_enable_flag !=  ROZOFS_MBCACHE_DISABLE)
  {
    rozofs_mbcache_enable_flag = ROZOFS_MBCACHE_DISABLE;
    /*
    ** request for flushing the 256K buffer cache pool
    */
    rozofs_gcache_flush();  
  }
}
/*
**______________________________________________________________________________
*/
/**
* creation of the block mode cache
 That API is intented to be called during the initialization of the module
 
 The max number of entries is given the ROZOFS_MBCACHE_MAX_ENTRIES constant
 and the size of the level 0 entry set is given by ROZOFS_MBCACHE_LVL0_SZ_POWER_OF_2 constant
 
 @param init_state : initial state of the cache 1: enable , 0 disable
 retval 0 on success
 retval < 0 on error
*/
 
uint32_t rozofs_mbcache_cache_init(uint32_t init_state)
{

  com_cache_usr_fct_t callbacks;
  
  if (rozofs_mbcache_cache_p != NULL)
  {
    return 0;
  }
  callbacks.usr_exact_match_fct = rozofs_mbcache_exact_match;
  callbacks.usr_hash_fct        = rozofs_mbcache_hash_compute;
  callbacks.usr_delete_fct      = rozofs_mbcache_release_entry;
  
  rozofs_mbcache_cache_p = com_cache_create(ROZOFS_MBCACHE_LVL0_SZ_POWER_OF_2,
                                      ROZOFS_MBCACHE_MAX_ENTRIES,
                                      &callbacks);
  if (rozofs_mbcache_cache_p == NULL)
  {
    /*
    ** we run out of memory
    */
    return -1;  
  }
  /*
  ** init of the key
  */
  rozofs_mbcache_key = 1;
  /*
  ** clear the stats
  */
  memset(&rozofs_mbcache_stats,0,sizeof(rozofs_mbcache_stats));
  /*
  ** init of the mode block cache state
  */
  rozofs_mbcache_enable_flag = init_state;
  
  return 0;
}
