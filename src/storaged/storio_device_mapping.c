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
#define _XOPEN_SOURCE 500

#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <uuid/uuid.h>
#include <sys/vfs.h> 
 
#include <rozofs/rozofs.h>
#include <rozofs/common/log.h>
#include <rozofs/common/list.h>
#include <rozofs/common/xmalloc.h>
#include <rozofs/common/profile.h>
#include <rozofs/common/mattr.h>
#include <rozofs/core/com_cache.h>
#include <rozofs/core/uma_dbg_api.h>
#include <rozofs/core/ruc_timer_api.h>

#include "storio_device_mapping.h"
#include "sconfig.h"
#include "storaged.h"


extern sconfig_t storaged_config;

storio_device_mapping_stat_t storio_device_mapping_stat = { };

/*
**______________________________________________________________________________

      Attributes LOOKUP SECTION
**______________________________________________________________________________
*/
com_cache_main_t  *storio_device_mapping_p = NULL; /**< pointer to the fid cache  */

#define NB_STORIO_FAULTY_FID_MAX 15

typedef struct _storio_disk_thread_file_desc_t {
  fid_t        fid;
  uint8_t      layout;
  uint8_t      bsize;
  uint8_t      cid;
  uint8_t      sid;
  uint8_t      dist[ROZOFS_SAFE_MAX];
} storio_disk_thread_file_desc_t;

typedef struct _storio_disk_thread_faulty_fid_t {
   uint32_t                         nb_faulty_fid_in_table;
   storio_disk_thread_file_desc_t   file[NB_STORIO_FAULTY_FID_MAX];
} storio_disk_thread_faulty_fid_t;

storio_disk_thread_faulty_fid_t storio_faulty_fid[ROZOFS_MAX_DISK_THREADS] = {  };

/*
**____________________________________________________
*/
/*
* Register the FID that has encountered an error
  
   @param threadNb the thread number
   @param layout   the file layout
   @param bsize    the block size as defined in enum ROZOFS_BSIZE_E
   @param cid      the faulty cid 
   @param sid      the faulty sid
   @param dist     the distribution      
   @param fid      the FID in fault   
*/
void storio_register_faulty_fid(int threadNb, uint8_t layout, uint8_t bsize, uint8_t cid, uint8_t sid, uint32_t * dist, fid_t fid) {
  storio_disk_thread_faulty_fid_t * p;
  int                               idx;
  storio_disk_thread_file_desc_t  * pf;
    
  if (threadNb >= ROZOFS_MAX_DISK_THREADS) return;
  
  p = &storio_faulty_fid[threadNb];
  
  // No space left to register this FID
  if (p->nb_faulty_fid_in_table >= NB_STORIO_FAULTY_FID_MAX) return;
  
  // Check this FID is not already registered in the table
  for (idx = 0; idx < p->nb_faulty_fid_in_table; idx++) {
    if (memcmp(p->file[idx].fid, fid, sizeof(fid_t))== 0) return;
  } 
  
  // Register this FID
  pf = &p->file[p->nb_faulty_fid_in_table];
  pf->layout = layout;
  pf->bsize  = bsize;
  pf->cid    = cid;  
  pf->sid    = sid;
  memcpy(pf->dist,dist,ROZOFS_SAFE_MAX);
  memcpy(pf->fid, fid, sizeof(fid_t));
  p->nb_faulty_fid_in_table++;
  return;
}

