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
 
#ifndef STORIO_DEVICE_MAPPING_H
#define STORIO_DEVICE_MAPPING_H


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

#include "storage.h"




/**
* Attributes cache constants
*/
#define STORIO_DEVICE_MAPPING_LVL0_SZ_POWER_OF_2  12
#define STORIO_DEVICE_MAPPING_MAX_ENTRIES  (64*1024)

#define STORIO_DEVICE_MAPPING_LVL0_SZ  (1 << STORIO_DEVICE_MAPPING_LVL0_SZ_POWER_OF_2) 
#define STORIO_DEVICE_MAPPING_LVL0_MASK  (STORIO_DEVICE_MAPPING_LVL0_SZ-1)


typedef struct storio_rebuild_t {
  ruc_obj_desc_t      link;
  uint8_t             ref;
  uint32_t            rebuild_ts;
  uint64_t            start_block;
  uint64_t            stop_block;
  fid_t               fid;
} STORIO_REBUILD_T;

#define MAX_STORIO_PARALLEL_REBUILD   32
STORIO_REBUILD_T * storio_rebuild_ctx_free_list;

typedef struct storio_rebuild_stat_s {
  uint64_t             allocated;
  uint64_t             stollen;
  uint64_t             out_of_ctx;
  uint64_t             lookup_hit;
  uint64_t             lookup_miss;
  uint64_t             lookup_bad_index;
  
} STORIO_REBUILD_STAT_S;
extern STORIO_REBUILD_STAT_S        storio_rebuild_stat;

#define MAX_FID_PARALLEL_REBUILD 4
typedef union storio_rebuild_ref_u {
  uint8_t     u8[MAX_FID_PARALLEL_REBUILD];
  uint32_t    u32;
} STORIO_REBUILD_REF_U;

typedef struct _storio_device_mapping_t
{
  ruc_obj_desc_t       link;  
  com_cache_entry_t    cache;   /** < common cache structure    */
  int                  index;
  fid_t                fid;
  uint8_t              device[ROZOFS_STORAGE_MAX_CHUNK_PER_FILE];   /**< Device number to write the data on        */
  list_t               running_request;
  list_t               waiting_request;
  uint64_t             consistency;
  STORIO_REBUILD_REF_U storio_rebuild_ref;
} storio_device_mapping_t;

storio_device_mapping_t * storio_device_mapping_ctx_free_list;
ruc_obj_desc_t            storio_device_mapping_ctx_lost_list;

typedef struct _storio_device_mapping_stat_t
{
  uint64_t            consistency; 
  uint64_t            allocated; 
  uint64_t            out_of_ctx;
  uint64_t            lost;
  uint64_t            recycled;
  uint64_t            released;
  uint64_t            inconsistent;   
} storio_device_mapping_stat_t;

extern storio_device_mapping_stat_t storio_device_mapping_stat;


/*
**______________________________________________________________________________
*/
/**
* Increment consistency (done when a total rebuild occurs)
  
*/
static inline void storage_device_mapping_increment_consistency() {
  storio_device_mapping_stat.consistency++;
}
/*
**______________________________________________________________________________

      Attributes LOOKUP SECTION
**______________________________________________________________________________
*/
extern com_cache_main_t  *storio_device_mapping_p; /**< pointer to the device cache  */



/*
**______________________________________________________________________________
*/
/**
* release an entry of the attributes cache

  @param p : pointer to the user cache entry 
  
*/
void storio_device_mapping_release_entry(storio_device_mapping_t *p);
void storio_device_mapping_delete_cbk(void *entry_p);

