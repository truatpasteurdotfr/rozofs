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
#ifndef COM_CACHE_H
#define COM_CACHE_H


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



/** @defgroup COM_CACHE_LVL0 Level 0 cache
 *  This module provides services related to level 0 cache\n

   The Level 0 cache is used to cache mdirent root file only. When a mdirent root file has some
   collision midrent file associated with it, they are referenced inside the memory representation
   of the mdirent file.
   As the consequence, the  insertion/removal  of a root mdirent file in the level cache 0 implies
   also the insertion of the associated collision file, however to access to a mdirent collision
   entry is always performed indirectly accross its associated mdirent root file cache entry.\n

   The level 0 cache is organized as follows\n
   - 64K buckets\n
   - Each bucket supports up to 256 collisions entries\n

   For an application standpoint, it is possible to enable/disable the Level 0 cache. \n

   Here is an example of the output of the level 0 cache statistics:\n
 <div class="fragment"><pre class="fragment">
Level 0 cache state : Enabled
Number of entries insert in level 0 cache 4096
hit/miss 1568768/4096
collisions cumul  level0/level1 54959/0
collisions Max level0/level1 1/0
 </pre></div>
 */
#define COM_BUCKET_DEPTH_IN_BIT   16

#define COM_BUCKET_MAX_ROOT_DIRENT (256000)
//#define COM_BUCKET_MAX_ROOT_DIRENT (4)

#define COM_BUCKET_MAX_COLLISIONS  256  /**< number of collisions that can be supported by a bucket  */
#define COM_BUCKET_MAX_COLLISIONS_BYTES  (COM_BUCKET_MAX_COLLISIONS/8)  /**< number of collisions that can be supported by a bucket  */
#define COM_BUCKET_NB_ENTRY_PER_ARRAY  32 /**< number of com_cache_bucket_entry_t strcuture per memory array */
#define COM_BUCKET_ENTRY_MAX_ARRAY (COM_BUCKET_MAX_COLLISIONS/COM_BUCKET_NB_ENTRY_PER_ARRAY)
/**
* common structure that must be embedded by each entry that is handled by the cache service
*/
typedef struct _com_cache_lru_t
{
    list_t global_lru_link; /**< linked list of the main cache entries                   */
    list_t bucket_lru_link; /**< linked list of the collision entries within a bucket     */
    void   *usr_entry_p;  /**< pointer to the user entry context                    */
    void   *usr_key_p;    /**< pointer to the user key context                      */
    uint64_t dirty_main_counter;
    uint64_t dirty_bucket_counter;
} com_cache_entry_t;

/**
*  exact match user callback

  @param in_cache_entry: pointer the entry that application has inserted in the cache
  @param usr_key: pointer to structure that contains the user key(s)
  
  @retval 0 on match
  @retval <>0 no match
*/
typedef uint32_t (*com_cache_usr_exact_match_fct)(void *in_cache_entry,void *usr_key);

/**
*  hash compute  user callback

  @param usr_key: pointer to structure that contains the user key(s)
  
  @retval hash value associated with the entry
*/
typedef uint32_t (*com_cache_usr_hash_fct)(void *usr_key);

/**
*  release the user entry

  @param in_cache_entry: pointer to the entry provided by the application
  
  @retval none
*/
typedef void (*com_cache_usr_delete_fct)(void *in_cache_entry);

/**
* user callback functions
*/
typedef struct _com_cache_usr_fct_t
{
   com_cache_usr_exact_match_fct  usr_exact_match_fct;  /**< callback for exact match            */
   com_cache_usr_hash_fct         usr_hash_fct;         /**< callback for hash value computation */
   com_cache_usr_delete_fct       usr_delete_fct;       /**< callback for user context deletion  */
} com_cache_usr_fct_t;


typedef struct _com_cache_bucket_entry_t {
    uint16_t hash_value_table[COM_BUCKET_NB_ENTRY_PER_ARRAY]; /**< table of the hash value applied to the parent_fid and index */
    void *entry_ptr_table[COM_BUCKET_NB_ENTRY_PER_ARRAY]; /**< table of the dirent cache entries: used for doing the exact match */
} com_cache_bucket_entry_t;

/**
 *  dirent cache structure
 */
typedef struct _com_cache_bucket_t {
    list_t bucket_lru_link; /**< link list for bucket LRU  */
    uint64_t  dirty_bucket_counter;  /**< used as a dirty bit: that counter is incremented each time the dirty bit of the entry is asserted */
    uint8_t bucket_free_bitmap[COM_BUCKET_MAX_COLLISIONS_BYTES]; /**< bitmap of the free entries  */
    com_cache_bucket_entry_t * entry_tb[COM_BUCKET_ENTRY_MAX_ARRAY]; /**< pointer to the memory array that contains the entries */
} com_cache_bucket_t;

/*
** cache statistics
*/
typedef struct _com_cache_stats_t
{
 uint64_t com_bucket_cache_hit_counter ;
 uint64_t com_bucket_cache_miss_counter ;
 uint64_t com_bucket_cache_collision_counter ;
 uint64_t com_bucket_cache_lru_counter_global ;
 uint64_t com_bucket_cache_lru_counter_coll ;
 uint64_t com_bucket_cache_lru_global_error ;
 uint64_t com_bucket_cache_lru_coll_error ;
 uint64_t com_bucket_cache_collision_level0_counter ;
 uint64_t com_bucket_cache_max_level0_collisions ; /**< max number of collision at level 0  */
 uint64_t com_bucket_cache_max_level1_collisions ; /**< max number of collision at level 1  */
 uint64_t com_bucket_cache_dirty_counter;
 uint64_t com_bucket_cache_dirty_bucket_update;
} com_cache_stats_t;