/*
**______________________________________________________________________________
*/
/**
* Debug 
  
*/
static char * storage_device_mapping_debug_help(char * pChar) {
  pChar += sprintf(pChar,"usage:\n");
  pChar += sprintf(pChar,"device_mapping fid [<FID>]\n");
  pChar += sprintf(pChar,"device_mapping device\n");
  return pChar; 
}  
void storage_device_mapping_debug(char * argv[], uint32_t tcpRef, void *bufRef) {
  fid_t                          fid;
  char                         * pChar=uma_dbg_get_buffer();
  storio_device_mapping_t      * com_cache_entry_p; 
  int                            idx,threadNb; 
  int                            first;

 
  if (argv[1] == NULL) {
    pChar = storage_device_mapping_debug_help(pChar);
    uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());
    return;       
  } 
  
  if (strcmp(argv[1],"fid")==0) {
  
    if (argv[2] == NULL) {
      pChar += sprintf(pChar,"Entries/max : %llu/%d\n",(unsigned long long)storio_device_mapping_stat.count,
                      STORIO_DEVICE_MAPPING_MAX_ENTRIES);
      pChar += sprintf(pChar,"Entry size  : %d\n",(int)sizeof(storio_device_mapping_t));
      pChar += sprintf(pChar,"Size/max    : %llu/%llu\n",
                      (unsigned long long)sizeof(storio_device_mapping_t)*storio_device_mapping_stat.count,
		      (unsigned long long)sizeof(storio_device_mapping_t)*STORIO_DEVICE_MAPPING_MAX_ENTRIES); 
      pChar += sprintf(pChar,"consistency : %llu\n", (unsigned long long)storio_device_mapping_stat.consistency);     
      pChar += sprintf(pChar,"miss        : %llu\n", (unsigned long long)storio_device_mapping_stat.miss);   
      pChar += sprintf(pChar,"match       : %llu\n", (unsigned long long)storio_device_mapping_stat.match);   
      pChar += sprintf(pChar,"insert      : %llu\n", (unsigned long long)storio_device_mapping_stat.insert);   
      pChar += sprintf(pChar,"release     : %llu\n", (unsigned long long)storio_device_mapping_stat.release);   
      pChar += sprintf(pChar,"inconsistent: %llu\n", (unsigned long long)storio_device_mapping_stat.inconsistent);   
      uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());
      return;       
    }     
 
    uuid_parse(argv[2],fid);
    com_cache_entry_p = com_cache_bucket_search_entry(storio_device_mapping_p,fid);
    if (com_cache_entry_p == NULL) {
      pChar += sprintf(pChar,"%s no match found !!!\n",argv[1]);
    } 
    else {    
      if (com_cache_entry_p->consistency == storio_device_mapping_stat.consistency) {
        pChar += sprintf(pChar,"%s is stored on device %d (consistency %llu)\n",
	          argv[2],
		  com_cache_entry_p->device_number,
		  (unsigned long long)storio_device_mapping_stat.consistency);
      }
      else {
        pChar += sprintf(pChar,"%s was stored on device %d (inconsistent %d vs %llu)\n",
	         argv[2],
	         com_cache_entry_p->device_number,
	         com_cache_entry_p->consistency, 
		 (unsigned long long)storio_device_mapping_stat.consistency);        
      }
    }
    uma_dbg_send(tcpRef,bufRef,TRUE,uma_dbg_get_buffer());
    return;
  }

  if (strcmp(argv[1],"device")==0) {    
    storage_t   * st;
    int           faulty_devices[STORAGE_MAX_DEVICE_NB];
 
    st = NULL;
    while ((st = storaged_next(st)) != NULL) {
      int           dev;
      int           fault=0;
      storio_disk_thread_file_desc_t * pf;
       
      pChar += sprintf(pChar,"cid = %d sid = %d\n", st->cid, st->sid);
            
      pChar += sprintf(pChar,"    root              = %s\n", st->root);
      pChar += sprintf(pChar,"    mapper_modulo     = %d\n",st->mapper_modulo);
      pChar += sprintf(pChar,"    mapper_redundancy = %d\n",st->mapper_redundancy);		          
      pChar += sprintf(pChar,"    device_number     = %d\n",st->device_number);
      for (dev = 0; dev < st->device_number; dev++) {
        pChar += sprintf(pChar,"       device %2d     %12llu blocks   errors total/period %d/%d\n", 
                         dev, 
		         (long long unsigned int)st->device_free.blocks[st->device_free.active][dev], 
                         st->device_errors.total[dev], 
			 st->device_errors.errors[st->device_errors.active][dev]);
        if (st->device_errors.total[dev] != 0) {
	  faulty_devices[fault++] = dev;
	}  			 
      }
      
      // Display faulty FID table
      first = 1;
      for (threadNb=0; threadNb < ROZOFS_MAX_DISK_THREADS; threadNb++) {
        for (idx=0; idx < storio_faulty_fid[threadNb].nb_faulty_fid_in_table; idx++) {
          if (first) {
	    pChar += sprintf(pChar,"Faulty FIDs:\n");
            first = 0;
          }
	  pf = &storio_faulty_fid[threadNb].file[idx];
	  
	  // Check whether this FID has already been listed
	  {
	    int already_listed = 0;
	    int prevThread,prevIdx;
            storio_disk_thread_file_desc_t * prevPf;	    
	    for (prevThread=0; prevThread<threadNb; prevThread++) {
              for (prevIdx=0; prevIdx < storio_faulty_fid[prevThread].nb_faulty_fid_in_table; prevIdx++) {
	        prevPf = &storio_faulty_fid[prevThread].file[prevIdx];
		if ((pf->cid == prevPf->cid) && (pf->sid == prevPf->sid)
		&&  (memcmp(pf->fid, prevPf->fid, sizeof(fid_t))==0)) {
		  already_listed = 1;
		  break; 
		}
              }
	      if (already_listed) break; 	      
	    }
	    if (already_listed) continue;
	  }
	  
	  char display[128];
	  int  i;
	  char * pt=display;
          int8_t rozofs_safe = rozofs_get_rozofs_safe(pf->layout);

          pt += sprintf(pt,"-s %d/%d -f %d/",pf->cid, pf->sid, pf->layout);
	  pt += sprintf(pt,"%3.3d",pf->dist[0]);
	  for (i=1; i< rozofs_safe; i++) {
	    pt += sprintf(pt,"-%3.3d",pf->dist[i]);
	  }
	  pt += sprintf(pt,"/");
	  uuid_unparse((const unsigned char *)pf->fid, pt);
	  
          pChar += sprintf(pChar,"    %s\n", display);
        }
      } 
      
      if (fault == 0) continue;
      
      // There is some faults on some devices
      pChar += sprintf(pChar,"\n    !!! %d faulty devices cid=%d/sid=%d/devices=%d", 
                       fault, st->cid, st->sid, faulty_devices[0]);
      
      for (dev = 1; dev < fault; dev++) {
        pChar += sprintf(pChar,",%d", faulty_devices[dev]);  
      } 
      pChar += sprintf(pChar,"\n");
                
    }    
    uma_dbg_send(tcpRef,bufRef,TRUE,uma_dbg_get_buffer());
    return;
  }
  
  pChar = storage_device_mapping_debug_help(pChar);
  uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());
  return;          
}