/*
**______________________________________________________________________________
*/
/**
* Reset a storio device_mapping context

  @param p the device_mapping context to initialize
 
*/
static inline void storio_device_mapping_ctx_reset(storio_device_mapping_t * p) {

  p->cache.usr_entry_p = p;
  
  memset(&p->fid,0,sizeof(fid_t));
  memset(p->device,ROZOFS_UNKNOWN_CHUNK,ROZOFS_STORAGE_MAX_CHUNK_PER_FILE);
  p->consistency   = storio_device_mapping_stat.consistency;

  p->cache.usr_key_p   = p->fid;
  list_init(&p->cache.global_lru_link);
  list_init(&p->cache.bucket_lru_link);
  p->cache.dirty_bucket_counter = 0;
  p->cache.dirty_main_counter = 0;
  list_init(&p->running_request);
  list_init(&p->waiting_request);

  p->storio_rebuild_ref.u32 = 0xFFFFFFFF;
}

/*
**______________________________________________________________________________
*/
/**
* Allocate a storio device_mapping context from the distributor

 
 @retval the pointer to the device_mapping context or NULL in case of error
*/
static inline storio_device_mapping_t * storio_device_mapping_ctx_allocate() {
  storio_device_mapping_t * p;
  
  /*
  ** Get first free context
  */
  p = (storio_device_mapping_t*) ruc_objGetFirst(&storio_device_mapping_ctx_free_list->link);
  if (p != NULL) {
    ruc_objRemove(&p->link);
    storio_device_mapping_stat.allocated++;
    storio_device_mapping_ctx_reset(p);  
    return p;
  }
  
  /*
  ** No more free context. Let's recycle a context from the lost list
  */
  p = (storio_device_mapping_t*) ruc_objGetFirst(&storio_device_mapping_ctx_lost_list);
  if (p != NULL) {
    storio_device_mapping_stat.recycled++;
    storio_device_mapping_stat.lost--;      
    ruc_objRemove(&p->link);
    storio_device_mapping_ctx_reset(p);  
    return p;
  }  
  
  storio_device_mapping_stat.out_of_ctx++;
  return NULL;
}
/*
**______________________________________________________________________________
*/
/**
* Free a storio device_mapping context 
*/
static inline void storio_device_mapping_ctx_free(storio_device_mapping_t * p) {

  /*
  ** In case some request is pending, do not clear the context
  ** but put it in the lost queue
  */
  if (!list_empty(&p->running_request)) {
    severe ("running not empty");
    ruc_objInsertTail(&storio_device_mapping_ctx_lost_list,&p->link); 
    storio_device_mapping_stat.lost++;
    return;
  }
  
  if (!list_empty(&p->waiting_request)) {
    severe ("waiting not empty");
    ruc_objInsertTail(&storio_device_mapping_ctx_lost_list,&p->link); 
    storio_device_mapping_stat.lost++;
    return;
  } 
   
  /*
  ** Free the context
  */    
  storio_device_mapping_ctx_reset(p);  
  ruc_objInsert(&storio_device_mapping_ctx_free_list->link,&p->link); 
  storio_device_mapping_stat.released++;
}
/*
**______________________________________________________________________________
*/
/**
* Retrieve a context from its index
*
* @param idx The context index
*
* @return the rebuild context address or NULL
*/
static inline storio_device_mapping_t * storio_device_mapping_ctx_retrieve(int idx) {
  storio_device_mapping_t * p;
 
  if (idx>=(STORIO_DEVICE_MAPPING_MAX_ENTRIES + 1024)) {
    return NULL;
  }  

  p = (storio_device_mapping_t*) ruc_objGetRefFromIdx(&storio_device_mapping_ctx_free_list->link,idx);  
  return p;  
}
/*
**______________________________________________________________________________
*/
/**
* Initialize the storio device_mapping context distributor

 
 retval 0 on success
 retval < 0 on error
*/
static inline void storio_device_mapping_ctx_distributor_init() {
  int                       nbCtx = STORIO_DEVICE_MAPPING_MAX_ENTRIES + 1024;
  storio_device_mapping_t * p;
  int                       idx;
  
  /*
  ** Reset stattistics 
  */
  memset(&storio_device_mapping_stat, 0, sizeof(storio_device_mapping_stat));
  
  /*
  ** Allocate memory
  */
  storio_device_mapping_ctx_free_list = (storio_device_mapping_t*) ruc_listCreate(nbCtx,sizeof(storio_device_mapping_t));
  if (storio_device_mapping_ctx_free_list == NULL) {
    /*
    ** error on distributor creation
    */
    fatal( "ruc_listCreate(%d,%d)", STORIO_DEVICE_MAPPING_MAX_ENTRIES,(int)sizeof(storio_device_mapping_t) );
  }
  
  
  for (idx=0; idx<nbCtx; idx++) {
    p = storio_device_mapping_ctx_retrieve(idx);
    p->index = idx;
  }  
}


