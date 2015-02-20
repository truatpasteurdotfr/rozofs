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
#include <rozofs/core/rozofs_share_memory.h>

#include "storio_device_mapping.h"
#include "storaged.h"




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
 
    pChar += sprintf(pChar,"storage_rebuild --quiet -c %s -R -l 4 -r %s --sid %d/%d --device %d -o selfhealing_cid%d_sid%d_dev%d",
                     storaged_config_file,
		     pRelocate->st->export_hosts,
		     pRelocate->st->cid,
		     pRelocate->st->sid,
		     pRelocate->dev,
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
** Check whether the access to the device is still granted
** and get the number of free blocks
** 
*/
static inline int storio_device_monitor_get_free_space(char *root, int dev, uint64_t * free, uint64_t * size, uint64_t * bs) {
  struct statfs sfs;
  char          path[FILENAME_MAX];
  char        * pChar = path;
  
  pChar += sprintf(pChar, "%s/%d", root, dev); 

  /*
  ** Check that the device is writable
  */
  if (access(path,W_OK) != 0) {
    return -1;
  }
  
  /*
  ** Get statistics
  */
  if (statfs(path, &sfs) != 0) {
    return -1;
  }  
  
  /*
  ** Check we can see an X file. 
  ** This would mean that the device is not mounted
  */
  pChar += sprintf(pChar, "/X");
  if (access(path,F_OK) == 0) {
    return -1;
  }

  *free = sfs.f_bfree;
  *size = sfs.f_blocks;
  *bs   = sfs.f_bsize;
  return 0;
}
/*
**____________________________________________________
 * API to be called periodically to monitor errors on a period
 *
 * @param st: the storage to be initialized.
 *
 * @return a bitmask of the device having encountered an error
 */
static inline uint64_t storio_device_monitor_error(storage_t * st) {
  int dev;
  uint64_t bitmask = 0;  
  int old_active = st->device_errors.active;
  int new_active = 1 - old_active;
  
  
  for (dev = 0; dev < STORAGE_MAX_DEVICE_NB; dev++) {   
    st->device_errors.errors[new_active][dev] = 0;    
  }  
  st->device_errors.active = new_active;
 
  
  for (dev = 0; dev < STORAGE_MAX_DEVICE_NB; dev++) {    
    st->device_errors.total[dev] = st->device_errors.total[dev] + st->device_errors.errors[old_active][dev];
    bitmask |= (1ULL<<dev);
  }  
  return bitmask;
}
/*
**____________________________________________________
**  Device monotoring 
** 
**  @param param: Not significant
*/
void storio_device_monitor() {
  int           dev;
  int           passive;
  storage_t   * st;
  storage_device_ctx_t *pDev;
  int           max_failures;
  int           rebuilding;
  storage_device_info_t *info;
  uint64_t      bfree=0;
  uint64_t      bmax=0;
  uint64_t      bsz=0;   
    

  /*
  ** Loop on every storage managed by this storio
  */ 
  st = NULL;
  while ((st = storaged_next(st)) != NULL) {

    /*
    ** Resolve share memory with storaged to report device status
    */
    if (st->info == NULL) {
      st->info = rozofs_share_memory_resolve_from_name(st->root);
    }
    info = st->info; 
    if (info == NULL) {
      severe("rozofs_share_memory_resolve_from_name(%s) %s",st->root,strerror(errno));
    }

    if (st->selfHealing == -1) {
      /* No self healing configured */
      max_failures = -1;
      rebuilding   = 1; /* Prevents going on rebuilding */
    }
    else {

      /*
      ** Compute the maximium number of failures before relocation
      */
      max_failures = (st->selfHealing * 60)/STORIO_DEVICE_PERIOD;

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
    ** Monitor errors on devices
    */
    storio_device_monitor_error(st);

    /*
    ** Update the table of free block count on device to help
    ** for distribution of new files on devices 
    */
    passive = 1 - st->device_free.active; 

    for (dev = 0; dev < st->device_number; dev++) {

      pDev = &st->device_ctx[dev];
      bfree = 0;
      bmax  = 0;

      /*
      ** Check whether re-init is required
      */
      if (pDev->action==STORAGE_DEVICE_REINIT) {
        /*
	** Wait for the end of the relocation to re initialize the device
	*/
        if (pDev->status != storage_device_status_relocating) {
	  pDev->action = 0;
	  pDev->status = storage_device_status_init;
	}
      }

      /*
      ** Check wether errors must be reset
      */
      if (pDev->action==STORAGE_DEVICE_RESET_ERRORS) {
	pDev->action = 0;
        memset(&st->device_errors, 0, sizeof(storage_device_errors_t));
        storio_clear_faulty_fid();      
      }


      switch(pDev->status) {

        /* 
	** (re-)Initialization 
	*/
        case storage_device_status_init:
	  /*
	  ** Clear every thing
	  */
          memset(&st->device_errors, 0, sizeof(storage_device_errors_t));
          storio_clear_faulty_fid();      
	  pDev->failure = 0;
	  pDev->status = storage_device_status_is;   
	  // continue on next case 


	/*
	** Device In Service. No fault up to now
	*/  
        case storage_device_status_is:	
	  /*
	  ** When some errors have occured the device goes to degraded
	  ** which is equivallent to IS but with some errors
	  */
	  if (st->device_errors.total[dev] != 0 ) {
	    pDev->status = storage_device_status_degraded;
	  }

	  /*
	  ** Check whether the access to the device is still granted
	  ** and get the number of free blocks
	  */
	  if (storio_device_monitor_get_free_space(st->root, dev, &bfree, &bmax, &bsz) != 0) {
	    /*
	    ** The device is failing !
	    */
	    pDev->status = storage_device_status_failed;
	  }
	  break;

	case storage_device_status_degraded:

	  /*
	  ** When some errors have occured the device goes to degraded
	  ** which is equivallent to IS but with some errors
	  */
	  if (st->device_errors.total[dev] == 0 ) {
	    pDev->status = storage_device_status_is;
	  }
	  	  /*
	  ** Check whether the access to the device is still granted
	  ** and get the number of free blocks
	  */
	  if (storio_device_monitor_get_free_space(st->root, dev, &bfree, &bmax, &bsz) != 0) {
	    /*
	    ** The device is failing !
	    */
	    pDev->status = storage_device_status_failed;
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
	  if (storio_device_monitor_get_free_space(st->root, dev, &bfree, &bmax, &bsz) != 0) {
	    /*
	    ** Still failed
	    */	
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
	        rebuilding = 1; /* On rebuild at a time */
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
	  break;


	case storage_device_status_relocating:  
	  break;

	case storage_device_status_oos:
	  break;

	default:
	  break;    
      }	

      st->device_free.blocks[passive][dev] = bfree;  

      if (info) {
	info[dev].status  = pDev->status;
	info[dev].padding = 0;
	info[dev].free    = bfree * bsz;
	info[dev].size    = bmax  * bsz;
      }	
    } 

    /*
    ** Switch active and passive records
    */
    st->device_free.active = passive; 

  }              
}
/*
**____________________________________________________
**  Device monotoring thread
** 
**  @param param: Not significant
*/
void * storio_device_monitor_thread(void * param) {
  /*
  ** Never ending loop
  */ 
  while ( 1 ) {
    sleep(STORIO_DEVICE_PERIOD);
    storio_device_monitor();         
  }  
}

/*
**____________________________________________________
*/
/*
  start a periodic timer to update the available volume on devices
*/
int storio_device_mapping_monitor_thread_start() {
  pthread_attr_t             attr;
  int                        err;
  pthread_t                  thrdId;

  /*
  ** 1rst call to monitoring function
  */
  storio_device_monitor();

  /*
  ** Start monitoring thread
  */
  err = pthread_attr_init(&attr);
  if (err != 0) {
    severe("storio_device_mapping_monitor_thread_start pthread_attr_init() %s",strerror(errno));
    return -1;
  }  

  err = pthread_create(&thrdId,&attr,storio_device_monitor_thread,NULL);
  if (err != 0) {
    severe("storio_device_mapping_monitor_thread_start pthread_create() %s", strerror(errno));
    return -1;
  }    
  
  return 0;
}