/*
**____________________________________________________
*/
/*
  Allocate a device for a file
  
   @param st: storage context
*/
uint32_t storio_device_mapping_allocate_device(storage_t * st) {
  int           dev;
  uint64_t    * pBlocks;
  uint64_t      max = 0;
  uint64_t      val;
  uint64_t      choosen_device = 0;
  int           active;
  
  active = st->device_free.active;
  
  for (dev = 0; dev < st->device_number; dev++,pBlocks++) {
    
    val = st->device_free.blocks[active][dev];
    if (val > max) {
      max = val;
      choosen_device = dev;
    }
  }
  if (max > 256) max -= 256;
  st->device_free.blocks[active][choosen_device] = max;
  
  return choosen_device;
}
/*
**____________________________________________________
*/
/*
  Periodic timer expiration
  
   @param param: Not significant
*/
void storio_device_mapping_periodic_ticker(void * param) {
  struct statfs sfs;
  int           dev;
  int           passive;
  char          path[FILENAME_MAX];
  storage_t   * st;
  uint64_t      error_bitmask;
 
  st = NULL;
  while ((st = storaged_next(st)) != NULL) {

  
  
    /*
    ** Update the table of free block count on device to help
    ** for distribution of new files on devices 
    */
    passive = 1 - st->device_free.active; 

    for (dev = 0; dev < st->device_number; dev++) {

      sprintf(path, "%s/%d/", st->root, dev); 

      if (statfs(path, &sfs) == -1) {
	if (statfs(path, &sfs) == -1) {
          st->device_free.blocks[passive][dev] = 0;
	}	
      }

      st->device_free.blocks[passive][dev] = sfs.f_bfree;
    }  

    
    st->device_free.active = passive; 
    
    
    
    /*
    ** Monitor errors on devices
    */
    if (st->device_errors.reset) {
      /* Reset error counters requested */
      memset(&st->device_errors, 0, sizeof(storage_device_errors_t));
      memset(storio_faulty_fid, 0, sizeof(storio_faulty_fid));      
      continue;
      
    }
    error_bitmask = storage_periodic_error_on_device_monitoring(st);
    if (error_bitmask == 0) continue;
    
    /* Some errors occured */
  }         
}