/*
**______________________________________________________________________________
*/
/**
* Insert an entry in the cache if it does not yet exist
* 
*  @param fid the FID
*  @param device_id The device number 
*
*/
static inline storio_device_mapping_t * storio_device_mapping_insert(void * fid) {
  storio_device_mapping_t            * p;  
  
  /*
  ** allocate an entry
  */
  p = storio_device_mapping_ctx_allocate();
  if (p == NULL) {
    return NULL;
  }
  
  memcpy(&p->fid,fid,sizeof(fid_t));
 
  if (com_cache_bucket_insert_entry(storio_device_mapping_p, &p->cache) < 0) {
     severe("error device mapping insertion"); 
     storio_device_mapping_release_entry(p->cache.usr_entry_p);
     return NULL;
  }
  return p;
}
/*
**______________________________________________________________________________
*/
/**
* Search an entry in the cache 
* 
*  @param fid the FID
*
*  @retval found entry or NULL
*
*/
static inline storio_device_mapping_t * storio_device_mapping_search(void * fid) {
  storio_device_mapping_t   * p;  

  /*
  ** Lookup for an entry
  */
  p = com_cache_bucket_search_entry(storio_device_mapping_p,fid);
  if (p == NULL) {
    return NULL;
  }
  /*
  ** Check the entry is conistent
  */
  if (p->consistency == storio_device_mapping_stat.consistency) {
    return p;
  }
  /*
  ** The entry is inconsistent and must be removed 
  */
  storio_device_mapping_stat.inconsistent++; 
  storio_device_mapping_release_entry(p);  
  return NULL;
}
/*
**____________________________________________________
*/
/*
  Allocate a device for a file
  
   @param st: storage context
*/
uint32_t storio_device_mapping_allocate_device(storage_t * st);
/*
**______________________________________________________________________________
*/
/**
* creation of the FID cache
 That API is intented to be called during the initialization of the module
 
 The max number of entries is given the storio_device_mapping_MAX_ENTRIES constant
 and the size of the level 0 entry set is given by storio_device_mapping_LVL0_SZ_POWER_OF_2 constant
 
 retval 0 on success
 retval < 0 on error
*/
 
uint32_t storio_device_mapping_init();
/*
**____________________________________________________
*/
/*
* Register the FID that has encountered an error
  
   @param threadNb the thread number
   @param cid      the faulty cid 
   @param sid      the faulty sid      
   @param fid      the FID in fault   
*/
void storio_register_faulty_fid(int threadNb, uint8_t cid, uint8_t sid, fid_t fid) ;









/*
** - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 
*
* Rebuild contexts
*
** - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 
*/






/*
**______________________________________________________________________________
*/
/**
* Reset a storio rebuild context

  @param p the rebuild context to initialize
 
*/
static inline void storio_rebuild_ctx_reset(STORIO_REBUILD_T * p) {
  ruc_objRemove(&p->link);  
  p->rebuild_ts  = 0;
  p->start_block = 0;
  p->stop_block  = 0;
  memset(p->fid,0, sizeof(fid_t));
}