/**
* Main structure of a cache
*/
typedef struct _com_cache_main_t {
    uint32_t level0_sz;   /**< size of the level 0 in power of 2         */
    uint32_t level0_mask; /**< mask to applied to the primary hash value */
    uint32_t max;       /**< maximum number of entries in the cache */
    uint32_t size;      /**< current number of entries in the cache */
    uint64_t  dirty_main_counter;  /**< used as a dirty bit: that counter is incremented each time the dirty bit of the entry is asserted */
    list_t global_lru_link;     /**< entries cached: used for LRU             */
    com_cache_stats_t stats;    /**< cache statistics                         */
    com_cache_bucket_t *htable; /**< pointer to the bucket array of the cache */
    com_cache_usr_exact_match_fct  usr_exact_match_fct;  /**< callback for exact match            */
    com_cache_usr_hash_fct         usr_hash_fct;         /**< callback for hash value computation */
    com_cache_usr_delete_fct       usr_delete_fct;       /**< callback for user context deletion  */
} com_cache_main_t;


#define SHOW_STAT_CACHE(prefix,probe) pChar += sprintf(pChar,"%-28s :  %10llu\n","  "#probe ,(long long unsigned int) stat_p->prefix ## probe);

/**
*  API for displaying the statistics of a cache

  @param buffer: buffer where to format the output
  @param p : pointer to the cache structure
  @param cache_name: name of the cache
  
  @retval none
*/
static inline char *com_cache_show_cache_stats(char *buffer,com_cache_main_t *p, char *cache_name)
{
 char *pChar = buffer;
 com_cache_stats_t *stat_p = &p->stats;
 
 pChar += sprintf(pChar,"%s:\n",cache_name);
 pChar += sprintf(pChar,"level0_sz        : %d\n",p->level0_sz);
 pChar += sprintf(pChar,"entries (max/cur): %u/%u\n\n",p->max,p->size);

 
 SHOW_STAT_CACHE(com_bucket_cache_,hit_counter );
 SHOW_STAT_CACHE(com_bucket_cache_,miss_counter );
 SHOW_STAT_CACHE(com_bucket_cache_,collision_counter );
 SHOW_STAT_CACHE(com_bucket_cache_,lru_counter_global );
 SHOW_STAT_CACHE(com_bucket_cache_,lru_counter_coll );
 SHOW_STAT_CACHE(com_bucket_cache_,lru_global_error );
 SHOW_STAT_CACHE(com_bucket_cache_,lru_coll_error );
 SHOW_STAT_CACHE(com_bucket_cache_,collision_level0_counter );
 SHOW_STAT_CACHE(com_bucket_cache_,max_level0_collisions ); 
 SHOW_STAT_CACHE(com_bucket_cache_,max_level1_collisions ); 
 SHOW_STAT_CACHE(com_bucket_cache_,dirty_counter);
 SHOW_STAT_CACHE(com_bucket_cache_,dirty_bucket_update);

 pChar += sprintf(pChar,"\n\n");
 return pChar;


}

/*
**______________________________________________________________________________
*/
/**
 *  Creation a a cache entity
    That service is intended to allocate the management cache structure and to 
    return it as returned parameter.
 
 @param level0_sz: size of the level 0 in power of 2
 @param max: max number of entries supported by the cache before activing LRU

 @retval <> NULL : success
 @retval == NULL : error
 */
com_cache_main_t *com_cache_create(uint32_t level0_sz, uint32_t max,com_cache_usr_fct_t  *usr_fct);

/*
 **______________________________________________________________________________
 */
/**
 *  Insert a user entry in the cache
 *  Note : the bitmap is aligned on a 8 byte boundary, so we can perform
  control by using a uint64_t

   @param cache : pointer to the main cache structure
   @param entry   : pointer to the entry to insert (needed to be a com_cache_entry_t structure)

   @retval 0 -> success
   @retval -1 -> failure, entry has not been inserted  .
 */

int com_cache_bucket_insert_entry(com_cache_main_t *cache, com_cache_entry_t *entry);

/*
 **______________________________________________________________________________
 */
 /**
 *  Search an entry in the cache
 *
   @param cache : pointer to the main cache structure
   @param key_p : pointer to the key to search

   @retval <>NULL: pointer to the root dirent cache entry
   @retval ==NULL:no entry
 */
void *com_cache_bucket_search_entry(com_cache_main_t *cache, void *key_p);

/*
 **______________________________________________________________________________
 */
/**
 *  Remove a root dirent file reference from the cache
 *
   @param cache : pointer to the main cache structure
   @param key_p : key to search 

   @retval 0 : success
   @retval -1 : not found
 */
int com_cache_bucket_remove_entry(com_cache_main_t *cache, void *key_p); 

/*
**______________________________________________________________________________
*/
/**
*  update the main dirty "bits" of the cache

   @param cache : pointer to the main cache structure
   
   @retval none

*/
static inline void com_cache_dirty_main_update(com_cache_main_t *cache)
{
  cache->dirty_main_counter++;
}

/*
**______________________________________________________________________________
*/
/**
*  update the bucket dirty "bits" of the cache

   @param cache : pointer to the main cache structure
   @param bucket_first_idx : index of the first bit in the dirty bitmap
   @param nb_entries : number of entries in the bitmap (modulo 8)
   @param bitmap_p : pointer to the bitmap
   
   @retval none

*/
void com_cache_dirty_bucket_update(com_cache_main_t *cache,uint32_t bucket_first_idx,uint32_t nb_entries,uint8_t *bitmap_p);

#ifdef __cplusplus
}
#endif /*__cplusplus */



#endif
