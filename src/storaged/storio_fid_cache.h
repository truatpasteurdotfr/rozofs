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
 
#ifndef STORIO_FID_CACHE_H
#define STORIO_FID_CACHE_H


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
#include <rozofs/core/ruc_list.h>

/*
**
** cache->bucket[]---> empty sub bucket bitmap
**                     full sub bucket bitmap
**                     sub_bucket[]----------------> allocated entry bitmap
**                                                   entry|]
**
*/


/**
** Exact match function
** @param key      The context key (i.e FID)
** @param index    The context index
** 
** @retval true in case of a match
*/
typedef uint32_t (*storio_fid_exact_match_fct)(void * key,uint32_t index);
/**
** Request from the cache to free a context if possible
**
** @param index    The context index
** 
** @retval true in case the context has been deleted
*/
typedef uint32_t (*storio_fid_delete_request_fct)(uint32_t index);


/*
** Number of cache entries in power of 2 
*/
#define  STORIO_FID_CACHE_LVL0_SZ_POWER_OF_2 13

/*
** In an 32 bit is stored the index as well as a hash
** The hash size STORIO_FID_CACHE_HASH_BITS 
*/
#define STORIO_FID_CACHE_HASH_BITS 14
#define STORIO_INDEX_BITS (32-STORIO_FID_CACHE_HASH_BITS)
typedef union storio_fid_cache_entry_u {
  uint32_t      u32;
  struct {
    uint32_t    hash:STORIO_FID_CACHE_HASH_BITS;
    uint32_t    index:STORIO_INDEX_BITS;
  } s;
} STORIO_FID_CACHE_ENTRY_U;


/*
** Each sub bucket contains a bit map of the used entries
** as well as the array of entries
*/
#define STORIO_FID_CACHE_MAX_SUB_BUCKET_ENTRIES     32
#define STORIO_FID_CACHE_MAX_SUB_BUCKET_ENTRIES_BITMAP (STORIO_FID_CACHE_MAX_SUB_BUCKET_ENTRIES/8)
typedef struct storio_fid_cache_sub_bucket_t {
  /* Bit map of allocated entries in the buket */
  uint8_t                             allocated[STORIO_FID_CACHE_MAX_SUB_BUCKET_ENTRIES_BITMAP];
  STORIO_FID_CACHE_ENTRY_U            entry[STORIO_FID_CACHE_MAX_SUB_BUCKET_ENTRIES];  
} STORIO_FID_CACHE_SUB_BUCKET_T;

/*
** A bucket is a collection of sub buckets containing entry data
** A bitmap gives the list of empty sub buckets where the search needs not to read.
** An other gives the list of full sub buckets where the insert needs not to read.
** The 1rst sub bucket is in the bucket conext, while extra sub buckets are allocated
** and released on usage.
*/
#define STORIO_FID_CACHE_MAX_SUB_BUCKET     32
#define STORIO_FID_CACHE_MAX_SUB_BUCKET_BITMAP (STORIO_FID_CACHE_MAX_SUB_BUCKET/8)
typedef struct storio_fid_cache_bucket_t {
  /* Bit map of the empty sub-buckets */
  uint8_t       empty[STORIO_FID_CACHE_MAX_SUB_BUCKET_BITMAP];
  /* Bit map of the full sub-buckets */  
  uint8_t       full[STORIO_FID_CACHE_MAX_SUB_BUCKET_BITMAP];
  /* 1rst bucket */
  STORIO_FID_CACHE_SUB_BUCKET_T   bucket0;
  /* Extra Sub buckets address */
  STORIO_FID_CACHE_SUB_BUCKET_T * xtra_bucket[STORIO_FID_CACHE_MAX_SUB_BUCKET-1];
} STORIO_FID_CACHE_BUCKET_T;


/*
** Cache statistics
*/
typedef struct storio_fid_stat_t {
  uint64_t        count;   // nb of entries in the cache
  uint64_t        hit;     // count of successfull search
  uint64_t        miss;    // count of failed search
  uint64_t        bkts;    // number of allocated extra buckets
  uint64_t        mxbkt;   // The biggest number of sub  buckets
  uint64_t        mxcol;   // max collision found on entry hash & bucket index
} STORIO_FID_STAT_T;


/*
**______________________________________________________________________________
** Display hash statistics
**
** @param pChar where to format the display
** @retval The end of the display
*/
char * display_cache_fid_stat(char * pChar);
/*
**______________________________________________________________________________
** Insert a FID context reference in the cache 
** 
** @param hash   A 32 bits hash value of the entry
** @param index  The index to store
**
** @retval 0  on success
** @retval -1 on error 
*/
int storio_fid_cache_insert(uint32_t hash, uint32_t index) ;
/*
**______________________________________________________________________________
** Search a FID context reference in the cache 
** 
** @param hash   A 32 bits hash value of the searched entry
** @param key    Pointer to the key for exact match 
**
** @retval    The index found in the cache
** @retval    -1 when no entry found
*/
uint32_t storio_fid_cache_search(uint32_t hash, void * key) ;
/*
**______________________________________________________________________________
** Remove a FID context reference from the cache 
** 
** @param hash   A 32 bits hash value of the entry to remove
** @param key    Pointer to the key for exact match 
**
** @retval    The index of the removed entry
** @retval    -1 when no entry found
*/
uint32_t storio_fid_cache_remove(uint32_t hash, void * key) ;
/*
**______________________________________________________________________________
** Cache initialization 
** 
** @param exact_match_fct     A function to be called for checking the exact match
** @param delete_request_fct  A function to be called to request a context deletion
**                            if possible
**
*/
void storio_fid_cache_init(storio_fid_exact_match_fct exact_match_fct, storio_fid_delete_request_fct delete_request_fct) ;
#endif
