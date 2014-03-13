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

storio_device_mapping_stat_t storio_device_mapping_stat = { 0 };

/*
**______________________________________________________________________________

      Attributes LOOKUP SECTION
**______________________________________________________________________________
*/
com_cache_main_t  *storio_device_mapping_p = NULL; /**< pointer to the fid cache  */

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
      pChar += sprintf(pChar,"Size/max    : %d/%d\n",
                              (int)sizeof(storio_device_mapping_t)*storio_device_mapping_stat.count,
			      (int)sizeof(storio_device_mapping_t)*STORIO_DEVICE_MAPPING_MAX_ENTRIES); 
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
        pChar += sprintf(pChar,"%s is stored on device %d (consistency %d)\n",
	          argv[2],
		  com_cache_entry_p->device_number,
		  storio_device_mapping_stat.consistency);
      }
      else {
        pChar += sprintf(pChar,"%s was stored on device %d (inconsistent %d vs %d)\n",
	         argv[2],
	         com_cache_entry_p->device_number,
	         com_cache_entry_p->consistency, 
		 storio_device_mapping_stat.consistency);        
      }
    }
    uma_dbg_send(tcpRef,bufRef,TRUE,uma_dbg_get_buffer());
    return;
  }

  if (strcmp(argv[1],"device")==0) {    
    storage_t   * st;
    int           faulty_devices[STORAGE_MAX_DEVICE_NB];

    pChar += sprintf(pChar,"consistency index = %d\n",storio_device_mapping_stat.consistency);
 
    st = NULL;
    while ((st = storaged_next(st)) != NULL) {
      int           dev;
      int           fault=0;

      pChar += sprintf(pChar,"    cid = %d sid = %d\n", st->cid, st->sid);
            
      pChar += sprintf(pChar,"        root              = %s\n", st->root);
      pChar += sprintf(pChar,"        device_number     = %d\n",st->device_number);
      pChar += sprintf(pChar,"        mapper_modulo     = %d\n",st->mapper_modulo);
      pChar += sprintf(pChar,"        mapper_redundancy = %d\n",st->mapper_redundancy);		          
      for (dev = 0; dev < st->device_number; dev++) {
        pChar += sprintf(pChar,"           device %2d     %12llu blocks   errors total/period %d/%d\n", 
                         dev, 
		         (long long unsigned int)st->device_free.blocks[st->device_free.active][dev], 
                         st->device_errors.total[dev], 
			 st->device_errors.errors[st->device_errors.active][dev]);
        if (st->device_errors.total[dev] != 0) {
	  faulty_devices[fault++] = dev;
	}  			 
      }
      
      if (fault == 0) continue;
      
      // There is some faults on some devices
      pChar += sprintf(pChar,"    !!! %d faulty devices cid=%d/sid=%d/devices=%d", 
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
    ** Monitor errors on devces
    */
    if (st->device_errors.reset) {
      /* Reset error counters requested */
      memset(&st->device_errors, 0, sizeof(storage_device_errors_t));
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