/*
**______________________________________________________________________________
*/
/**
* Allocate a storio rebuild context from the distributor

 
 @retval the pointer to the rebuild context or NULL in case of error
*/
static inline STORIO_REBUILD_T * storio_rebuild_ctx_allocate() {
  STORIO_REBUILD_T * p;
  int                storio_rebuild_ref;
  
  /*
  ** Get first free context
  */
  p = (STORIO_REBUILD_T*) ruc_objGetFirst(&storio_rebuild_ctx_free_list->link);
  if (p != NULL) {
    storio_rebuild_stat.allocated++;
    storio_rebuild_ctx_reset(p);  
    return p;
  }

  /* No Free context found. Let's check whether a context is old enough to be stollen */
  uint32_t ts = time(NULL);
  
  p = (STORIO_REBUILD_T*) ruc_objGetRefFromIdx(&storio_rebuild_ctx_free_list->link,0);
  for (storio_rebuild_ref=0; storio_rebuild_ref<MAX_STORIO_PARALLEL_REBUILD; storio_rebuild_ref++,p++) {
    if (ts - p->rebuild_ts > 25) {
      storio_rebuild_stat.stollen++;
      storio_rebuild_ctx_reset(p);  
      return p;      
    }
  }
  
  storio_rebuild_stat.out_of_ctx++;
  return NULL;
}
/*
**______________________________________________________________________________
*/
/**
* Free a storio rebuild context 
*/
static inline void storio_rebuild_ctx_free(STORIO_REBUILD_T * p) {
  storio_rebuild_ctx_reset(p);  
  ruc_objInsert(&storio_rebuild_ctx_free_list->link,&p->link); 
}
/*
**______________________________________________________________________________
*/
/**
* Initialize the storio rebuild context distributor

 
 retval 0 on success
 retval < 0 on error
*/
static inline void storio_rebuild_ctx_distributor_init() {
  STORIO_REBUILD_T * p;
  uint8_t            storio_rebuild_ref;

  /*
  ** Initi lost list
  */
  ruc_listHdrInit(&storio_device_mapping_ctx_lost_list);

  /*
  ** Allocate memory
  */
  storio_rebuild_ctx_free_list = (STORIO_REBUILD_T*) ruc_listCreate(MAX_STORIO_PARALLEL_REBUILD,sizeof(STORIO_REBUILD_T));
  if (storio_rebuild_ctx_free_list == NULL) {
    /*
    ** error on distributor creation
    */
    fatal( "ruc_listCreate(%d,%d)", MAX_STORIO_PARALLEL_REBUILD,(int)sizeof(STORIO_REBUILD_T) );
  }
  
  p = (STORIO_REBUILD_T*) ruc_objGetRefFromIdx(&storio_rebuild_ctx_free_list->link,0);
  for (storio_rebuild_ref=0; storio_rebuild_ref<MAX_STORIO_PARALLEL_REBUILD; storio_rebuild_ref++,p++) {
    p->ref = storio_rebuild_ref;
    storio_rebuild_ctx_free(p); 
  }
}
/*
**______________________________________________________________________________
*/
/**
* Free a storio rebuild context 
*
* @param idx The context index
* @param fid The fid the context should rebuild or NULL 
*
* @return the rebuild context address or NULL
*/
static inline STORIO_REBUILD_T * storio_rebuild_ctx_retrieve(int idx, char * fid) {
  STORIO_REBUILD_T * p;
 
  if (idx>=MAX_STORIO_PARALLEL_REBUILD) {
    storio_rebuild_stat.lookup_bad_index++;
    return NULL;
  }  

  p = (STORIO_REBUILD_T*) ruc_objGetRefFromIdx(&storio_rebuild_ctx_free_list->link,idx);  
  /*
  ** Check FID if any is given as input 
  */
  if (fid == NULL) {
    return p;
  }
  
  if (memcmp(fid,p->fid,sizeof(fid_t)) == 0) {
    storio_rebuild_stat.lookup_hit++;
    return p; 
  }  
  
  return NULL;  
}


#ifdef __cplusplus
}
#endif /*__cplusplus */

#endif
