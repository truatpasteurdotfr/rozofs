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
#include <pthread.h> 
#include <sys/wait.h>

#include <rozofs/rozofs.h>
#include <rozofs/common/log.h>
#include <rozofs/common/list.h>
#include <rozofs/common/xmalloc.h>
#include <rozofs/common/profile.h>
#include <rozofs/common/mattr.h>
#include <rozofs/core/uma_dbg_api.h>
#include <rozofs/core/ruc_timer_api.h>

#include "storio_device_mapping.h"
#include "sconfig.h"
#include "storaged.h"
#include "storio_fid_cache.h"

#define STORIO_DEVICE_PERIOD    5000

extern sconfig_t storaged_config;

storio_device_mapping_stat_t storio_device_mapping_stat = { };
STORIO_REBUILD_STAT_S        storio_rebuild_stat = {0};
/*
**______________________________________________________________________________

      Attributes LOOKUP SECTION
**______________________________________________________________________________
*/

#define NB_STORIO_FAULTY_FID_MAX 15

typedef struct _storio_disk_thread_file_desc_t {
  fid_t        fid;
  uint8_t      cid;
  uint8_t      sid;
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
   @param cid      the faulty cid 
   @param sid      the faulty sid
   @param fid      the FID in fault   
*/
void storio_register_faulty_fid(int threadNb, uint8_t cid, uint8_t sid, fid_t fid) {
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
  pf->cid    = cid;  
  pf->sid    = sid;
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
  
void storage_fid_debug(char * argv[], uint32_t tcpRef, void *bufRef) {
  fid_t                          fid;
  char                         * pChar=uma_dbg_get_buffer();
  uint8_t                        nb_rebuild;
  uint8_t                        storio_rebuild_ref;
  STORIO_REBUILD_T             * pRebuild; 
  
  if ((ROZOFS_STORAGE_FILE_MAX_SIZE/ROZOFS_STORAGE_MAX_CHUNK_PER_FILE)>(1024*1024*1024)) {
    pChar += sprintf(pChar,"chunk size  : %llu G\n", 
               (long long unsigned int) ROZOFS_STORAGE_FILE_MAX_SIZE/ROZOFS_STORAGE_MAX_CHUNK_PER_FILE/(1024*1024*1024)); 
  }
  else if ((ROZOFS_STORAGE_FILE_MAX_SIZE/ROZOFS_STORAGE_MAX_CHUNK_PER_FILE)>(1024*1024)) {
    pChar += sprintf(pChar,"chunk size  : %llu M\n", 
               (long long unsigned int)ROZOFS_STORAGE_FILE_MAX_SIZE/ROZOFS_STORAGE_MAX_CHUNK_PER_FILE/(1024*1024)); 
  }  
  else if ((ROZOFS_STORAGE_FILE_MAX_SIZE/ROZOFS_STORAGE_MAX_CHUNK_PER_FILE)>(1024)) {
    pChar += sprintf(pChar,"chunk size  : %llu K\n", 
               (long long unsigned int)ROZOFS_STORAGE_FILE_MAX_SIZE/ROZOFS_STORAGE_MAX_CHUNK_PER_FILE/(1024)); 
  }
  else {
    pChar += sprintf(pChar,"chunk size  : %llu\n", 
               (long long unsigned int)ROZOFS_STORAGE_FILE_MAX_SIZE/ROZOFS_STORAGE_MAX_CHUNK_PER_FILE); 
  }  
 

  if (argv[1] == NULL) {
    pChar = display_cache_fid_stat(pChar);
    pChar += sprintf(pChar,"ctx nb x sz : %d x %d = %d\n",
            (int) STORIO_DEVICE_MAPPING_MAX_ENTRIES,
	    (int)sizeof(storio_device_mapping_t),
	    (int) (STORIO_DEVICE_MAPPING_MAX_ENTRIES * sizeof(storio_device_mapping_t)));
    pChar += sprintf(pChar,"free        : %llu\n", (long long unsigned int) storio_device_mapping_stat.free);  
    pChar += sprintf(pChar,"running     : %llu\n", (long long unsigned int) storio_device_mapping_stat.running);
    pChar += sprintf(pChar,"inactive    : %llu\n", (long long unsigned int) storio_device_mapping_stat.inactive);
    pChar += sprintf(pChar,"allocation  : %llu (release+%llu)\n", 
                          (long long unsigned int) storio_device_mapping_stat.allocation,
			  (long long unsigned int) (storio_device_mapping_stat.allocation-storio_device_mapping_stat.release));
    pChar += sprintf(pChar,"release     : %llu\n", (long long unsigned int) storio_device_mapping_stat.release);
    pChar += sprintf(pChar,"out of ctx  : %llu\n", (long long unsigned int) storio_device_mapping_stat.out_of_ctx);  
    uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());
    return;       
  }     

  uuid_parse(argv[1],fid);
  int index = storio_fid_cache_search(storio_device_mapping_hash32bits_compute(fid),fid);
  if (index == -1) {
    pChar += sprintf(pChar,"%s no match found !!!\n",argv[1]);
  } 
  else {
    storio_device_mapping_t * p = storio_device_mapping_ctx_retrieve(index);
    if (p == NULL) {
      pChar += sprintf(pChar,"%s no match found !!!\n",argv[1]);
    }
    else {
    
#if 0    
      if (p->consistency == storio_device_mapping_stat.consistency) {
	pChar += sprintf(pChar,"%s is consistent (%llu)\n",
	          argv[1],
		  (unsigned long long)storio_device_mapping_stat.consistency);
      }
      else {
	pChar += sprintf(pChar,"%s is inconsistent %llu vs %llu\n",
		 argv[1],
		 (unsigned long long)p->consistency, 
		 (unsigned long long)storio_device_mapping_stat.consistency);        
      }
#endif      
      pChar = trace_device(p->device, pChar);       
      pChar += sprintf(pChar,"\n");        
      pChar += sprintf(pChar,"running_request = %d\n",list_size(&p->running_request)); 
      pChar += sprintf(pChar,"waiting_request = %d\n",list_size(&p->waiting_request)); 
      if (p->storio_rebuild_ref.u32 != 0xFFFFFFFF) {
	for (nb_rebuild=0; nb_rebuild   <MAX_FID_PARALLEL_REBUILD; nb_rebuild++) {
	  storio_rebuild_ref = p->storio_rebuild_ref.u8[nb_rebuild];
	  if (storio_rebuild_ref == 0xFF) continue;
	  pChar += sprintf(pChar,"  rebuild %d : ",nb_rebuild+1);  
	  if (storio_rebuild_ref >= MAX_STORIO_PARALLEL_REBUILD) {
            pChar += sprintf(pChar,"Bad reference %d\n",storio_rebuild_ref);  
            continue;
	  }
	  pRebuild = storio_rebuild_ctx_retrieve(storio_rebuild_ref, (char*)fid);
	  if (pRebuild == 0) {
            pChar += sprintf(pChar,"Context reallocated to an other FID\n");  
            continue;
	  }
	  pChar += sprintf(pChar,"start %llu stop %llu aging %llu sec\n",
                	   (unsigned long long)pRebuild->start_block,
			   (unsigned long long)pRebuild->stop_block,
			   (unsigned long long)(time(NULL)-pRebuild->rebuild_ts));
	}
      }  
    }
  }
  uma_dbg_send(tcpRef,bufRef,TRUE,uma_dbg_get_buffer());
  return;         
}
 
void storage_device_debug(char * argv[], uint32_t tcpRef, void *bufRef) {
  char                         * pChar=uma_dbg_get_buffer();
  int                            idx,threadNb; 
  int                            first;

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
    if (st->selfHealing == -1) {
      pChar += sprintf(pChar,"    self-healing      = No\n");
    }  
    else {
      pChar += sprintf(pChar,"    self-healing      = %d min (%d failures)\n",
                             st->selfHealing,
			     (st->selfHealing * 60)/(STORIO_DEVICE_PERIOD/1000));
    }  
      
    pChar += sprintf(pChar,"\n    %6s | %6s | %8s | %12s | %12s |\n",
             "device","status","failures","blocks", "errors");
    pChar += sprintf(pChar,"    _______|________|__________|______________|______________|\n");
	     
    for (dev = 0; dev < st->device_number; dev++) {
      pChar += sprintf(pChar,"    %6d | %6s | %8d | %12llu | %12llu |\n", 
                       (int)dev, 
		       storage_device_status2string(st->device_ctx[dev].status),
		       (int)st->device_ctx[dev].failure,
		       (long long unsigned int)st->device_free.blocks[st->device_free.active][dev], 
                       (long long unsigned int)st->device_errors.total[dev]);
      if ((st->device_ctx[dev].status != storage_device_status_is)||(st->device_errors.total[dev])) {
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
	char * pt=display;

        pt += sprintf(pt,"-s %d/%d -f ",pf->cid, pf->sid);
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
void storage_rebuild_debug(char * argv[], uint32_t tcpRef, void *bufRef) {
  uint8_t                        storio_ref;
  STORIO_REBUILD_T             * pRebuild;
  char                         * pChar=uma_dbg_get_buffer();
  int                            nb=0;

  if ((argv[1] != NULL) && (strcmp(argv[1],"reset")==0)) {
    memset(&storio_rebuild_stat,0,sizeof(storio_rebuild_stat));     
  }

  pChar += sprintf(pChar,"allocated        : %llu\n",(long long unsigned int) storio_rebuild_stat.allocated);
  pChar += sprintf(pChar,"stollen          : %llu\n",(long long unsigned int) storio_rebuild_stat.stollen);
  pChar += sprintf(pChar,"aborted          : %llu\n",(long long unsigned int) storio_rebuild_stat.aborted);
  pChar += sprintf(pChar,"released         : %llu\n",(long long unsigned int) storio_rebuild_stat.released);
  pChar += sprintf(pChar,"out of ctx       : %llu\n",(long long unsigned int) storio_rebuild_stat.out_of_ctx);
  pChar += sprintf(pChar,"lookup hit       : %llu\n",(long long unsigned int) storio_rebuild_stat.lookup_hit);
  pChar += sprintf(pChar,"lookup miss      : %llu\n",(long long unsigned int) storio_rebuild_stat.lookup_miss);
  pChar += sprintf(pChar,"lookup bad index : %llu\n",(long long unsigned int) storio_rebuild_stat.lookup_bad_index);
  
  pChar += sprintf(pChar,"Running rebuilds : "); 
  
  for (storio_ref=0; storio_ref <MAX_STORIO_PARALLEL_REBUILD; storio_ref++) {

    pRebuild = storio_rebuild_ctx_retrieve(storio_ref, NULL);
    if (pRebuild->rebuild_ts == 0) continue;
    
    pChar += sprintf(pChar,"\n%2d) ",storio_ref);  
    uuid_unparse(pRebuild->fid,pChar);
    pChar += 36;
    pChar += sprintf(pChar," chunk %-3d device %-3d start %-10llu stop %-10llu aging %llu sec",
                     pRebuild->chunk, pRebuild->old_device,
                     (long long unsigned int)pRebuild->start_block,
		     (long long unsigned int)pRebuild->stop_block,
		     (long long unsigned int)(time(NULL)-pRebuild->rebuild_ts));
    nb++;
  }
  
  if (nb == 0) pChar += sprintf(pChar,"NONE\n");
  else         pChar += sprintf(pChar,"\n");

  uma_dbg_send(tcpRef,bufRef,TRUE,uma_dbg_get_buffer());
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
/*_____________________________________
** Parameter of the relocate thread
*/
typedef struct _storio_relocate_ctx_t {
  storage_t     * st;
  int             dev;
} storio_relocate_ctx_t;

/*
**____________________________________________________
**  Start a thread for relocation a device
**  
**  @param st   The storage context
**  @param dev  The device number to rebuild
**   
**  @retval 0 when the thread is succesfully started
*/
extern char storaged_config_file[];
extern char * storaged_hostname;
void * storio_device_relocate_thread(void *arg) {
  storio_relocate_ctx_t      *pRelocate = arg;
  storage_device_ctx_t    *   pDev = &pRelocate->st->device_ctx[pRelocate->dev];
  char            cmd[256];
  int             status;
  pid_t           pid;
  int             ret;
  
  pid = fork();  
  if (pid == 0) {
    char * pChar = cmd;
 
    pChar += sprintf(pChar,"storage_rebuild --quiet -c %s -R -l 4 -r %s --sid %d/%d --device %d",
                     storaged_config_file,
		     pRelocate->st->export_hosts,
		     pRelocate->st->cid,
		     pRelocate->st->sid,
		     pRelocate->dev);

    if (storaged_hostname != NULL) {
      pChar += sprintf(pChar," -H %s",storaged_hostname);
    }
            
    errno = 0;	
    status = system(cmd);
    exit(status);
  }
   
  /* Check for rebuild sub processes status */    
  ret = waitpid(pid,&status,0);
  
  if ((ret == pid)&&(WEXITSTATUS(status)==0)) {
    /* Relocate is successfull. Let's put the device Out of service */
    pDev->status = storage_device_status_oos;
  }
  else {
    /* Relocate has failed. Let's retry later */
    pDev->failure /= 2;
    pDev->status   = storage_device_status_failed;
  }
  
  free(pRelocate);
  pthread_exit(NULL); 
}
/*
**____________________________________________________
**  Start a thread for relocation a device
**  
**  @param st  The storage context
**  @param dev The device number to rebuild
**   
**  @retval 0 when the thread is succesfully started
*/
int storio_device_relocate(storage_t * st, int dev) {
  pthread_attr_t             attr;
  int                        err;
  pthread_t                  thrdId;
  storio_relocate_ctx_t    * pRelocate;

  if (st->export_hosts == NULL) {
    severe("storio_device_relocate cid %d sid %d no export hosts",st->cid, st->sid);
    return -1;
  }
  
  pRelocate = malloc(sizeof(storio_relocate_ctx_t));
  if (pRelocate == NULL) {
    severe("storio_device_relocate malloc(%d) %s",(int)sizeof(storio_relocate_ctx_t), strerror(errno));
    return -1;
  }  
  pRelocate->st  = st;
  pRelocate->dev = dev;

  err = pthread_attr_init(&attr);
  if (err != 0) {
    severe("storio_device_relocate pthread_attr_init() %s",strerror(errno));
    free(pRelocate);
    return -1;
  }  

  err = pthread_create(&thrdId,&attr,storio_device_relocate_thread,pRelocate);
  if (err != 0) {
    severe("storio_device_relocate pthread_create() %s", strerror(errno));
    free(pRelocate);
    return -1;
  }    
  
  return 0;
}
/*
**____________________________________________________
**  Periodic timer expiration
** 
**  @param param: Not significant
*/
void storio_device_mapping_periodic_ticker(void * param) {
  struct statfs sfs;
  int           dev;
  int           passive;
  char          path[FILENAME_MAX];
  storage_t   * st;
  uint64_t      error_bitmask;
  int           failed=0;
  storage_device_ctx_t *pDev;
  int           max_failures;
  int           rebuilding;
   
  /*
  ** Loop on every storage managed by this storio
  */ 
  st = NULL;
  while ((st = storaged_next(st)) != NULL) {


    if (st->selfHealing == -1) {
      /* No self healing configured */
      max_failures = -1;
      rebuilding   = 1; /* Prevents going on rebuilding */
    }
    else {
    
      /*
      ** Compute the maximium number of failures before relocation
      */
      max_failures = (st->selfHealing * 60)/(STORIO_DEVICE_PERIOD/1000);
      
      /*
      ** Check whether some device is already in relocating status
      */
      rebuilding = 0;       
      for (dev = 0; dev < st->device_number; dev++) {
        pDev = &st->device_ctx[dev];
	if (pDev->status == storage_device_status_relocating) {
	  rebuilding = 1; /* No more than 1 rebuild at a time */
	  break;
	}
      }             
    }  
    
    /*
    ** Update the table of free block count on device to help
    ** for distribution of new files on devices 
    */
    passive = 1 - st->device_free.active; 

    for (dev = 0; dev < st->device_number; dev++) {

      pDev = &st->device_ctx[dev];
      
      sprintf(path, "%s/%d/", st->root, dev); 
      st->device_free.blocks[passive][dev] = 0;

      /*
      ** Check whether re-init is required
      */
      if (pDev->action==STORAGE_DEVICE_REINIT) {
        /*
	** Wait for the end of the relocation to re initialize the device
	*/
        if (pDev->status != storage_device_status_relocating) {
	  pDev->status = storage_device_status_init;
	  pDev->action = 0;
	}
      }

      /*
      ** Check wether errors must be reset
      */
      if (pDev->action==STORAGE_DEVICE_RESET_ERRORS) {
        memset(&st->device_errors, 0, sizeof(storage_device_errors_t));
        memset(storio_faulty_fid, 0, sizeof(storio_faulty_fid));      
	pDev->action = 0;
      }

      failed = 0;	        
      switch(pDev->status) {
      
        /* 
	** (re-)Initialization 
	*/
        case storage_device_status_init:
	  /*
	  ** Clear every thing
	  */
          memset(&st->device_errors, 0, sizeof(storage_device_errors_t));
          memset(storio_faulty_fid, 0, sizeof(storio_faulty_fid));      
	  pDev->failure = 0;
	  pDev->status = storage_device_status_is;   
	  // continue on next case 
	  
	  
	/*
	** Device In Service. No fault up to now
	*/  
        case storage_device_status_is:
	
	  /*
	  ** Check whether the access to the device is still granted
	  ** and get the number of free blocks
	  */
	  failed = 1;
	  if ((access(path,W_OK) == 0)&&(statfs(path, &sfs) == 0)) {
	    failed = 0; 	
	  }	
	  
	  /*
	  ** The device is failing !
	  */
	  if (failed) {
	    pDev->status = storage_device_status_failed;
	  }
	  else {
	    st->device_free.blocks[passive][dev] = sfs.f_bfree;
	  }  
	  break;
	  
	/*
	** Device failed. Check whether the status is confirmed
	*/  
	case storage_device_status_failed:
	
	  /*
	  ** Check whether the access to the device is still granted
	  ** and get the number of free blocks
	  */
	  failed = 1;
	  if ((access(path,W_OK) == 0)&&(statfs(path, &sfs) == 0)) {
	    failed = 0; 	
	  }	
	  
	  /*
	  ** Still failed
	  */	
	  if (failed) {
	    pDev->failure++;
	    /*
	    ** When self healing is configured and no other device
	    ** is relocating on this storage and failure threashold
	    ** has been crossed, relocation should take place
	    */
	    if ((rebuilding==0) && (pDev->failure >= max_failures)) {
	      /*
	      ** Let's start self healing
	      */
	      pDev->status = storage_device_status_relocating;
	      if (storio_device_relocate(st,dev) == 0) {
	        rebuilding = 1;
	      }	
	      else {
	        pDev->status = storage_device_status_failed;	        
	      }
	    }
	    break;
	  }
	  
	  /*
	  ** The device is repaired
	  */
	  pDev-> status = storage_device_status_is;
	  pDev->failure = 0;	  
	  st->device_free.blocks[passive][dev] = sfs.f_bfree;
	  break;
	  
	  
	case storage_device_status_relocating:  
	  break;
	  
	case storage_device_status_oos:
	  break;
	  
	default:
	  break;    
      }	
    }  
    
    /*
    ** Switch active and passive records
    */
    st->device_free.active = passive; 
    
    
    
    /*
    ** Monitor errors on devices
    */
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
                            STORIO_DEVICE_PERIOD, // every 5 sec
 	                    storio_device_mapping_periodic_ticker,
 			    0);

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

uint32_t storio_device_mapping_exact_match(void *key ,uint32_t index) {
  storio_device_mapping_t   * p;  
  
  p = storio_device_mapping_ctx_retrieve(index);
  if (p == NULL) return 0;
    
  if (uuid_compare(p->fid,key) != 0) {
    return 0;
  }
  /*
  ** Match !!
  */
  return 1;
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

uint32_t storio_device_mapping_delete_req(uint32_t index) {
  storio_device_mapping_t   * p;  
  
  /*
  ** Retrieve the context from the index
  */
  p = storio_device_mapping_ctx_retrieve(index);
  if (p == NULL) return 0;
    
  /*
  ** Release the context when inactive
  */  
  if (p->status == STORIO_FID_INACTIVE) {
    storio_device_mapping_release_entry(p);
    return 1;
  }
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

  /*
  ** Initialize rebuild context distributor
  */
  storio_rebuild_ctx_distributor_init();
  
  /*
  ** Initialize the FID cache 
  */
  storio_fid_cache_init(storio_device_mapping_exact_match, storio_device_mapping_delete_req);

  /*
  ** Initialize dev mapping distributor
  */
//  storio_device_mapping_stat.consistency = 1;   
  storio_device_mapping_ctx_distributor_init();

  /*
  ** Register periodic ticker
  */
  storio_device_mapping_start_timer();
  
    
  /*
  ** Add a debug topic
  */
  uma_dbg_addTopic("device", storage_device_debug); 
  uma_dbg_addTopic("fid", storage_fid_debug); 
  uma_dbg_addTopic_option("rebuild", storage_rebuild_debug, UMA_DBG_OPTION_RESET); 
  return 0;
}
