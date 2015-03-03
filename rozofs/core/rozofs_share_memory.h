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
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <rozofs/common/log.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#ifndef ROZOFS_SHARE_MEMORY_H
#define ROZOFS_SHARE_MEMORY_H

/*__________________________________________________________________________
*/
/**
*  Compure a key from a string
*
* @param name           A string
*
* @retval a key
*/
static inline key_t rozofs_share_memory_key_from_name(char * name) {
  char local[PATH_MAX];
  char *p = local;
  
  if (realpath(name, local)==NULL) {
    // Can not run realpath. name may be a string but not a path.
    // Let's compute a key directly from name
    p = name;
  }
  
  key_t K = 0;
  while (*p) K = 37 * K + *p++;
  return K; 
} 
/*__________________________________________________________________________
*/
/**
*  Resolve a block a share memory from its key
*
* @param key            The unic block identifier 
*
* @retval the block address or NULL
*/
static inline void * rozofs_share_memory_resolve_from_key(key_t key, char * name) {
  void            * p;
  int               shmid;
        
  /*
  ** Resolve the block
  */
  shmid = shmget(key,1,0666);
  if (shmid < 0) { 
    return NULL;
  }

  /*
  * Map it on memory
  */  
  p = shmat(shmid,0,0);
  if (p == 0) {
    if (name==NULL) {
      severe("shmat(key=%d,name=%s) %s",key,name,strerror(errno));
    }
    else {
      severe("shmat(key=%d) %s",key,strerror(errno));  
    }  
    return NULL;        
  }  
  return p;
}
/*__________________________________________________________________________
*/
/**
*  Resolve a block a share memory from its name
*
* @param name           The share memory block name
*
* @retval the block address or NULL
*/
static inline void * rozofs_share_memory_resolve_from_name(char * name) {
  key_t key = rozofs_share_memory_key_from_name(name);        
  return rozofs_share_memory_resolve_from_key(key,name);
}
/*__________________________________________________________________________
*/
/**
*  Free a block a share memory from its key
*
* @param key            The unic block identifier 
* @param name           A share memory block name for debug or NULL
*
* @retval 0 on success -1 else
*/
static inline int rozofs_share_memory_free_from_key(key_t key, char * name) {
  int               shmid;
  struct shmid_ds   ds;        
  /*
  ** Remove the block when it exists 
  */
  shmid = shmget(key,1,0666);
  if (shmid < 0) return 0;
 
  if (shmctl(shmid,IPC_RMID,&ds) < 0) {
    if (name != NULL) {
      severe("shmctl(RMID,key=%d,name=%s) %s",key,name,strerror(errno));
    }  
    else {      
      severe("shmctl(RMID,key=%d) %s",key, strerror(errno)); 
    }  
    return -1;   
  }
  return 0;
}
/*__________________________________________________________________________
*/
/**
*  Free a block a share memory from ist name
*
* @param name           The share memory block name
*
* @retval 0 on success -1 else
*/
static inline int rozofs_share_memory_free_from_name(char * name) {
  key_t key = rozofs_share_memory_key_from_name(name);
  return rozofs_share_memory_free_from_key(key, name);     
}
/*__________________________________________________________________________
*/
/**
*  Allocate a block a share memory from its key
*
* @param key            The unic block identifier 
* @param size           Size in bytes of the block
* @param name           A share memory block name for debug or NULL
*
* @retval the block address or NULL
*/
static inline void * rozofs_share_memory_allocate_from_key(key_t key, int size, char * name) {
  int               shmid;
  void            * p;
      
  /*
  ** Remove the block when it already exists 
  */
  rozofs_share_memory_free_from_key(key,name);
  
  /* 
  * Allocate a block 
  */
  shmid = shmget(key, size, IPC_CREAT | 0666);
  if (shmid < 0) {
    if (name==NULL) {
      severe("shmget(CREAT,key=%d,size=%d,name=%s) %s",key,size,name,strerror(errno));
    }
    else {
      severe("shmget(CREAT,key=%d,size=%d) %s",key,size,strerror(errno));
    }  
    return NULL;
  }  

  /*
  * Map it on memory
  */  
  p = shmat(shmid,0,0);
  if (p == 0) {
    if (name==NULL) {
      severe("shmat(key=%d,size=%d,name=%s) %s",key,size,name,strerror(errno));
    }
    else {
      severe("shmat(key=%d,size=%d) %s",key,size,strerror(errno));  
    }  
    rozofs_share_memory_free_from_key(key, name);
    return NULL;        
  }
    
  return p;
}
/*__________________________________________________________________________
*/
/**
*  Allocate a block a share memory from its name
*
* @param name           The share memory block name
* @param size           Size in bytes of the block
*
* @retval the block address or NULL
*/
static inline void * rozofs_share_memory_allocate_from_name(char * name, int size) {
  key_t key = rozofs_share_memory_key_from_name(name);
  return rozofs_share_memory_allocate_from_key(key, size, name);
}
#endif
