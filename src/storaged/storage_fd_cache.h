
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

#ifndef STORAGE_FD_CACHE_H
#define STORAGE_FD_CACHE_H

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

#define STORAGE_READAHEAD_SZ (1024*132)

typedef struct _storage_fd_cache_t
{
   uint64_t  filler;
   fid_t fid;
   int   inuse_count;
   int   fd;
   char  *read_ahead_p;
   size_t len_read;
   off_t  offset;
} storage_fd_cache_t;

extern storage_fd_cache_t *storage_fd_cache_p ;
extern uint32_t storage_fd_cache_max ;
extern uint32_t storage_fd_max_inuse_count;
extern storage_fd_cache_t *current_storage_fd_cache_p;


/*
**__________________________________________________________________________
*/
/**
 Init of the cache
  
   @param nb_entries
   
   @retval 0 on success
   @retval -1 on error
*/
int storage_fd_cache_init(uint32_t entries,uint32_t max_inuse_count);

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
storage_fd_cache_t *storage_fd_cache_insert(int fd, fid_t fid);
/*
**__________________________________________________________________________
*/
/**
  cache fd search
  
  @param fid : fid associated with the file
  
  @retval <>NULL  success
  @retval NULL failed
*/
storage_fd_cache_t *storage_fd_cache_search(fid_t fid);
/*
**__________________________________________________________________________
*/
/**
  cache fd deletion
  
  @param fid : fid associated with the file
  
  @retval !=0 success: fd is returned
  @retval -1 not found
*/
int storage_fd_cache_delete(fid_t fid);


#endif
