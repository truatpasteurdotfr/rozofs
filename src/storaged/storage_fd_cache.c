
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


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>

#include <rozofs/rozofs.h>
#include <rozofs/common/log.h>
#include <rozofs/common/list.h>
#include <rozofs/rozofs_srv.h>

#include "storage_fd_cache.h"

storage_fd_cache_t *storage_fd_cache_p = NULL;
uint32_t storage_fd_cache_max = 0;
uint32_t storage_fd_max_inuse_count = 0;
storage_fd_cache_t *current_storage_fd_cache_p= NULL;

/*
**__________________________________________________________________________
*/
/**
 Init of the cache
  
   @param nb_entries
   
   @retval 0 on success
   @retval -1 on error
*/
int storage_fd_cache_init(uint32_t entries,uint32_t max_inuse_count)
{
  int i;
  
  if(storage_fd_cache_p != NULL) return 0;
  
  storage_fd_max_inuse_count = max_inuse_count;
  storage_fd_cache_p = malloc(sizeof(storage_fd_cache_t)*entries);
  if (storage_fd_cache_p == NULL) 
  {
    errno = ENOMEM;
    return -1;
  }
  storage_fd_cache_max = entries;
  memset(storage_fd_cache_p,0,sizeof(storage_fd_cache_t)*entries);
  for (i = 0 ; i < storage_fd_cache_max; i ++)
  {
    storage_fd_cache_p[i].fd = -1;
    storage_fd_cache_p[i].read_ahead_p = malloc(STORAGE_READAHEAD_SZ);
    storage_fd_cache_p[i].len_read =0;
  }
  return 0;  
}

/*
**__________________________________________________________________________
*/
/**
  cache fd insert
  
  @param fd : file descriptor
  @param fid : fid associated with the file
  
  @retval <>NULL  success
  @retval NULL failed
*/
storage_fd_cache_t *storage_fd_cache_insert(int fd, fid_t fid)
{
  int i;
  
  for (i = 0 ; i < storage_fd_cache_max; i ++)
  {
    if (storage_fd_cache_p[i].fd == -1)
    {
      memcpy(storage_fd_cache_p[i].fid,fid,sizeof(fid_t));
      storage_fd_cache_p[i].fd = fd;
      storage_fd_cache_p[i].inuse_count = 1;  
      storage_fd_cache_p[i].len_read =0;
      return &storage_fd_cache_p[i];
    }
    /*
    ** check the inuse count
    */
    if (storage_fd_cache_p[i].inuse_count >  storage_fd_max_inuse_count)
    {
      close(storage_fd_cache_p[i].fd);
      memcpy(storage_fd_cache_p[i].fid,fid,sizeof(fid_t));
      storage_fd_cache_p[i].fd = fd;
      storage_fd_cache_p[i].inuse_count = 1;  
      storage_fd_cache_p[i].len_read =0;
      return &storage_fd_cache_p[i];;          
    } 
  }
  return NULL;
}


/*
**__________________________________________________________________________
*/
/**
  cache fd search
  
  @param fid : fid associated with the file
  
  @retval <>NULL  success
  @retval NULL failed
*/
storage_fd_cache_t *storage_fd_cache_search(fid_t fid)
{
  int i;
  int ret;
  
  for (i = 0 ; i < storage_fd_cache_max; i ++)
  {
    if (storage_fd_cache_p[i].fd != -1)
    {
      ret = memcmp(storage_fd_cache_p[i].fid,fid,sizeof(fid_t));
      if (ret == 0)
      { 
        storage_fd_cache_p[i].inuse_count++; 
        return &storage_fd_cache_p[i];
      } 
    }
  }
  return NULL;
}

/*
**__________________________________________________________________________
*/
/**
  cache fd deletion
  
  @param fid : fid associated with the file
  
  @retval !=0 success: fd is returned
  @retval -1 not found
*/
int storage_fd_cache_delete(fid_t fid)
{
  int i;
  int ret;
  
  for (i = 0 ; i < storage_fd_cache_max; i ++)
  {
    if (storage_fd_cache_p[i].fd != -1)
    {
      ret = memcmp(storage_fd_cache_p[i].fid,fid,sizeof(fid_t));
      if (ret == 0) 
      {
        memset(storage_fd_cache_p[i].fid,0,sizeof(fid_t));
        storage_fd_cache_p[i].fd = -1;
      }
    }
  }
  return -1;
}