/*
**____________________________________________________
*/
/*
  start a periodic timer to update the available volume on devices
*/
void storio_device_mapping_start_timer() {
  struct timer_cell * periodic_timer;
  
  // 1rst call to the periodic task
  storio_device_mapping_periodic_ticker(NULL);

  periodic_timer = ruc_timer_alloc(0,0);
  if (periodic_timer == NULL) {
    severe("no timer");
    return;
  }
  ruc_periodic_timer_start (periodic_timer, 
                            8000, // every 8 sec
 	                    storio_device_mapping_periodic_ticker,
 			    0);

}
/*
**______________________________________________________________________________
*/
/**
* release an entry of the attributes cache

  @param p : pointer to the user cache entry 
  
*/
void storio_device_mapping_release_entry(void *entry_p)
{
  com_cache_entry_t  *p = entry_p;
  com_cache_bucket_remove_entry(storio_device_mapping_p, p->usr_key_p);
  free(p);
  storio_device_mapping_stat.release++;
  storio_device_mapping_stat.count--;
}
void storio_device_mapping_delete_cbk(void * entry) 
{
  return;
}  
/*
**______________________________________________________________________________
*/
/**
*  hash computation from  fid 

  @param h : initial hahs value
  @param key2: parent fid
  @param hash2 : pointer to the secondary hash (optional)
  @param len: pointer to an array that return the length of the filename string
  
  @retval hash value
*/
static inline uint32_t uuid_hash_fnv(uint32_t h, void *key) {

    unsigned char *d ;

    if (h == 0) h = 2166136261U;

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
uint32_t storio_device_mapping_hash_compute(void *usr_key)
{
  uint32_t hash;

  hash = uuid_hash_fnv(0,usr_key);
  return hash;
}

/*
**______________________________________________________________________________
*/
/**
* attributes entry hash compute 

  @param key1 : pointer to the key associated with the entry from cache 
  @param key2 : pointer to array that contains the searching key
  
  @retval 0 on match
  @retval <> 0  no match  
*/

uint32_t storio_device_mapping_exact_match(void *key1, void *key2)
{
  if (uuid_compare(key1,key2) != 0)  
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
* creation of the FID cache
 That API is intented to be called during the initialization of the module
 
 The max number of entries is given the STORIO_DEVICE_MAPPING_MAX_ENTRIES constant
 and the size of the level 0 entry set is given by STORIO_DEVICE_MAPPING_LVL0_SZ_POWER_OF_2 constant
 
 retval 0 on success
 retval < 0 on error
*/
 
uint32_t storio_device_mapping_init()
{

  com_cache_usr_fct_t callbacks;
  
  if (storio_device_mapping_p != NULL)
  {
    return 0;
  }
  
  
  storio_device_mapping_stat.consistency = 1;
  
  
  callbacks.usr_exact_match_fct = storio_device_mapping_exact_match;
  callbacks.usr_hash_fct        = storio_device_mapping_hash_compute;
  callbacks.usr_delete_fct      = storio_device_mapping_delete_cbk;
  
  storio_device_mapping_p = com_cache_create(STORIO_DEVICE_MAPPING_LVL0_SZ_POWER_OF_2,
                                      STORIO_DEVICE_MAPPING_MAX_ENTRIES,
                                      &callbacks);
  if (storio_device_mapping_p == NULL)
  {
    /*
    ** we run out of memory
    */
    return -1;  
  }


  /*
  ** Register periodic ticker
  */
  storio_device_mapping_start_timer();
  
    
  /*
  ** Add a debug topic
  */
  uma_dbg_addTopic("device_mapping", storage_device_mapping_debug); 
  
  return 0;
}
